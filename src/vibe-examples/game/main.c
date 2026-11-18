/* NOTEFALL - an 8-lane rhythm game for the M-VAVE FM-1, made for FM-1 Studio.
 *
 * Notes fall in 8 lanes to the line at the bottom. Hit them with the white
 * note keys C4..C5 (computer: A S D F G H J K). Each hit plays that note of
 * the melody with a bell sound; drums, bass, chords and pads play by themselves.
 *
 * Title:   OCT- / OCT+ (Z / X) choose the song, a lane key starts it.
 * Game:    lane keys hit, hold the long notes, PLAY pauses, HOME quits.
 * Results: a lane key goes back to the title.
 */
#include "fm1.h"
#include <math.h>

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xFF) >> 3)))
#define BIT(n) (1u << (n))
#define WHITE RGB(255, 255, 255)
#define GREY  RGB(150, 150, 165)
#define BLACK 0
#define PANEL RGB(16, 16, 40)

/* Screen layout */
#define PF_Y    20    /* playfield: y 20..191, 172 rows */
#define PF_H    172
#define LINE_Y  192   /* hit line: y 192..195 */
#define PAD_Y   196   /* lane pads: y 196..211 */
#define PAD_H   16
#define STRIP_Y 212   /* judgment and combo: y 212..239 */
#define LANE_W  30
#define GEM_H   10
#define MS_R0   26    /* playfield rows of the combo panel */
#define MS_R1   86
#define PS_R0   62    /* playfield rows of the pause panel */
#define PS_R1   106

/* Times in samples (44100 a second) */
#define W_PERFECT 2425   /* +-55 ms */
#define W_GOOD    5513   /* +-125 ms */
#define T_FLASH   6615   /* 150 ms */
#define T_MINGATE 4410   /* 100 ms */
#define T_MS      44100  /* the combo panel stays 1 s */

#define HZ2INC  97391.548f    /* phase steps per Hz: 2^32 / 44100 */
#define RAD2PH  683565275.6f  /* phase steps per radian: 2^32 / (2 pi) */
#define SN_INC  18017437u     /* 185 Hz, the tone of the snare */
#define MAXN    1024
#define MAXBARS 64
#define MAXP    40
#define DLEN    6400          /* echo buffer, one per side */
#define NPIX    11040         /* pixel buffer for blits */

/* Kinds of playfield rows; a row code is kind * 16 + background level */
enum { K_BG, K_BEAT, K_BAR, K_GT, K_GH, K_GI, K_BN, K_BH, K_BD, K_CN, K_CH, K_CD };
enum { ST_WAIT, ST_HOLD, ST_DONE, ST_DEAD };
enum { J_NONE, J_PERFECT, J_GOOD, J_MISS };
enum { M_NONE, M_TITLE, M_GAME, M_RESULT };

/* 5x7 font, ASCII 32..90, 5 columns per character, bit 0 is the top row */
static const uint8_t font[59 * 5] = {
  0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7F,0x14,0x7F,0x14,
  0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00,
  0x00,0x1C,0x22,0x41,0x00, 0x00,0x41,0x22,0x1C,0x00, 0x14,0x08,0x3E,0x08,0x14, 0x08,0x08,0x3E,0x08,0x08,
  0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00, 0x20,0x10,0x08,0x04,0x02,
  0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00, 0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31,
  0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
  0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1E, 0x00,0x36,0x36,0x00,0x00, 0x00,0x56,0x36,0x00,0x00,
  0x08,0x14,0x22,0x41,0x00, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x51,0x09,0x06,
  0x32,0x49,0x79,0x41,0x3E, 0x7E,0x11,0x11,0x11,0x7E, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
  0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x49,0x49,0x7A,
  0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
  0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x0C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
  0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31,
  0x01,0x01,0x7F,0x01,0x01, 0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
  0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43
};

/* ---- Lanes: the white keys C4..C5 ---- */
static const uint8_t lane_key[8]  = { 7, 9, 11, 12, 14, 16, 18, 19 };  /* note key numbers */
static const uint8_t lane_midi[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };
static const char lane_chr[] = "cdefgabC";                               /* in the charts */
static const char *const lane_label[8] = { "A", "S", "D", "F", "G", "H", "J", "K" };
static const uint8_t lane_rgb[8][3] = {
  { 255, 70, 70 }, { 255, 150, 40 }, { 250, 230, 50 }, { 80, 230, 90 },
  { 50, 220, 230 }, { 70, 120, 255 }, { 170, 90, 255 }, { 255, 90, 200 }
};
#define LANE_KEYS (BIT(7) | BIT(9) | BIT(11) | BIT(12) | BIT(14) | BIT(16) | BIT(18) | BIT(19))

/* ---- Songs ----
 * A song is a form: a list of sections (intro, verse, chorus, break, ...).
 * chart: a note is its lane (c d e f g a b C) and its length in 16ths;
 *   "=" after it makes it a long note (hold it), "-" is a rest, "+" puts the
 *   next note at the same time as the one before.
 * chords: two per bar (one per half bar). Upper case is major (C and F with
 *   a major 7th, G with a 7th), lower case is minor 7th.
 * groove: one character per 16th. kick/snare: x = hit, g = ghost snare;
 *   hat: x, X = louder, o = open; bass: x = root, o = octave, f = fifth;
 *   comp: x = chord stab. pad: 0 none, 1 soft, 2 full. */
typedef struct { const char *kick, *snare, *hat, *bass, *comp; uint8_t pad; } groove_t;
typedef struct { char key; uint8_t bars, groove; const char *chords, *chart; } section_t;
typedef struct {
  const char *name, *diff, *style;
  uint16_t bpm, fall_ms;
  uint8_t dmg, inst, r, g, b, nsec;
  const char *form;
  const section_t *secs;
  const groove_t *grooves;
} song_t;

/* EASY: light, happy pop */
static const groove_t pop_grooves[4] = {
  { "x.......x.......", "................", "..x...x...x...x.", "x.......x.......", "x.......x.......", 1 },
  { "x.....x.x.......", "....x.......x...", "x.x.x.x.x.x.x.x.", "x.....x.x...o...", "..x...x...x...x.", 1 },
  { "x.....x.x.....x.", "....x.......x...", "x.x.x.xox.x.x.xo", "x..x..x.x..x..o.", "x.x.x.x.x.x.x.x.", 2 },
  { "................", "................", "....x.......x...", "x...............", "x.......x.......", 2 }
};
static const section_t pop_secs[5] = {
  { 'I', 4, 0, "CC GG aa FG", "e16= | d16= | c8= e8 | f8 g8=" },
  { 'V', 8, 1, "CC GG aa FF CC GG FF GG",
    "e4 e4 g4 e4 | d4 d4 g8= | c4 c4 e4 a4 | a8= g8 |"
    "e4 e4 g4 C4 | b4 a4 g8= | f4 a4 g4 f4 | e4 d4 g8=" },
  { 'C', 8, 2, "FF GG ee aa FF GG CC CG",
    "a2 g2 a2 C2 C4 a4 | b2 a2 g2 a2 b4 g4 | g2 e2 g2 b2 b4 a4 | a2 g2 e2 c2 e4= -4 |"
    "a2 g2 a2 C2 C4 a4 | b2 C2 b2 a2 g4 d4 | e2 g2 C2 e2 g2 C2 e4 | C8= -4 g4" },
  { 'B', 4, 3, "aa FF CC GG", "a8= e8 | f8= a8 | g16= | g8 a4 b4" },
  { 'L', 8, 2, "FF GG ee aa FF GG CC CC",
    "a2 g2 a2 C2 C2 b2 a2 g2 | b2 a2 g2 a2 b2 C2 b2 g2 | g2 e2 g2 b2 C2 b2 a2 g2 | a2 g2 e2 c2 e2 g2 a4 |"
    "a2 g2 a2 C2 C4 a4 | b2 C2 b2 a2 g2 a2 b2 d2 | e2 g2 C2 e2 g2 C2 e2 g2 | C16=" }
};

/* NORMAL: a funky groove in D dorian, with syncopation */
static const groove_t funk_grooves[4] = {
  { "x.........x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "x..x..o...x..x..", "...x..x....x..x.", 0 },
  { "x..x......x..x..", "....x..g.g..x..g", "XxxxXxxxXxxxXxxx", "x..xo..x.x.ox.f.", "..x..x.x..x..x..", 0 },
  { "x..x..x...x..x..", "....x..g.g..x..g", "XxxoXxxxXxxoXxxx", "x.ox.ox.x..xo.x.", ".x.x..x..x.x..x.", 1 },
  { "x...............", "............x...", "x.x.x.x.x.x.x.x.", "x..x..o.........", "................", 2 }
};
static const section_t funk_secs[5] = {
  { 'I', 4, 0, "dd dG dd dG",
    "-4 d2 f2 -2 a4= -2 | -4 a2 g2 -2 f2 d4 | -4 d2 f2 -2 a4= -2 | -2 C2 a2 g2 -4 f2 g2" },
  { 'V', 8, 1, "dd dG dd dG dd dG FF GG",
    "d3 d3 f2 -2 a2 g2 f2 | g3 f3 d2 -4 C2 a2 | d3 d3 f2 -2 a2 C2 a2 | b3 a3 g2 -4 f2 g2 |"
    "d3 d3 f2 -2 a2 g2 f2 | g3 f3 d2 -2 d2 f2 g2 | a3 a3 C2 -2 C2 a2 f2 | g6= -2 d2 e2 f2 g2" },
  { 'C', 8, 2, "FF GG ee aa FF GG aa dG",
    "a2 C2 -1 C1 a2 g2 a1 C3 a2 | b2 d2 -1 d1 g2 b2 a1 g3 d2 | e2 g2 -1 g1 b2 a2 g1 e3 g2 | a3 a3 C2 e4= -2 g2 |"
    "a2 C2 -1 C1 a2 g2 a1 C3 a2 | b2 d2 -1 d1 g2 b2 C1 b3 g2 | a1 g1 e2 g1 a1 C2 e2 g2 a4 | d4= -2 f2 g2 a2 b2 g2" },
  { 'B', 4, 3, "dd dd GG GG", "d8= -4 f2 g2 | a8= -4 g2 f2 | g8= -4 a2 b2 | g12= -4" },
  { 'L', 8, 2, "FF GG ee aa FF GG aa dd",
    "a1 a1 C2 -1 C1 a2 g1 a1 C2 a1 g1 f2 | b1 b1 d2 -1 d1 g2 b1 C1 b2 a1 g1 d2 |"
    "e1 e1 g2 -1 g1 b2 a1 g1 e2 g1 a1 b2 | a3 a3 C2 e4= g2 a2 |"
    "a1 a1 C2 -1 C1 a2 g1 a1 C2 a1 g1 f2 | b1 b1 d2 -1 d1 g2 b1 C1 b2 a1 g1 a2 |"
    "a1 g1 e2 g1 a1 C2 e2 g2 a1 b1 C2 | d16=" }
};

/* HARD: fast electronic music in A minor, with runs over the lanes */
static const groove_t edm_grooves[5] = {
  { "x...x...x...x...", "................", "..x...x...x...x.", "..x...x...x...x.", "................", 2 },
  { "x...x...x...x...", "....x.......x...", "xxXxxxXxxxXxxxXx", "..xx..xx..xx..xx", "x..x..x...x..x..", 1 },
  { "x...x...x...x...", "....x.......x...", "xxoxxxoxxxoxxxox", "x.xox.xox.xox.xo", "x..x..x...x..x..", 2 },
  { "................", "................", "x.......x.......", "x...............", "x...............", 2 },
  { "x...x...x...x...", "x.x.x.x.xxxxxxxx", "..x...x...x...x.", "..x...x...x...x.", "................", 1 }
};
static const section_t edm_secs[6] = {
  { 'I', 4, 0, "aa FF CC GG",
    "a2 -2 C2 -2 e4= -4 | f2 -2 a2 -2 C4= -4 | e2 -2 g2 -2 C4= -4 | d2 -2 g2 -2 b4= -4" },
  { 'V', 8, 1, "aa FF CC GG aa FF GG GG",
    "a2 C2 e2 a2 C2 e2 a1 b1 C2 | a2 C2 f2 a2 C2 f2 g1 a1 g2 | g2 C2 e2 g2 C2 e2 d1 e1 g2 | g2 b2 d2 g2 b4= -4 |"
    "a2 C2 e2 a2 C2 e2 a1 b1 C2 | C2 a2 f2 a2 C2 f2 a1 C1 a2 | b2 g2 d2 g2 b2 d2 g1 a1 b2 | C1 b1 a1 g1 f1 e1 d1 c1 d4 g4" },
  { 'C', 8, 2, "FF GG ee aa FF GG aa aa",
    "c1 d1 e1 f1 a2 C2 a1 g1 f1 e1 f2 a2 | d1 e1 f1 g1 b2 d2 g1 a1 b1 C1 b2 g2 |"
    "e1 f1 g1 a1 b2 e2 g1 a1 b1 g1 e2 b2 | a1 b1 C1 b1 a1 g1 e2 a4= -4 |"
    "C1 a1 f1 a1 C1 a1 f1 a1 C2 a2 f2 a2 | b1 g1 d1 g1 b1 g1 d1 g1 b2 g2 d2 g2 |"
    "a1 b1 C1 a1 b1 C1 e1 g1 a1 b1 C1 b1 a2 e2 | a4+e4 -4 a1 b1 C1 b1 a2 g2" },
  { 'B', 4, 3, "FF FF GG GG", "f8= a8= | C16= | g8= b8= | d16=" },
  { 'U', 4, 4, "aa aa GG GG",
    "a4 a4 a4 a4 | a2 a2 a2 a2 C2 C2 C2 C2 | b2 b2 b2 b2 g2 g2 g2 g2 |"
    "d1 e1 f1 g1 a1 b1 C1 b1 a1 g1 f1 e1 d1 e1 f1 g1" },
  { 'L', 8, 2, "FF GG ee aa FF GG aa aa",
    "c1 d1 e1 f1 a2+f2 C2 a1 g1 f1 e1 f2+a2 C2 | d1 e1 f1 g1 b2+g2 d2 g1 a1 b1 C1 b2+g2 d2 |"
    "e1 f1 g1 a1 b2+e2 g2 g1 a1 b1 g1 e2+b2 g2 | a1 b1 C1 b1 a1 g1 e2 a4+C4= -4 |"
    "C1 a1 f1 a1 C1 a1 f1 a1 C1 a1 f1 a1 C2 f2 | b1 g1 d1 g1 b1 g1 d1 g1 b1 g1 d1 g1 b2 d2 |"
    "C1 b1 a1 g1 f1 e1 d1 c1 d1 e1 f1 g1 a1 b1 C2 | a8=+C8= -8" }
};

static const song_t songs[3] = {
  { "MORNING KITE", "EASY", "LIGHT HAPPY POP", 104, 1700, 5, 0, 90, 230, 110, 5,
    "IVCVCBL", pop_secs, pop_grooves },
  { "SUNSET FUNK", "NORMAL", "FUNKY GROOVE", 108, 1450, 7, 1, 250, 200, 60, 5,
    "IVCVCBL", funk_secs, funk_grooves },
  { "NEON SPRINT", "HARD", "FAST ELECTRO", 150, 1150, 7, 2, 255, 80, 90, 6,
    "IVCVCBUCL", edm_secs, edm_grooves }
};

/* ---- Shared between loop() and audio() ---- */
static const song_t *volatile song = &songs[0];
static volatile uint32_t sps = 6360;            /* samples per 16th */
static volatile uint32_t song_pos, seq_ctr, seq_stp, nbars;
static volatile uint8_t g_playing, g_paused, g_crash;
static volatile float g_vol;
static volatile uint32_t g_blocks;
static volatile uint8_t m_trig[8], m_gate[8];    /* melody voice of each lane */
static volatile uint8_t bar_sec[MAXBARS], bar_off[MAXBARS];
static volatile char chord_at[MAXBARS * 2];

/* ---- Game state ---- */
typedef struct { uint16_t step; uint8_t lane, len, lng, st; } note_t;
static note_t notes[MAXN];
static int nn, first, mode = M_NONE, sel, paused, life, combo, maxcombo;
static uint32_t score, cnt[3], best[3], fall, end_t, res_t0;
static uint32_t flash_until[8], gate_off[8];
static uint8_t holding[8];
static int jdg, jdg_seq, sh_jseq, sh_jstage;   /* judgment pop-up */
static uint32_t jdg_t;
static int ms_on, ms_val, ms_stage;             /* combo panel */
static uint32_t ms_t;
static uint32_t demo_k, demo_q;                 /* notes on the title screen */

/* ---- Drawing state ---- */
static uint8_t want[8][PF_H], shown[8][PF_H];   /* row codes: wanted and on the screen */
static uint8_t lockm[PF_H];                     /* lanes covered by a panel, per row */
static uint16_t bgc[8][16], gedge[8], ghi[8], gin[8], bedge[8][3], binner[8][3];
static uint16_t beatc[8], barc[8], padc[8][3];
static uint16_t pix[NPIX];
static int pw, ph;
static uint32_t sh_score;
static int sh_life, sh_combo, sh_pulse, sh_prog, pad_sh[8];
static int8_t led_on[FM1_NKEY];
static uint32_t rng = 12345u;

static const uint16_t sepc[5]  = { RGB(34, 34, 52), RGB(60, 60, 90), RGB(95, 95, 140), RGB(150, 150, 210), RGB(255, 235, 150) };
static const uint16_t linec[5] = { RGB(160, 160, 180), RGB(195, 195, 215), RGB(230, 230, 245), WHITE, RGB(255, 225, 110) };

/* ---- Small helpers ---- */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char *put_str(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }

/* Writes v in decimal, with zeros in front up to `digits` characters. */
static char *put_num(char *d, uint32_t v, int digits) {
  char t[12];
  int n = 0;
  do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
  while (n < digits && n < 10) t[n++] = '0';
  while (n) *d++ = t[--n];
  *d = 0;
  return d;
}

/* A colour: c times k, plus w of white. */
static uint16_t colr(const uint8_t *c, float k, int w) {
  int v[3], i;
  for (i = 0; i < 3; i++) { v[i] = (int)((float)c[i] * k) + w; if (v[i] > 255) v[i] = 255; }
  return RGB(v[0], v[1], v[2]);
}

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---- Pixel box: build an image in pix[], then blit it in one go ---- */
static void box(int w, int h, uint16_t bg) {
  int i;
  if (w * h > NPIX) h = NPIX / w;
  pw = w; ph = h;
  for (i = 0; i < w * h; i++) pix[i] = bg;
}

static void box_rect(int x, int y, int w, int h, uint16_t c) {
  int i, j;
  for (j = y; j < y + h; j++)
    for (i = x; i < x + w; i++)
      if (i >= 0 && i < pw && j >= 0 && j < ph) pix[j * pw + i] = c;
}

static int text_w(const char *s, int sc) { return slen(s) * 6 * sc - sc; }

/* Text in the box, without background (only the pixels of the letters). */
static void box_text(int x, int y, const char *s, int sc, uint16_t fg) {
  int ci, fx, fy;
  for (ci = 0; s[ci]; ci++) {
    int c = (unsigned char)s[ci];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 90) c = 32;
    for (fx = 0; fx < 5; fx++) {
      uint8_t bits = font[(c - 32) * 5 + fx];
      for (fy = 0; fy < 7; fy++)
        if ((bits >> fy) & 1u) box_rect(x + (ci * 6 + fx) * sc, y + fy * sc, sc, sc, fg);
    }
  }
}

static void box_text_c(int y, const char *s, int sc, uint16_t fg) {
  box_text((pw - text_w(s, sc)) / 2, y, s, sc, fg);
}

static void box_show(int x, int y) {
  if (pw > 0 && ph > 0 && x >= 0 && y >= 0 && x + pw <= 240 && y + ph <= 240)
    fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)pw, (uint32_t)ph, pix, (uint32_t)(pw * ph));
}

static void text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg) {
  box(text_w(s, sc) + sc, 7 * sc, bg);
  box_text(0, 0, s, sc, fg);
  box_show(x, y);
}

static void text_c(int y, const char *s, int sc, uint16_t fg, uint16_t bg) {
  text((240 - text_w(s, sc)) / 2, y, s, sc, fg, bg);
}

/* ---- LEDs ---- */
static void set_led(uint32_t id, int on) {
  on = on ? 1 : 0;
  if (led_on[id] != on) { led_on[id] = (int8_t)on; fm1_led_key(id, on); }
}

/* The LEDs of the 14 buttons and of the 8 lane keys (bit l = lane l). */
static void set_leds(uint32_t buttons, uint32_t lanes) {
  uint32_t i;
  for (i = 0; i < 14u; i++) set_led(i, (int)((buttons >> i) & 1u));
  for (i = 0; i < 8u; i++) set_led(FM1_KEY_NOTE0 + lane_key[i], (int)((lanes >> i) & 1u));
}

/* ---- Songs to notes ---- */
/* Reads a chart from step t on, after the first n notes; returns the new number of notes. */
static int parse_chart(const char *s, uint32_t t, int n) {
  uint32_t last = t;
  while (*s) {
    char c = *s++;
    int lane = -1, len = 0, lng = 0, l;
    if (c == '+') { t = last; continue; }
    if (c != '-') {
      for (l = 0; l < 8; l++) if (lane_chr[l] == c) lane = l;
      if (lane < 0) continue;   /* spaces and bar lines */
    }
    while (*s >= '0' && *s <= '9') len = len * 10 + (*s++ - '0');
    if (*s == '=') { lng = 1; s++; }
    if (len < 1) len = 1;
    if (lane >= 0 && n < MAXN) {
      notes[n].step = (uint16_t)t;
      notes[n].lane = (uint8_t)lane;
      notes[n].len = (uint8_t)len;
      notes[n].lng = (uint8_t)lng;
      notes[n].st = ST_WAIT;
      n++;
    }
    last = t;
    t += (uint32_t)len;
  }
  return n;
}

/* Lays out the form of a song: the bars, their chords and all notes. */
static int build_song(const song_t *sg) {
  const char *f;
  int n = 0;
  uint32_t nb = 0;
  for (f = sg->form; *f; f++) {
    const section_t *sc = 0;
    const char *c;
    int k, i;
    for (k = 0; k < sg->nsec; k++) if (sg->secs[k].key == *f) { sc = &sg->secs[k]; break; }
    if (!sc || nb + sc->bars > MAXBARS) continue;
    n = parse_chart(sc->chart, 16u * (nb + 1u), n);   /* bar 0 is the count-in */
    c = sc->chords;
    for (i = 0; i < sc->bars * 2; i++) {
      while (*c == ' ') c++;
      chord_at[nb * 2u + (uint32_t)i] = *c ? *c++ : 'C';
    }
    for (i = 0; i < sc->bars; i++) {
      bar_sec[nb + (uint32_t)i] = (uint8_t)k;
      bar_off[nb + (uint32_t)i] = (uint8_t)i;
    }
    nb += sc->bars;
  }
  nbars = nb;
  return n;
}

/* ---- Audio ---- */
static float sintab[256];
static uint32_t inc_tab[128];                 /* phase step of each MIDI note */
static float mixL[FM1_AUDIO_BLOCK], mixR[FM1_AUDIO_BLOCK], ein[FM1_AUDIO_BLOCK];
static float pan_l[8], pan_r[8];
static int16_t dl_l[DLEN], dl_r[DLEN];        /* ping-pong echo of the melody */
static uint32_t dl_i;

/* Melody: a bright FM bell-pluck for each lane */
typedef struct { uint32_t ph, inc; float a, idx, tine; uint8_t att, seen; } voice_t;
static voice_t mv[8];

/* The sound of each style: bass, chord stabs, pad, kick, snare, pumping */
typedef struct {
  float bass_idx, bass_idec, bass_dec;
  float cp_idx, cp_idec, cp_dec;
  float pad_fm, kick_f0, kick_f1, kick_dec, sn_dec, duck;
  uint32_t cp_ratio;
} inst_t;
static const inst_t insts[3] = {
  /* pop: round bass, electric piano, soft pad */
  { 1.0f, 0.9997f,  0.99994f, 1.2f, 0.9996f,  0.99990f, 0.3f, 130.0f, 50.0f, 0.99975f, 0.9992f,  0.15f, 1 },
  /* funk: slap bass, clav stabs */
  { 3.0f, 0.9990f,  0.99989f, 1.6f, 0.9990f,  0.99965f, 0.2f, 140.0f, 55.0f, 0.99970f, 0.9990f,  0.20f, 3 },
  /* electro: saw bass, saw stabs, bright pumping pad, long kick */
  { 2.0f, 0.99995f, 0.99977f, 2.2f, 0.99995f, 0.99985f, 1.2f, 170.0f, 42.0f, 0.99987f, 0.99935f, 0.65f, 1 }
};

static uint32_t bass_ph, bass_inc, kick_ph, sn_ph, nz = 22222u;
static uint32_t cp_ph[4], cp_inc[4], pd_ph[8], pd_inc[8];
static float bass_a, bass_idx, kick_a, kick_f, sn_a, sn_t, hat_a, hat_dec = 0.997f, hat_prev, crash_a;
static float cp_a, cp_idx, pad_a, pad_tgt;
static char cur_chord;
static uint8_t crash_seen;
static const float cp_pl[4] = { 0.95f, 0.30f, 0.75f, 0.50f };
static const float cp_pr[4] = { 0.30f, 0.95f, 0.50f, 0.75f };
static const float pad_lv[3] = { 0.0f, 0.022f, 0.04f };

static int semi(char c) {
  switch (c) {
    case 'C': return 0;  case 'D': return 2; case 'E': return 4; case 'F': return 5;
    case 'G': return 7;  case 'A': return 9; default:  return 11;
  }
}

/* The four notes of a chord, for the pad (E3..G4) and the stabs (A3..C5). */
static void set_chord(char c) {
  int minor = (c >= 'a' && c <= 'z');
  char up = minor ? (char)(c - 32) : c;
  int r = semi(up), k, iv[4];
  iv[0] = 0; iv[1] = minor ? 3 : 4; iv[2] = 7; iv[3] = (minor || up == 'G') ? 10 : 11;
  cur_chord = c;
  for (k = 0; k < 4; k++) {
    int n = 48 + r + iv[k], m = n;
    while (n > 67) n -= 12;
    while (n < 52) n += 12;
    while (m > 72) m -= 12;
    while (m < 57) m += 12;
    pd_inc[k] = inc_tab[n];
    pd_inc[k + 4] = (uint32_t)((float)inc_tab[n] * 1.004f);   /* right side a bit higher: width */
    cp_inc[k] = inc_tab[m];
  }
}

static void kick_hit(const inst_t *in) { kick_a = 0.45f; kick_f = in->kick_f0; kick_ph = 0; }

static void bass_hit(const inst_t *in, char ch, int add) {
  int n = 36 + semi((ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch);
  if (n < 40) n += 12;
  bass_inc = inc_tab[n + add];
  bass_a = 0.28f;
  bass_idx = in->bass_idx;
}

/* The band: called at the start of each 16th of the song. */
static void seq_step(uint32_t s) {
  const song_t *sg = song;
  const inst_t *in = &insts[sg->inst];
  uint32_t bar = s >> 4, p = s & 15u, nb = nbars;
  if (bar == 0) {                                   /* count-in: four clicks */
    if (p == 0) pad_tgt = 0.0f;
    if ((p & 3u) == 0) { hat_a = p ? 0.05f : 0.09f; hat_dec = 0.9975f; }
    return;
  }
  if (bar <= nb) {
    uint32_t b = bar - 1;
    const section_t *sc = &sg->secs[bar_sec[b]];
    const groove_t *gv = &sg->grooves[sc->groove];
    char ch = chord_at[b * 2u + (p >= 8u ? 1u : 0u)];
    char k = gv->kick[p], sn = gv->snare[p], h = gv->hat[p], bs = gv->bass[p];
    if ((p == 0 || p == 8) && ch != cur_chord) set_chord(ch);
    if (p == 0) {
      pad_tgt = pad_lv[gv->pad];
      if (bar_off[b] == 0 && sc->groove == 2) crash_a = 0.11f;   /* a chorus starts */
    }
    if (k == 'x') kick_hit(in);
    if (sn == 'x') { sn_a = 0.20f; sn_t = 0.22f; }
    else if (sn == 'g') { sn_a = 0.05f; sn_t = 0.04f; }
    if (h == 'x') { hat_a = 0.035f; hat_dec = 0.9970f; }
    else if (h == 'X') { hat_a = 0.06f; hat_dec = 0.9970f; }
    else if (h == 'o') { hat_a = 0.05f; hat_dec = 0.99965f; }
    if (bs == 'x' || bs == 'o' || bs == 'f') bass_hit(in, ch, bs == 'o' ? 12 : bs == 'f' ? 7 : 0);
    if (gv->comp[p] == 'x') { cp_a = 1.0f; cp_idx = in->cp_idx; }
  } else if (bar == nb + 1u) {                      /* the last chord rings out */
    if (p == 0) {
      char ch = chord_at[nb * 2u - 1u];
      if (ch != cur_chord) set_chord(ch);
      kick_hit(in);
      crash_a = 0.12f;
      bass_hit(in, ch, 0);
      cp_a = 1.0f; cp_idx = in->cp_idx;
    }
    if (p == 8) pad_tgt = 0.0f;
  }
}

static int16_t to16(float x) {
  x *= 32767.0f;
  if (x > 32767.0f) x = 32767.0f;
  if (x < -32767.0f) x = -32767.0f;
  return (int16_t)x;
}

void audio(int32_t *out, int frames) {
  const inst_t *in = &insts[song->inst];
  int i, l, k, run = g_playing && !g_paused;
  uint32_t pos = song_pos, ctr = seq_ctr, stp = seq_stp, len = sps, dT = sps;
  float vol = g_vol, ptgt;
  if (frames > (int)FM1_AUDIO_BLOCK) frames = (int)FM1_AUDIO_BLOCK;
  if (dT >= DLEN) dT = DLEN - 1;
  g_blocks++;
  if (!g_playing) pad_tgt = 0.0f;
  if (g_crash != crash_seen) { crash_seen = g_crash; crash_a = 0.13f; }
  for (i = 0; i < frames; i++) { mixL[i] = 0.0f; mixR[i] = 0.0f; ein[i] = 0.0f; }

  /* Melody: FM with the modulator at 2x, plus a fast tine at 4x */
  for (l = 0; l < 8; l++) {
    voice_t *v = &mv[l];
    int gate = m_gate[l];
    float pl = pan_l[l], pr = pan_r[l];
    if (m_trig[l] != v->seen) { v->seen = m_trig[l]; v->att = 1; v->idx = 2.6f; v->tine = 0.5f; }
    if (!gate && !v->att && v->a < 0.0003f) { v->a = 0.0f; continue; }
    for (i = 0; i < frames; i++) {
      float m, x;
      if (v->att) { v->a += 0.05f; if (v->a >= 1.0f) { v->a = 1.0f; v->att = 0; } }
      else if (gate) v->a = 0.35f + (v->a - 0.35f) * 0.99991f;
      else v->a *= 0.99962f;
      m = sintab[(v->ph << 1) >> 24] * v->idx;
      v->idx = 0.45f + (v->idx - 0.45f) * 0.9993f;
      x = (sintab[(v->ph + (uint32_t)(int32_t)(m * RAD2PH)) >> 24] + sintab[(v->ph << 2) >> 24] * v->tine) * v->a * 0.3f;
      v->tine *= 0.9985f;
      v->ph += v->inc;
      ein[i] += x;
      mixL[i] += x * pl;
      mixR[i] += x * pr;
    }
  }

  /* The band, sample by sample so that the steps are exact */
  ptgt = run ? pad_tgt : 0.0f;
  for (i = 0; i < frames; i++) {
    float s = 0.0f, L = 0.0f, R = 0.0f, n, duck, h, dl, dr;
    if (run) {
      if (ctr == 0) seq_step(stp);
      if (++ctr >= len) { ctr = 0; stp++; }
      pos++;
      ptgt = pad_tgt;
    }
    duck = 1.0f - in->duck * kick_a * 2.0f;           /* the pad and stabs make room for the kick */
    if (duck < 0.0f) duck = 0.0f;
    if (bass_a > 0.0002f) {
      float m = sintab[bass_ph >> 24] * bass_idx;
      s += sintab[(bass_ph + (uint32_t)(int32_t)(m * RAD2PH)) >> 24] * bass_a;
      bass_ph += bass_inc;
      bass_a *= in->bass_dec;
      bass_idx *= in->bass_idec;
    }
    if (kick_a > 0.0002f) {
      s += sintab[kick_ph >> 24] * kick_a;
      kick_ph += (uint32_t)(kick_f * HZ2INC);
      kick_f = in->kick_f1 + (kick_f - in->kick_f1) * 0.9984f;
      kick_a *= in->kick_dec;
    }
    nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5;
    n = (float)(int32_t)nz * 4.656613e-10f;
    if (sn_a > 0.0002f || sn_t > 0.0002f) {
      s += n * sn_a + sintab[sn_ph >> 24] * sn_t;
      sn_ph += SN_INC;
      sn_a *= in->sn_dec;
      sn_t *= 0.9982f;
    }
    h = (n - hat_prev) * (hat_a + crash_a);           /* hats and crash: a bit to the right */
    hat_a *= hat_dec;
    crash_a *= 0.99993f;
    hat_prev = n;
    L += h * 0.6f;
    R += h;
    if (cp_a > 0.0005f) {                             /* chord stabs, spread wide */
      float g = cp_a * 0.07f * duck;
      for (k = 0; k < 4; k++) {
        float m = sintab[(cp_ph[k] * in->cp_ratio) >> 24] * cp_idx;
        float x = sintab[(cp_ph[k] + (uint32_t)(int32_t)(m * RAD2PH)) >> 24] * g;
        L += x * cp_pl[k];
        R += x * cp_pr[k];
        cp_ph[k] += cp_inc[k];
      }
      cp_a *= in->cp_dec;
      cp_idx *= in->cp_idec;
    }
    pad_a += (ptgt - pad_a) * 0.0002f;
    if (pad_a > 0.0003f) {                            /* pad: left and right a bit detuned */
      float g = pad_a * duck, fm = in->pad_fm * RAD2PH;
      for (k = 0; k < 4; k++) {
        uint32_t a = pd_ph[k], b = pd_ph[k + 4];
        L += sintab[(a + (uint32_t)(int32_t)(sintab[a >> 24] * fm)) >> 24] * g;
        R += sintab[(b + (uint32_t)(int32_t)(sintab[b >> 24] * fm)) >> 24] * g;
        pd_ph[k] = a + pd_inc[k];
        pd_ph[k + 4] = b + pd_inc[k + 4];
      }
    }
    /* ping-pong echo: left line gets the melody and the right line, right gets the left */
    dl = (float)dl_l[dl_i] * (1.0f / 32768.0f);
    dr = (float)dl_r[dl_i] * (1.0f / 32768.0f);
    dl_l[dl_i] = to16(ein[i] + dr * 0.42f);
    dl_r[dl_i] = to16(dl * 0.42f);
    if (++dl_i >= dT) dl_i = 0;
    L += dl * 0.35f;
    R += dr * 0.35f;
    mixL[i] += L + s;
    mixR[i] += R + s;
  }
  if (run) { song_pos = pos; seq_ctr = ctr; seq_stp = stp; }

  for (i = 0; i < frames; i++) {
    float a = mixL[i] * vol, b = mixR[i] * vol;
    if (a > 0.95f) a = 0.95f; else if (a < -0.95f) a = -0.95f;
    if (b > 0.95f) b = 0.95f; else if (b < -0.95f) b = -0.95f;
    out[2 * i]     = (int32_t)(a * (float)FM1_AUDIO_MAX);
    out[2 * i + 1] = (int32_t)(b * (float)FM1_AUDIO_MAX);
  }
}

/* ---- Playfield drawing ----
 * Each lane is 28 pixels wide; each of its 172 rows has a code (kind and
 * background level). Each frame builds the wanted codes, then blits only the
 * rows whose code changed. Panels lock rows so that the lanes leave them alone. */

static void make_row(uint16_t *d, int l, uint8_t code) {
  int k = code >> 4, i;
  uint16_t b = bgc[l][code & 15];
  for (i = 0; i < 28; i++) d[i] = b;
  switch (k) {
    case K_BEAT: for (i = 0; i < 28; i++) d[i] = beatc[l]; break;
    case K_BAR:  for (i = 0; i < 28; i++) d[i] = barc[l]; break;
    case K_GT:   for (i = 1; i < 27; i++) d[i] = gedge[l]; break;          /* gem: top and bottom edge */
    case K_GH:   d[1] = d[26] = gedge[l]; for (i = 2; i < 26; i++) d[i] = ghi[l]; break;   /* shine */
    case K_GI:   d[1] = d[2] = d[25] = d[26] = gedge[l]; for (i = 3; i < 25; i++) d[i] = gin[l]; break;
    case K_BN: case K_BH: case K_BD: {                                     /* long-note body */
      int v = k - K_BN;
      d[8] = d[9] = d[18] = d[19] = bedge[l][v];
      for (i = 10; i < 18; i++) d[i] = binner[l][v];
      break;
    }
    case K_CN: case K_CH: case K_CD: {                                     /* top of a body */
      int v = k - K_CN;
      for (i = 9; i < 19; i++) d[i] = bedge[l][v];
      break;
    }
    default: break;
  }
}

static void flush(void) {
  int l, r;
  for (l = 0; l < 8; l++) {
    uint8_t m = (uint8_t)BIT(l);
    r = 0;
    while (r < PF_H) {
      int s0;
      if ((lockm[r] & m) || want[l][r] == shown[l][r]) { r++; continue; }
      s0 = r;
      while (r < PF_H && !(lockm[r] & m) && want[l][r] != shown[l][r]) {
        make_row(pix + (r - s0) * 28, l, want[l][r]);
        shown[l][r] = want[l][r];
        r++;
      }
      fm1_lcd_blit((uint32_t)(l * LANE_W + 1), (uint32_t)(PF_Y + s0), 28, (uint32_t)(r - s0),
                   pix, (uint32_t)((r - s0) * 28));
    }
  }
}

/* Row of the playfield where time t is now (PF_H = the line). */
static int ypos(uint32_t t, uint32_t now) {
  return PF_H - (int)((int32_t)(t - now) * PF_H / (int32_t)fall);
}

/* Background: dark lanes, lit from the line upward while a key is down,
 * and a bright flash at the line after a hit. */
static void lanes_bg(uint32_t t, uint32_t kmask) {
  int l, r;
  for (l = 0; l < 8; l++) {
    int kd = (int)((kmask >> l) & 1u), fl = 0;
    int32_t rem = (int32_t)(flash_until[l] - t);
    if (rem > 0) fl = rem > T_FLASH * 2 / 3 ? 15 : rem > T_FLASH / 3 ? 10 : 5;
    for (r = 0; r < PF_H; r++) {
      int d = PF_H - 1 - r, lv = 0;
      if (kd && d < 64) lv = 1 + (63 - d) * 6 / 63;
      if (fl && d < 48) { int f = fl * (48 - d) / 48; if (f > lv) lv = f; }
      want[l][r] = (uint8_t)lv;
    }
  }
}

static void put_row(int l, int r, int kind) {
  if (r >= 0 && r < PF_H) want[l][r] = (uint8_t)((kind << 4) | (want[l][r] & 15));
}

/* Beat and bar lines that fall with the music (maxbeat 0: no end). */
static void grid(uint32_t t, uint32_t beat, uint32_t maxbeat) {
  uint32_t k;
  int l;
  for (k = t / beat; ; k++) {
    uint32_t tk = k * beat;
    int r;
    if (tk < t) continue;
    if (tk - t > fall || (maxbeat && k > maxbeat)) break;
    r = ypos(tk, t);
    if (r >= 0 && r < PF_H)
      for (l = 0; l < 8; l++)
        if ((want[l][r] >> 4) == K_BG) put_row(l, r, (k & 3u) ? K_BEAT : K_BAR);
  }
}

/* A gem whose bottom is at row yb: bright edge, shine, darker inside. */
static void paint_gem(int l, int yb) {
  int i;
  for (i = 0; i < GEM_H; i++)
    put_row(l, yb - GEM_H + i, (i == 0 || i == GEM_H - 1) ? K_GT : (i == 1) ? K_GH : K_GI);
}

/* The body of a long note from row y0 to y1; v: 0 waiting, 1 held, 2 missed. */
static void paint_body(int l, int y0, int y1, int v) {
  int r;
  for (r = y0 < 0 ? 0 : y0; r < y1 && r < PF_H; r++) put_row(l, r, r == y0 ? K_CN + v : K_BN + v);
}

/* ---- Sparks ---- */
typedef struct { uint32_t t0; int16_t x0, y0, vx, vy, dx, dy; uint8_t lane, alive, drawn, ds; uint16_t col; } part_t;
static part_t parts[MAXP];

static void sparks(int l, uint32_t t, int n) {
  int i;
  for (i = 0; i < MAXP && n > 0; i++) {
    part_t *p = &parts[i];
    if (p->alive) continue;
    p->alive = 1;
    p->t0 = t;
    p->lane = (uint8_t)l;
    p->x0 = (int16_t)(l * LANE_W + 13);
    p->y0 = (int16_t)(PF_Y + PF_H - 4);
    p->vx = (int16_t)((int)(rnd() % 141u) - 70);
    p->vy = (int16_t)(110 + (int)(rnd() % 170u));
    p->col = (n & 1) ? WHITE : ghi[l];
    n--;
  }
}

/* Before the flush: the rows under the sparks of the last frame get redrawn. */
static void parts_pre(void) {
  int i, r;
  for (i = 0; i < MAXP; i++) {
    part_t *p = &parts[i];
    if (!p->drawn) continue;
    for (r = p->dy - PF_Y; r < p->dy - PF_Y + p->ds; r++)
      if (r >= 0 && r < PF_H) shown[p->lane][r] = 0xFF;
    p->drawn = 0;
  }
}

/* After the flush: draw the sparks where they are now. */
static void parts_post(uint32_t t) {
  int i, r;
  for (i = 0; i < MAXP; i++) {
    part_t *p = &parts[i];
    float s;
    int x, y, sz, lx0, lx1, ok = 1;
    if (!p->alive) continue;
    s = (float)(int32_t)(t - p->t0) * (1.0f / 44100.0f);
    if (s < 0.0f || s > 0.45f) { p->alive = 0; continue; }
    sz = s < 0.2f ? 3 : 2;
    x = p->x0 + (int)((float)p->vx * s);
    y = p->y0 - (int)((float)p->vy * s - 420.0f * s * s);
    lx0 = p->lane * LANE_W + 1;
    lx1 = p->lane * LANE_W + 29 - sz;
    if (x < lx0) x = lx0;
    if (x > lx1) x = lx1;
    r = y - PF_Y;
    if (r < 0 || r + sz > PF_H) continue;
    for (; r < y - PF_Y + sz; r++) if (lockm[r] & BIT(p->lane)) ok = 0;
    if (!ok) continue;
    fm1_lcd_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)sz, (uint32_t)sz, p->col);
    p->dx = (int16_t)x; p->dy = (int16_t)y; p->ds = (uint8_t)sz; p->drawn = 1;
  }
}

/* ---- Lane borders and the line: they pulse with the beat ---- */
static void draw_seps(uint16_t c) {
  int b;
  for (b = 0; b <= 8; b++) {
    int x = (b == 0) ? 0 : b * LANE_W - 1, w = (b == 0 || b == 8) ? 1 : 2, r = 0;
    uint8_t m = (b == 0) ? 1u : (b == 8) ? 0x80u : (uint8_t)(3u << (b - 1));
    while (r < PF_H) {
      int s0;
      while (r < PF_H && (lockm[r] & m) == m) r++;
      s0 = r;
      while (r < PF_H && (lockm[r] & m) != m) r++;
      if (r > s0) fm1_lcd_fill_rect((uint32_t)x, (uint32_t)(PF_Y + s0), (uint32_t)w, (uint32_t)(r - s0), c);
    }
  }
}

static void pulse(int lv) {
  if (lv == sh_pulse) return;
  sh_pulse = lv;
  draw_seps(sepc[lv]);
  fm1_lcd_fill_rect(0, LINE_Y, 240, 4, linec[lv]);
}

static int beat_level(uint32_t t, uint32_t beat) {
  uint32_t ph = t % beat;
  return ph < beat / 8u ? 3 : ph < beat / 4u ? 2 : ph < beat / 2u ? 1 : 0;
}

/* Pads under the line: dark, lit while the key is down, bright on a hit. */
static void draw_pads(uint32_t t, uint32_t kmask) {
  int l;
  for (l = 0; l < 8; l++) {
    int st = ((int32_t)(flash_until[l] - t) > 0) ? 2 : (int)((kmask >> l) & 1u);
    if (st != pad_sh[l]) {
      pad_sh[l] = st;
      box(28, PAD_H, padc[l][st]);
      box_text(9, 1, lane_label[l], 2, st == 2 ? BLACK : WHITE);
      box_show(l * LANE_W + 1, PAD_Y);
    }
  }
}

static void lock_rows(int r0, int r1, uint8_t m) { for (; r0 < r1; r0++) lockm[r0] |= m; }

static void unlock_rows(int r0, int r1) {
  int r, l;
  for (r = r0; r < r1; r++) {
    lockm[r] = 0;
    for (l = 0; l < 8; l++) shown[l][r] = 0xFF;
  }
  sh_pulse = -1;
}

/* After a full clear: everything must be drawn again. */
static void reset_screen(void) {
  int l, r, i;
  fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);
  for (r = 0; r < PF_H; r++) {
    lockm[r] = 0;
    for (l = 0; l < 8; l++) shown[l][r] = 0xFF;
  }
  for (l = 0; l < 8; l++) { pad_sh[l] = -1; flash_until[l] = 0; }
  for (i = 0; i < MAXP; i++) { parts[i].alive = 0; parts[i].drawn = 0; }
  sh_pulse = -1;
}

/* ---- Title screen ---- */
static uint32_t title_time(void) { return g_blocks * FM1_AUDIO_BLOCK; }

/* A demo note on each 8th or not, and its lane: -1 is none. */
static int demo_lane(uint32_t k) {
  uint32_t h = k * 2654435761u;
  h ^= h >> 15;
  return ((h >> 9) & 1u) ? -1 : (int)((h >> 4) & 7u);
}

static void title_song(void) {
  const song_t *s = &songs[sel];
  char b[48], *e;
  uint32_t total, secs;
  uint16_t c = RGB(s->r, s->g, s->b);
  int n = build_song(s);
  sps = 661500u / s->bpm;
  fall = (uint32_t)s->fall_ms * 441u / 10u;
  demo_q = 2u * sps;
  demo_k = title_time() / demo_q + 1u;
  total = (nbars + 2u) * 16u * sps;
  secs = total / 44100u;

  box(240, 38, PANEL);
  box_rect(0, 0, 240, 1, c);
  box_text(8, 6, "<", 3, GREY);
  box_text(215, 6, ">", 3, GREY);
  box_text_c(6, s->diff, 3, c);
  box_text_c(29, s->style, 1, GREY);
  box_show(0, PF_Y + 74);

  box(240, 38, PANEL);
  box_rect(0, 37, 240, 1, c);
  box_text_c(2, s->name, 2, WHITE);
  e = put_num(b, s->bpm, 0);
  e = put_str(e, " BPM   ");
  e = put_num(e, secs / 60u, 0);
  e = put_str(e, ":");
  e = put_num(e, secs % 60u, 2);
  e = put_str(e, "   ");
  e = put_num(e, (uint32_t)n, 0);
  put_str(e, " NOTES");
  box_text_c(19, b, 1, GREY);
  e = put_str(b, "BEST ");
  put_num(e, best[sel], 7);
  box_text_c(29, b, 1, RGB(120, 230, 255));
  box_show(0, PF_Y + 112);
}

static void title_enter(void) {
  mode = M_TITLE;
  reset_screen();
  lock_rows(4, 42, 0xFF);
  lock_rows(74, 150, 0xFF);
  text_c(6, "8-LANE RHYTHM GAME", 1, GREY, BLACK);
  box(240, 38, PANEL);
  box_rect(0, 0, 240, 1, RGB(120, 230, 255));
  box_rect(0, 37, 240, 1, RGB(120, 230, 255));
  box_text_c(6, "NOTEFALL", 4, RGB(150, 240, 255));
  box_show(0, PF_Y + 4);
  title_song();
  text_c(216, "OCT- / OCT+ (Z / X): SONG", 1, GREY, BLACK);
  text_c(229, "LANE KEY (A - K): START", 1, GREY, BLACK);
  set_leds(BIT(FM1_BTN_OCT_DOWN) | BIT(FM1_BTN_OCT_UP), 0xFFu);
}

/* Demo notes fall behind the panels, at the tempo of the chosen song. */
static void title_frame(void) {
  uint32_t t = title_time(), k, beat = 4u * sps;
  lanes_bg(t, 0);
  grid(t, beat, 0);
  for (k = t / demo_q + 1u; k * demo_q <= t + fall; k++) {
    int l = demo_lane(k);
    if (l >= 0) paint_gem(l, ypos(k * demo_q, t));
  }
  while (demo_k * demo_q <= t) {                 /* a demo note reaches the line */
    int l = demo_lane(demo_k);
    if (l >= 0) { flash_until[l] = t + T_FLASH; sparks(l, t, 5); }
    demo_k++;
  }
  parts_pre();
  flush();
  parts_post(t);
  pulse(beat_level(t, beat));
  draw_pads(t, 0);
}

/* ---- Results screen ---- */
static void game_stop(void) {
  int l;
  g_playing = 0;
  g_paused = 0;
  for (l = 0; l < 8; l++) { m_gate[l] = 0; holding[l] = 0; }
}

static void result_enter(int clear) {
  const song_t *s = song;
  char b[40], *e;
  int nb = score > best[sel];
  uint32_t total = cnt[0] + cnt[1] + cnt[2];
  uint32_t acc = total ? (cnt[0] * 100u + cnt[1] * 50u) / total : 0u;
  game_stop();
  if (nb) best[sel] = score;
  mode = M_RESULT;
  res_t0 = g_blocks;
  reset_screen();
  text_c(10, clear ? "CLEAR!" : "FAILED", 4, clear ? RGB(90, 240, 120) : RGB(255, 80, 80), BLACK);
  text_c(46, s->name, 2, RGB(s->r, s->g, s->b), BLACK);
  e = put_str(b, "SCORE "); put_num(e, score, 7);
  text_c(68, b, 2, WHITE, BLACK);
  if (clear) {
    const char *rk = acc >= 95u ? "S" : acc >= 85u ? "A" : acc >= 70u ? "B" : "C";
    e = put_str(b, "RANK ");
    e = put_str(e, rk);
    e = put_str(e, "  ");
    e = put_num(e, acc, 0);
    put_str(e, "%");
    text_c(92, b, 3, RGB(255, 215, 90), BLACK);
  }
  e = put_str(b, "PERFECT ");   put_num(e, cnt[0], 0);             text_c(124, b, 2, RGB(255, 220, 80), BLACK);
  e = put_str(b, "GOOD ");      put_num(e, cnt[1], 0);             text_c(142, b, 2, RGB(110, 230, 140), BLACK);
  e = put_str(b, "MISS ");      put_num(e, cnt[2], 0);             text_c(160, b, 2, RGB(255, 100, 100), BLACK);
  e = put_str(b, "MAX COMBO "); put_num(e, (uint32_t)maxcombo, 0); text_c(178, b, 2, WHITE, BLACK);
  if (nb) text_c(201, "NEW BEST!", 1, RGB(120, 230, 255), BLACK);
  text_c(224, "LANE KEY: BACK TO TITLE", 1, GREY, BLACK);
  set_leds(0, 0);
}

/* ---- Game ---- */
static void ms_start(uint32_t now) {
  int l;
  ms_on = 1; ms_t = now; ms_val = combo; ms_stage = -1;
  lock_rows(MS_R0, MS_R1, 0x7E);               /* lanes 1..6 */
  g_crash++;
  for (l = 0; l < 8; l++) { flash_until[l] = now + T_FLASH; sparks(l, now, 3); }
}

/* The combo panel: a white flash, then the big number; gone after 1 s. */
static void ms_update(uint32_t now) {
  int32_t el;
  int st;
  if (!ms_on) return;
  el = (int32_t)(now - ms_t);
  st = el < 4410 ? 0 : el < T_MS ? 1 : 2;
  if (st == 2) { ms_on = 0; unlock_rows(MS_R0, MS_R1); return; }
  if (st != ms_stage) {
    char b[12];
    uint16_t gold = RGB(255, 210, 80);
    ms_stage = st;
    put_num(b, (uint32_t)ms_val, 0);
    box(178, MS_R1 - MS_R0, st == 0 ? RGB(255, 245, 200) : PANEL);
    box_rect(0, 0, 178, 2, gold); box_rect(0, 58, 178, 2, gold);
    box_rect(0, 0, 2, 60, gold);  box_rect(176, 0, 2, 60, gold);
    box_text_c(5, b, 5, st == 0 ? RGB(120, 60, 0) : gold);
    box_text_c(43, "COMBO!", 2, st == 0 ? RGB(120, 60, 0) : WHITE);
    box_show(31, PF_Y + MS_R0);
  }
}

static void judge_add(int j, uint32_t now) {
  if (j == J_MISS) {
    combo = 0;
    life -= song->dmg;
    if (life < 0) life = 0;
  } else {
    combo++;
    if (combo > maxcombo) maxcombo = combo;
    score += (j == J_PERFECT ? 100u : 50u) + (uint32_t)(combo < 100 ? combo : 100);
    life += (j == J_PERFECT) ? 2 : 1;
    if (life > 100) life = 100;
    if (combo == 25 || combo == 50 || combo % 100 == 0) ms_start(now);
  }
  cnt[j - 1]++;
  jdg = j; jdg_t = now; jdg_seq++;
}

/* A lane key was pressed: hit the first waiting note of that lane near the line. */
static void try_hit(int l, uint32_t now) {
  int i;
  for (i = first; i < nn; i++) {
    note_t *p = &notes[i];
    uint32_t ts = p->step * sps;
    int32_t dt = (int32_t)(now - ts);
    int j;
    if (dt < -W_GOOD) break;
    if (p->lane != l || p->st != ST_WAIT || dt > W_GOOD) continue;
    j = (((dt < 0) ? -dt : dt) <= W_PERFECT) ? J_PERFECT : J_GOOD;
    judge_add(j, now);
    m_gate[l] = 1;
    m_trig[l]++;
    flash_until[l] = now + T_FLASH;
    sparks(l, now, j == J_PERFECT ? 7 : 4);
    if (p->lng) {
      p->st = ST_HOLD;
      holding[l] = 1;
    } else {
      p->st = ST_DONE;
      gate_off[l] = ts + (uint32_t)p->len * sps;
      if ((int32_t)(gate_off[l] - now) < T_MINGATE) gate_off[l] = now + T_MINGATE;
    }
    return;
  }
}

/* Misses, long notes, the end of short notes, the end of the game. */
static void game_update(uint32_t now) {
  uint32_t held = fm1_in.notes;
  int i, l;
  for (i = first; i < nn; i++) {
    note_t *p = &notes[i];
    uint32_t ts = p->step * sps;
    if ((int32_t)(ts - now) > W_GOOD) break;
    if (p->st == ST_WAIT && (int32_t)(now - ts) > W_GOOD) {
      p->st = ST_DEAD;
      judge_add(J_MISS, now);
    } else if (p->st == ST_HOLD) {
      int32_t left = (int32_t)((uint32_t)(p->step + p->len) * sps - now);
      l = p->lane;
      if (left <= 0 || !((held >> lane_key[l]) & 1u)) {
        int j = (left <= W_PERFECT) ? J_PERFECT : (left <= W_GOOD) ? J_GOOD : J_MISS;
        judge_add(j, now);
        p->st = (j == J_MISS) ? ST_DEAD : ST_DONE;
        holding[l] = 0;
        m_gate[l] = 0;
        if (j != J_MISS) { flash_until[l] = now + T_FLASH; sparks(l, now, 5); }
      }
    }
  }
  for (l = 0; l < 8; l++)
    if (!holding[l] && m_gate[l] && (int32_t)(now - gate_off[l]) >= 0) m_gate[l] = 0;
  while (first < nn) {
    note_t *p = &notes[first];
    uint32_t te = (uint32_t)(p->step + (p->lng ? p->len : 0)) * sps;
    if (p->st < ST_DONE || (int32_t)(now - te) < 0) break;
    first++;
  }
  if (life <= 0) result_enter(0);
  else if ((int32_t)(now - end_t) >= 0) result_enter(1);
}

static void game_frame(uint32_t now, uint32_t kmask) {
  int i;
  lanes_bg(now, kmask);
  grid(now, 4u * sps, (nbars + 1u) * 4u);
  for (i = first; i < nn; i++) {
    note_t *p = &notes[i];
    uint32_t ts = p->step * sps, te = (uint32_t)(p->step + p->len) * sps;
    int l = p->lane, yh, yt;
    if ((int32_t)(ts - now) > (int32_t)fall) break;
    if (p->st == ST_DONE) continue;
    yh = ypos(ts, now);
    yt = ypos(te, now);
    if (p->st == ST_WAIT) {
      if (p->lng) paint_body(l, yt, yh - GEM_H, 0);
      paint_gem(l, yh);
    } else if (p->st == ST_HOLD) {
      paint_body(l, yt, PF_H - GEM_H, 1);
      paint_gem(l, PF_H);
    } else if (p->lng) {
      paint_body(l, yt, yh, 2);                  /* a long note that was missed */
    }
  }
  parts_pre();
  flush();
  parts_post(now);
  pulse((ms_on && (int32_t)(now - ms_t) < 11025) ? 4 : beat_level(now, 4u * sps));
  draw_pads(now, kmask);
}

/* Score, life bar, progress, judgment pop-up and combo: drawn when they change. */
static void hud_draw(uint32_t now) {
  static const char *const jtext[4] = { "", "PERFECT", "GOOD", "MISS" };
  static const uint16_t jcol[4][3] = {
    { 0, 0, 0 },
    { RGB(255, 220, 80), RGB(150, 130, 48), RGB(75, 65, 24) },
    { RGB(110, 230, 140), RGB(66, 138, 84), RGB(33, 69, 42) },
    { RGB(255, 90, 90), RGB(150, 54, 54), RGB(75, 27, 27) }
  };
  char b[24];
  int st;
  int32_t el;
  if (score != sh_score) {
    sh_score = score;
    put_num(b, score, 7);
    text(4, 2, b, 2, WHITE, BLACK);
  }
  if (life != sh_life) {
    int w = life * 108 / 100;
    uint16_t c = life > 50 ? RGB(80, 230, 100) : life > 25 ? RGB(250, 210, 60) : RGB(255, 70, 70);
    sh_life = life;
    if (w > 0) fm1_lcd_fill_rect(127, 4, (uint32_t)w, 9, c);
    if (w < 108) fm1_lcd_fill_rect((uint32_t)(127 + w), 4, (uint32_t)(108 - w), 9, RGB(40, 40, 50));
  }
  {
    int w = (int)(now / (end_t / 240u + 1u));
    if (w > 240) w = 240;
    if (w > sh_prog) { fm1_lcd_fill_rect((uint32_t)sh_prog, 17, (uint32_t)(w - sh_prog), 2, RGB(90, 200, 255)); sh_prog = w; }
  }
  el = (int32_t)(now - jdg_t);
  st = (jdg == J_NONE) ? 4 : el < 3087 ? 0 : el < 14553 ? 1 : el < 19845 ? 2 : el < 24255 ? 3 : 4;
  if (jdg_seq != sh_jseq || st != sh_jstage) {
    sh_jseq = jdg_seq;
    sh_jstage = st;
    box(168, 28, BLACK);
    if (st == 0) box_text_c(0, jtext[jdg], 4, WHITE);                 /* pops up big */
    else if (st < 4) box_text_c(4, jtext[jdg], 3, jcol[jdg][st - 1]); /* then fades */
    box_show(0, STRIP_Y);
  }
  if (combo != sh_combo) {
    sh_combo = combo;
    box(70, 28, BLACK);
    if (combo >= 2) {
      uint16_t c = combo < 25 ? WHITE : combo < 50 ? RGB(120, 230, 255) : combo < 100 ? RGB(255, 210, 80) : RGB(255, 120, 230);
      put_num(b, (uint32_t)combo, 0);
      box_text(68 - text_w(b, 3), 0, b, 3, c);
      box_text(68 - text_w("COMBO", 1), 21, "COMBO", 1, GREY);
    }
    box_show(170, STRIP_Y);
  }
}

/* Lane key LEDs: on while a note of the lane is at the line. PLAY: blinks
 * with the beat, steady while paused. HOME: on (quit). */
static void game_leds(uint32_t now) {
  uint32_t lanes = 0, beat = 4u * sps;
  int i, play;
  if (!paused) {
    for (i = first; i < nn; i++) {
      int32_t dt = (int32_t)(now - notes[i].step * sps);
      if (dt < -W_GOOD) break;
      if (notes[i].st == ST_WAIT && dt <= W_GOOD) lanes |= BIT(notes[i].lane);
    }
    for (i = 0; i < 8; i++) if (holding[i]) lanes |= BIT(i);
  }
  play = paused || (now % beat) < beat / 2u;
  set_leds((play ? BIT(FM1_BTN_PLAY) : 0u) | BIT(FM1_BTN_HOME), lanes);
}

static void pause_toggle(void) {
  int l;
  paused = !paused;
  if (paused) {
    g_paused = 1;
    for (l = 0; l < 8; l++) m_gate[l] = 0;
    if (ms_on) { ms_on = 0; unlock_rows(MS_R0, MS_R1); }
    lock_rows(PS_R0, PS_R1, 0xFF);
    box(240, PS_R1 - PS_R0, PANEL);
    box_rect(0, 0, 240, 1, GREY);
    box_rect(0, PS_R1 - PS_R0 - 1, 240, 1, GREY);
    box_text_c(6, "PAUSED", 3, WHITE);
    box_text_c(32, "PLAY: GO ON   HOME: QUIT", 1, GREY);
    box_show(0, PF_Y + PS_R0);
  } else {
    unlock_rows(PS_R0, PS_R1);
    for (l = 0; l < 8; l++) if (holding[l]) { m_gate[l] = 1; m_trig[l]++; }
    g_paused = 0;
  }
}

static void game_start(void) {
  const song_t *s = &songs[sel];
  int l;
  game_stop();
  song = s;
  sps = 661500u / s->bpm;
  nn = build_song(s);
  first = 0;
  fall = (uint32_t)s->fall_ms * 441u / 10u;
  end_t = (nbars + 2u) * 16u * sps;
  score = 0; combo = 0; maxcombo = 0; life = 100; paused = 0;
  cnt[0] = cnt[1] = cnt[2] = 0;
  jdg = J_NONE; jdg_t = 0; jdg_seq = 0; sh_jseq = -1; sh_jstage = -1;
  ms_on = 0;
  for (l = 0; l < 8; l++) { holding[l] = 0; gate_off[l] = 0; }
  reset_screen();
  sh_score = 0xFFFFFFFFu; sh_life = -1; sh_combo = -1; sh_prog = 0;
  text(100, 5, "LIFE", 1, GREY, BLACK);
  fm1_lcd_fill_rect(125, 2, 112, 13, GREY);
  fm1_lcd_fill_rect(0, 17, 240, 2, RGB(35, 35, 50));
  mode = M_GAME;
  song_pos = 0; seq_ctr = 0; seq_stp = 0;
  g_paused = 0;
  g_playing = 1;
}

/* ---- The three modes ---- */
static void title_loop(uint32_t btn, uint32_t nk) {
  if (btn & BIT(FM1_BTN_OCT_DOWN)) { sel = (sel + 2) % 3; title_song(); }
  if (btn & BIT(FM1_BTN_OCT_UP))   { sel = (sel + 1) % 3; title_song(); }
  if (nk & LANE_KEYS) { game_start(); return; }
  title_frame();
}

static void game_loop(uint32_t btn, uint32_t nk) {
  uint32_t now, kmask = 0, held = fm1_in.notes;
  int l;
  if (btn & BIT(FM1_BTN_HOME)) { game_stop(); title_enter(); return; }
  if (btn & BIT(FM1_BTN_PLAY)) pause_toggle();
  now = song_pos;
  if (!paused) {
    for (l = 0; l < 8; l++) if ((held >> lane_key[l]) & 1u) kmask |= BIT(l);
    for (l = 0; l < 8; l++) if (nk & BIT(lane_key[l])) try_hit(l, now);
    game_update(now);
    if (mode != M_GAME) return;
    ms_update(now);
    game_frame(now, kmask);
  }
  hud_draw(now);
  game_leds(now);
}

static void result_loop(uint32_t nk) {
  int ready = (uint32_t)(g_blocks - res_t0) > 150u;   /* about 0.9 s */
  set_leds(0, ready ? 0xFFu : 0u);
  if (ready && (nk & LANE_KEYS)) title_enter();
}

/* ---- Program ---- */
void setup(void) {
  int i, l, lv;
  for (i = 0; i < 256; i++) sintab[i] = sinf((float)i * (6.2831853f / 256.0f));
  for (i = 0; i < 128; i++) inc_tab[i] = (uint32_t)(440.0f * powf(2.0f, (float)(i - 69) / 12.0f) * HZ2INC);
  for (l = 0; l < 8; l++) {
    const uint8_t *c = lane_rgb[l];
    float p = (float)l / 7.0f;
    for (lv = 0; lv < 16; lv++) {
      float f = (float)lv / 15.0f;
      bgc[l][lv] = colr(c, 0.08f + 0.5f * f, (int)(45.0f * f));
    }
    beatc[l] = colr(c, 0.15f, 14);
    barc[l]  = colr(c, 0.28f, 34);
    gedge[l] = colr(c, 0.8f, 60);
    ghi[l]   = colr(c, 0.5f, 140);
    gin[l]   = colr(c, 0.5f, 0);
    bedge[l][0] = colr(c, 0.85f, 25); binner[l][0] = colr(c, 0.35f, 0);
    bedge[l][1] = WHITE;              binner[l][1] = colr(c, 0.45f, 140);
    bedge[l][2] = RGB(110, 110, 125); binner[l][2] = RGB(55, 55, 68);
    padc[l][0] = colr(c, 0.3f, 0);
    padc[l][1] = colr(c, 0.6f, 20);
    padc[l][2] = colr(c, 0.45f, 150);
    pan_l[l] = 1.0f - 0.45f * p;
    pan_r[l] = 0.55f + 0.45f * p;
    mv[l].inc = inc_tab[lane_midi[l] + 12];   /* the bell sounds one octave up */
  }
}

void loop(void) {
  uint32_t btn = fm1_input_edges(0), nk = fm1_input_note_edges();
  float m = (float)fm1_master() * (1.0f / (float)FM1_MASTER_MAX);
  g_vol = m * m * 0.5f;
  switch (mode) {
    case M_NONE: {
      uint32_t i;
      for (i = 0; i < FM1_NKEY; i++) { led_on[i] = -1; set_led(i, 0); }
      title_enter();
      break;
    }
    case M_TITLE: title_loop(btn, nk); break;
    case M_GAME:  game_loop(btn, nk); break;
    default:      result_loop(nk); break;
  }
}
