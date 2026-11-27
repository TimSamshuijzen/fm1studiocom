#include "fm1.h"
/* MIDI test: fm1_midi_send() and fm1_midi_take(), in the simulator and on a real FM-1.
 *
 * The program sends these messages to the computer, on MIDI channel 1:
 *
 *    Note keys   note on and note off: note key 0 is note 53 (F3), velocity 100
 *    KNOB 1      controller 1 (the modulation wheel), 0 to 127
 *    PLAY        start
 *    REC         stop
 *
 * The screen shows, of the messages to the computer (OUT):
 *
 *    SENT        how many times fm1_midi_send() gave 1
 *    NOT SENT    how many times it gave 0
 *    LAST        the bytes of the last message, and what fm1_midi_send() gave: OK (1) or NO (0)
 *    REFUSE      OK when fm1_midi_send() gave 0 for two packets that are not a message that it sends
 *                (a system exclusive start, and data bytes without a status byte), else FAIL
 *
 * and of the messages from the computer (IN):
 *
 *    TAKEN       how many messages fm1_midi_take() gave
 *    LAST        the bytes of the last one
 *
 * The LED of a note key is on while the computer holds its note (53 to 79).
 *
 * On a real FM-1 with a USB cable to a computer, each message counts as SENT. Look at the MIDI port of the FM-1
 * with a MIDI monitor, or switch on MIDI IN in FM-1 Studio: the note keys of the FM-1 then play the program in the
 * simulator. Without a computer (a charger, the battery), each message counts as NOT SENT. In the simulator, each
 * message counts as SENT, and the MIDI OUT lane of the FM-1 API shows it. */

enum { GREY = 0x8410, GREEN = 0x07E0, RED = 0xF800, CYAN = 0x07FF, WHITE = 0xFFFF, BLACK = 0x0000 };

#define BIT(n) (1u << (n))
#define FIRST_NOTE 53u               /* the MIDI note of note key 0: F3 */

static uint32_t sent, not_sent, taken;
static uint32_t last_out, last_in;   /* the last packet of each direction */
static int last_out_ok, refuse_ok;
static int any_out, any_in;          /* 1 after the first message of each direction */
static uint32_t notes_down;          /* the note keys that were down in the last loop() */
static uint32_t notes_in;            /* the notes that the computer holds, as bits of note keys */
static uint32_t leds_shown;
static int32_t wheel = 64;           /* the value of controller 1 */
static int changed = 1;              /* the screen must be drawn */

/* ---- a 3 x 5 font ---- */

static uint16_t glyph(char c) {
  switch (c) {
    case '0': case 'O': return 075557; case '1': return 026227; case '2': return 071747; case '3': return 071717;
    case '4': return 055711; case '5': case 'S': return 074717; case '6': return 074757; case '7': return 071111;
    case '8': return 075757; case '9': return 075717; case 'A': return 025755; case 'B': return 065656;
    case 'C': return 034443; case 'D': return 065556; case 'E': return 074647; case 'F': return 074644;
    case 'I': return 072227; case 'K': return 055655; case 'L': return 044447; case 'M': return 057755;
    case 'N': return 065555; case 'R': return 065655; case 'T': return 072222; case 'U': return 055557;
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

/* A line of text: put(), num() and hex() add to it, end() fills it with spaces to `width` characters. */
typedef struct { char s[32]; uint32_t n; } line_t;

static void put(line_t *l, const char *s) { while (*s && l->n < 31) l->s[l->n++] = *s++; }

static void num(line_t *l, uint32_t value) {
  char digits[12];
  uint32_t n = 0;
  do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
  while (n && l->n < 31) l->s[l->n++] = digits[--n];
}

/* The three bytes of a packet, as "90 3C 64". */
static void hex(line_t *l, uint32_t packet) {
  const uint32_t bytes[3] = { FM1_MIDI_STATUS(packet), FM1_MIDI_DATA1(packet), FM1_MIDI_DATA2(packet) };
  for (uint32_t i = 0; i < 3; i++) {
    char pair[4] = { "0123456789ABCDEF"[bytes[i] >> 4], "0123456789ABCDEF"[bytes[i] & 15u], ' ', 0 };
    put(l, pair);
  }
}

static const char *end(line_t *l, uint32_t width) {
  while (l->n < width && l->n < 31) l->s[l->n++] = ' ';
  l->s[l->n] = 0;
  return l->s;
}

/* ---- the screen ---- */

static void count_line(uint32_t y, const char *name, uint32_t value, uint16_t colour) {
  line_t l = { "", 0 };
  put(&l, name);
  num(&l, value);
  text(20, y, 3, end(&l, 18), colour);
}

static void last_line(uint32_t y, int any, uint32_t packet, const char *tail) {
  line_t l = { "", 0 };
  put(&l, "LAST ");
  if (any) { hex(&l, packet); put(&l, tail); }
  text(20, y, 3, end(&l, 18), GREY);
}

static void draw(void) {
  count_line(62, "SENT      ", sent, sent ? GREEN : WHITE);
  count_line(82, "NOT SENT  ", not_sent, not_sent ? RED : WHITE);
  last_line(102, any_out, last_out, last_out_ok ? "OK" : "NO");
  text(20, 122, 3, "REFUSE    ", GREY);
  text(140, 122, 3, refuse_ok ? "OK  " : "FAIL", refuse_ok ? GREEN : RED);
  count_line(176, "TAKEN     ", taken, taken ? GREEN : WHITE);
  last_line(196, any_in, last_in, "");
}

/* ---- the program ---- */

static void send(uint32_t packet) {
  last_out = packet;
  last_out_ok = fm1_midi_send(packet);
  if (last_out_ok) sent++; else not_sent++;
  any_out = 1;
  changed = 1;
}

void setup(void) {
  /* Not messages that fm1_midi_send() sends: both must give 0. */
  refuse_ok = !fm1_midi_send(FM1_MIDI_PACKET(0xF0, 0, 0)) && !fm1_midi_send(0x00403C00u);
  fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);
  text(8, 8, 4, "MIDI TEST", WHITE);
  text(8, 40, 3, "OUT", CYAN);
  text(8, 154, 3, "IN", CYAN);
  fm1_led_key(FM1_BTN_PLAY, 1);    /* the buttons that do something */
  fm1_led_key(FM1_BTN_REC, 1);
}

void audio(int32_t *out, int frames) {
  for (int i = 0; i < 2 * frames; i++) out[i] = 0;
}

void loop(void) {
  /* to the computer */
  uint32_t notes = fm1_in.notes, pressed = fm1_input_edges(0);
  for (uint32_t n = 0; n < FM1_NOTE_KEYS; n++)
    if ((notes ^ notes_down) & BIT(n)) send(FM1_MIDI_PACKET((notes & BIT(n)) ? 0x90 : 0x80, FIRST_NOTE + n, 100));
  notes_down = notes;
  int32_t steps = fm1_enc_take(FM1_ENC_KNOB1);
  if (steps) {
    int32_t value = wheel + steps;
    value = value < 0 ? 0 : value > 127 ? 127 : value;
    if (value != wheel) { wheel = value; send(FM1_MIDI_PACKET(0xB0, 1, wheel)); }
  }
  if (pressed & BIT(FM1_BTN_PLAY)) send(FM1_MIDI_PACKET(0xFA, 0, 0));
  if (pressed & BIT(FM1_BTN_REC)) send(FM1_MIDI_PACKET(0xFC, 0, 0));

  /* from the computer */
  uint32_t packet;
  while (fm1_midi_take(&packet)) {
    uint32_t type = FM1_MIDI_STATUS(packet) & 0xF0u, n = FM1_MIDI_DATA1(packet) - FIRST_NOTE;
    taken++;
    last_in = packet;
    any_in = 1;
    changed = 1;
    if (n >= FM1_NOTE_KEYS) continue;
    if (type == 0x90u && FM1_MIDI_DATA2(packet)) notes_in |= BIT(n);
    else if (type == 0x80u || type == 0x90u) notes_in &= ~BIT(n);
  }
  for (uint32_t n = 0; n < FM1_NOTE_KEYS; n++)
    if ((notes_in ^ leds_shown) & BIT(n)) fm1_led_key(FM1_KEY_NOTE0 + n, (notes_in >> n) & 1u);
  leds_shown = notes_in;

  if (changed) {
    changed = 0;
    draw();
  }
}
