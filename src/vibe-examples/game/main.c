/* STAR CAVE - an original arcade game for the M-VAVE FM-1 (FM-1 Studio).
 *
 * A small ship flies through a cave that scrolls from right to left.
 * Collect stars: each star plays the next note of a scale, and 8 stars in a
 * row complete the scale for a bonus. Shoot the mines, keep off the walls.
 *
 * Controls (note keys; in FM-1 Studio the computer keys W, S and J):
 *   note key 8  (W)  up
 *   note key 9  (S)  down
 *   note key 18 (J)  fire (hold for auto fire)
 *   PLAY             start / pause / resume
 *   HOME             back to the title (in the pause and after game over)
 *   MASTER           volume
 *
 * The game's clock is the audio: audio() counts its blocks and loop() runs
 * one game tick for every 3 blocks (about 57 ticks per second).
 */
#include "fm1.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* Layout and constants                                                */
/* ------------------------------------------------------------------ */

#define SCR_W       240
#define SCR_H       240
#define PLAY_Y      20      /* the HUD is rows 0..19 */
#define PLAY_H      220     /* the cave is rows 20..239 */
#define SEG_SHIFT   3       /* the cave is made of 8 px wide segments */
#define RING        64      /* segments kept in memory (512 px) */
#define EDGE        3       /* light edge of the cave walls, in px */
#define SHIP_X      22
#define SHIP_W      14
#define SHIP_H      9
#define NOBJ        32      /* stars, mines, bullets, effects */
#define MAX_BULLETS 3
#define MAX_LIVES   5
#define BUF_N       1024    /* pixels in the drawing buffer */
#define TICK_BLOCKS 3       /* audio blocks per game tick */
#define STEP_BLOCKS 20      /* audio blocks per 16th note (about 129 BPM) */

#define NK_UP    8u         /* note keys used as game controls */
#define NK_DOWN  9u
#define NK_FIRE  18u

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_W      RGB(255, 255, 255)
#define C_LY     RGB(255, 250, 170)
#define C_Y      RGB(255, 220, 40)
#define C_O      RGB(255, 140, 20)
#define C_R      RGB(230, 40, 50)
#define C_K      RGB(130, 130, 150)
#define C_B      RGB(50, 100, 240)
#define C_C      RGB(140, 220, 255)
#define C_ROCK1  RGB(72, 44, 80)
#define C_ROCK2  RGB(60, 36, 68)
#define C_EDGE   RGB(160, 100, 170)
#define C_HUDBG  RGB(12, 10, 30)
#define C_HUDLN  RGB(110, 80, 180)
#define C_TXT    RGB(220, 220, 250)
#define C_DIM    RGB(150, 150, 190)
#define C_PANEL  RGB(20, 16, 48)

/* ------------------------------------------------------------------ */
/* Shared between loop() and audio()                                   */
/* ------------------------------------------------------------------ */

static volatile uint32_t g_blocks;      /* audio blocks so far: the clock */
static volatile int      g_step;        /* music step now sounding */
static volatile int      g_music;       /* 1 while the music plays */
static volatile float    g_vol_target;  /* from the MASTER pot */

enum { EV_MUSIC_START, EV_MUSIC_CONT, EV_MUSIC_STOP, EV_STAR, EV_SHOOT,
       EV_BOOM, EV_DEATH, EV_BONUS };

#define EVQ 16
static volatile uint8_t  evq_type[EVQ], evq_arg[EVQ];
static volatile uint32_t evq_head, evq_tail;

static void ev_push(int type, int arg)
{
  uint32_t h = evq_head;
  if (h - evq_tail >= EVQ) return;          /* queue full: drop it */
  evq_type[h & (EVQ - 1)] = (uint8_t)type;
  evq_arg[h & (EVQ - 1)] = (uint8_t)arg;
  evq_head = h + 1;
}

/* ------------------------------------------------------------------ */
/* Font: 5x7, ASCII 32..90, one byte per column, bit 0 at the top      */
/* ------------------------------------------------------------------ */

static const uint8_t FONT[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},
  {0x14,0x14,0x14,0x14,0x14},{0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
  {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
  {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}
};

/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */

enum { O_NONE, O_STAR, O_MINE, O_BULLET, O_BOOM, O_SPARK, O_SHIP };
static const uint8_t TW[7] = { 0, 9, 9, 6, 11, 11, SHIP_W };
static const uint8_t TH[7] = { 0, 8, 9, 2, 11, 11, SHIP_H };

static const char *const SHIP_BMP[2][SHIP_H] = {
  { "..bb..........",
    "..cbbb........",
    "...ccbbbb.....",
    "ro.cccccwwb...",
    "oyccccccwwccbb",
    "ro.ccccccccb..",
    "...ccbbbb.....",
    "..cbbb........",
    "..bb.........." },
  { "..bb..........",
    "..cbbb........",
    "...ccbbbb.....",
    ".r.cccccwwb...",
    "roccccccwwccbb",
    ".r.ccccccccb..",
    "...ccbbbb.....",
    "..cbbb........",
    "..bb.........." }
};

static const char *const STAR_BMP[8] = {
  "....y....",
  "...yly...",
  "yyyylyyyy",
  ".yylwlyy.",
  "..ylwly..",
  "..yllly..",
  ".yly.yly.",
  ".yy...yy."
};

static const char *const MINE_BMP[9] = {
  "....k....",
  ".k.rrr.k.",
  "..rrrrr..",
  ".rrryrrr.",
  "krryoyrrk",
  ".rrryrrr.",
  "..rrrrr..",
  ".k.rrr.k.",
  "....k...."
};

static const char *const BULLET_BMP[2] = { "bcwwww", "bcwwww" };

static uint16_t pal(char ch)
{
  switch (ch) {
  case 'w': return C_W;
  case 'l': return C_LY;
  case 'y': return C_Y;
  case 'o': return C_O;
  case 'r': return C_R;
  case 'k': return C_K;
  case 'b': return C_B;
  default:  return C_C;
  }
}

static int boom_px(int f, int dx, int dy, uint16_t *c)
{
  int d2 = dx * dx + dy * dy;
  int h = (dx * 7 + dy * 13 + f * 5) & 3;
  switch (f) {
  case 0:
    if (d2 <= 5)  { *c = C_W; return 1; }
    if (d2 <= 13) { *c = C_Y; return 1; }
    return 0;
  case 1:
    if (d2 <= 8)  { *c = C_Y; return 1; }
    if (d2 <= 20) { *c = C_O; return 1; }
    return 0;
  case 2:
    if (d2 >= 10 && d2 <= 26) { *c = h ? C_O : C_R; return 1; }
    if (d2 < 10 && h == 0)    { *c = C_R; return 1; }
    return 0;
  default:
    if (d2 >= 16 && d2 <= 30 && h == 0) { *c = C_R; return 1; }
    return 0;
  }
}

static int spark_px(int f, int dx, int dy, uint16_t *c)
{
  int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  int d = ax > ay ? ax : ay;
  if (!(dx == 0 || dy == 0 || ax == ay)) return 0;
  if (ax == ay && ax > 3) return 0;
  if (d == f + 1 || (f == 0 && d == 0)) { *c = C_W; return 1; }
  if (d == f + 2) { *c = C_Y; return 1; }
  return 0;
}

static int spr_px(int type, int look, int lx, int ly, uint16_t *c)
{
  char ch;
  switch (type) {
  case O_SHIP:   ch = SHIP_BMP[look & 1][ly][lx]; break;
  case O_STAR:
    ch = STAR_BMP[ly][lx];
    if (look) { if (ch == 'l') ch = 'w'; else if (ch == 'w') ch = 'l'; }
    break;
  case O_MINE:
    ch = MINE_BMP[ly][lx];
    if (look) { if (ch == 'o') ch = 'w'; else if (ch == 'y') ch = 'o'; }
    break;
  case O_BULLET: ch = BULLET_BMP[ly][lx]; break;
  case O_BOOM:   return boom_px(look, lx - 5, ly - 5, c);
  case O_SPARK:  return spark_px(look, lx - 5, ly - 5, c);
  default:       return 0;
  }
  if (ch == '.') return 0;
  *c = pal(ch);
  return 1;
}

/* ------------------------------------------------------------------ */
/* Game state                                                          */
/* ------------------------------------------------------------------ */

enum { M_TITLE, M_PLAY, M_PAUSE, M_OVER };

typedef struct {
  uint8_t  type;
  uint16_t t;          /* ticks since it appeared */
  int32_t  x;          /* world x (screen x for bullets) */
  float    y;          /* row in the cave, 0 = top of the cave */
  float    base, amp, ph;
} Obj;

typedef struct { int16_t x, y, w, h; uint8_t look; } Shown;

static int      g_mode, g_need_full;
static int32_t  g_scroll, g_drawn_scroll;
static float    g_sacc;
static uint8_t  g_top[RING], g_bot[RING];
static int32_t  g_gen_seg;
static float    g_cc, g_drift, g_run_off;
static int      g_drift_t, g_run, g_run_gap, g_spawn_cd;

static float    g_sy, g_vy;
static int      g_alive, g_dead_t, g_invuln, g_fire_cd, g_fire_edge;
static uint32_t g_score, g_hi, g_next_extra, g_ticks, g_tick_block;
static int      g_lives, g_streak, g_streak_t;

static Obj      g_obj[NOBJ];
static Shown    g_shown[NOBJ + 1];      /* last one is the ship */
static int32_t  g_drawn_score;
static int      g_drawn_lives;

static uint16_t g_buf[BUF_N];
static uint16_t g_bgcol[PLAY_H];
static int8_t   g_led[FM1_NKEY];

static uint32_t g_rng = 0x2545F491u;

static uint32_t rnd32(void)
{
  uint32_t x = g_rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return g_rng = x;
}
static int rnd(int n) { return (int)(rnd32() % (uint32_t)n); }
static float frand(float a, float b)
{
  return a + (b - a) * (float)(rnd32() & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int top_at(int32_t wc) { return g_top[(wc >> SEG_SHIFT) & (RING - 1)]; }
static int bot_at(int32_t wc) { return PLAY_H - g_bot[(wc >> SEG_SHIFT) & (RING - 1)]; }

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static uint16_t rock(int yr) { return (((yr >> 3) + (yr >> 5)) & 1) ? C_ROCK1 : C_ROCK2; }

static int obj_sx(const Obj *o) { return o->type == O_BULLET ? (int)o->x : (int)(o->x - g_scroll); }

static int obj_look(const Obj *o)
{
  switch (o->type) {
  case O_STAR: return (o->t >> 3) & 1;
  case O_MINE: return (o->t >> 4) & 1;
  case O_BOOM: case O_SPARK: return imin(o->t >> 2, 3);
  default: return 0;
  }
}

static int ship_visible(void)
{
  return g_alive && !(g_invuln > 0 && ((g_invuln >> 2) & 1));
}

/* Paints one sprite into the buffer that holds the region (x0, y0, w, h). */
static void stamp(int type, int look, int sx, int sy, int x0, int y0, int w, int h)
{
  int ax = imax(sx, x0), bx = imin(sx + TW[type], x0 + w);
  int ay = imax(sy, y0), by = imin(sy + TH[type], y0 + h);
  int x, y;
  uint16_t c;
  for (y = ay; y < by; y++)
    for (x = ax; x < bx; x++)
      if (spr_px(type, look, x - sx, y - sy, &c)) g_buf[(y - y0) * w + (x - x0)] = c;
}

/* Builds the picture of a region of the cave (with all sprites) in g_buf. */
static void compose(int x0, int y0, int w, int h)
{
  int c, r, i;
  for (c = 0; c < w; c++) {
    int32_t wc = g_scroll + x0 + c;
    int t = top_at(wc), b = bot_at(wc);
    for (r = 0; r < h; r++) {
      int yr = y0 + r - PLAY_Y;
      uint16_t col;
      if (yr < t)       col = (yr >= t - EDGE) ? C_EDGE : rock(yr);
      else if (yr >= b) col = (yr < b + EDGE) ? C_EDGE : rock(yr);
      else              col = g_bgcol[yr];
      g_buf[r * w + c] = col;
    }
  }
  for (i = 0; i < NOBJ; i++) {
    const Obj *o = &g_obj[i];
    if (o->type)
      stamp(o->type, obj_look(o), obj_sx(o), PLAY_Y + (int)o->y, x0, y0, w, h);
  }
  if (ship_visible())
    stamp(O_SHIP, (g_ticks >> 2) & 1, SHIP_X, PLAY_Y + (int)g_sy, x0, y0, w, h);
}

/* Draws a region of the cave, clipped to the cave, in pieces that fit g_buf. */
static void render_region(int x, int y, int w, int h)
{
  int x1 = imin(x + w, SCR_W), y1 = imin(y + h, SCR_H), rows;
  x = imax(x, 0);
  y = imax(y, PLAY_Y);
  if (x >= x1 || y >= y1) return;
  w = x1 - x;
  rows = BUF_N / w;
  while (y < y1) {
    int hh = imin(rows, y1 - y);
    compose(x, y, w, hh);
    fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)hh, g_buf, (uint32_t)(w * hh));
    y += hh;
  }
}

static void draw_text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
  int cw = 6 * sc, ch = 7 * sc;
  for (; *s; s++, x += cw) {
    int c = (unsigned char)*s, px, py;
    const uint8_t *g;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 90) c = 32;
    g = FONT[c - 32];
    for (py = 0; py < ch; py++)
      for (px = 0; px < cw; px++) {
        int gx = px / sc, gy = py / sc;
        g_buf[py * cw + px] = (gx < 5 && ((g[gx] >> gy) & 1)) ? fg : bg;
      }
    if (x >= 0 && x + cw <= SCR_W && y >= 0 && y + ch <= SCR_H)
      fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)cw, (uint32_t)ch, g_buf, (uint32_t)(cw * ch));
  }
}

static int text_w(const char *s, int sc)
{
  int n = 0;
  while (s[n]) n++;
  return n * 6 * sc - sc;
}

static void draw_text_c(int y, const char *s, int sc, uint16_t fg, uint16_t bg)
{
  draw_text((SCR_W - text_w(s, sc)) / 2, y, s, sc, fg, bg);
}

/* A sprite on a plain background (HUD and title). */
static void draw_icon(int x, int y, int type, int look, uint16_t bg)
{
  int w = TW[type], h = TH[type], lx, ly;
  uint16_t c;
  for (ly = 0; ly < h; ly++)
    for (lx = 0; lx < w; lx++)
      g_buf[ly * w + lx] = spr_px(type, look, lx, ly, &c) ? c : bg;
  fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, g_buf, (uint32_t)(w * h));
}

static void fmt6(char *d, uint32_t v)
{
  int i;
  if (v > 999999u) v = 999999u;
  for (i = 5; i >= 0; i--) { d[i] = (char)('0' + v % 10u); v /= 10u; }
  d[6] = 0;
}

static void panel(int x, int y, int w, int h)
{
  fm1_lcd_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_HUDLN);
  fm1_lcd_fill_rect((uint32_t)(x + 2), (uint32_t)(y + 2), (uint32_t)(w - 4), (uint32_t)(h - 4), C_PANEL);
}

/* HUD: score at the left, lives as small ships at the right. */
static void hud_update(void)
{
  if ((int32_t)g_score != g_drawn_score) {
    char d[7];
    fmt6(d, g_score);
    draw_text(76, 3, d, 2, C_Y, C_HUDBG);
    g_drawn_score = (int32_t)g_score;
  }
  if (g_lives != g_drawn_lives) {
    int i;
    for (i = 0; i < MAX_LIVES; i++) {
      int x = SCR_W - 3 - (i + 1) * 16;
      if (i < g_lives) draw_icon(x, 5, O_SHIP, 0, C_HUDBG);
      else fm1_lcd_fill_rect((uint32_t)x, 5, SHIP_W, SHIP_H, C_HUDBG);
    }
    g_drawn_lives = g_lives;
  }
}

static void hud_full(void)
{
  fm1_lcd_fill_rect(0, 0, SCR_W, PLAY_Y - 1, C_HUDBG);
  fm1_lcd_fill_rect(0, PLAY_Y - 1, SCR_W, 1, C_HUDLN);
  draw_text(4, 3, "SCORE", 2, C_DIM, C_HUDBG);
  g_drawn_score = -1;
  g_drawn_lives = -1;
  hud_update();
}

static void cur_rect(int i, Shown *r)
{
  int type, sx, sy, look, x1, y1;
  r->w = 0; r->h = 0; r->x = 0; r->y = 0; r->look = 0;
  if (i == NOBJ) {
    if (!ship_visible()) return;
    type = O_SHIP; sx = SHIP_X; sy = PLAY_Y + (int)g_sy; look = (g_ticks >> 2) & 1;
  } else {
    const Obj *o = &g_obj[i];
    if (!o->type) return;
    type = o->type; sx = obj_sx(o); sy = PLAY_Y + (int)o->y; look = obj_look(o);
  }
  x1 = imin(sx + TW[type], SCR_W); y1 = imin(sy + TH[type], SCR_H);
  sx = imax(sx, 0); sy = imax(sy, PLAY_Y);
  if (sx >= x1 || sy >= y1) return;
  r->x = (int16_t)sx; r->y = (int16_t)sy;
  r->w = (int16_t)(x1 - sx); r->h = (int16_t)(y1 - sy);
  r->look = (uint8_t)look;
}

static void sync_shown(void)
{
  int i;
  for (i = 0; i <= NOBJ; i++) cur_rect(i, &g_shown[i]);
  g_drawn_scroll = g_scroll;
}

/* Redraws the columns where a wall moved since the last frame. */
static void cave_diff(int32_t os, int32_t ns)
{
  int x, x1;
  if (os == ns) return;
  for (x = 0; x < SCR_W; x = x1) {          /* the ceiling */
    int ot = top_at(os + x), nt = top_at(ns + x);
    x1 = x + 1;
    if (ot == nt) continue;
    while (x1 < SCR_W && top_at(os + x1) == ot && top_at(ns + x1) == nt) x1++;
    {
      int lo = imax(imin(ot, nt) - EDGE, 0), hi = imax(ot, nt);
      render_region(x, PLAY_Y + lo, x1 - x, hi - lo);
    }
  }
  for (x = 0; x < SCR_W; x = x1) {          /* the floor */
    int ob = bot_at(os + x), nb = bot_at(ns + x);
    x1 = x + 1;
    if (ob == nb) continue;
    while (x1 < SCR_W && bot_at(os + x1) == ob && bot_at(ns + x1) == nb) x1++;
    {
      int lo = imin(ob, nb), hi = imin(imax(ob, nb) + EDGE, PLAY_H);
      render_region(x, PLAY_Y + lo, x1 - x, hi - lo);
    }
  }
}

/* Draws only what changed since the last frame. */
static void render_play(void)
{
  int i;
  cave_diff(g_drawn_scroll, g_scroll);
  g_drawn_scroll = g_scroll;
  for (i = 0; i <= NOBJ; i++) {
    Shown n, *s = &g_shown[i];
    int same;
    cur_rect(i, &n);
    same = n.x == s->x && n.y == s->y && n.w == s->w && n.h == s->h;
    if (same && n.look == s->look) continue;
    if (s->w && !same) render_region(s->x, s->y, s->w, s->h);
    if (n.w) render_region(n.x, n.y, n.w, n.h);
    *s = n;
  }
  hud_update();
}

static void draw_play_full(void)
{
  hud_full();
  render_region(0, PLAY_Y, SCR_W, PLAY_H);
  sync_shown();
}

static void draw_title(void)
{
  char line[12] = "HI ";
  fm1_lcd_fill_rect(0, 0, SCR_W, SCR_H, C_HUDBG);
  fm1_lcd_fill_rect(0, 0, SCR_W, 12, C_ROCK1);
  fm1_lcd_fill_rect(0, 12, SCR_W, EDGE, C_EDGE);
  fm1_lcd_fill_rect(0, 225, SCR_W, EDGE, C_EDGE);
  fm1_lcd_fill_rect(0, 228, SCR_W, 12, C_ROCK1);
  draw_text(74, 28, "STAR", 4, C_Y, C_HUDBG);
  draw_text(74, 62, "CAVE", 4, C_C, C_HUDBG);
  draw_icon(40, 34, O_STAR, 0, C_HUDBG);
  draw_icon(194, 74, O_STAR, 1, C_HUDBG);
  draw_icon(192, 32, O_MINE, 0, C_HUDBG);
  draw_icon(36, 72, O_SHIP, 0, C_HUDBG);
  draw_text(60, 110, "W/S  MOVE", 2, C_TXT, C_HUDBG);
  draw_text(60, 132, "J    FIRE", 2, C_TXT, C_HUDBG);
  draw_text(60, 154, "PLAY START", 2, C_Y, C_HUDBG);
  fmt6(line + 3, g_hi);
  draw_text_c(192, line, 2, C_DIM, C_HUDBG);
}

#define PAUSE_X 36
#define PAUSE_Y 80
#define PAUSE_W 168
#define PAUSE_H 84

static void draw_pause(void)
{
  panel(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H);
  draw_text_c(PAUSE_Y + 10, "PAUSED", 3, C_Y, C_PANEL);
  draw_text_c(PAUSE_Y + 42, "PLAY RESUME", 2, C_TXT, C_PANEL);
  draw_text_c(PAUSE_Y + 62, "HOME QUIT", 2, C_TXT, C_PANEL);
}

static void draw_over(int newhi)
{
  char line[16] = "SCORE ";
  panel(24, 56, 192, 128);
  draw_text_c(68, "GAME OVER", 3, C_R, C_PANEL);
  fmt6(line + 6, g_score);
  draw_text_c(100, line, 2, C_TXT, C_PANEL);
  if (newhi) {
    draw_text_c(120, "NEW HI SCORE", 2, C_Y, C_PANEL);
  } else {
    line[0] = 'H'; line[1] = 'I'; line[2] = ' ';
    fmt6(line + 3, g_hi);
    draw_text_c(120, line, 2, C_DIM, C_PANEL);
  }
  draw_text_c(144, "PLAY AGAIN", 2, C_TXT, C_PANEL);
  draw_text_c(164, "HOME TITLE", 2, C_TXT, C_PANEL);
}

/* ------------------------------------------------------------------ */
/* The cave and its objects                                            */
/* ------------------------------------------------------------------ */

static int difficulty(void)
{
  int d = (int)(g_scroll / 1600);
  return d > 10 ? 10 : d;
}

static Obj *obj_new(int type)
{
  int i;
  for (i = 0; i < NOBJ; i++)
    if (!g_obj[i].type) {
      Obj *o = &g_obj[i];
      o->type = (uint8_t)type; o->t = 0; o->x = 0; o->y = 0;
      o->base = 0; o->amp = 0; o->ph = 0;
      return o;
    }
  return 0;
}

static void gen_segment(void)
{
  int32_t s = g_gen_seg++;
  int d = difficulty();
  float gap = 150.0f - 6.5f * (float)d;
  float lo = gap * 0.5f + 6.0f, hi = (float)PLAY_H - gap * 0.5f - 6.0f;
  int t, b, gy0, gy1;

  if (s > 30) {
    if (--g_drift_t <= 0) {
      float m = 2.5f + 0.1f * (float)d;
      g_drift = frand(-m, m);
      g_drift_t = 3 + rnd(7);
    }
    g_cc += g_drift + frand(-1.2f, 1.2f);
  }
  if (g_cc < lo) { g_cc = lo; if (g_drift < 0) g_drift = -g_drift; }
  if (g_cc > hi) { g_cc = hi; if (g_drift > 0) g_drift = -g_drift; }
  t = (int)(g_cc - gap * 0.5f);
  b = PLAY_H - (int)(g_cc + gap * 0.5f);
  g_top[s & (RING - 1)] = (uint8_t)t;
  g_bot[s & (RING - 1)] = (uint8_t)b;

  if (s < 36) return;                     /* nothing in the first screen */
  gy0 = t + 4;                            /* free rows of this segment */
  gy1 = PLAY_H - b - 4;

  if (g_run > 0) {                        /* a line of stars */
    Obj *o;
    int y;
    if (g_run_gap > 0) { g_run_gap--; return; }
    y = (int)(g_cc - 4.0f + g_run_off + 9.0f * sinf((float)g_run * 0.8f));
    y = imax(gy0, imin(y, gy1 - 8));
    o = obj_new(O_STAR);
    if (o) { o->x = s << SEG_SHIFT; o->y = (float)y; }
    g_run--;
    g_run_gap = 1;
    if (g_run == 0) g_spawn_cd = 3;
    return;
  }
  if (g_spawn_cd > 0) { g_spawn_cd--; return; }
  {
    int r = rnd(1000), pm = 40 + 10 * d;
    if (r < 50) {
      g_run = 4 + rnd(4);
      g_run_gap = 0;
      g_run_off = frand(-gap * 0.2f, gap * 0.2f);
    } else if (r < 50 + pm) {
      int amp = 3 + rnd(8), ylo = gy0 + amp, yhi = gy1 - 9 - amp;
      if (yhi > ylo) {
        Obj *o = obj_new(O_MINE);
        if (o) {
          o->x = s << SEG_SHIFT;
          o->base = (float)(ylo + rnd(yhi - ylo + 1));
          o->amp = (float)amp;
          o->ph = frand(0.0f, 6.28f);
          o->y = o->base + o->amp * sinf(o->ph);
        }
      }
      g_spawn_cd = 2;
    } else if (r < 80 + pm) {
      if (gy1 - 8 > gy0) {
        Obj *o = obj_new(O_STAR);
        if (o) { o->x = s << SEG_SHIFT; o->y = (float)(gy0 + rnd(gy1 - 8 - gy0)); }
      }
      g_spawn_cd = 2;
    }
  }
}

static void gen_ahead(void)
{
  while (g_gen_seg <= ((g_scroll + SCR_W + 16) >> SEG_SHIFT)) gen_segment();
}

/* ------------------------------------------------------------------ */
/* Game logic                                                          */
/* ------------------------------------------------------------------ */

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void add_score(uint32_t n)
{
  g_score += n;
  if (g_score >= g_next_extra) {
    g_next_extra += 1000;
    if (g_lives < MAX_LIVES) { g_lives++; ev_push(EV_BONUS, 0); }
  }
}

/* The ship touches a wall: pixel test with the ship's shape (not its flame). */
static int ship_hits_wall(void)
{
  int sy = (int)g_sy, lx, ly;
  for (lx = 0; lx < SHIP_W; lx++) {
    int32_t wc = g_scroll + SHIP_X + lx;
    int t = top_at(wc), b = bot_at(wc);
    for (ly = 0; ly < SHIP_H; ly++) {
      char ch = SHIP_BMP[0][ly][lx];
      if (ch == '.' || ch == 'r' || ch == 'o' || ch == 'y') continue;
      if (sy + ly < t || sy + ly >= b) return 1;
    }
  }
  return 0;
}

static void ship_die(void)
{
  Obj *o = obj_new(O_BOOM);
  if (o) { o->x = g_scroll + SHIP_X + 2; o->y = g_sy - 1.0f; }
  g_alive = 0;
  g_dead_t = 75;
  g_lives--;
  g_streak = 0;
  ev_push(EV_DEATH, 0);
}

static void respawn(void)
{
  int32_t wc = g_scroll + SHIP_X + SHIP_W / 2;
  g_sy = (float)((top_at(wc) + bot_at(wc)) / 2 - SHIP_H / 2);
  g_vy = 0;
  g_alive = 1;
  g_invuln = 120;
}

static void game_over(void);

static void game_tick(void)
{
  int i, d = difficulty();
  int sy;

  g_ticks++;
  g_sacc += 1.2f + 0.1f * (float)d;
  while (g_sacc >= 1.0f) { g_sacc -= 1.0f; g_scroll++; }
  gen_ahead();

  /* the ship */
  if (g_alive) {
    uint32_t n = fm1_in.notes;
    int up = (int)((n >> NK_UP) & 1u), dn = (int)((n >> NK_DOWN) & 1u);
    float tv = (float)(dn - up) * 2.3f;
    g_vy += (tv - g_vy) * 0.35f;
    g_sy += g_vy;
    if (g_sy < 0) { g_sy = 0; g_vy = 0; }
    if (g_sy > (float)(PLAY_H - SHIP_H)) { g_sy = (float)(PLAY_H - SHIP_H); g_vy = 0; }
    if (g_invuln > 0) {                   /* after a respawn: kept inside the cave */
      int lx, mt = 0, mb = PLAY_H;
      for (lx = 0; lx < SHIP_W; lx++) {
        mt = imax(mt, top_at(g_scroll + SHIP_X + lx));
        mb = imin(mb, bot_at(g_scroll + SHIP_X + lx));
      }
      if (mb - SHIP_H - 1 >= mt + 1) {
        if (g_sy < (float)(mt + 1)) g_sy = (float)(mt + 1);
        if (g_sy > (float)(mb - SHIP_H - 1)) g_sy = (float)(mb - SHIP_H - 1);
      }
      g_invuln--;
    }
    if (g_fire_cd > 0) g_fire_cd--;
    if ((g_fire_edge || ((n >> NK_FIRE) & 1u)) && g_fire_cd == 0) {
      int nb = 0;
      for (i = 0; i < NOBJ; i++) if (g_obj[i].type == O_BULLET) nb++;
      if (nb < MAX_BULLETS) {
        Obj *o = obj_new(O_BULLET);
        if (o) {
          o->x = SHIP_X + SHIP_W - 2;
          o->y = (float)((int)g_sy + 4);
          g_fire_cd = 9;
          ev_push(EV_SHOOT, 0);
        }
      }
    }
  } else if (g_dead_t > 0 && --g_dead_t == 0) {
    if (g_lives <= 0) { game_over(); return; }
    respawn();
  }
  g_fire_edge = 0;
  if (g_streak_t > 0 && --g_streak_t == 0) g_streak = 0;

  /* stars, mines, bullets, effects */
  for (i = 0; i < NOBJ; i++) {
    Obj *o = &g_obj[i];
    int sx;
    if (!o->type) continue;
    o->t++;
    sx = obj_sx(o);
    switch (o->type) {
    case O_STAR:
      if (sx + TW[O_STAR] < 0) o->type = O_NONE;
      break;
    case O_MINE:
      o->ph += 0.07f;
      if (o->ph > 6.2831853f) o->ph -= 6.2831853f;
      o->y = o->base + o->amp * sinf(o->ph);
      if (sx + TW[O_MINE] < 0) o->type = O_NONE;
      break;
    case O_BULLET: {
      int j, by = (int)o->y, tip;
      o->x += 5;
      if (o->x >= SCR_W) { o->type = O_NONE; break; }
      tip = (int)o->x + 5;
      if (tip < SCR_W && (by < top_at(g_scroll + tip) || by + 2 > bot_at(g_scroll + tip))) {
        o->type = O_NONE;
        break;
      }
      for (j = 0; j < NOBJ; j++) {
        Obj *m = &g_obj[j];
        if (m->type == O_MINE && overlap((int)o->x, by, 6, 2, obj_sx(m), (int)m->y, 9, 9)) {
          m->type = O_BOOM; m->t = 0; m->x -= 1; m->y -= 1.0f;
          o->type = O_NONE;
          add_score(25);
          ev_push(EV_BOOM, 0);
          break;
        }
      }
      break;
    }
    case O_BOOM:
    case O_SPARK:
      if (o->t >= 16) o->type = O_NONE;
      break;
    }
  }

  /* the ship against stars, mines and walls */
  if (!g_alive) return;
  sy = (int)g_sy;
  for (i = 0; i < NOBJ; i++) {
    Obj *o = &g_obj[i];
    int sx = obj_sx(o), oy = (int)o->y;
    if (o->type == O_STAR && overlap(SHIP_X, sy, SHIP_W, SHIP_H, sx + 1, oy + 1, 7, 6)) {
      o->type = O_SPARK; o->t = 0; o->x -= 1; o->y -= 2.0f;
      ev_push(EV_STAR, g_streak);
      add_score(10);
      g_streak++;
      g_streak_t = 150;
      if (g_streak >= 8) {                /* the whole scale: bonus */
        g_streak = 0;
        add_score(50);
        ev_push(EV_BONUS, 0);
      }
    } else if (o->type == O_MINE && g_invuln == 0 &&
               overlap(SHIP_X + 3, sy + 1, 10, 7, sx + 2, oy + 2, 5, 5)) {
      o->type = O_BOOM; o->t = 0; o->x -= 1; o->y -= 1.0f;
      ship_die();
      return;
    }
  }
  if (g_invuln == 0 && ship_hits_wall()) ship_die();
}

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */

static void start_game(void)
{
  int i;
  g_rng ^= g_blocks * 2654435761u;
  if (!g_rng) g_rng = 1;
  for (i = 0; i < NOBJ; i++) g_obj[i].type = O_NONE;
  g_scroll = 0; g_sacc = 0;
  g_gen_seg = 0; g_cc = PLAY_H * 0.5f; g_drift = 0; g_drift_t = 0;
  g_run = 0; g_run_gap = 0; g_spawn_cd = 0;
  gen_ahead();
  g_score = 0; g_next_extra = 1000; g_lives = 3;
  g_streak = 0; g_streak_t = 0;
  g_sy = (float)(PLAY_H / 2 - SHIP_H / 2); g_vy = 0;
  g_alive = 1; g_dead_t = 0; g_invuln = 0; g_fire_cd = 0; g_fire_edge = 0;
  g_ticks = 0;
  g_mode = M_PLAY;
  draw_play_full();
  ev_push(EV_MUSIC_START, 0);
  g_tick_block = g_blocks;
}

static void game_over(void)
{
  int newhi = g_score > g_hi;
  render_play();
  if (newhi) g_hi = g_score;
  g_mode = M_OVER;
  ev_push(EV_MUSIC_STOP, 0);
  draw_over(newhi);
}

static void go_title(void)
{
  g_mode = M_TITLE;
  g_need_full = 1;
  ev_push(EV_MUSIC_STOP, 0);
}

/* LEDs: on = this does something now. PLAY blinks with the beat while the
 * music plays. fm1_led_key() is called only for an LED that changes. */
static void led(uint32_t id, int on)
{
  if (g_led[id] != on) {
    g_led[id] = (int8_t)on;
    fm1_led_key(id, on);
  }
}

static void update_leds(void)
{
  uint32_t id;
  int beat = g_music && ((g_step & 3) < 2);
  for (id = 0; id < FM1_NKEY; id++) {
    int on = 0;
    if (id == FM1_BTN_PLAY)
      on = (g_mode == M_PLAY) ? beat : 1;
    else if (id == FM1_BTN_HOME)
      on = (g_mode == M_PAUSE || g_mode == M_OVER);
    else if (id == FM1_KEY_NOTE0 + NK_UP || id == FM1_KEY_NOTE0 + NK_DOWN ||
             id == FM1_KEY_NOTE0 + NK_FIRE)
      on = (g_mode == M_PLAY);
    led(id, on);
  }
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

#define SINE_N   1024
#define PH_PER_HZ (4294967296.0f / 44100.0f)

static float    sintab[SINE_N];
static uint32_t note_inc[128];

typedef struct {
  uint32_t pc, pm, ic, im;
  float env, tgt, adec, dep, dmin, ddec;
  int on;
} FmV;

#define NCHIME 3
static FmV v_bass, v_lead, v_chime[NCHIME];
static int a_chime_rr;

static float k_bass_a, k_bass_d, k_lead_a, k_lead_d, k_chime_a, k_chime_d;
static float k_kick_a, k_kick_f, k_hat_a, k_hat2_a;
static float k_shoot_a, k_shoot_sw, k_boom_a, k_boom_c, k_death_a, k_death_sw, k_death_na;

static uint32_t kick_ph;  static float kick_env, kick_f;
static float hat_env, hat_dec, hat_prev;
static uint32_t tone_ph;  static float tone_inc, tone_sw, tone_env, tone_dec;
static float nz_lp, nz_c, nz_cdec, nz_env, nz_dec;
static uint32_t a_rng = 0x9E3779B9u;
static int a_music, a_step, a_blk;
static float a_vol;

/* the tune: A minor, 4 bars of 16 steps (Am F C G) */
static const uint8_t BASS[64] = {
  45,0,45,57, 0,45,57,0, 45,0,45,57, 0,45,55,57,
  41,0,41,53, 0,41,53,0, 41,0,41,53, 0,41,52,53,
  48,0,48,60, 0,48,60,0, 48,0,48,60, 0,48,55,60,
  43,0,43,55, 0,43,55,0, 43,0,43,55, 0,47,50,55
};
static const uint8_t LEAD[64] = {
  76,0,0,74, 0,0,72,0, 69,0,72,0, 74,0,0,0,
  72,0,0,69, 0,0,77,0, 76,0,74,0, 72,0,0,0,
  79,0,0,76, 0,0,72,0, 74,0,76,0, 79,0,81,0,
  79,0,0,74, 0,0,71,0, 74,0,0,0,  0,0,0,0
};
/* the stars' scale: A minor pentatonic, E5 to A6 */
static const uint8_t STAR_NOTES[8] = { 76, 79, 81, 84, 86, 88, 91, 93 };

static float t60(float sec) { return powf(0.001f, 1.0f / (sec * 44100.0f)); }

static void fm_trig(FmV *v, int note, float ratio, float peak, float adec,
                    float dep0, float dmin, float ddec)
{
  v->ic = note_inc[note & 127];
  v->im = (uint32_t)((float)v->ic * ratio);
  v->tgt = peak; v->adec = adec;
  v->dep = dep0; v->dmin = dmin; v->ddec = ddec;
  v->on = 1;
}

static inline float fm_run(FmV *v)
{
  float m = sintab[v->pm >> 22];
  int32_t idx = (int32_t)(v->pc >> 22) + (int32_t)(m * v->dep);
  float s = sintab[(uint32_t)idx & (SINE_N - 1)];
  v->pc += v->ic;
  v->pm += v->im;
  v->env += (v->tgt - v->env) * 0.03f;
  v->tgt *= v->adec;
  v->dep = v->dmin + (v->dep - v->dmin) * v->ddec;
  return s * v->env;
}

static void fm_check(FmV *v)
{
  if (v->on && v->tgt < 1e-5f && v->env < 1e-5f && v->env > -1e-5f) {
    v->on = 0; v->tgt = 0; v->env = 0;
  }
}

static void chime(int note)
{
  FmV *v = &v_chime[a_chime_rr];
  a_chime_rr = (a_chime_rr + 1) % NCHIME;
  fm_trig(v, note, 3.5f, 1.0f, k_chime_a, 420.0f, 20.0f, k_chime_d);
}

static void seq_step(int st)
{
  int pos = st & 15;
  if (BASS[st]) fm_trig(&v_bass, BASS[st], 1.0f, 1.0f, k_bass_a, 330.0f, 90.0f, k_bass_d);
  if (LEAD[st]) fm_trig(&v_lead, LEAD[st], 2.0f, 1.0f, k_lead_a, 230.0f, 110.0f, k_lead_d);
  if ((pos & 3) == 0) { kick_ph = 0; kick_env = 1.0f; kick_f = 110.0f; }
  if ((pos & 3) == 2)  { hat_env = 1.0f; hat_dec = k_hat2_a; }
  else if (pos & 1)    { hat_env = 0.45f; hat_dec = k_hat_a; }
}

static void do_event(int type, int arg)
{
  switch (type) {
  case EV_MUSIC_START: a_step = 0; a_blk = 0; a_music = 1; break;
  case EV_MUSIC_CONT:  a_music = 1; break;
  case EV_MUSIC_STOP:  a_music = 0; break;
  case EV_STAR:        chime(STAR_NOTES[arg & 7]); break;
  case EV_BONUS:       chime(81); chime(88); break;
  case EV_SHOOT:
    tone_ph = 0; tone_inc = 1500.0f * PH_PER_HZ; tone_sw = k_shoot_sw;
    tone_env = 1.0f; tone_dec = k_shoot_a;
    break;
  case EV_BOOM:
    nz_c = 0.6f; nz_cdec = k_boom_c; nz_env = 1.0f; nz_dec = k_boom_a;
    break;
  case EV_DEATH:
    tone_ph = 0; tone_inc = 700.0f * PH_PER_HZ; tone_sw = k_death_sw;
    tone_env = 1.0f; tone_dec = k_death_a;
    nz_c = 0.35f; nz_cdec = k_boom_c; nz_env = 1.0f; nz_dec = k_death_na;
    break;
  }
}

void audio(int32_t *out, int frames)
{
  int i, k;
  float v0 = a_vol, v1 = g_vol_target, dv;
  const float scale = 0.89f * (float)FM1_AUDIO_MAX;

  g_blocks++;
  while (evq_tail != evq_head) {
    uint32_t t = evq_tail;
    do_event(evq_type[t & (EVQ - 1)], evq_arg[t & (EVQ - 1)]);
    evq_tail = t + 1;
  }
  if (a_music) {
    if (a_blk == 0) { seq_step(a_step); g_step = a_step; }
    if (++a_blk >= STEP_BLOCKS) { a_blk = 0; a_step = (a_step + 1) & 63; }
  }
  g_music = a_music;

  dv = (v1 - v0) / (float)frames;
  for (i = 0; i < frames; i++) {
    float l = 0.0f, r = 0.0f, s, n;
    a_rng ^= a_rng << 13; a_rng ^= a_rng >> 17; a_rng ^= a_rng << 5;
    n = (float)(int32_t)a_rng * (1.0f / 2147483648.0f);

    if (v_bass.on) { s = fm_run(&v_bass) * 0.30f; l += s; r += s; }
    if (v_lead.on) { s = fm_run(&v_lead) * 0.13f; l += s * 1.2f; r += s * 0.8f; }
    for (k = 0; k < NCHIME; k++)
      if (v_chime[k].on) {
        s = fm_run(&v_chime[k]) * 0.24f;
        l += s * (k == 1 ? 0.8f : 1.1f);
        r += s * (k == 1 ? 1.1f : 0.8f);
      }
    if (kick_env > 1e-4f) {
      s = sintab[kick_ph >> 22] * kick_env * 0.38f;
      kick_ph += (uint32_t)((45.0f + kick_f) * PH_PER_HZ);
      kick_f *= k_kick_f;
      kick_env *= k_kick_a;
      l += s; r += s;
    }
    if (hat_env > 1e-4f) {
      s = (n - hat_prev) * hat_env * 0.035f;
      hat_env *= hat_dec;
      l += s * 0.7f; r += s * 1.3f;
    }
    hat_prev = n;
    if (tone_env > 1e-4f) {
      s = ((tone_ph & 0x80000000u) ? 0.09f : -0.09f) * tone_env;
      tone_ph += (uint32_t)tone_inc;
      tone_inc *= tone_sw;
      tone_env *= tone_dec;
      l += s; r += s;
    }
    if (nz_env > 1e-4f) {
      nz_lp += nz_c * (n - nz_lp);
      if (nz_c > 0.02f) nz_c *= nz_cdec;
      s = nz_lp * nz_env * 0.7f;
      nz_env *= nz_dec;
      l += s; r += s;
    }

    /* soft limit, then the MASTER volume: never above 0.89 of full scale */
    l *= 1.3f; r *= 1.3f;
    if (l > 3.0f) l = 3.0f; else if (l < -3.0f) l = -3.0f;
    if (r > 3.0f) r = 3.0f; else if (r < -3.0f) r = -3.0f;
    l = l * (27.0f + l * l) / (27.0f + 9.0f * l * l);
    r = r * (27.0f + r * r) / (27.0f + 9.0f * r * r);
    out[2 * i]     = (int32_t)(l * v0 * scale);
    out[2 * i + 1] = (int32_t)(r * v0 * scale);
    v0 += dv;
  }
  a_vol = v1;

  fm_check(&v_bass);
  fm_check(&v_lead);
  for (k = 0; k < NCHIME; k++) fm_check(&v_chime[k]);
  if (kick_env <= 1e-4f) kick_env = 0;
  if (hat_env <= 1e-4f) hat_env = 0;
  if (tone_env <= 1e-4f) tone_env = 0;
  if (nz_env <= 1e-4f) { nz_env = 0; nz_lp = 0; }
}

/* ------------------------------------------------------------------ */
/* setup() and loop()                                                  */
/* ------------------------------------------------------------------ */

static float master_vol(void)
{
  float m = (float)fm1_master() * (1.0f / (float)FM1_MASTER_MAX);
  return m * m;
}

void setup(void)
{
  int i;
  for (i = 0; i < SINE_N; i++) sintab[i] = sinf(6.2831853f * (float)i / (float)SINE_N);
  for (i = 0; i < 128; i++)
    note_inc[i] = (uint32_t)(440.0f * powf(2.0f, (float)(i - 69) / 12.0f) * PH_PER_HZ);
  for (i = 0; i < PLAY_H; i++) {
    int d = i < PLAY_H / 2 ? i : PLAY_H - 1 - i;      /* 0 at the walls, 109 in the middle */
    g_bgcol[i] = RGB(8, 8, 16 + d * 40 / 110);
  }
  k_bass_a = t60(0.45f);  k_bass_d = t60(0.15f);
  k_lead_a = t60(0.55f);  k_lead_d = t60(0.30f);
  k_chime_a = t60(1.2f);  k_chime_d = t60(0.40f);
  k_kick_a = t60(0.30f);  k_kick_f = t60(0.08f);
  k_hat_a = t60(0.04f);   k_hat2_a = t60(0.08f);
  k_shoot_a = t60(0.12f); k_shoot_sw = powf(0.3f, 1.0f / (0.1f * 44100.0f));
  k_boom_a = t60(0.60f);  k_boom_c = t60(0.50f);
  k_death_a = t60(1.0f);  k_death_sw = powf(0.12f, 1.0f / (0.9f * 44100.0f));
  k_death_na = t60(0.9f);

  for (i = 0; i < (int)FM1_NKEY; i++) g_led[i] = 0;    /* all LEDs start off */
  g_vol_target = master_vol();
  a_vol = g_vol_target;
  g_mode = M_TITLE;
  g_need_full = 1;
}

void loop(void)
{
  uint32_t pressed, notes_pressed, now;

  g_vol_target = master_vol();
  pressed = fm1_input_edges(0);
  notes_pressed = fm1_input_note_edges();
  now = g_blocks;

  switch (g_mode) {
  case M_TITLE:
    if (g_need_full) { draw_title(); g_need_full = 0; }
    if (pressed & (1u << FM1_BTN_PLAY)) start_game();
    break;

  case M_PLAY:
    if (pressed & (1u << FM1_BTN_PLAY)) {
      g_mode = M_PAUSE;
      ev_push(EV_MUSIC_STOP, 0);
      draw_pause();
      break;
    }
    if (notes_pressed & (1u << NK_FIRE)) g_fire_edge = 1;
    {
      uint32_t n = (now - g_tick_block) / TICK_BLOCKS, i;
      if (n > 4) { n = 4; g_tick_block = now; }
      else g_tick_block += n * TICK_BLOCKS;
      for (i = 0; i < n && g_mode == M_PLAY; i++) game_tick();
      if (n > 0 && g_mode == M_PLAY) render_play();
    }
    break;

  case M_PAUSE:
    if (pressed & (1u << FM1_BTN_PLAY)) {
      g_mode = M_PLAY;
      render_region(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H);
      ev_push(EV_MUSIC_CONT, 0);
      g_tick_block = now;
    } else if (pressed & (1u << FM1_BTN_HOME)) {
      go_title();
    }
    break;

  case M_OVER:
    if (pressed & (1u << FM1_BTN_PLAY)) start_game();
    else if (pressed & (1u << FM1_BTN_HOME)) go_title();
    break;
  }
  update_leds();
}