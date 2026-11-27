#include "fm1.h"
/* USB audio test: a computer records the sound of the FM-1 over the USB cable.
 *
 * Over USB, an FM-1 with an fm1.h program is also a USB audio device (UAC1, class compliant): a computer records
 * the sound of audio(), 16-bit stereo at 44.1 kHz, as from a microphone. This program plays a known sound for that
 * test, at a fixed level (MASTER does not change it):
 *
 *    LEFT    a sine with a period of 64 frames: 689.0625 Hz at 44100 frames a second
 *    RIGHT   a sine with a period of 100 frames: 441 Hz
 *    LEVEL   about 0.003 of full scale: -50 dBFS at its peaks, -53 dBFS RMS (quiet on the FM-1's speaker; loud
 *            enough for the check)
 *    PLAY    switches the sound off and on (its LED is on while the sound plays)
 *
 * Each frame of the sound is the same as the frame 64 frames before it on the left, and 100 frames before it on
 * the right. So in a recording, a frame that was lost or sent two times shows exactly. On Windows,
 * check_usb_audio.ps1 (next to this file) records the FM-1 and checks the recording: the device, the two sines,
 * their levels, and that no frame was lost or repeated. With the FM-1's own clock (about 44118 frames a second),
 * the sines are about 0.04 % higher than the frequencies above.
 *
 * In the simulator, the program plays the same sound; the simulator has no USB. */

#include <math.h>

enum { GREY = 0x8410, GREEN = 0x07E0, RED = 0xF800, CYAN = 0x07FF, WHITE = 0xFFFF, BLACK = 0x0000 };

#define LEFT_PERIOD  64u             /* frames */
#define RIGHT_PERIOD 100u
#define LEVEL        26527.0f        /* -50 dBFS: 0.00316 of FM1_AUDIO_MAX + 1 (8388608); 104 at 16 bits */

static int32_t left_wave[LEFT_PERIOD], right_wave[RIGHT_PERIOD];
static uint32_t left_at, right_at;   /* the next frame of each sine */
static volatile int sound_on = 1;    /* PLAY: loop() writes it, audio() reads it */
static int changed = 1;              /* the line of the sound must be drawn */

/* ---- a 3 x 5 font ---- */

static uint16_t glyph(char c) {
  switch (c) {
    case '0': case 'O': return 075557; case '1': return 026227; case '2': return 071747; case '3': return 071717;
    case '4': return 055711; case '5': case 'S': return 074717; case '6': return 074757; case '7': return 071111;
    case '8': return 075757; case '9': return 075717; case 'A': return 025755; case 'B': return 065656;
    case 'C': return 034443; case 'D': return 065556; case 'E': return 074647; case 'F': return 074644;
    case 'G': return 034557; case 'H': return 055755; case 'I': return 072227; case 'K': return 055655;
    case 'L': return 044447; case 'M': return 057755; case 'N': return 065555; case 'P': return 065644;
    case 'R': return 065655; case 'T': return 072222; case 'U': return 055557; case 'V': return 055552;
    case 'W': return 055775; case 'X': return 055255; case 'Y': return 055222; case 'Z': return 071247;
    case '-': return 000700; case '.': return 000002; case ',': return 000012; case ':': return 002020;
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

/* ---- the program ---- */

void setup(void) {
  for (uint32_t n = 0; n < LEFT_PERIOD; n++)
    left_wave[n] = (int32_t)(LEVEL * sinf(2.0f * M_PI * (float)n / (float)LEFT_PERIOD));
  for (uint32_t n = 0; n < RIGHT_PERIOD; n++)
    right_wave[n] = (int32_t)(LEVEL * sinf(2.0f * M_PI * (float)n / (float)RIGHT_PERIOD));
  fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);
  text(8, 8, 4, "USB AUDIO TEST", WHITE);
  text(8, 44, 3, "LEFT   689 HZ", CYAN);
  text(8, 64, 3, "RIGHT  441 HZ", CYAN);
  text(8, 84, 3, "LEVEL  -50 DBFS", CYAN);
  text(8, 150, 2, "RECORD THE FM-1 OVER USB:", GREY);
  text(8, 164, 2, "44.1 KHZ, 16 BIT, STEREO", GREY);
  text(8, 192, 2, "PLAY: SOUND OFF AND ON", GREY);
}

void audio(int32_t *out, int frames) {
  const int on = sound_on;
  for (int i = 0; i < frames; i++) {
    out[2 * i] = on ? left_wave[left_at] : 0;
    out[2 * i + 1] = on ? right_wave[right_at] : 0;
    left_at = left_at + 1 == LEFT_PERIOD ? 0 : left_at + 1;
    right_at = right_at + 1 == RIGHT_PERIOD ? 0 : right_at + 1;
  }
}

void loop(void) {
  if (fm1_input_edges(0) & (1u << FM1_BTN_PLAY)) {
    sound_on = !sound_on;
    changed = 1;
  }
  if (changed) {
    changed = 0;
    fm1_led_key(FM1_BTN_PLAY, sound_on);
    text(8, 112, 3, sound_on ? "SOUND  ON " : "SOUND  OFF", sound_on ? GREEN : RED);
  }
}
