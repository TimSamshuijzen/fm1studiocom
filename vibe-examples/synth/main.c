/* main.c - STRINGS for the M-VAVE FM-1 (FM-1 Studio)
 *
 * A plucked-string instrument made with physical modelling (Karplus-Strong).
 * Each string is a delay line with a loss filter and a fractional-delay
 * allpass for exact tuning. A pluck (filtered noise, or a hammer pulse for the
 * dulcimer, shaped by the pluck position) fills the delay line, and the loop
 * then rings and decays like a real string. A body resonance per instrument
 * and a stereo hall reverb (4-line feedback delay network) follow.
 *
 *   note keys   pluck strings (up to 8 ring at the same time)
 *   ALGORITHM   instrument: harp, nylon guitar, koto, hammered dulcimer
 *   KNOB1       pluck position: soft (middle of the string) to bright (bridge)
 *   KNOB2       how long the strings ring
 *   KNOB3       size of the stereo hall reverb (0 = dry)
 *   KNOB4       speed of the strum (ARP)
 *   ARP         strum: hold a chord, its notes play up and down again and again
 *   SCL         pentatonic harp: each key plays the next note of C D E G A,
 *               G1 to A6, middle C on the 13th key
 *   OCT- OCT+   octave (-1 to +2; with SCL 0 to +1)
 *   MASTER      volume
 */
#include "fm1.h"
#include <math.h>

#define NV      8           /* strings that can ring at the same time */
#define SBUF    1024        /* delay line of each string */
#define NKEYS   27
#define SR      44100.0f
#define PI_F    3.14159265f
#define TWO_PI  6.2831853f
#define THR     0.002f      /* a string under this level counts as silent */
#define OUT_FS  (0.95f * (float)FM1_AUDIO_MAX)

/* ===================== instruments ===================== */

typedef struct {
  const char *name;
  float decay;     /* ring time, relative */
  float damp;      /* loss of the high partials (at 220 Hz) */
  float bright;    /* hardness of the finger, plectrum or hammer */
  float pos;       /* pluck position, relative (smaller: nearer the bridge) */
  float level;
  int   hammer;    /* struck with a hammer, two strings per note */
  float bf[3], bq[3], bg[3];   /* body resonances: Hz, Q, gain */
  float direct;    /* string sound next to the body sound */
  uint8_t r, g, b; /* colour on the screen */
} inst_t;

static const inst_t insts[4] = {
  { "HARP", 1.00f, 0.060f, 0.80f, 1.00f, 1.00f, 0,
    { 110.f, 240.f, 820.f }, { 3.f, 4.f, 2.f }, { 0.40f, 0.35f, 0.18f },
    0.90f, 255, 196, 80 },
  { "NYLON GUITAR", 0.60f, 0.120f, 0.62f, 0.85f, 1.10f, 0,
    { 98.f, 200.f, 410.f }, { 10.f, 7.f, 4.f }, { 0.90f, 0.60f, 0.30f },
    0.70f, 255, 140, 64 },
  { "KOTO", 0.55f, 0.080f, 1.00f, 0.65f, 0.95f, 0,
    { 165.f, 640.f, 1900.f }, { 5.f, 3.f, 4.f }, { 0.40f, 0.45f, 0.35f },
    0.80f, 255, 84, 72 },
  { "HAMMERED DULCIMER", 1.40f, 0.025f, 0.90f, 0.90f, 0.80f, 1,
    { 270.f, 720.f, 2600.f }, { 6.f, 5.f, 3.f }, { 0.40f, 0.40f, 0.30f },
    0.85f, 110, 200, 255 },
};

/* C major pentatonic */
static const uint8_t pent[5] = { 0, 2, 4, 7, 9 };

/* ===================== shared state ===================== */

/* loop() writes, audio() reads */
static volatile int p_inst = 0;
static volatile int p_knob[4] = { 50, 60, 40, 50 };  /* pluck, ring, hall, strum */
static volatile int p_oct = 0, p_scl = 0, p_arp = 0;
static volatile uint32_t p_held = 0, p_master = 700;

#define EVQ 32u
static volatile uint8_t evq[EVQ];          /* note keys pressed */
static volatile uint32_t ev_w = 0, ev_r = 0;

/* audio() writes, loop() reads */
static volatile float key_amp[NKEYS];      /* level of the string of each key */
static volatile uint32_t key_pblk[NKEYS];  /* audio block of the last pluck */
static volatile float key_beta[NKEYS];     /* where it was plucked (0..1 from the bridge) */
static volatile uint32_t g_blocks = 0;     /* audio blocks so far: the clock */
static volatile int arp_led = 0;

/* ===================== strings ===================== */

typedef struct {
  int active, key, midi, inst, dual, age;
  int D, D2;            /* integer delays of string 1 and 2 */
  float eta, eta2;      /* allpass coefficients (fractional delay) */
  float b;              /* loss filter: b, 1-2b, b */
  float tf;             /* for the loop gain */
  float g;              /* loop gain */
  float x1, x2, ax, ay; /* string 1: filter state */
  float u1, u2, bx, by; /* string 2 (dulcimer): filter state */
  uint32_t w;           /* write position */
  float pl, pr;         /* pan */
  float pk, peak;       /* peak in this block, in the last block */
} voice_t;

static voice_t vc[NV];
static float sbuf[NV][SBUF];
static float ex[SBUF];
static float pan_l[NKEYS], pan_r[NKEYS];
static float cur_t60 = 3.f;
static uint32_t rng = 22222u;

static float mixL[FM1_AUDIO_BLOCK], mixR[FM1_AUDIO_BLOCK], mixM[FM1_AUDIO_BLOCK];

/* body resonances */
static float bd_b0[4][3], bd_a1[4][3], bd_a2[4][3];
static float bs1[3], bs2[3];

/* strum */
static uint32_t arp_prev = 0, arp_count = 0;
static int arp_idx = -1, arp_dir = 1, arp_timer = 0;

static float vol = 0.f;

static float rnd(void)
{
  rng = rng * 1664525u + 1013904223u;
  return (float)(rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

static void set_delay(float P, int *D, float *eta)
{
  int d0 = (int)(P - 1.5f);
  float fr = P - 1.f - (float)d0;       /* 0.5 .. 1.5 */
  *D = d0;
  *eta = (1.f - fr) / (1.f + fr);
}

/* the excitation with the pluck position: a circular comb */
static float comb_at(int j, int n, int M)
{
  int q = j - M;
  if (q < 0) q += n;
  return ex[j] - ex[q];
}

/* the MIDI note of a note key */
static int key_note(int key)
{
  int m;
  if (p_scl) {             /* pentatonic harp: key 12 is middle C */
    int st = key - 12, o;
    o = st >= 0 ? st / 5 : -((4 - st) / 5);
    m = 60 + 12 * o + pent[st - 5 * o];
  } else {
    m = 53 + key;          /* note key 0 is F3 */
  }
  m += 12 * p_oct;
  while (m < 29) m += 12;  /* stay in the delay line */
  while (m > 112) m -= 12;
  return m;
}

static void note_on(int key, float vel)
{
  int inst = p_inst;
  const inst_t *ins = &insts[inst];
  int m = key_note(key), i, j, n, M, re = 0, dual;
  voice_t *v = 0;
  float f0, P, kb, bright, beta, amp, pk, sc, keep, b;
  float *buf;
  uint32_t msk;

  f0 = 440.f * powf(2.f, (float)(m - 69) * (1.f / 12.f));
  P = SR / f0;
  dual = ins->hammer && P < 480.f;

  /* the same string again, else a free one, else the quietest */
  for (i = 0; i < NV; i++)
    if (vc[i].active && vc[i].key == key && vc[i].midi == m &&
        vc[i].inst == inst && vc[i].dual == dual) { v = &vc[i]; re = 1; break; }
  if (!v)
    for (i = 0; i < NV; i++)
      if (!vc[i].active) { v = &vc[i]; break; }
  if (!v) {
    float best = 1e9f;
    v = &vc[0];
    for (i = 0; i < NV; i++)
      if (vc[i].peak < best) { best = vc[i].peak; v = &vc[i]; }
  }
  buf = sbuf[v - vc];

  if (dual) {   /* a course of two strings, slightly detuned */
    set_delay(P * 0.99925f, &v->D, &v->eta);
    set_delay(P * 1.00075f, &v->D2, &v->eta2);
  } else {
    set_delay(P, &v->D, &v->eta);
  }

  /* excitation */
  n = v->D;
  kb = (float)p_knob[0] * 0.01f;
  bright = 0.12f + 0.88f * kb * ins->bright;
  beta = (0.38f - 0.31f * kb) * ins->pos;
  M = (int)(beta * (float)n + 0.5f);
  if (M > n - 1) M = n - 1;
  if (M < 1) M = 1;
  if (!ins->hammer) {           /* finger or plectrum: filtered noise */
    float a = 0.04f + 0.96f * bright * bright, s = 0.f;
    for (j = 0; j < 24; j++) s += a * (rnd() - s);
    for (j = 0; j < n; j++) { s += a * (rnd() - s); ex[j] = s; }
  } else {                      /* hammer: a short pulse */
    int pw = 1 + (int)((1.f - bright) * 16.f);
    if (pw > n / 3) pw = n / 3;
    if (pw < 1) pw = 1;
    for (j = 0; j < n; j++) ex[j] = 0.05f * rnd();
    for (j = 0; j < pw; j++)
      ex[j] += 0.5f - 0.5f * cosf(2.f * PI_F * ((float)j + 0.5f) / (float)pw);
  }
  pk = 1e-6f;
  for (j = 0; j < n; j++) {
    float e = fabsf(comb_at(j, n, M));
    if (e > pk) pk = e;
  }
  amp = 0.42f * ins->level * vel;
  sc = amp / pk;
  keep = re ? 0.3f : 0.f;       /* the finger damps a string that rings */
  msk = dual ? 511u : 1023u;
  for (j = 0; j < n; j++) {
    uint32_t ix = (v->w - 1u - (uint32_t)j) & msk;
    buf[ix] = buf[ix] * keep + comb_at(j, n, M) * sc;
  }
  if (dual) {
    float *bb = buf + 512;
    for (j = 0; j < v->D2; j++) {
      uint32_t ix = (v->w - 1u - (uint32_t)j) & msk;
      bb[ix] = bb[ix] * keep + comb_at(j % n, n, M) * sc;
    }
  }
  v->x1 *= keep; v->x2 *= keep; v->ax *= keep; v->ay *= keep;
  v->u1 *= keep; v->u2 *= keep; v->bx *= keep; v->by *= keep;

  b = ins->damp * sqrtf(220.f / f0);
  if (b > 0.25f) b = 0.25f;
  if (b < 0.004f) b = 0.004f;
  v->b = b;
  v->tf = f0 * ins->decay * powf(220.f / f0, 0.35f);
  v->g = expf(-6.9078f / (cur_t60 * v->tf));
  v->active = 1;
  v->key = key;
  v->midi = m;
  v->inst = inst;
  v->dual = dual;
  v->age = 0;
  v->pl = pan_l[key];
  v->pr = pan_r[key];
  v->pk = amp;
  v->peak = amp;

  key_pblk[key] = g_blocks;
  key_beta[key] = (float)M / (float)n;
}

static void arp_step(uint32_t held)
{
  int list[NKEYS], n = 0, k;
  for (k = 0; k < NKEYS; k++)
    if ((held >> k) & 1u) list[n++] = k;
  if (n == 0) return;
  if (n == 1) {
    arp_idx = 0;
  } else {
    arp_idx += arp_dir;
    if (arp_idx >= n) { arp_idx = n - 2; arp_dir = -1; }
    else if (arp_idx < 0) { arp_idx = 1; arp_dir = 1; }
  }
  if (arp_idx >= n) arp_idx = n - 1;
  if (arp_idx < 0) arp_idx = 0;
  note_on(list[arp_idx], 0.75f + 0.25f * fabsf(rnd()));
}

static void render(int n0, int n1)
{
  int i, s;
  for (i = 0; i < NV; i++) {
    voice_t *v = &vc[i];
    float *buf, g, cb, cc, eta, x1, x2, ax, ay, pl, pr, pk;
    uint32_t w, D;
    if (!v->active) continue;
    buf = sbuf[i];
    g = v->g;
    cb = v->b * g;
    cc = (1.f - 2.f * v->b) * g;
    eta = v->eta;
    x1 = v->x1; x2 = v->x2; ax = v->ax; ay = v->ay;
    w = v->w;
    D = (uint32_t)v->D;
    pl = v->pl; pr = v->pr; pk = v->pk;
    if (!v->dual) {
      for (s = n0; s < n1; s++) {
        float x = buf[(w - D) & 1023u];
        float f = cb * (x + x2) + cc * x1;
        float y;
        x2 = x1; x1 = x;
        y = eta * (f - ay) + ax;
        ax = f; ay = y;
        buf[w & 1023u] = y;
        w++;
        mixL[s] += y * pl;
        mixR[s] += y * pr;
        mixM[s] += y;
        y = fabsf(y);
        if (y > pk) pk = y;
      }
    } else {
      float *bb = buf + 512;
      float eta2 = v->eta2, u1 = v->u1, u2 = v->u2, bx = v->bx, by = v->by;
      uint32_t D2 = (uint32_t)v->D2;
      for (s = n0; s < n1; s++) {
        float x = buf[(w - D) & 511u];
        float f = cb * (x + x2) + cc * x1;
        float xb = bb[(w - D2) & 511u];
        float fb = cb * (xb + u2) + cc * u1;
        float ya, yb, y;
        x2 = x1; x1 = x;
        u2 = u1; u1 = xb;
        ya = eta * (f - ay) + ax;
        ax = f; ay = ya;
        yb = eta2 * (fb - by) + bx;
        bx = fb; by = yb;
        buf[w & 511u] = ya;
        bb[w & 511u] = yb;
        w++;
        y = 0.5f * (ya + yb);
        mixL[s] += y * pl;
        mixR[s] += y * pr;
        mixM[s] += y;
        y = fabsf(y);
        if (y > pk) pk = y;
      }
      v->u1 = u1; v->u2 = u2; v->bx = bx; v->by = by;
    }
    v->x1 = x1; v->x2 = x2; v->ax = ax; v->ay = ay;
    v->w = w;
    v->pk = pk;
  }
}

/* ===================== hall reverb ===================== */

#define RV_AP 4
static const int ap_len[RV_AP] = { 142, 107, 379, 277 };
static const int ap_off[RV_AP] = { 0, 142, 249, 628 };
static const float ap_g[RV_AP] = { 0.70f, 0.70f, 0.62f, 0.62f };
static float ap_mem[905];
static int ap_idx[RV_AP];

#define RV_N 4
static const int fd_len[RV_N] = { 1423, 1637, 1879, 2113 };
static const int fd_off[RV_N] = { 0, 1423, 3060, 4939 };
static float fd_mem[7052];
static int fd_idx[RV_N];
static float fd_lp[RV_N], fd_g[RV_N];

static float rv_in = 0.5f, rv_wet = 0.f, rv_dry = 1.f, rv_damp = 0.5f;
static int rv_last = -1;

static void rv_set(int k)
{
  float sz = (float)k * 0.01f;
  float t60 = 0.4f * powf(17.5f, sz);       /* 0.4 s .. 7 s */
  int i;
  for (i = 0; i < RV_N; i++)
    fd_g[i] = powf(10.f, -3.f * (float)fd_len[i] / (t60 * SR));
  rv_in = 0.6f * sqrtf(1.f - fd_g[1] * fd_g[1]);
  rv_wet = 0.9f * sz;
  rv_dry = 1.f - 0.3f * sz;
  rv_damp = 0.55f - 0.25f * sz;
}

static float sclip(float x)
{
  float x2;
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

/* ===================== screen: data ===================== */

#define TITLE_Y   4
#define LABEL_Y   24
#define VALUE_Y   34
#define GAUGE_Y   51
#define BRIDGE1_Y 58
#define STR_Y     61
#define STR_H     160
#define BAND_H    16          /* the strings are drawn in bands of rows */
#define NBANDS    (STR_H / BAND_H)
#define BRIDGE2_Y 221
#define STATUS_Y  230
#define AMAX      5.0f        /* widest swing of a string, pixels */
#define FLASH_R   3.5f        /* radius of the pluck flash, pixels */
#define BG_R 8
#define BG_G 8
#define BG_B 14

static uint16_t pix[240 * BAND_H];
static int8_t led_state[FM1_NKEY];

/* the strings */
typedef struct {
  int on;                 /* sounding */
  float d1, d2, d3;       /* displacement of modes 1, 2, 3 (pixels) */
  float G, cw;            /* glow radius, core width */
  float gr, gg, gb;       /* glow colour */
  float cr, cg, cb;       /* core colour */
  float fl, fx, fy;       /* pluck flash: strength, place */
} sstate_t;

static int sx[NKEYS];                        /* pixel column of each string */
static float p1[STR_H], p2[STR_H], p3[STR_H]; /* shapes of the modes */
static sstate_t ss[NKEYS];                   /* each string as it is now */
static sstate_t ds[NKEYS];                   /* each string as the screen shows it */
static float sphase[NKEYS];
static uint32_t last_pblk[NKEYS];
static int16_t cur_lo[NKEYS][NBANDS], cur_hi[NKEYS][NBANDS];  /* columns of ss */
static int16_t prv_lo[NKEYS][NBANDS], prv_hi[NKEYS][NBANDS];  /* columns of ds */
static uint8_t need[NKEYS];
static float accR[240], accG[240], accB[240];

/* ===================== program ===================== */

void setup(void)
{
  int i, j;
  for (i = 0; i < (int)FM1_NKEY; i++) led_state[i] = -1;
  for (i = 0; i < NKEYS; i++) {
    float th = (0.12f + 0.76f * (float)i / 26.f) * PI_F * 0.5f;
    pan_l[i] = cosf(th);
    pan_r[i] = sinf(th);
  }
  for (i = 0; i < 4; i++)
    for (j = 0; j < 3; j++) {
      float w0 = 2.f * PI_F * insts[i].bf[j] / SR;
      float al = sinf(w0) / (2.f * insts[i].bq[j]);
      float a0 = 1.f + al;
      bd_b0[i][j] = al / a0;
      bd_a1[i][j] = -2.f * cosf(w0) / a0;
      bd_a2[i][j] = (1.f - al) / a0;
    }
  /* strings from x = 11 to 229: room for the full swing at both sides */
  for (i = 0; i < NKEYS; i++) {
    sx[i] = 11 + (i * 218 + 13) / 26;
    for (j = 0; j < NBANDS; j++) {
      prv_lo[i][j] = (int16_t)sx[i];
      prv_hi[i][j] = (int16_t)(sx[i] + 1);
    }
  }
  for (i = 0; i < STR_H; i++) {
    float u = ((float)i + 0.5f) / (float)STR_H;
    p1[i] = sinf(PI_F * u);
    p2[i] = sinf(2.f * PI_F * u);
    p3[i] = sinf(3.f * PI_F * u);
  }
  rv_set(p_knob[2]);
  rv_last = p_knob[2];
  cur_t60 = 0.25f * powf(80.f, (float)p_knob[1] * 0.01f);
  p_master = fm1_master();
}

void audio(int32_t *out, int frames)
{
  int i, j, k, pos, step, arp_on;
  int inst = p_inst;
  const inst_t *ins = &insts[inst];
  uint32_t held;
  float vt, dv, mm, dir, b0[3], a1[3], a2[3], bgn[3];

  if (frames > FM1_AUDIO_BLOCK) frames = FM1_AUDIO_BLOCK;
  if (frames <= 0) return;

  /* parameters */
  cur_t60 = 0.25f * powf(80.f, (float)p_knob[1] * 0.01f);       /* 0.25 .. 20 s */
  step = (int)(SR * 0.4f * powf(0.075f, (float)p_knob[3] * 0.01f)); /* 400 .. 30 ms */
  if (step < 64) step = 64;
  k = p_knob[2];
  if (k != rv_last) { rv_set(k); rv_last = k; }

  for (i = 0; i < NV; i++) {
    vc[i].pk = 0.f;
    if (vc[i].active) vc[i].g = expf(-6.9078f / (cur_t60 * vc[i].tf));
  }

  /* note keys */
  while (ev_r != ev_w) {
    k = evq[ev_r % EVQ];
    ev_r = ev_r + 1u;
    if (k < NKEYS) note_on(k, 0.8f + 0.2f * fabsf(rnd()));
  }

  /* strum */
  arp_on = p_arp;
  held = arp_on ? p_held : 0u;
  if (held && !arp_prev) { arp_idx = -1; arp_dir = 1; arp_timer = 0; }
  arp_prev = held;
  if (arp_timer > step) arp_timer = step;

  for (i = 0; i < frames; i++) mixL[i] = mixR[i] = mixM[i] = 0.f;
  pos = 0;
  while (pos < frames) {
    int seg = frames - pos;
    if (arp_on) {
      if (arp_timer <= 0) {
        if (held) arp_step(held);
        arp_count++;
        arp_timer += step;
        if (arp_timer <= 0) arp_timer = step;
      }
      if (arp_timer < seg) seg = arp_timer;
    }
    render(pos, pos + seg);
    pos += seg;
    if (arp_on) arp_timer -= seg;
  }

  /* body, hall, volume */
  for (j = 0; j < 3; j++) {
    b0[j] = bd_b0[inst][j];
    a1[j] = bd_a1[inst][j];
    a2[j] = bd_a2[inst][j];
    bgn[j] = ins->bg[j];
  }
  dir = ins->direct;
  mm = (float)p_master * (1.f / (float)FM1_MASTER_MAX);
  vt = mm * mm;
  dv = (vt - vol) / (float)frames;

  for (i = 0; i < frames; i++) {
    float m = mixM[i] * 0.7f, body = 0.f, L, R, x, wl, wr;
    float o0, o1, o2, o3, a, bq, c, dd;
    for (j = 0; j < 3; j++) {
      float y = b0[j] * m + bs1[j];
      bs1[j] = bs2[j] - a1[j] * y;
      bs2[j] = -b0[j] * m - a2[j] * y;
      body += bgn[j] * y;
    }
    L = mixL[i] * dir + body;
    R = mixR[i] * dir + body;

    /* diffusion */
    x = (L + R) * 0.5f * rv_in + 1e-10f;
    for (j = 0; j < RV_AP; j++) {
      float *ab = ap_mem + ap_off[j];
      int ix = ap_idx[j];
      float un = ab[ix];
      float u = x + ap_g[j] * un;
      ab[ix] = u;
      x = un - ap_g[j] * u;
      if (++ix >= ap_len[j]) ix = 0;
      ap_idx[j] = ix;
    }
    /* feedback delay network */
    o0 = fd_mem[fd_off[0] + fd_idx[0]];
    o1 = fd_mem[fd_off[1] + fd_idx[1]];
    o2 = fd_mem[fd_off[2] + fd_idx[2]];
    o3 = fd_mem[fd_off[3] + fd_idx[3]];
    fd_lp[0] += rv_damp * (o0 - fd_lp[0]);
    fd_lp[1] += rv_damp * (o1 - fd_lp[1]);
    fd_lp[2] += rv_damp * (o2 - fd_lp[2]);
    fd_lp[3] += rv_damp * (o3 - fd_lp[3]);
    a = fd_lp[0] + fd_lp[1];
    bq = fd_lp[0] - fd_lp[1];
    c = fd_lp[2] + fd_lp[3];
    dd = fd_lp[2] - fd_lp[3];
    fd_mem[fd_off[0] + fd_idx[0]] = x + fd_g[0] * 0.5f * (a + c);
    fd_mem[fd_off[1] + fd_idx[1]] = x + fd_g[1] * 0.5f * (bq + dd);
    fd_mem[fd_off[2] + fd_idx[2]] = x + fd_g[2] * 0.5f * (a - c);
    fd_mem[fd_off[3] + fd_idx[3]] = x + fd_g[3] * 0.5f * (bq - dd);
    for (j = 0; j < RV_N; j++)
      if (++fd_idx[j] >= fd_len[j]) fd_idx[j] = 0;
    wl = fd_lp[0] + fd_lp[2];
    wr = fd_lp[1] + fd_lp[3];

    L = L * rv_dry + wl * rv_wet;
    R = R * rv_dry + wr * rv_wet;
    vol += dv;
    out[2 * i]     = (int32_t)(sclip(L * vol) * OUT_FS);
    out[2 * i + 1] = (int32_t)(sclip(R * vol) * OUT_FS);
  }
  vol = vt;

  /* levels for the screen and the LEDs; end strings that are silent */
  {
    float ka[NKEYS];
    for (k = 0; k < NKEYS; k++) ka[k] = 0.f;
    for (i = 0; i < NV; i++) {
      voice_t *v = &vc[i];
      if (!v->active) continue;
      v->peak = v->pk;
      v->age++;
      if (v->pk < 1e-4f && v->age > 3) { v->active = 0; v->peak = 0.f; continue; }
      if (v->pk > ka[v->key]) ka[v->key] = v->pk;
    }
    for (k = 0; k < NKEYS; k++) key_amp[k] = ka[k];
  }
  for (j = 0; j < 3; j++)
    if (fabsf(bs1[j]) < 1e-12f && fabsf(bs2[j]) < 1e-12f) bs1[j] = bs2[j] = 0.f;

  /* ARP LED: blinks with the strum (at most about 5 times a second) */
  if (arp_on) {
    int div = 1, beat, ph;
    while (div * step < 4410) div++;
    beat = div * step;
    ph = (int)((arp_count + (uint32_t)div - 1u) % (uint32_t)div) * step + (step - arp_timer);
    arp_led = ph < beat / 2;
  } else {
    arp_led = 0;
  }
  g_blocks = g_blocks + 1u;
}

/* ===================== screen: drawing ===================== */

static const uint8_t font5x7[41][7] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
  { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, /* + */
  { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, /* - */
  { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 }, /* : */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C }, /* . */
  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /* 2 */
  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* 3 */
  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* 5 */
  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* 9 */
  { 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11 }, /* A */
  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
  { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }, /* D */
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
  { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, /* G */
  { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
  { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* J */
  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
  { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
  { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
  { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
  { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* Q */
  { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
  { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* S */
  { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, /* W */
  { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
  { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 }, /* Y */
  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* Z */
};

static const char *const labels[4] = { "PLUCK", "RING", "HALL", "STRUM" };

static uint16_t rgb(int r, int g, int b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
#define BGC rgb(BG_R, BG_G, BG_B)

static const uint8_t *glyph(char ch)
{
  int i = 0;
  if (ch >= '0' && ch <= '9') i = 5 + (ch - '0');
  else if (ch >= 'A' && ch <= 'Z') i = 15 + (ch - 'A');
  else if (ch == '+') i = 1;
  else if (ch == '-') i = 2;
  else if (ch == ':') i = 3;
  else if (ch == '.') i = 4;
  return font5x7[i];
}

/* text centred in a box w wide, 7*s high */
static void text_box(int x, int y, int w, int s, const char *str, uint16_t fg, uint16_t bgc)
{
  int h = 7 * s, n = 0, i, r, c, a, b, x0, tw;
  while (str[n]) n++;
  tw = n * 6 * s - s;
  x0 = (w - tw) / 2;
  if (x0 < 0) x0 = 0;
  for (i = 0; i < w * h; i++) pix[i] = bgc;
  for (i = 0; i < n; i++) {
    const uint8_t *gl = glyph(str[i]);
    int cx = x0 + i * 6 * s;
    for (r = 0; r < 7; r++)
      for (c = 0; c < 5; c++)
        if (gl[r] & (0x10 >> c))
          for (a = 0; a < s; a++)
            for (b = 0; b < s; b++) {
              int px = cx + c * s + b;
              if (px < w) pix[(r * s + a) * w + px] = fg;
            }
  }
  fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, pix, (uint32_t)(w * h));
}

static void itos(int v, char *s)
{
  char t[12];
  int n = 0, i = 0;
  if (v < 0) { s[i++] = '-'; v = -v; }
  do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) s[i++] = t[--n];
  s[i] = 0;
}

static void draw_bridge(int y)
{
  int x, r, k;
  for (r = 0; r < 3; r++)
    for (x = 0; x < 240; x++)
      pix[r * 240 + x] = (r == 1) ? rgb(120, 86, 52) : rgb(84, 58, 36);
  for (k = 0; k < NKEYS; k++)
    for (r = 0; r < 3; r++) pix[r * 240 + sx[k]] = rgb(200, 190, 160);
  fm1_lcd_blit(0, (uint32_t)y, 240, 3, pix, 720);
}

static void draw_value(int j)
{
  const inst_t *ins = &insts[p_inst];
  char s[12];
  int v = p_knob[j], fw;
  itos(v, s);
  text_box(j * 60, VALUE_Y, 60, 2, s, rgb(236, 236, 242), BGC);
  fw = v * 48 / 100;
  if (fw > 0)
    fm1_lcd_fill_rect((uint32_t)(j * 60 + 6), GAUGE_Y, (uint32_t)fw, 3, rgb(ins->r, ins->g, ins->b));
  if (fw < 48)
    fm1_lcd_fill_rect((uint32_t)(j * 60 + 6 + fw), GAUGE_Y, (uint32_t)(48 - fw), 3, rgb(40, 40, 52));
}

static void draw_status(void)
{
  const inst_t *ins = &insts[p_inst];
  uint16_t on = rgb(ins->r, ins->g, ins->b), off = rgb(120, 120, 135);
  char s[8];
  int o = p_oct;
  s[0] = 'O'; s[1] = 'C'; s[2] = 'T'; s[3] = ' ';
  if (o == 0) { s[4] = '0'; s[5] = 0; }
  else { s[4] = o > 0 ? '+' : '-'; s[5] = (char)('0' + (o > 0 ? o : -o)); s[6] = 0; }
  text_box(0, STATUS_Y, 80, 1, s, o ? on : off, BGC);
  text_box(80, STATUS_Y, 80, 1, p_scl ? "PENTATONIC" : "CHROMATIC", p_scl ? on : off, BGC);
  text_box(160, STATUS_Y, 80, 1, p_arp ? "ARP ON" : "ARP OFF", p_arp ? on : off, BGC);
}

/* ---- the strings ----
 * A sounding string is a curve: modes 1 to 3 of a string fixed at both
 * bridges, each swinging at its own speed. Just after the pluck it has the
 * kink of the pluck and swings fast and wide; then it smooths to mode 1 and
 * swings less as it gets quieter.
 *
 * Each string is redrawn whole, from bridge to bridge, in one call: in each
 * band of rows, the columns where it is now or was. Its neighbours in those
 * columns are drawn as the screen shows them (ds), so no string ever shows
 * two moments. Strings at rest are not redrawn. */

static int ui_init = 0, dirty_static = 1, dirty_title = 1, dirty_status = 1;
static int dirty_val[4] = { 1, 1, 1, 1 };
static int force_cols = 1, frame_on = 0, frame_k = 0;
static uint32_t frame_blk = 0;

static void frame_start(void)
{
  const inst_t *ins = &insts[p_inst];
  float dt = (float)(g_blocks - frame_blk) * (256.f / SR);
  int k, r, b;
  if (dt > 0.1f) dt = 0.1f;
  frame_blk = g_blocks;
  for (k = 0; k < NKEYS; k++) {
    sstate_t *s = &ss[k];
    float a = key_amp[k], lv = 0.f, c = (float)sx[k] + 0.5f;
    if (a > THR) {
      lv = (8.6859f * logf(a) + 54.f) / 48.f;   /* dB, -54 .. -6 */
      if (lv < 0.04f) lv = 0.04f;
      if (lv > 1.f) lv = 1.f;
    }
    s->on = lv > 0.f;
    for (b = 0; b < NBANDS; b++) {
      cur_lo[k][b] = (int16_t)sx[k];
      cur_hi[k][b] = (int16_t)(sx[k] + 1);
    }
    if (s->on) {
      uint32_t pb = key_pblk[k];
      float age = (float)(g_blocks - pb) * (256.f / SR);
      float fr = expf(-age * 3.3f);            /* fresh after the pluck */
      float up = 1.f - key_beta[k];            /* the pluck, from the top */
      float s1 = sinf(PI_F * up), s2 = sinf(2.f * PI_F * up), s3 = sinf(3.f * PI_F * up);
      float w1 = s1, w2 = 0.25f * s2 * fr, w3 = (1.f / 9.f) * s3 * fr * fr;
      float A = AMAX * lv / (fabsf(w1) + fabsf(w2) + fabsf(w3) + 1e-6f);
      float fv = (6.f + 14.f * fr) * (1.25f - 0.5f * (float)k / 26.f);  /* swings a second */
      float ph, fl, gs, R;
      if (pb != last_pblk[k]) {
        last_pblk[k] = pb;
        sphase[k] = 0.f;                       /* starts at the pulled shape */
      } else {
        sphase[k] += TWO_PI * fv * dt;
        if (sphase[k] > TWO_PI) sphase[k] = fmodf(sphase[k], TWO_PI);
      }
      ph = sphase[k];
      s->d1 = A * w1 * cosf(ph);
      s->d2 = A * w2 * cosf(2.f * ph + 0.7f);
      s->d3 = A * w3 * cosf(3.f * ph + 1.9f);
      s->G = 1.5f + 2.f * lv;
      s->cw = 1.f + 0.8f * lv;
      gs = 0.35f + 0.55f * lv;
      s->gr = (float)ins->r * gs;
      s->gg = (float)ins->g * gs;
      s->gb = (float)ins->b * gs;
      s->cr = (float)ins->r + (255.f - (float)ins->r) * 0.8f * lv;
      s->cg = (float)ins->g + (255.f - (float)ins->g) * 0.8f * lv;
      s->cb = (float)ins->b + (255.f - (float)ins->b) * 0.8f * lv;
      fl = 1.f - age * 5.f;                    /* 0.2 s */
      if (fl < 0.f) fl = 0.f;
      s->fl = fl * fl;
      s->fy = up * (float)STR_H;
      s->fx = c + s->d1 * s1 + s->d2 * s2 + s->d3 * s3;

      /* where it is, in each band */
      R = s->G + 1.f;
      for (b = 0; b < NBANDS; b++) {
        float mn = 1e9f, mx = -1e9f;
        int lo, hi;
        for (r = b * BAND_H; r < (b + 1) * BAND_H; r++) {
          float xs = c + s->d1 * p1[r] + s->d2 * p2[r] + s->d3 * p3[r];
          if (xs < mn) mn = xs;
          if (xs > mx) mx = xs;
        }
        mn -= R;
        mx += R;
        if (s->fl > 0.f && s->fy + FLASH_R + 1.f > (float)(b * BAND_H) &&
            s->fy - FLASH_R - 1.f < (float)((b + 1) * BAND_H)) {
          if (s->fx - FLASH_R - 1.f < mn) mn = s->fx - FLASH_R - 1.f;
          if (s->fx + FLASH_R + 1.f > mx) mx = s->fx + FLASH_R + 1.f;
        }
        if (mn < 0.f) mn = 0.f;
        if (mx > 239.f) mx = 239.f;
        lo = (int)mn;
        hi = (int)mx + 1;
        if (lo > sx[k]) lo = sx[k];
        if (hi < sx[k] + 1) hi = sx[k] + 1;
        cur_lo[k][b] = (int16_t)lo;
        cur_hi[k][b] = (int16_t)hi;
      }
    } else {                                   /* at rest: a thin line */
      s->d1 = s->d2 = s->d3 = 0.f;
      s->G = 0.f;
      s->cw = 1.f;
      s->gr = s->gg = s->gb = 0.f;
      s->cr = 40.f + 0.3f * (float)ins->r;
      s->cg = 40.f + 0.3f * (float)ins->g;
      s->cb = 40.f + 0.3f * (float)ins->b;
      s->fl = 0.f;
      s->fx = c;
      s->fy = 0.f;
    }
    /* redraw it if it sounds, if the screen shows it sounding, or if all
     * must be redrawn (first frame, other instrument) */
    need[k] = (uint8_t)(force_cols || s->on || ds[k].on);
  }
  force_cols = 0;
  frame_on = 1;
  frame_k = 0;
}

/* draws columns x0..x1-1 of band b for string k: string k as it is now,
 * every other string that reaches these columns as the screen shows it */
static void render_band(int k, int b, int x0, int x1)
{
  int w = x1 - x0, y0 = b * BAND_H, r, x, i, j, n = 0;
  const sstate_t *st[NKEYS];
  float cx[NKEYS];
  uint16_t *p = pix;
  for (j = 0; j < NKEYS; j++) {
    int lo = (j == k) ? cur_lo[j][b] : prv_lo[j][b];
    int hi = (j == k) ? cur_hi[j][b] : prv_hi[j][b];
    if (lo < x1 && hi > x0) {
      st[n] = (j == k) ? &ss[j] : &ds[j];
      cx[n] = (float)sx[j] + 0.5f;
      n++;
    }
  }
  for (r = 0; r < BAND_H; r++) {
    int row = y0 + r;
    float fyr = (float)row + 0.5f;
    for (x = 0; x < w; x++) { accR[x] = BG_R; accG[x] = BG_G; accB[x] = BG_B; }
    for (i = 0; i < n; i++) {
      const sstate_t *s = st[i];
      float xs = cx[i] + s->d1 * p1[row] + s->d2 * p2[row] + s->d3 * p3[row];
      float R = s->G + 1.f;
      int xa = (int)(xs - R), xb = (int)(xs + R) + 1;
      if (xa < x0) xa = x0;
      if (xb > x1) xb = x1;
      for (x = xa; x < xb; x++) {
        float d = fabsf((float)x + 0.5f - xs);
        float core = 0.5f * s->cw + 0.5f - d;
        float gl = 0.f;
        if (core < 0.f) core = 0.f;
        if (core > 1.f) core = 1.f;
        if (d < s->G) { gl = 1.f - d / s->G; gl *= gl; }
        accR[x - x0] += core * s->cr + gl * s->gr;
        accG[x - x0] += core * s->cg + gl * s->gg;
        accB[x - x0] += core * s->cb + gl * s->gb;
      }
      if (s->fl > 0.f) {                       /* the pluck flash */
        float dy = fyr - s->fy;
        if (dy > -FLASH_R && dy < FLASH_R) {
          int fa = (int)(s->fx - FLASH_R), fb = (int)(s->fx + FLASH_R) + 1;
          if (fa < x0) fa = x0;
          if (fb > x1) fb = x1;
          for (x = fa; x < fb; x++) {
            float dx = (float)x + 0.5f - s->fx;
            float f = 1.f - sqrtf(dx * dx + dy * dy) / FLASH_R;
            if (f > 0.f) {
              f *= 255.f * s->fl;
              accR[x - x0] += f;
              accG[x - x0] += f;
              accB[x - x0] += f;
            }
          }
        }
      }
    }
    for (x = 0; x < w; x++) {
      float R = accR[x], G = accG[x], B = accB[x];
      if (R > 255.f) R = 255.f;
      if (G > 255.f) G = 255.f;
      if (B > 255.f) B = 255.f;
      *p++ = rgb((int)R, (int)G, (int)B);
    }
  }
  fm1_lcd_blit((uint32_t)x0, (uint32_t)(STR_Y + y0), (uint32_t)w, BAND_H, pix,
               (uint32_t)(w * BAND_H));
}

/* redraws string k whole, from bridge to bridge; returns the pixels drawn */
static int update_string(int k)
{
  int b, px = 0;
  for (b = 0; b < NBANDS; b++) {
    int x0 = cur_lo[k][b] < prv_lo[k][b] ? cur_lo[k][b] : prv_lo[k][b];
    int x1 = cur_hi[k][b] > prv_hi[k][b] ? cur_hi[k][b] : prv_hi[k][b];
    if (x0 < 0) x0 = 0;
    if (x1 > 240) x1 = 240;
    if (x1 > x0) {
      render_band(k, b, x0, x1);
      px += (x1 - x0) * BAND_H;
    }
  }
  ds[k] = ss[k];
  for (b = 0; b < NBANDS; b++) {
    prv_lo[k][b] = cur_lo[k][b];
    prv_hi[k][b] = cur_hi[k][b];
  }
  return px;
}

static void ui_draw(void)
{
  int budget = 6000, j;      /* pixels for each call of loop() */

  if (!ui_init) {
    fm1_lcd_fill_rect(0, 0, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, BGC);
    ui_init = 1;
    return;
  }
  if (dirty_static) {
    for (j = 0; j < 4; j++)
      text_box(j * 60, LABEL_Y, 60, 1, labels[j], rgb(130, 130, 148), BGC);
    draw_bridge(BRIDGE1_Y);
    draw_bridge(BRIDGE2_Y);
    dirty_static = 0;
    budget -= 3200;
  }
  if (dirty_title && budget > 0) {
    const inst_t *ins = &insts[p_inst];
    text_box(0, TITLE_Y, 240, 2, ins->name, rgb(ins->r, ins->g, ins->b), BGC);
    dirty_title = 0;
    budget -= 3400;
  }
  for (j = 0; j < 4 && budget > 0; j++)
    if (dirty_val[j]) { draw_value(j); dirty_val[j] = 0; budget -= 1000; }
  if (dirty_status && budget > 0) { draw_status(); dirty_status = 0; budget -= 1700; }

  /* the strings: a new frame at most every 3 audio blocks (about 17 ms) */
  if (!frame_on && (force_cols || (uint32_t)(g_blocks - frame_blk) >= 3u))
    frame_start();
  /* whole strings only: one or a few in each call */
  while (frame_on && budget > 0) {
    while (frame_k < NKEYS && !need[frame_k]) frame_k++;
    if (frame_k >= NKEYS) { frame_on = 0; break; }
    budget -= update_string(frame_k);
    frame_k++;
  }
}

/* ---- LEDs ---- */
static int oct_lo(void) { return p_scl ? 0 : -1; }
static int oct_hi(void) { return p_scl ? 1 : 2; }

static void set_led(uint32_t id, int on)
{
  on = on ? 1 : 0;
  if (led_state[id] != on) {
    led_state[id] = (int8_t)on;
    fm1_led_key(id, on);
  }
}

static void leds(void)
{
  static const uint8_t unused[10] = {
    FM1_BTN_FX, FM1_BTN_ENV, FM1_BTN_LFO, FM1_BTN_EDIT, FM1_BTN_GLO,
    FM1_BTN_HOME, FM1_BTN_SAVE, FM1_BTN_SEQ, FM1_BTN_PLAY, FM1_BTN_REC
  };
  int j;
  for (j = 0; j < 10; j++) set_led(unused[j], 0);
  set_led(FM1_BTN_OCT_DOWN, p_oct > oct_lo());
  set_led(FM1_BTN_OCT_UP, p_oct < oct_hi());
  set_led(FM1_BTN_SCL, p_scl ? (int)(((g_blocks / 40u) & 1u) == 0u) : 1);
  set_led(FM1_BTN_ARP, p_arp ? arp_led : 1);
  for (j = 0; j < NKEYS; j++)
    set_led(FM1_KEY_NOTE0 + (uint32_t)j, key_amp[j] > THR);
}

void loop(void)
{
  int j;
  int32_t d;
  uint32_t btn = fm1_input_edges(0);
  uint32_t nk = fm1_input_note_edges();

  d = fm1_enc_take(FM1_ENC_ALGORITHM);
  if (d) {
    p_inst = ((p_inst + (int)(d % 4)) % 4 + 4) % 4;
    dirty_title = 1;
    dirty_status = 1;
    force_cols = 1;
    for (j = 0; j < 4; j++) dirty_val[j] = 1;
  }
  for (j = 0; j < 4; j++) {
    d = fm1_enc_take(FM1_ENC_KNOB1 + (uint32_t)j);
    if (d) {
      int v = p_knob[j] + 2 * (int)d;
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      if (v != p_knob[j]) { p_knob[j] = v; dirty_val[j] = 1; }
    }
  }

  if ((btn >> FM1_BTN_OCT_DOWN) & 1u)
    if (p_oct > oct_lo()) { p_oct = p_oct - 1; dirty_status = 1; }
  if ((btn >> FM1_BTN_OCT_UP) & 1u)
    if (p_oct < oct_hi()) { p_oct = p_oct + 1; dirty_status = 1; }
  if ((btn >> FM1_BTN_SCL) & 1u) {
    int scl = !p_scl, o = p_oct;
    int lo = scl ? 0 : -1, hi = scl ? 1 : 2;
    if (o < lo) o = lo;
    if (o > hi) o = hi;
    p_oct = o;               /* the octave first, then the scale */
    p_scl = scl;
    dirty_status = 1;
  }
  if ((btn >> FM1_BTN_ARP) & 1u) { p_arp = !p_arp; dirty_status = 1; }

  p_held = fm1_in.notes & ((1u << NKEYS) - 1u);
  if (!p_arp && nk) {
    for (j = 0; j < NKEYS; j++)
      if ((nk >> j) & 1u) {
        if (ev_w - ev_r < EVQ) {
          evq[ev_w % EVQ] = (uint8_t)j;
          ev_w = ev_w + 1u;
        }
      }
  }
  p_master = fm1_master();

  leds();
  ui_draw();
}
