#include "fm1.h"
/* Battery test: fm1_battery(), in the simulator and on a real FM-1.
 *
 * The screen shows the level as a battery with three bars (USB: a computer powers the FM-1; ?: the battery is not
 * measured yet), and below it:
 *
 *    LEVEL      the value of fm1_battery() now
 *    IN SETUP   the value that it gave in setup()
 *    FIRST AT   when the first measured level came (-1 before it), in milliseconds after setup()
 *    CHANGES    how many times the level changed after that, and when it last changed (seconds after setup())
 *    TIME       the seconds since setup()
 *
 * The last line says WAIT until the first measured level, then OK, or FAIL with the reason:
 *    RANGE      a value that is not -1, 0 to FM1_BATTERY_FULL, or FM1_BATTERY_USB
 *    -1 AGAIN   -1 after a measured level
 *
 * On a real FM-1 that a computer powers, the level is USB. Disconnect the cable: the bars show the battery, and
 * they go down as the battery runs empty. In the simulator, the BATTERY button sets the level. */

enum { GREY = 0x8410, DARK = 0x2104, GREEN = 0x07E0, AMBER = 0xFD20, RED = 0xF800, CYAN = 0x07FF,
       WHITE = 0xFFFF, BLACK = 0x0000 };

static volatile uint32_t blocks;     /* audio blocks since setup() */
static int setup_level;              /* fm1_battery() in setup() */
static int last = -2;                /* the last value of fm1_battery() (-2: none yet) */
static int measured;                 /* 1 after the first level that is not -1 */
static uint32_t first_ms, changes, change_s, shown_s = ~0u;
static const char *fail;             /* the first failure, or 0 */

static uint32_t seconds(void) { return blocks * 100u / 17227u; }   /* 172.27 blocks a second */

/* ---- a 3 x 5 font ---- */

static uint16_t glyph(char c) {
  switch (c) {
    case '0': case 'O': return 075557; case '1': return 026227; case '2': return 071747; case '3': return 071717;
    case '4': return 055711; case '5': case 'S': return 074717; case '6': return 074757; case '7': return 071111;
    case '8': return 075757; case '9': return 075717; case 'A': return 025755; case 'B': return 065656;
    case 'C': return 034443; case 'E': return 074647; case 'F': return 074644; case 'G': return 034553;
    case 'H': return 055755; case 'I': return 072227; case 'K': return 055655; case 'L': return 044447; case 'M': return 057755;
    case 'N': return 065555; case 'P': return 065644; case 'R': return 065655; case 'T': return 072222;
    case 'U': return 055557; case 'V': return 055552; case 'W': return 055775; case 'Y': return 055222;
    case '-': return 000700; case '?': return 071202;
    default: return 0;
  }
}

/* Each character on a black cell, so that a text replaces the one before it. */
static void text(uint32_t x, uint32_t y, uint32_t scale, const char *s, uint16_t colour) {
  for (; *s; s++, x += 4 * scale) {
    uint16_t bits = glyph(*s);
    fm1_lcd_fill_rect(x, y, 4 * scale, 5 * scale, BLACK);
    for (uint32_t row = 0; row < 5; row++)
      for (uint32_t col = 0; col < 3; col++)
        if (bits >> (14 - row * 3 - col) & 1) fm1_lcd_fill_rect(x + col * scale, y + row * scale, scale, scale, colour);
  }
}

/* A line of text: put() and num() add to it, end() fills it with spaces to `width` characters. */
typedef struct { char s[32]; uint32_t n; } line_t;

static void put(line_t *l, const char *s) { while (*s && l->n < 31) l->s[l->n++] = *s++; }

static void num(line_t *l, int32_t value) {
  char digits[12];
  uint32_t n = 0, v = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
  if (value < 0) put(l, "-");
  do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n && l->n < 31) l->s[l->n++] = digits[--n];
}

static const char *end(line_t *l, uint32_t width) {
  while (l->n < width && l->n < 31) l->s[l->n++] = ' ';
  l->s[l->n] = 0;
  return l->s;
}

/* ---- the screen ---- */

static void draw_battery(int level) {
  uint16_t frame = level == 0 ? RED : level >= 0 && level <= FM1_BATTERY_USB ? WHITE : GREY;
  uint16_t bar = level >= 2 ? GREEN : level == 1 ? AMBER : RED;
  fm1_lcd_fill_rect(20, 40, 160, 70, frame);
  fm1_lcd_fill_rect(26, 46, 148, 58, BLACK);
  fm1_lcd_fill_rect(180, 60, 12, 30, frame);
  if (level == FM1_BATTERY_USB) {
    text(64, 60, 6, "USB", CYAN);
  } else if (level >= 0 && level <= FM1_BATTERY_FULL) {
    for (int i = 0; i < FM1_BATTERY_FULL; i++) fm1_lcd_fill_rect(32 + 46 * (uint32_t)i, 52, 40, 46, i < level ? bar : DARK);
  } else {
    text(88, 60, 6, "?", GREY);
  }
}

static void draw(int level) {
  line_t l = { "", 0 };
  draw_battery(level);
  put(&l, "LEVEL ");
  if (level == FM1_BATTERY_USB) put(&l, "USB");
  else num(&l, level);
  text(8, 120, 5, end(&l, 10), WHITE);

  l.n = 0;
  put(&l, "IN SETUP  ");
  num(&l, setup_level);
  text(8, 152, 2, end(&l, 28), GREY);

  l.n = 0;
  put(&l, "FIRST AT  ");
  if (measured) { num(&l, (int32_t)first_ms); put(&l, " MS"); }
  else put(&l, "-");
  text(8, 166, 2, end(&l, 28), GREY);

  l.n = 0;
  put(&l, "CHANGES  ");
  num(&l, (int32_t)changes);
  if (changes) { put(&l, "  LAST AT "); num(&l, (int32_t)change_s); put(&l, " S"); }
  text(8, 180, 2, end(&l, 28), GREY);

  text(8, 216, 3, fail ? "FAIL" : measured ? "OK  " : "WAIT", fail ? RED : measured ? GREEN : GREY);
  text(68, 216, 3, fail ? fail : "        ", RED);
}

static void draw_time(void) {
  line_t l = { "", 0 };
  put(&l, "TIME  ");
  num(&l, (int32_t)shown_s);
  put(&l, " S");
  text(8, 194, 2, end(&l, 28), GREY);
}

/* ---- the program ---- */

void setup(void) {
  setup_level = fm1_battery();
  fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);
  text(4, 8, 4, "BATTERY TEST", WHITE);
}

void audio(int32_t *out, int frames) {
  blocks++;
  for (int i = 0; i < 2 * frames; i++) out[i] = 0;
}

void loop(void) {
  int level = fm1_battery();
  int valid = level >= -1 && level <= FM1_BATTERY_USB;
  if (!valid && !fail) fail = "RANGE";
  if (level == -1 && measured && !fail) fail = "-1 AGAIN";
  if (level != last) {
    if (valid && level != -1) {
      if (!measured) {
        measured = 1;
        first_ms = blocks * 5805u / 1000u;   /* 5.805 ms a block */
      } else {
        changes++;
        change_s = seconds();
      }
    }
    last = level;
    draw(level);
  }
  if (seconds() != shown_s) {
    shown_s = seconds();
    draw_time();
  }
}
