#include "fm1.h"
/* Store test: fm1_store_load and fm1_store_save, in the simulator, in fm1-base's host simulator and on a real FM-1.
 *
 * At each start, setup() runs the tests below and saves to slots 0 to 3 and 15. The screen shows a square for each
 * test, left to right: green passed, red failed, grey not run (yet). BOOT counts the starts (slot 0): after a power
 * cycle, or RUN in the simulator, it is one more. KNOB 1 sets KNOB; SAVE saves it to slot 4, and it comes back at the
 * next start. The last line says FAIL when a test failed, and PASS when all passed (press SAVE one time).
 *
 *    0  a slot that is not valid: -2 (load and save)
 *    1  a save of more than FM1_STORE_MAX bytes: -2
 *    2  data NULL: -2 (load and save)
 *    3  slots 1 and 15 still have the 3840 bytes of the start before (grey at the first start)
 *    4  a save of FM1_STORE_MAX bytes to slots 1 and 15, and a load of slot 1
 *    5  a load with a smaller max: max bytes, the rest of the buffer untouched
 *    6  a save of 0 bytes empties slot 2: the load gives -1
 *    7  a second save to slot 3 replaces the first; a load with a larger max gives its length
 *    8  slot 1 is as it was after the saves to slots 2 and 3
 *    9  the save of the start count to slot 0
 *   10  load and save from audio(): -2 (grey until audio() runs)
 *   11  SAVE: the save of KNOB to slot 4 in loop(), and its load (grey until SAVE) */

#define NTEST 12
enum { GREY = 0x8410, GREEN = 0x07E0, RED = 0xF800, WHITE = 0xFFFF, BLACK = 0x0000 };

static int8_t result[NTEST];                 /* -1 not run, 0 failed, 1 passed */
static volatile int8_t audio_result = -1;    /* test 10, set in audio() */
static uint8_t big[FM1_STORE_MAX], back[FM1_STORE_MAX];
static uint32_t boots, knob, shown_knob = ~0u;
static int dirty = 1;

typedef struct { uint32_t magic, value; } record_t;
#define BOOT_MAGIC 0x31425453u               /* "STB1" */
#define KNOB_MAGIC 0x31424E4Bu               /* "KNB1" */
#define LAST (FM1_STORE_SLOTS - 1)           /* the last slot: tests 3 and 4 use it too */

static uint8_t pattern(uint32_t i, uint32_t boot) { return (uint8_t)(i * 7u + boot * 13u + (i >> 8)); }

static int same(const uint8_t *a, const uint8_t *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
  return 1;
}

/* ---- a 3 x 5 font, for the labels and numbers ---- */

static uint16_t glyph(char c) {
  switch (c) {
    case '0': case 'O': return 075557; case '1': return 026227; case '2': return 071747; case '3': return 071717;
    case '4': return 055711; case '5': case 'S': return 074717; case '6': return 074757; case '7': return 071111;
    case '8': return 075757; case '9': return 075717; case 'A': return 025755; case 'B': return 065656;
    case 'E': return 074647; case 'F': return 074644; case 'I': return 072227; case 'K': return 055655;
    case 'L': return 044447; case 'N': return 065555; case 'P': return 065644; case 'R': return 065655;
    case 'T': return 072222; case 'V': return 055552; default: return 0;
  }
}

static void text(uint32_t x, uint32_t y, uint32_t scale, const char *s, uint16_t colour) {
  for (; *s; s++, x += 4 * scale) {
    uint16_t bits = glyph(*s);
    fm1_lcd_fill_rect(x, y, 3 * scale, 5 * scale, BLACK);
    for (uint32_t row = 0; row < 5; row++)
      for (uint32_t col = 0; col < 3; col++)
        if (bits >> (14 - row * 3 - col) & 1) fm1_lcd_fill_rect(x + col * scale, y + row * scale, scale, scale, colour);
  }
}

static void number(uint32_t x, uint32_t y, uint32_t value) {
  char s[8] = "     ";
  int at = 4;
  do { s[at--] = (char)('0' + value % 10); value /= 10; } while (value && at >= 0);
  text(x, y, 5, s, WHITE);
}

static void draw(void) {
  int failed = 0, all = 1;
  for (int t = 0; t < NTEST; t++) {
    int8_t r = t == 10 ? audio_result : result[t];
    fm1_lcd_fill_rect(2 + 20 * t, 44, 16, 16, r < 0 ? GREY : r ? GREEN : RED);
    failed |= r == 0;
    all &= r == 1 || t == 3;  /* test 3 is grey at the first start */
  }
  number(0, 104, boots);
  number(120, 104, knob);
  text(30, 190, 6, failed ? "FAIL" : all ? "PASS" : "    ", failed ? RED : GREEN);
  shown_knob = knob;
}

/* ---- the tests ---- */

void setup(void) {
  record_t record;
  uint8_t small[32];
  fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);
  text(4, 8, 4, "STORE TEST", WHITE);
  text(0, 80, 3, "BOOT", GREY);
  text(120, 80, 3, "KNOB", GREY);
  for (int t = 0; t < NTEST; t++) result[t] = -1;
  fm1_led_key(FM1_BTN_SAVE, 1);

  boots = fm1_store_load(0, &record, sizeof record) == (int)sizeof record && record.magic == BOOT_MAGIC ? record.value + 1 : 1;
  if (fm1_store_load(4, &record, sizeof record) == (int)sizeof record && record.magic == KNOB_MAGIC) knob = record.value;

  result[0] = fm1_store_load(FM1_STORE_SLOTS, small, sizeof small) == -2 && fm1_store_save(FM1_STORE_SLOTS, small, 4) == -2;
  result[1] = fm1_store_save(1, big, FM1_STORE_MAX + 1) == -2;
  result[2] = fm1_store_load(0, 0, 4) == -2 && fm1_store_save(5, 0, 4) == -2;

  if (boots > 1) {
    for (uint32_t i = 0; i < FM1_STORE_MAX; i++) big[i] = pattern(i, boots - 1);
    result[3] = fm1_store_load(1, back, sizeof back) == (int)FM1_STORE_MAX && same(back, big, FM1_STORE_MAX) &&
                fm1_store_load(LAST, back, sizeof back) == (int)FM1_STORE_MAX && same(back, big, FM1_STORE_MAX);
  }

  for (uint32_t i = 0; i < FM1_STORE_MAX; i++) big[i] = pattern(i, boots);
  for (uint32_t i = 0; i < FM1_STORE_MAX; i++) back[i] = 0;
  result[4] = fm1_store_save(1, big, FM1_STORE_MAX) == 0 && fm1_store_save(LAST, big, FM1_STORE_MAX) == 0 &&
              fm1_store_load(1, back, sizeof back) == (int)FM1_STORE_MAX && same(back, big, FM1_STORE_MAX);

  for (uint32_t i = 0; i < sizeof small; i++) small[i] = 0xA5;
  result[5] = fm1_store_load(1, small, 16) == 16 && same(small, big, 16);
  for (uint32_t i = 16; i < sizeof small; i++) if (small[i] != 0xA5) result[5] = 0;

  result[6] = fm1_store_save(2, big, 100) == 0 && fm1_store_save(2, big, 0) == 0 && fm1_store_load(2, back, sizeof back) == -1;

  result[7] = fm1_store_save(3, "first", 5) == 0 && fm1_store_save(3, "second!", 7) == 0 &&
              fm1_store_load(3, back, sizeof back) == 7 && same(back, (const uint8_t *)"second!", 7);

  for (uint32_t i = 0; i < FM1_STORE_MAX; i++) back[i] = 0;
  result[8] = fm1_store_load(1, back, sizeof back) == (int)FM1_STORE_MAX && same(back, big, FM1_STORE_MAX);

  record.magic = BOOT_MAGIC;
  record.value = boots;
  result[9] = fm1_store_save(0, &record, sizeof record) == 0;
}

void audio(int32_t *out, int frames) {
  if (audio_result < 0) {
    uint8_t b[4] = {1, 2, 3, 4};
    audio_result = fm1_store_load(0, b, sizeof b) == -2 && fm1_store_save(5, b, sizeof b) == -2;
    dirty = 1;
  }
  for (int i = 0; i < 2 * frames; i++) out[i] = 0;
}

void loop(void) {
  int32_t turn = fm1_enc_take(FM1_ENC_KNOB1);
  if (turn) {
    int32_t value = (int32_t)knob + turn;
    knob = value < 0 ? 0 : value > 999 ? 999 : (uint32_t)value;
  }
  if (fm1_input_edges(0) & (1u << FM1_BTN_SAVE)) {
    record_t record = { KNOB_MAGIC, knob }, again = { 0, 0 };
    result[11] = fm1_store_save(4, &record, sizeof record) == 0 &&
                 fm1_store_load(4, &again, sizeof again) == (int)sizeof again && again.value == knob && again.magic == KNOB_MAGIC;
    dirty = 1;
  }
  if (dirty || knob != shown_knob) {
    dirty = 0;
    draw();
  }
}
