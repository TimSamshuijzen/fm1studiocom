/* SWOOPERS - an original arcade shooter in the style of the late 1970s,
 * for the M-VAVE FM-1 and FM-1 Studio.
 *
 * Note key 7 (A) and note key 11 (D) move the ship, note key 18 (J) fires.
 * On the title screen PLAY or the fire key starts a game. PLAY pauses.
 * MASTER is the volume.
 */
#include "fm1.h"
#include <math.h>

/* ---- Colours (RGB565) ---- */
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BLACK   RGB(0, 0, 0)
#define C_WHITE   RGB(255, 255, 255)
#define C_YELLOW  RGB(255, 225, 0)
#define C_ORANGE  RGB(255, 140, 0)
#define C_RED     RGB(255, 40, 40)
#define C_DKRED   RGB(130, 0, 0)
#define C_CYAN    RGB(0, 220, 255)
#define C_BLUE    RGB(50, 100, 255)
#define C_PURPLE  RGB(190, 70, 255)
#define C_GREY    RGB(150, 150, 160)
#define C_MAGENTA RGB(255, 70, 200)

/* ---- Layout and game constants ---- */
#define SCR      240
#define CBUF     4800           /* pixels in the compose buffer */
#define NAL      40             /* aliens */
#define COLSP    18             /* pixels between columns of the formation */
#define ROW_Y(r) (28 + (r) * 13)
#define FX_MIN   6.f
#define FX_MAX   61.f
#define SHIP_Y   213
#define SHIP_SP  1.8f
#define PB_SP    4.6f
#define NEB      8              /* alien bullets */
#define NEX      6              /* explosions */
#define NSTAR    36
#define ARC_T    36             /* ticks of the curve at the start of a dive */
#define TICK_BLK 3              /* audio blocks in a game tick (about 57 Hz) */

/* Note keys used as game controls, and their key ids */
#define K_LEFT   7
#define K_RIGHT  11
#define K_FIRE   18
#define ID_LEFT  (FM1_KEY_NOTE0 + K_LEFT)
#define ID_RIGHT (FM1_KEY_NOTE0 + K_RIGHT)
#define ID_FIRE  (FM1_KEY_NOTE0 + K_FIRE)

/* Sprite kinds: 1..8 are the aliens (type * 2 + wing frame + 1) */
#define K_SHIP  9
#define K_SHIPE 10              /* ship without a missile ready */
#define K_PB    11
#define K_EB    12
#define K_EXS   13              /* small explosion, 8 frames */
#define K_EXB   21              /* big explosion, 8 frames */

/* Sprite slots */
#define S_SHIP 40
#define S_PB   41
#define S_EB   42
#define S_EX   (S_EB + NEB)
#define S_RES  (S_EX + NEX)
#define NSPR   (S_RES + 4)

/* ---- Pictures: '.' is transparent, '1' '2' '3' are the palette colours ---- */
typedef struct { uint8_t w, h; const char *pix; const uint16_t *pal; } img_t;

static const char DRONE0[] =
  "1.........1"
  "1...222...1"
  "11.23232.11"
  ".112222211."
  "..1222221.."
  "...22222..."
  "...2.2.2..."
  "..2.....2..";
static const char DRONE1[] =
  "..........."
  "....222...."
  "...23232..."
  ".112222211."
  "11.22222.11"
  "1..22222..1"
  "1..2.2.2..1"
  "..2.....2..";
static const char FLAG0[] =
  ".....3....."
  "....333...."
  "1..22222..1"
  "11.23232.11"
  ".112222211."
  "..1222221.."
  "...2.2.2..."
  "..2..2..2..";
static const char FLAG1[] =
  ".....3....."
  "....333...."
  "...22222..."
  "1..23232..1"
  "11122222111"
  "1..12221..1"
  "...2.2.2..."
  "..2..2..2..";
static const char SHIP_A[] =
  "......3......"
  "......3......"
  ".....121....."
  ".....111....."
  "..2..111..2.."
  "..2.11211.2.."
  "..211111112.."
  ".21111111112."
  "2211.111.1122"
  "22...1.1...22";
static const char SHIP_B[] =
  "............."
  "............."
  ".....121....."
  ".....111....."
  "..2..111..2.."
  "..2.11211.2.."
  "..211111112.."
  ".21111111112."
  "2211.111.1122"
  "22...1.1...22";
static const char PBUL[] = "11" "22" "22" "22" "22" "22";
static const char EBUL[] = "11" "11" "22" "22" "22";

static const uint16_t PAL_T0[3]   = { C_RED, C_YELLOW, C_BLUE };    /* leaders */
static const uint16_t PAL_T1[3]   = { C_BLUE, C_RED, C_YELLOW };    /* red row */
static const uint16_t PAL_T2[3]   = { C_BLUE, C_PURPLE, C_YELLOW }; /* purple row */
static const uint16_t PAL_T3[3]   = { C_CYAN, C_BLUE, C_RED };      /* blue rows */
static const uint16_t PAL_SHIP[3] = { C_WHITE, C_RED, C_YELLOW };
static const uint16_t PAL_PB[3]   = { C_WHITE, C_YELLOW, C_YELLOW };
static const uint16_t PAL_EB[3]   = { C_WHITE, C_ORANGE, C_ORANGE };

static const img_t IMG[13] = {
  { 0, 0, 0, 0 },
  { 11, 8, FLAG0, PAL_T0 },  { 11, 8, FLAG1, PAL_T0 },
  { 11, 8, DRONE0, PAL_T1 }, { 11, 8, DRONE1, PAL_T1 },
  { 11, 8, DRONE0, PAL_T2 }, { 11, 8, DRONE1, PAL_T2 },
  { 11, 8, DRONE0, PAL_T3 }, { 11, 8, DRONE1, PAL_T3 },
  { 13, 10, SHIP_A, PAL_SHIP }, { 13, 10, SHIP_B, PAL_SHIP },
  { 2, 6, PBUL, PAL_PB }, { 2, 5, EBUL, PAL_EB }
};

/* ---- 5 x 7 font: 7 rows of 5 bits, bit 4 is the left column ---- */
static const uint8_t FONT[40][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
  {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* D */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* N */
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
  {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, /* Y */
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}  /* : */
};

static const uint8_t *glyph(char c) {
  if (c >= '0' && c <= '9') return FONT[c - '0'];
  if (c >= 'A' && c <= 'Z') return FONT[10 + (c - 'A')];
  if (c == '-') return FONT[36];
  if (c == '!') return FONT[37];
  if (c == '.') return FONT[38];
  if (c == ':') return FONT[39];
  return 0;
}

/* ---- Types and state ---- */
typedef struct { int16_t x, y, ox, oy; uint8_t k, ok; } spr_t;  /* o*: what is on the screen */
typedef struct {
  char s[32];
  int16_t x, y, rx, ry, rw, rh;   /* r*: the rectangle on the screen */
  uint8_t sc, dirty;
  uint16_t col;
} txt_t;
typedef struct {
  float y, sp;
  int16_t x, cx, cy, ox, oy;
  uint8_t vis, ovis, ph;
  uint16_t col;
} star_t;
typedef struct {
  float x, y, vx, vy, ang, turn, dsp, ox;
  int16_t t;
  int8_t leader;                  /* for an escort: the alien it follows */
  uint8_t state, type, row, col, phase, shots, esc_n, esc_dead;
} alien_t;
typedef struct { float x, y, vx, vy; uint8_t on; } bul_t;
typedef struct { int16_t x, y; uint8_t on, f, cnt, big; } expl_t;

enum { M_TITLE, M_PLAY, M_OVER };
enum { A_DEAD, A_FORM, A_DIVE, A_RET };
enum { T_SCL, T_SCV, T_HIL, T_HIV, T_WAVE, T_MID1, T_MID2, T_POP, T_PAUSE,
       T_TITLE, T_HEAD, T_ROW0, T_BON1 = T_ROW0 + 4, T_BON2, T_PROMPT, T_HELP, NTXT };
enum { SN_ZAP, SN_BOOM, SN_BIG, SN_DIVE, SN_TUNE, NSN };

static spr_t spr[NSPR];
static txt_t txt[NTXT];
static star_t star[NSTAR];
static alien_t al[NAL];
static bul_t eb[NEB];
static expl_t ex[NEX];
static uint16_t cbuf[CBUF];
static int crx, cry, crw, crh;     /* the rectangle being composed */
static float exd_x[12], exd_y[12]; /* directions of explosion particles */

static int mode, paused, need_full;
static uint32_t tick, last_blk, score, hiscore;
static int lives, wave, ship_alive, dead_timer, pb_on, fire_pending;
static int ready_timer, clear_timer, dive_timer, pop_timer, over_timer;
static float ship_x, pb_x, pb_y, fx, fdir;
static int8_t led_st[FM1_NKEY];
static uint32_t rng = 0x2545F491u;

/* ---- Helpers ---- */
static uint32_t rnd(uint32_t n) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return n ? rng % n : 0u;
}
static int ri(float v) { return (int)floorf(v + 0.5f); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int tw(const char *s, int sc) { int n = slen(s); return n ? n * 6 * sc - sc : 0; }
static int hit(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
static int fmt_u(char *d, uint32_t v) {
  char t[10];
  int n = 0, i;
  do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
  for (i = 0; i < n; i++) d[i] = t[n - 1 - i];
  d[n] = 0;
  return n;
}
static void fmt6(char *d, uint32_t v) {
  int i;
  for (i = 5; i >= 0; i--) { d[i] = (char)('0' + v % 10u); v /= 10u; }
  d[6] = 0;
}

/* ---- Text items ---- */
static void text_set(int i, int x, int y, int sc, uint16_t col, const char *s) {
  txt_t *t = &txt[i];
  int k = 0;
  if (t->x == x && t->y == y && t->sc == sc && t->col == col) {
    while (t->s[k] && t->s[k] == s[k]) k++;
    if (t->s[k] == s[k]) return;               /* nothing changed */
  }
  t->x = (int16_t)x; t->y = (int16_t)y; t->sc = (uint8_t)sc; t->col = col;
  for (k = 0; s[k] && k < 31; k++) t->s[k] = s[k];
  t->s[k] = 0;
  t->dirty = 1;
}
static void text_clear(int i) {
  if (txt[i].s[0]) { txt[i].s[0] = 0; txt[i].dirty = 1; }
}
static void text_center(int i, int y, int sc, uint16_t col, const char *s) {
  text_set(i, (SCR - tw(s, sc)) / 2, y, sc, col, s);
}

/* ---- Compose: draw a rectangle of the scene into cbuf, then blit it ---- */
static int ovl(int x, int y, int w, int h) {
  return x < crx + crw && x + w > crx && y < cry + crh && y + h > cry;
}
static void bfill(int x, int y, int w, int h, uint16_t c) {
  int x1 = x + w, y1 = y + h, i, j;
  if (x < crx) x = crx;
  if (y < cry) y = cry;
  if (x1 > crx + crw) x1 = crx + crw;
  if (y1 > cry + crh) y1 = cry + crh;
  if (x >= x1 || y >= y1) return;
  for (j = y; j < y1; j++) {
    uint16_t *p = cbuf + (j - cry) * crw + (x - crx);
    for (i = x; i < x1; i++) *p++ = c;
  }
}
static void spr_size(int k, int *w, int *h) {
  if (k >= K_EXB) { *w = 29; *h = 29; }
  else if (k >= K_EXS) { *w = 17; *h = 17; }
  else if (k > 0) { *w = IMG[k].w; *h = IMG[k].h; }
  else { *w = 0; *h = 0; }
}
static void draw_img(int k, int sx, int sy) {
  const img_t *im = &IMG[k];
  int x0 = sx, y0 = sy, x1 = sx + im->w, y1 = sy + im->h, x, y;
  if (x0 < crx) x0 = crx;
  if (y0 < cry) y0 = cry;
  if (x1 > crx + crw) x1 = crx + crw;
  if (y1 > cry + crh) y1 = cry + crh;
  for (y = y0; y < y1; y++) {
    const char *row = im->pix + (y - sy) * im->w;
    int base = (y - cry) * crw - crx;
    for (x = x0; x < x1; x++) {
      char c = row[x - sx];
      if (c != '.') cbuf[base + x] = im->pal[c - '1'];
    }
  }
}
static void draw_expl(int k, int sx, int sy) {
  static const uint16_t CA[8] = { C_WHITE, C_YELLOW, C_YELLOW, C_ORANGE, C_ORANGE, C_RED, C_RED, C_DKRED };
  static const uint16_t CB[8] = { C_YELLOW, C_WHITE, C_ORANGE, C_RED, C_YELLOW, C_ORANGE, C_DKRED, C_DKRED };
  int big = k >= K_EXB, f = k - (big ? K_EXB : K_EXS), c = big ? 14 : 8, i;
  int sz = f < 5 ? 2 : 1;
  float r = big ? 2.f + (float)f * 1.5f : 1.f + (float)f * 0.72f;
  if (f < 2) bfill(sx + c - 1, sy + c - 1, 3, 3, C_WHITE);
  for (i = 0; i < 12; i++) {
    float rr = (i & 1) ? r * 0.6f : r;
    bfill(sx + c + (int)(exd_x[i] * rr) - 1, sy + c + (int)(exd_y[i] * rr) - 1, sz, sz,
          (i & 1) ? CB[f] : CA[f]);
    if (big)
      bfill(sx + c + (int)(exd_x[i] * rr * 0.45f), sy + c + (int)(exd_y[i] * rr * 0.45f), 1, 1, CB[f]);
  }
}
static void draw_text(const txt_t *t) {
  int sc = t->sc, x = t->x, row, c;
  const char *p;
  if (!ovl(t->x, t->y, tw(t->s, sc), 7 * sc)) return;
  for (p = t->s; *p; p++, x += 6 * sc) {
    const uint8_t *g;
    if (x >= crx + crw) break;
    if (x + 5 * sc <= crx) continue;
    g = glyph(*p);
    if (!g) continue;
    for (row = 0; row < 7; row++) {
      int y = t->y + row * sc;
      if (y + sc <= cry || y >= cry + crh) continue;
      for (c = 0; c < 5; c++)
        if (g[row] & (0x10 >> c)) bfill(x + c * sc, y, sc, sc, t->col);
    }
  }
}
static void compose_chunk(void) {
  int n = crw * crh, i, w, h;
  for (i = 0; i < n; i++) cbuf[i] = C_BLACK;
  for (i = 0; i < NSTAR; i++) {
    const star_t *s = &star[i];
    if (s->vis && s->cx >= crx && s->cx < crx + crw && s->cy >= cry && s->cy < cry + crh)
      cbuf[(s->cy - cry) * crw + (s->cx - crx)] = s->col;
  }
  for (i = 0; i < NSPR; i++) {
    const spr_t *p = &spr[i];
    if (!p->k) continue;
    spr_size(p->k, &w, &h);
    if (!ovl(p->x, p->y, w, h)) continue;
    if (p->k >= K_EXS) draw_expl(p->k, p->x, p->y);
    else draw_img(p->k, p->x, p->y);
  }
  for (i = 0; i < NTXT; i++)
    if (txt[i].s[0]) draw_text(&txt[i]);
  fm1_lcd_blit((uint32_t)crx, (uint32_t)cry, (uint32_t)crw, (uint32_t)crh, cbuf, (uint32_t)n);
}
static void compose(int x, int y, int w, int h) {
  int rows, y1;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR) w = SCR - x;
  if (y + h > SCR) h = SCR - y;
  if (w <= 0 || h <= 0) return;
  rows = CBUF / w;
  y1 = y + h;
  crx = x; crw = w;
  for (cry = y; cry < y1; cry += rows) {
    crh = (y1 - cry < rows) ? y1 - cry : rows;
    compose_chunk();
  }
}
/* Redraw the old and the new place of something: one rectangle when they are close. */
static void redraw_pair(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  int ux, uy, ux1, uy1;
  if (aw <= 0 || ah <= 0) { compose(bx, by, bw, bh); return; }
  if (bw <= 0 || bh <= 0) { compose(ax, ay, aw, ah); return; }
  ux = ax < bx ? ax : bx;
  uy = ay < by ? ay : by;
  ux1 = ax + aw > bx + bw ? ax + aw : bx + bw;
  uy1 = ay + ah > by + bh ? ay + ah : by + bh;
  if ((ux1 - ux) * (uy1 - uy) <= aw * ah + bw * bh + 64) compose(ux, uy, ux1 - ux, uy1 - uy);
  else { compose(ax, ay, aw, ah); compose(bx, by, bw, bh); }
}

/* Put the game objects into the sprite slots. */
static void sync_sprites(void) {
  int i;
  for (i = 0; i < NSPR; i++) spr[i].k = 0;
  if (mode == M_TITLE) {
    for (i = 0; i < 4; i++) {
      spr[i].k = (uint8_t)(1 + i * 2 + (((tick >> 4) + (uint32_t)i) & 1u));
      spr[i].x = 62;
      spr[i].y = (int16_t)(91 + i * 16);
    }
    return;
  }
  for (i = 0; i < NAL; i++) {
    const alien_t *a = &al[i];
    int fr;
    if (a->state == A_DEAD) continue;
    fr = a->state == A_DIVE ? (int)((tick >> 2) & 1u) : (int)(((tick >> 4) + a->col) & 1u);
    spr[i].k = (uint8_t)(1 + a->type * 2 + fr);
    spr[i].x = (int16_t)ri(a->x);
    spr[i].y = (int16_t)ri(a->y);
  }
  if (ship_alive) {
    spr[S_SHIP].k = pb_on ? K_SHIPE : K_SHIP;
    spr[S_SHIP].x = (int16_t)ri(ship_x);
    spr[S_SHIP].y = SHIP_Y;
  }
  if (pb_on) {
    spr[S_PB].k = K_PB;
    spr[S_PB].x = (int16_t)ri(pb_x);
    spr[S_PB].y = (int16_t)ri(pb_y);
  }
  for (i = 0; i < NEB; i++) {
    if (!eb[i].on) continue;
    spr[S_EB + i].k = K_EB;
    spr[S_EB + i].x = (int16_t)ri(eb[i].x);
    spr[S_EB + i].y = (int16_t)ri(eb[i].y);
  }
  for (i = 0; i < NEX; i++) {
    const expl_t *e = &ex[i];
    int half = e->big ? 14 : 8;
    if (!e->on) continue;
    spr[S_EX + i].k = (uint8_t)((e->big ? K_EXB : K_EXS) + e->f);
    spr[S_EX + i].x = (int16_t)(e->x - half);
    spr[S_EX + i].y = (int16_t)(e->y - half);
  }
  for (i = 0; i < 4; i++) {
    if (i >= lives - 1) continue;
    spr[S_RES + i].k = K_SHIP;
    spr[S_RES + i].x = (int16_t)(4 + i * 16);
    spr[S_RES + i].y = 229;
  }
}

/* Draw only what changed since the last call. */
static void render(void) {
  int i, w, h, ow, oh;
  sync_sprites();
  if (need_full) {
    need_full = 0;
    compose(0, 0, SCR, SCR);
    for (i = 0; i < NSTAR; i++) {
      star[i].ox = star[i].cx; star[i].oy = star[i].cy; star[i].ovis = star[i].vis;
    }
    for (i = 0; i < NSPR; i++) {
      spr[i].ox = spr[i].x; spr[i].oy = spr[i].y; spr[i].ok = spr[i].k;
    }
    for (i = 0; i < NTXT; i++) {
      txt_t *t = &txt[i];
      t->rx = t->x; t->ry = t->y;
      t->rw = (int16_t)tw(t->s, t->sc);
      t->rh = (int16_t)(t->s[0] ? 7 * t->sc : 0);
      t->dirty = 0;
    }
    return;
  }
  for (i = 0; i < NSTAR; i++) {
    star_t *s = &star[i];
    if (s->cx == s->ox && s->cy == s->oy && s->vis == s->ovis) continue;
    redraw_pair(s->ox, s->oy, s->ovis, s->ovis, s->cx, s->cy, s->vis, s->vis);
    s->ox = s->cx; s->oy = s->cy; s->ovis = s->vis;
  }
  for (i = 0; i < NSPR; i++) {
    spr_t *p = &spr[i];
    if (p->x == p->ox && p->y == p->oy && p->k == p->ok) continue;
    spr_size(p->ok, &ow, &oh);
    spr_size(p->k, &w, &h);
    redraw_pair(p->ox, p->oy, ow, oh, p->x, p->y, w, h);
    p->ox = p->x; p->oy = p->y; p->ok = p->k;
  }
  for (i = 0; i < NTXT; i++) {
    txt_t *t = &txt[i];
    if (!t->dirty) continue;
    w = tw(t->s, t->sc);
    h = t->s[0] ? 7 * t->sc : 0;
    redraw_pair(t->rx, t->ry, t->rw, t->rh, t->x, t->y, w, h);
    t->rx = t->x; t->ry = t->y; t->rw = (int16_t)w; t->rh = (int16_t)h;
    t->dirty = 0;
  }
}

/* ---- Sound ----
 * loop() asks for a sound by counting up snd_req[]; audio() starts it when it
 * sees a new count. All sounds are synthesized here. */
#define INV_SR  (1.f / 44100.f)
#define WH_LEN  1.1f            /* seconds of a dive whistle */
#define ZAP_LEN 0.2f            /* seconds of a shot */
#define TSTEP   4851            /* samples in a 16th note of the tune */

static volatile uint32_t g_blocks;          /* audio blocks so far: the game clock */
static volatile uint32_t snd_req[NSN];
static volatile float snd_pan[NSN];
static volatile int snd_hum, snd_paused;
static volatile float snd_rate = 1.6f, snd_pitch = 1.f, snd_vol = 0.5f, beat_ph;

typedef struct { float ph, f, t, gl, gr; int on; } whis_t;
typedef struct { float env, dec, lp, cut, cdec, vol, gl, gr; int on; } noise_t;

static uint32_t snd_ack[NSN];
static float nfreq[64];                     /* MIDI notes 36..99 */
static float k_wh, k_zap, k_nds, k_ndb, k_ncs, k_ncb;
static whis_t wh[2];
static noise_t nz[3];
static float hum_p1, hum_p2, hum_lfo, hum_lp, hum_amp, vib_ph, mvol;
static float zap_ph, zap_f, zap_t;
static int zap_on;
static int tune_on, tune_step, tune_cnt, mel_i, mel_left;
static float lead_ph, lead_f, lead_env, bass_ph, bass_f, bass_env;
static uint32_t nseed = 0x1234567u;

/* The start tune: 32 sixteenth notes in A minor */
static const uint8_t MEL_N[20] = { 69, 72, 76, 81, 79, 76, 77, 74, 76, 72,
                                   74, 76, 77, 79, 81, 79, 76, 80, 83, 81 };
static const uint8_t MEL_L[20] = { 2, 1, 1, 2, 1, 1, 2, 2, 2, 2,
                                   1, 1, 1, 1, 2, 1, 1, 2, 2, 4 };
static const uint8_t BASS[8] = { 45, 48, 38, 45, 38, 41, 40, 45 };

static void snd_init(void) {
  int i;
  for (i = 0; i < 64; i++) nfreq[i] = 440.f * powf(2.f, (float)(i - 33) / 12.f);
  k_wh  = powf(330.f / 1500.f, 1.f / (WH_LEN * 44100.f));
  k_zap = powf(220.f / 1900.f, 1.f / (ZAP_LEN * 44100.f));
  k_nds = powf(0.001f, 1.f / (0.4f * 44100.f));
  k_ndb = powf(0.001f, 1.f / (1.7f * 44100.f));
  k_ncs = powf(0.15f, 1.f / (0.4f * 44100.f));
  k_ncb = powf(0.08f, 1.f / (1.7f * 44100.f));
}

static void pan_gains(float p, float *gl, float *gr) {
  p = clampf(p, 0.f, 1.f);
  *gl = 1.f - 0.5f * p;
  *gr = 0.5f + 0.5f * p;
}

static void start_sound(int id, float p) {
  int i, k = 0;
  if (id == SN_ZAP) {
    zap_on = 1; zap_f = 1900.f; zap_t = 0.f;
  } else if (id == SN_DIVE) {
    whis_t *w;
    k = !wh[0].on ? 0 : (!wh[1].on ? 1 : (wh[1].t > wh[0].t ? 1 : 0));
    w = &wh[k];
    w->on = 1; w->f = 1500.f; w->t = 0.f; w->ph = 0.f;
    pan_gains(p, &w->gl, &w->gr);
  } else if (id == SN_BOOM || id == SN_BIG) {
    noise_t *z;
    for (i = 0; i < 3; i++) {
      if (!nz[i].on) { k = i; break; }
      if (nz[i].env < nz[k].env) k = i;
    }
    z = &nz[k];
    z->on = 1; z->env = 1.f; z->lp = 0.f;
    if (id == SN_BOOM) { z->dec = k_nds; z->cut = 0.5f; z->cdec = k_ncs; z->vol = 0.8f; }
    else               { z->dec = k_ndb; z->cut = 0.3f; z->cdec = k_ncb; z->vol = 1.5f; }
    pan_gains(p, &z->gl, &z->gr);
  } else if (id == SN_TUNE) {
    tune_on = 1; tune_step = 0; tune_cnt = 1; mel_i = 0; mel_left = 0;
  }
}

static void tune_next(void) {
  if (tune_step >= 32) { tune_on = 0; return; }
  if (mel_left == 0 && mel_i < 20) {
    lead_f = nfreq[MEL_N[mel_i] - 36];
    lead_env = 1.f;
    mel_left = MEL_L[mel_i];
    mel_i++;
  }
  if (mel_left > 0) mel_left--;
  if ((tune_step & 1) == 0) {
    int n = BASS[tune_step >> 2] + (((tune_step >> 1) & 1) ? 12 : 0);
    bass_f = nfreq[n - 36];
    bass_env = 1.f;
  }
  tune_step++;
}

void audio(int32_t *out, int frames) {
  int i, k, any_nz = 0;
  float hum_tgt, f1, f2, rate, g, ld;
  g_blocks++;
  if (snd_paused) {
    for (i = 0; i < frames * 2; i++) out[i] = 0;
    return;
  }
  for (k = 0; k < NSN; k++) {
    uint32_t q = snd_req[k];
    if (q != snd_ack[k]) { snd_ack[k] = q; start_sound(k, snd_pan[k]); }
  }
  hum_tgt = (snd_hum && !tune_on) ? 1.f : 0.f;
  f1 = 55.f * snd_pitch * INV_SR;
  f2 = f1 * 1.5f;
  rate = snd_rate * INV_SR;
  mvol += (snd_vol - mvol) * 0.25f;
  g = mvol * 0.6f;
  for (k = 0; k < 3; k++) any_nz |= nz[k].on;

  for (i = 0; i < frames; i++) {
    float m = 0.f, l = 0.f, r = 0.f, v, vib;

    /* the swarm: a low hum that pulses */
    hum_lfo += rate;
    if (hum_lfo >= 1.f) hum_lfo -= 1.f;
    hum_amp += (hum_tgt - hum_amp) * 0.0004f;
    if (hum_amp > 0.0005f) {
      float e = hum_lfo < 0.5f ? hum_lfo * 2.f : 2.f - hum_lfo * 2.f;
      hum_p1 += f1; if (hum_p1 >= 1.f) hum_p1 -= 1.f;
      hum_p2 += f2; if (hum_p2 >= 1.f) hum_p2 -= 1.f;
      v = (hum_p1 < 0.5f ? 1.f : -1.f) + (hum_p2 < 0.5f ? 0.6f : -0.6f);
      hum_lp += (v - hum_lp) * 0.04f;
      m += hum_lp * e * e * hum_amp * 0.15f;
    }

    /* dive whistles: falling, with a little vibrato */
    vib_ph += 8.f * INV_SR;
    if (vib_ph >= 1.f) vib_ph -= 1.f;
    vib = vib_ph < 0.5f ? 4.f * vib_ph - 1.f : 3.f - 4.f * vib_ph;
    for (k = 0; k < 2; k++) {
      whis_t *w = &wh[k];
      float a;
      if (!w->on) continue;
      w->f *= k_wh;
      w->ph += w->f * (1.f + 0.03f * vib) * INV_SR;
      if (w->ph >= 1.f) w->ph -= 1.f;
      w->t += INV_SR;
      a = w->t < 0.02f ? w->t * 50.f : 1.f;
      if (w->t > WH_LEN - 0.2f) a *= (WH_LEN - w->t) * 5.f;
      if (w->t >= WH_LEN) { w->on = 0; a = 0.f; }
      v = (w->ph < 0.5f ? 4.f * w->ph - 1.f : 3.f - 4.f * w->ph) * a * 0.13f;
      l += v * w->gl;
      r += v * w->gr;
    }

    /* zap: a fast falling square wave */
    if (zap_on) {
      zap_f *= k_zap;
      zap_ph += zap_f * INV_SR;
      if (zap_ph >= 1.f) zap_ph -= 1.f;
      zap_t += INV_SR;
      if (zap_t >= ZAP_LEN) zap_on = 0;
      else m += (zap_ph < 0.5f ? 0.1f : -0.1f) * (1.f - zap_t * (1.f / ZAP_LEN));
    }

    /* explosions: noise through a closing low-pass filter */
    if (any_nz) {
      float n;
      nseed ^= nseed << 13; nseed ^= nseed >> 17; nseed ^= nseed << 5;
      n = (float)(int32_t)nseed * (1.f / 2147483648.f);
      for (k = 0; k < 3; k++) {
        noise_t *z = &nz[k];
        if (!z->on) continue;
        z->lp += (n - z->lp) * z->cut;
        z->cut *= z->cdec;
        z->env *= z->dec;
        if (z->env < 0.001f) z->on = 0;
        v = z->lp * z->env * z->vol;
        l += v * z->gl;
        r += v * z->gr;
      }
    }

    /* the start tune: pulse lead and triangle bass */
    if (tune_on && --tune_cnt <= 0) { tune_cnt = TSTEP; tune_next(); }
    ld = tune_on ? 0.99996f : 0.9997f;
    if (lead_env > 0.0005f) {
      lead_ph += lead_f * INV_SR;
      if (lead_ph >= 1.f) lead_ph -= 1.f;
      m += (lead_ph < 0.25f ? 0.75f : -0.25f) * 0.16f * lead_env;
      lead_env *= ld;
    }
    if (bass_env > 0.0005f) {
      bass_ph += bass_f * INV_SR;
      if (bass_ph >= 1.f) bass_ph -= 1.f;
      v = bass_ph < 0.5f ? 4.f * bass_ph - 1.f : 3.f - 4.f * bass_ph;
      m += v * 0.22f * bass_env;
      bass_env *= 0.99994f;
    }

    {
      float L = clampf((m + l) * g, -0.95f, 0.95f);
      float R = clampf((m + r) * g, -0.95f, 0.95f);
      out[2 * i]     = (int32_t)(L * (float)FM1_AUDIO_MAX);
      out[2 * i + 1] = (int32_t)(R * (float)FM1_AUDIO_MAX);
    }
  }
  beat_ph = hum_lfo;
}

/* ---- Game ---- */
static void enter_title(void);
static void game_over(void);

static const uint16_t PTS_FORM[4] = { 60, 50, 40, 30 };
static const uint16_t PTS_DIVE[4] = { 150, 100, 80, 60 };

static void snd(int id, float pan) {
  snd_pan[id] = pan;
  snd_req[id] = snd_req[id] + 1u;
}

/* A value that grows with each wave, up to a limit. */
static float wv(float base, float step, float max) {
  float v = base + step * (float)(wave - 1);
  return v > max ? max : v;
}

static void add_score(uint32_t p) {
  score += p;
  if (score > hiscore) hiscore = score;
}

static void explode(float cx, float cy, int big) {
  int i, k = 0;
  for (i = 0; i < NEX; i++) if (!ex[i].on) { k = i; break; }
  ex[k].on = 1; ex[k].f = 0; ex[k].cnt = 0; ex[k].big = (uint8_t)big;
  ex[k].x = (int16_t)ri(cx); ex[k].y = (int16_t)ri(cy);
}

static void enemy_fire(const alien_t *a) {
  int i;
  for (i = 0; i < NEB; i++) {
    bul_t *b = &eb[i];
    if (b->on) continue;
    b->on = 1;
    b->x = a->x + 4.5f;
    b->y = a->y + 8.f;
    b->vx = clampf((ship_x + 5.5f - b->x) * 0.012f, -0.9f, 0.9f);
    b->vy = wv(2.f, 0.15f, 3.4f);
    return;
  }
}

/* The escorts of leader L go on alone. */
static void release_escorts(int L) {
  int j;
  for (j = 0; j < NAL; j++) {
    alien_t *e = &al[j];
    if (e->state != A_DIVE || e->leader != L) continue;
    e->leader = -1;
    e->phase = 1;
    e->vx = al[L].vx;
    e->vy = al[L].vy;
    e->ox = (float)((int)rnd(41) - 20);
  }
}

static void kill_alien(int i) {
  alien_t *a = &al[i];
  uint32_t pts;
  if (a->state == A_DIVE) {
    if (a->type == 0) {
      char b[8];
      pts = (a->esc_n == 2 && a->esc_dead == 2) ? 800u : (a->esc_n ? 300u : 150u);
      fmt_u(b, pts);
      text_set(T_POP, clampi(ri(a->x) - 2, 0, 220), clampi(ri(a->y), 14, 200), 1,
               pts == 800u ? C_MAGENTA : C_YELLOW, b);
      pop_timer = 70;
    } else {
      pts = PTS_DIVE[a->type];
    }
    if (a->leader >= 0) al[a->leader].esc_dead++;
    release_escorts(i);
  } else {
    pts = PTS_FORM[a->type];
  }
  add_score(pts);
  explode(a->x + 5.5f, a->y + 4.f, 0);
  snd(SN_BOOM, a->x / 240.f);
  a->state = A_DEAD;
  a->leader = -1;
}

static void ship_die(void) {
  ship_alive = 0;
  lives--;
  dead_timer = 150;
  fire_pending = 0;
  explode(ship_x + 6.5f, (float)SHIP_Y + 5.f, 1);
  snd(SN_BIG, ship_x / 240.f);
}

static void start_dive(int i) {
  alien_t *a = &al[i];
  a->state = A_DIVE; a->phase = 0; a->t = 0; a->leader = -1;
  a->esc_n = 0; a->esc_dead = 0;
  a->ang = -1.5707963f;                                   /* up */
  a->turn = (a->x + 5.5f < 120.f ? -1.f : 1.f) * (3.1415927f / (float)ARC_T);
  a->dsp = wv(1.5f, 0.15f, 3.f);
  a->shots = (uint8_t)(wave >= 5 ? 3 : 1 + (wave - 1) / 2);
  a->ox = (float)((int)rnd(41) - 20);
  a->vx = 0.f;
  a->vy = -a->dsp;
  snd(SN_DIVE, a->x / 240.f);
}

static void make_escort(int e, int L, float ox) {
  alien_t *a = &al[e];
  a->state = A_DIVE; a->leader = (int8_t)L; a->ox = ox; a->phase = 1; a->t = 0;
  a->dsp = al[L].dsp; a->shots = al[L].shots; a->vx = 0.f; a->vy = 0.f;
  a->esc_n = 0; a->esc_dead = 0;
  al[L].esc_n++;
}

/* A leader dives, with up to two escorts from the row below it. */
static int launch_leader(void) {
  int c[4], n = 0, i, L, e1 = -1, e2 = -1, d1 = 99, d2 = 99;
  for (i = 0; i < NAL; i++)
    if (al[i].row == 0 && al[i].state == A_FORM && n < 4) c[n++] = i;
  if (!n) return 0;
  L = c[rnd((uint32_t)n)];
  start_dive(L);
  for (i = 0; i < NAL; i++) {
    int d;
    if (al[i].row != 1 || al[i].state != A_FORM) continue;
    d = al[i].col - al[L].col;
    if (d < 0) d = -d;
    if (d > 2) continue;
    if (d < d1) { e2 = e1; d2 = d1; e1 = i; d1 = d; }
    else if (d < d2) { e2 = i; d2 = d; }
  }
  if (e1 >= 0 && e2 >= 0) {
    int lo = al[e1].col < al[e2].col ? e1 : e2, hi = (lo == e1) ? e2 : e1;
    make_escort(lo, L, -13.f);
    make_escort(hi, L, 13.f);
  } else if (e1 >= 0) {
    make_escort(e1, L, al[e1].col <= al[L].col ? -13.f : 13.f);
  }
  return 1;
}

/* The outermost alien of a row that is in the formation, or -1. */
static int pick_edge(int row, int right) {
  int i, best = -1;
  for (i = 0; i < NAL; i++) {
    if (al[i].row != row || al[i].state != A_FORM) continue;
    if (best < 0 || (right ? al[i].col > al[best].col : al[i].col < al[best].col)) best = i;
  }
  return best;
}

static void launch_single(void) {
  int r0 = (int)rnd(4), k, i;
  for (k = 0; k < 5; k++) {
    int row = k < 4 ? 1 + (r0 + k) % 4 : 0;
    i = pick_edge(row, (int)rnd(2));
    if (i >= 0) { start_dive(i); return; }
  }
}

static void try_launch(int alive, int divers) {
  int maxd = 1 + wave, iv = 170 - 20 * (wave - 1);
  if (ready_timer > 0 || !ship_alive || alive == 0) return;
  if (dive_timer > 0) { dive_timer--; return; }
  if (maxd > 6) maxd = 6;
  if (iv < 45) iv = 45;
  if (alive <= 6) { maxd = alive; iv /= 2; }
  if (divers >= maxd) { dive_timer = 10; return; }
  dive_timer = iv * 3 / 4 + (int)rnd((uint32_t)(iv / 2));
  if ((int)rnd(100) < (wave == 1 ? 15 : 25) && launch_leader()) return;
  launch_single();
}

static void formation_step(void) {
  fx += fdir * wv(0.25f, 0.05f, 0.6f);
  if (fx > FX_MAX) { fx = FX_MAX; fdir = -1.f; }
  if (fx < FX_MIN) { fx = FX_MIN; fdir = 1.f; }
}

static void dive_step(int i) {
  alien_t *a = &al[i];
  a->t++;
  if (a->leader >= 0) {                     /* escort: stay beside the leader */
    const alien_t *L = &al[a->leader];
    a->vx = (L->x + a->ox - a->x) * 0.3f;
    a->vy = (L->y + 4.f - a->y) * 0.3f;
  } else if (a->phase == 0) {               /* the curve out of the formation */
    a->ang += a->turn;
    a->vx = cosf(a->ang) * a->dsp;
    a->vy = sinf(a->ang) * a->dsp;
    if (a->t >= ARC_T) a->phase = 1;
  } else {                                  /* swoop down at the ship */
    float vmax = wv(1.3f, 0.1f, 2.4f);
    if (a->y < 170.f) {
      float tx = ship_x + 1.f + a->ox + 26.f * sinf((float)a->t * 0.07f);
      a->vx += clampf((tx - a->x) * 0.01f, -0.09f, 0.09f);
    }
    a->vx = clampf(a->vx, -vmax, vmax);
    a->vy += (a->dsp - a->vy) * 0.06f;
  }
  a->x += a->vx;
  a->y += a->vy;
  if (a->x < 0.f) { a->x = 0.f; if (a->vx < 0.f) a->vx = -a->vx * 0.5f; }
  if (a->x > 229.f) { a->x = 229.f; if (a->vx > 0.f) a->vx = -a->vx * 0.5f; }
  if (a->phase == 1 && a->shots && ship_alive && a->y > 50.f && a->y < 160.f && rnd(40) == 0) {
    enemy_fire(a);
    a->shots--;
  }
  if (a->y > 244.f) {                       /* missed: back to its place from the top */
    release_escorts(i);
    a->state = A_RET; a->t = 0; a->y = -12.f; a->leader = -1;
  }
}

static void aliens_step(void) {
  int i;
  for (i = 0; i < NAL; i++) {
    alien_t *a = &al[i];
    float tx = fx + (float)(a->col * COLSP), ty = (float)ROW_Y(a->row);
    if (a->state == A_FORM) {
      a->x = tx; a->y = ty;
    } else if (a->state == A_RET) {
      float dx, dy, d;
      if (a->t > 0) { a->t--; continue; }
      dx = tx - a->x; dy = ty - a->y;
      d = sqrtf(dx * dx + dy * dy);
      if (d <= 1.8f) { a->x = tx; a->y = ty; a->state = A_FORM; }
      else { a->x += dx / d * 1.8f; a->y += dy / d * 1.8f; }
    } else if (a->state == A_DIVE) {
      dive_step(i);
    }
  }
}

static void bullets_step(void) {
  int i;
  for (i = 0; i < NEB; i++) {
    bul_t *b = &eb[i];
    if (!b->on) continue;
    b->x += b->vx;
    b->y += b->vy;
    if (b->y > 226.f || b->x < 0.f || b->x > 238.f) { b->on = 0; continue; }
    if (ship_alive && hit(b->x, b->y, 2.f, 5.f, ship_x + 2.f, (float)SHIP_Y + 2.f, 9.f, 8.f)) {
      b->on = 0;
      ship_die();
    }
  }
}

static void expl_step(void) {
  int i;
  for (i = 0; i < NEX; i++) {
    expl_t *e = &ex[i];
    if (!e->on) continue;
    if (++e->cnt >= (e->big ? 5 : 3)) {
      e->cnt = 0;
      if (++e->f >= 8) e->on = 0;
    }
  }
}

static void stars_step(void) {
  int i;
  for (i = 0; i < NSTAR; i++) {
    star_t *s = &star[i];
    s->y += s->sp;
    if (s->y >= 228.f) { s->y -= 216.f; s->x = (int16_t)rnd(240); }
    s->cx = s->x;
    s->cy = (int16_t)s->y;
    s->vis = (uint8_t)(((tick + s->ph) & 63u) > 5u);   /* twinkle */
  }
}

static void hud(void) {
  char b[16];
  fmt6(b, score);
  text_set(T_SCL, 4, 2, 1, C_RED, "SCORE");
  text_set(T_SCV, 40, 2, 1, C_WHITE, b);
  fmt6(b, hiscore);
  text_set(T_HIL, 183, 2, 1, C_RED, "HI");
  text_set(T_HIV, 201, 2, 1, C_WHITE, b);
  if (mode == M_TITLE) {
    text_clear(T_WAVE);
  } else {
    b[0] = 'W'; b[1] = 'A'; b[2] = 'V'; b[3] = 'E'; b[4] = ' ';
    fmt_u(b + 5, (uint32_t)wave);
    text_set(T_WAVE, 236 - tw(b, 1), 231, 1, C_CYAN, b);
  }
}

/* The aliens of a new wave fly in from the top to their places. */
static void setup_wave(void) {
  int i;
  char b[16] = "WAVE ";
  fx = 33.f; fdir = 1.f;
  for (i = 0; i < NAL; i++) {
    alien_t *a = &al[i];
    a->state = A_RET; a->leader = -1; a->phase = 0; a->esc_n = 0; a->esc_dead = 0;
    a->t = (int16_t)((4 - a->row) * 12 + (a->col % 5) * 3);
    a->x = fx + (float)(a->col * COLSP) + ((a->col & 1) ? 24.f : -24.f);
    a->y = -14.f;
  }
  for (i = 0; i < NEB; i++) eb[i].on = 0;
  ready_timer = 150; dive_timer = 60; clear_timer = 0;
  fmt_u(b + 5, (uint32_t)wave);
  text_center(T_MID1, 116, 2, C_CYAN, b);
}

static void play_tick(void) {
  int i, alive = 0, divers = 0, all_div = 0, bullets = 0;
  uint32_t notes = fm1_in.notes;
  if (ready_timer > 0 && --ready_timer == 0) { text_clear(T_MID1); text_clear(T_MID2); }
  if (pop_timer > 0 && --pop_timer == 0) text_clear(T_POP);
  formation_step();
  if (ship_alive) {
    if (notes & (1u << K_LEFT)) ship_x -= SHIP_SP;
    if (notes & (1u << K_RIGHT)) ship_x += SHIP_SP;
    ship_x = clampf(ship_x, 2.f, 225.f);
    if (fire_pending && !pb_on) {
      pb_on = 1; pb_x = ship_x + 5.f; pb_y = (float)SHIP_Y - 4.f; fire_pending = 0;
      snd(SN_ZAP, ship_x / 240.f);
    }
  }
  if (fire_pending > 0) fire_pending--;
  if (pb_on) { pb_y -= PB_SP; if (pb_y < 12.f) pb_on = 0; }
  aliens_step();
  if (pb_on) {
    for (i = 0; i < NAL; i++) {
      const alien_t *a = &al[i];
      if (a->state != A_DEAD && hit(pb_x, pb_y, 2.f, 6.f, a->x, a->y, 11.f, 8.f)) {
        kill_alien(i);
        pb_on = 0;
        break;
      }
    }
  }
  bullets_step();
  if (ship_alive) {
    for (i = 0; i < NAL; i++) {
      const alien_t *a = &al[i];
      if (a->state == A_DIVE &&
          hit(a->x + 1.f, a->y + 1.f, 9.f, 6.f, ship_x + 2.f, (float)SHIP_Y + 2.f, 9.f, 8.f)) {
        kill_alien(i);
        ship_die();
        break;
      }
    }
  }
  expl_step();
  for (i = 0; i < NAL; i++) {
    if (al[i].state != A_DEAD) alive++;
    if (al[i].state == A_DIVE) { all_div++; if (al[i].leader < 0) divers++; }
  }
  for (i = 0; i < NEB; i++) bullets += eb[i].on;
  try_launch(alive, divers);
  snd_rate = clampf(1.6f + 0.15f * (float)(wave - 1) + 0.5f * (float)divers, 1.f, 6.f);
  snd_pitch = 1.f + 0.012f * (float)(NAL - alive);
  if (!ship_alive) {
    if (dead_timer > 0) dead_timer--;
    else if (lives <= 0) game_over();
    else if (all_div == 0 && bullets == 0) {
      ship_alive = 1; ship_x = 113.f; ready_timer = 70;
      text_center(T_MID1, 116, 2, C_CYAN, "READY");
    }
  } else if (alive == 0) {
    if (clear_timer == 0) clear_timer = 100;
    else if (--clear_timer == 0) { wave++; setup_wave(); }
  }
}

static void over_tick(void) {
  if (pop_timer > 0 && --pop_timer == 0) text_clear(T_POP);
  formation_step();
  aliens_step();
  bullets_step();
  expl_step();
  if (--over_timer <= 0) enter_title();
}

static void title_tick(void) {
  if ((tick & 31u) == 0u) {
    if (tick & 32u) text_clear(T_PROMPT);
    else text_center(T_PROMPT, 188, 2, C_CYAN, "PRESS PLAY OR J");
  }
}

/* ---- Modes ---- */
static void enter_title(void) {
  int i;
  mode = M_TITLE; paused = 0; snd_paused = 0; snd_hum = 0;
  ship_alive = 0; pb_on = 0; fire_pending = 0; pop_timer = 0;
  for (i = 0; i < NAL; i++) al[i].state = A_DEAD;
  for (i = 0; i < NEB; i++) eb[i].on = 0;
  for (i = 0; i < NEX; i++) ex[i].on = 0;
  for (i = T_WAVE; i < NTXT; i++) text_clear(i);
  text_center(T_TITLE, 34, 3, C_YELLOW, "SWOOPERS");
  text_set(T_HEAD, 84, 76, 1, C_RED, "FORM  DIVE");
  text_set(T_ROW0,     84,  92, 1, C_WHITE, "60     150");
  text_set(T_ROW0 + 1, 84, 108, 1, C_WHITE, "50     100");
  text_set(T_ROW0 + 2, 84, 124, 1, C_WHITE, "40      80");
  text_set(T_ROW0 + 3, 84, 140, 1, C_WHITE, "30      60");
  text_center(T_BON1, 158, 1, C_MAGENTA, "SHOOT THE TWO ESCORTS");
  text_center(T_BON2, 168, 1, C_MAGENTA, "THEN THEIR LEADER: 800");
  text_center(T_PROMPT, 188, 2, C_CYAN, "PRESS PLAY OR J");
  text_center(T_HELP, 216, 1, C_GREY, "A D MOVE  J FIRE  PLAY PAUSE");
  hud();
  need_full = 1;
}

static void start_game(void) {
  int i;
  mode = M_PLAY; paused = 0; snd_paused = 0;
  score = 0; lives = 3; wave = 1;
  ship_alive = 1; ship_x = 113.f; dead_timer = 0;
  pb_on = 0; fire_pending = 0; pop_timer = 0;
  for (i = 0; i < NEX; i++) ex[i].on = 0;
  for (i = T_WAVE; i < NTXT; i++) text_clear(i);
  setup_wave();
  ready_timer = 210;                        /* while the tune plays */
  text_center(T_MID2, 140, 1, C_WHITE, "GET READY");
  snd(SN_TUNE, 0.5f);
  snd_hum = 1;
  hud();
  need_full = 1;
}

static void game_over(void) {
  mode = M_OVER;
  over_timer = 240;
  snd_hum = 0;
  pb_on = 0;
  text_clear(T_MID2);
  text_center(T_MID1, 116, 2, C_RED, "GAME OVER");
}

/* ---- LEDs: on when the key has a use now; PLAY blinks with the hum while the game runs ---- */
static void led(uint32_t id, int on) {
  if (led_st[id] != on) { led_st[id] = (int8_t)on; fm1_led_key(id, on); }
}

static void leds(void) {
  int play = 0, l = 0, r = 0, f = 0;
  if (mode == M_TITLE) {
    play = 1; f = 1;
  } else if (mode == M_PLAY) {
    if (paused) {
      play = 1;
    } else {
      play = beat_ph < 0.5f;
      if (ship_alive) { l = 1; r = 1; f = !pb_on; }
    }
  }
  led(FM1_BTN_PLAY, play);
  led(ID_LEFT, l);
  led(ID_RIGHT, r);
  led(ID_FIRE, f);
}

static void game_tick(void) {
  if (mode == M_PLAY && paused) return;
  tick++;
  stars_step();
  if (mode == M_TITLE) title_tick();
  else if (mode == M_PLAY) play_tick();
  else over_tick();
  hud();
}

/* ---- Program ---- */
void setup(void) {
  static const uint8_t C0[5] = { 3, 1, 1, 0, 0 }, CN[5] = { 4, 8, 8, 10, 10 };
  static const uint16_t SCOL[6] = { C_WHITE, C_CYAN, C_YELLOW, RGB(255, 110, 110),
                                    RGB(130, 150, 255), RGB(210, 130, 255) };
  int i, r, c, n = 0;
  for (i = 0; i < (int)FM1_NKEY; i++) { fm1_led_key((uint32_t)i, 0); led_st[i] = 0; }
  snd_init();
  for (i = 0; i < 12; i++) {
    float an = (float)i * 0.5235988f + 0.2f;
    exd_x[i] = cosf(an);
    exd_y[i] = sinf(an);
  }
  for (r = 0; r < 5; r++) {
    for (c = 0; c < CN[r]; c++) {
      alien_t *a = &al[n++];
      a->row = (uint8_t)r;
      a->col = (uint8_t)(C0[r] + c);
      a->type = (uint8_t)(r < 3 ? r : 3);
      a->state = A_DEAD;
      a->leader = -1;
    }
  }
  for (i = 0; i < NSTAR; i++) {
    star_t *s = &star[i];
    s->x = (int16_t)rnd(240);
    s->y = 12.f + (float)rnd(216);
    s->sp = 0.25f + (float)rnd(90) * 0.01f;
    s->ph = (uint8_t)rnd(64);
    s->col = s->sp < 0.5f ? RGB(90, 90, 130) : SCOL[rnd(6)];
    s->cx = s->x;
    s->cy = (int16_t)s->y;
    s->vis = 1;
  }
  last_blk = g_blocks;
  enter_title();
}

void loop(void) {
  uint32_t be = fm1_input_edges(0);
  uint32_t ne = fm1_input_note_edges();
  uint32_t now;
  int n = 0;

  snd_vol = (float)fm1_master() * (1.f / (float)FM1_MASTER_MAX);

  if (be & (1u << FM1_BTN_PLAY)) {
    if (mode == M_TITLE) {
      start_game();
    } else if (mode == M_PLAY) {
      paused = !paused;
      snd_paused = paused;
      if (paused) text_center(T_PAUSE, 150, 2, C_YELLOW, "PAUSED");
      else text_clear(T_PAUSE);
    }
  }
  if (ne & (1u << K_FIRE)) {
    if (mode == M_TITLE) start_game();
    else if (mode == M_PLAY && !paused) fire_pending = 8;   /* kept for a few ticks */
  }

  /* the game runs at a fixed rate, counted in audio blocks */
  now = g_blocks;
  while ((int32_t)(now - last_blk) >= TICK_BLK) {
    last_blk += TICK_BLK;
    if (++n > 4) { last_blk = now; break; }
    game_tick();
  }

  render();
  leds();
}
