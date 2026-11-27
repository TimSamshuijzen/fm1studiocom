/*
 * DRUM SOLO MACHINE - a drum machine for the M-VAVE FM-1 (FM-1 Studio)
 *
 *   PLAY        start / stop the drum solo (about 30 bars, a minute at 120 BPM)
 *   SEQ         loop on / off: the solo repeats, with a new take each time
 *   Note keys   play the 27 drums (General MIDI drum order, notes 35 to 61)
 *   KNOB1 tempo   KNOB2 swing   KNOB3 tune   KNOB4 room
 *   SELECT kit    ALGORITHM drive    PRESETS take (each take is another solo)
 *   MASTER      volume.  MIDI in: drum notes 35-61, start and stop.
 */
#include "fm1.h"
#include <math.h>

#define SR      44100.0f
#define INV_SR  (1.0f / 44100.0f)
#define PI_F    3.14159265f
#define NBLK    256

/* ======================================================================
   Instruments
   ====================================================================== */

enum {
  KICK2, KICK, STICK, SNARE, CLAP, SNARE2, FTL, HHC, FTH, HHP, TL, HHO, TLM, THM,
  CRASH, TH, RIDE, CHINA, BELL, TAMB, SPLASH, COWB, CRASH2, SHAKER, RIDE2,
  BONGOH, BONGOL, NINST
};

enum { T_KICK, T_SNARE, T_STICK, T_CLAP, T_HAT, T_TOM, T_CYM, T_BELL, T_COW, T_TAMB, T_SHAK };

typedef struct {
  uint8_t type, pad;
  int8_t pan;      /* -100 left .. 100 right (drummer's view) */
  uint8_t send;    /* room send, percent */
  float lvl, f, d, a, b, c;
  const char *name;
} Inst;

static const Inst INST[NINST] = {
  { T_KICK,  8,   0, 10, 0.95f,   45.0f, 0.21f,  110.0f, 0.050f, 0.70f, "KICK 2" },
  { T_KICK,  8,   0, 10, 1.00f,   52.0f, 0.15f,  135.0f, 0.032f, 0.90f, "KICK" },
  { T_STICK, 9,  -8, 30, 0.45f, 1650.0f, 0.016f, 520.0f, 0.0f,   0.0f,  "SIDE STICK" },
  { T_SNARE, 9,  -8, 40, 0.80f,  185.0f, 0.15f,  0.060f, 3200.0f, 0.75f, "SNARE" },
  { T_CLAP, 10,   5, 45, 0.80f, 1150.0f, 0.090f, 0.0105f, 0.0f,  0.0f,  "CLAP" },
  { T_SNARE, 9,  -8, 45, 0.88f,  225.0f, 0.13f,  0.050f, 3800.0f, 1.10f, "RIMSHOT" },
  { T_TOM,   7,  45, 35, 0.80f,   82.0f, 0.30f,  0.30f,  0.45f,  0.0f,  "LOW FLOOR TOM" },
  { T_HAT,   1, -35, 12, 0.42f,    1.0f, 0.035f, 7000.0f, 0.0f,  0.0f,  "CLOSED HAT" },
  { T_TOM,   7,  35, 35, 0.80f,   98.0f, 0.27f,  0.30f,  0.45f,  0.0f,  "HIGH FLOOR TOM" },
  { T_HAT,   1, -35, 10, 0.30f,    0.9f, 0.022f, 6000.0f, 0.0f,  1.0f,  "PEDAL HAT" },
  { T_TOM,   6,  20, 35, 0.78f,  117.0f, 0.24f,  0.30f,  0.45f,  0.0f,  "LOW TOM" },
  { T_HAT,   1, -35, 18, 0.38f,    1.0f, 0.40f,  6500.0f, 0.0f,  2.0f,  "OPEN HAT" },
  { T_TOM,   6,   8, 35, 0.78f,  139.0f, 0.22f,  0.30f,  0.45f,  0.0f,  "LOW-MID TOM" },
  { T_TOM,   5, -12, 35, 0.76f,  165.0f, 0.20f,  0.30f,  0.45f,  0.0f,  "HIGH-MID TOM" },
  { T_CYM,   0, -45, 28, 0.36f,   1.35f, 0.65f,  3800.0f, 0.0f,  0.0f,  "CRASH" },
  { T_TOM,   4, -30, 35, 0.74f,  196.0f, 0.18f,  0.30f,  0.45f,  0.0f,  "HIGH TOM" },
  { T_CYM,   2,  40, 22, 0.26f,   1.10f, 0.95f,  4500.0f, 0.0f,  3.0f,  "RIDE" },
  { T_CYM,   3,  60, 28, 0.30f,   1.05f, 0.45f,  2400.0f, 0.0f,  1.0f,  "CHINA" },
  { T_BELL,  2,  40, 22, 0.14f,  760.0f, 0.50f,  0.0f,   0.0f,   0.0f,  "RIDE BELL" },
  { T_TAMB, 11,  25, 25, 0.40f, 6500.0f, 0.12f,  0.014f, 0.0f,   0.0f,  "TAMBOURINE" },
  { T_CYM,   0, -60, 28, 0.32f,   1.70f, 0.22f,  5200.0f, 0.0f,  2.0f,  "SPLASH" },
  { T_COW,  11, -20, 25, 0.22f,  562.0f, 0.15f,  845.0f, 0.0f,   0.0f,  "COWBELL" },
  { T_CYM,   3,  50, 28, 0.36f,   1.55f, 0.55f,  4200.0f, 0.0f,  0.0f,  "CRASH 2" },
  { T_SHAK, 11,  20, 20, 0.35f, 5500.0f, 0.045f, 0.012f, 0.0f,   0.0f,  "SHAKER" },
  { T_CYM,   2,  30, 22, 0.25f,   0.95f, 1.10f,  4000.0f, 0.0f,  3.0f,  "RIDE 2" },
  { T_TOM,   4, -25, 30, 0.60f,  390.0f, 0.075f, 0.10f,  0.70f,  1.0f,  "HIGH BONGO" },
  { T_TOM,   5,  25, 30, 0.60f,  280.0f, 0.095f, 0.10f,  0.70f,  1.0f,  "LOW BONGO" },
};

typedef struct {
  const char *name;
  float pitch, decay, cym, kick, click, noise, t60, damp, metal;
} Kit;

static const Kit KITS[4] = {
  { "STUDIO", 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.85f, 0.45f, 0.45f },
  { "ARENA",  0.90f, 1.30f, 1.25f, 1.25f, 0.85f, 1.00f, 1.90f, 0.35f, 0.40f },
  { "TIGHT",  1.10f, 0.65f, 0.70f, 0.70f, 1.30f, 0.85f, 0.45f, 0.60f, 0.45f },
  { "ANALOG", 0.95f, 1.15f, 0.90f, 2.40f, 0.25f, 0.70f, 1.00f, 0.40f, 0.85f },
};

/* ======================================================================
   Settings (written by loop, read by audio)
   ====================================================================== */

static int set_bpm = 120, set_swing = 50, set_tune = 0, set_room = 35;
static int set_kit = 0, set_drive = 20, set_take = 1;

static volatile int g_kit = 0;
static volatile float g_tune = 1.0f;     /* 2^(tune/12) */
static volatile float g_upb;             /* sequencer units per sample */
static volatile float g_swing = 0.5f;
static volatile float g_wet = 1.05f;
static volatile float g_drv = 0.2f;
static volatile float g_vol = 0.5f;
static volatile int loop_on = 0;

/* ======================================================================
   DSP helpers
   ====================================================================== */

/* 2^x (|x| < 100), accurate near 0: the decay factors need that */
static float exp2_f(float x) {
  union { float f; uint32_t u; } b;
  if (x < -100.0f) return 0.0f;
  int i = (int)(x < 0.0f ? x - 0.5f : x + 0.5f);
  float f = x - (float)i;
  float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.05550411f +
            f * (0.009618129f + f * (0.001333355f + f * 0.0001540353f)))));
  b.u = (uint32_t)(i + 127) << 23;
  return p * b.f;
}

static float decay_k(float tau) {       /* per-sample factor for time constant tau */
  if (tau < 0.0003f) tau = 0.0003f;
  return exp2_f(-1.442695f / (tau * SR));
}

typedef struct { float a1, a2, a3, k, s1, s2; } Svf;

static void svf_set(Svf *f, float fc, float q) {
  if (fc > 0.45f * SR) fc = 0.45f * SR;
  if (fc < 20.0f) fc = 20.0f;
  float w = PI_F * fc * INV_SR;
  float g = sinf(w) / sinf(w + 0.5f * PI_F);
  f->k = 1.0f / q;
  f->a1 = 1.0f / (1.0f + g * (g + f->k));
  f->a2 = g * f->a1;
  f->a3 = g * f->a2;
}

static inline float svf_run(Svf *f, float x, float *lp) {
  float v3 = x - f->s2;
  float v1 = f->a1 * f->s1 + f->a2 * v3;
  float v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
  f->s1 = 2.0f * v1 - f->s1;
  f->s2 = 2.0f * v2 - f->s2;
  *lp = v2;
  return v1;
}
static inline float svf_bpn(Svf *f, float x) { float lp; return f->k * svf_run(f, x, &lp); }
static inline float svf_hp(Svf *f, float x) { float lp; float bp = svf_run(f, x, &lp); return x - f->k * bp - lp; }
static inline float svf_lp(Svf *f, float x) { float lp; svf_run(f, x, &lp); return lp; }

static uint32_t nz_s = 0x9E3779B9u;
static inline float noise(void) {
  nz_s ^= nz_s << 13; nz_s ^= nz_s >> 17; nz_s ^= nz_s << 5;
  return (float)(int32_t)nz_s * 4.656613e-10f;
}

/* sin(2 pi p) for p in [0, 1) */
static inline float sin2pi(float p) {
  float t = p - 0.5f;
  float y = 8.0f * t - 16.0f * t * fabsf(t);
  y = 0.225f * (y * fabsf(y) - y) + y;
  return -y;
}

/* soft clip: [-1.5, 1.5] -> [-1, 1], smooth */
static inline float sat(float x) {
  if (x > 1.5f) x = 1.5f; else if (x < -1.5f) x = -1.5f;
  return x - 0.148148f * x * x * x;
}

static const float MET[6]  = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
static const float MET2[6] = { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f };

static inline float metal(uint32_t *ph, const uint32_t *inc) {
  uint32_t s = 0;
  ph[0] += inc[0]; s += ph[0] >> 31;
  ph[1] += inc[1]; s += ph[1] >> 31;
  ph[2] += inc[2]; s += ph[2] >> 31;
  ph[3] += inc[3]; s += ph[3] >> 31;
  ph[4] += inc[4]; s += ph[4] >> 31;
  ph[5] += inc[5]; s += ph[5] >> 31;
  return (float)s * 0.3333333f - 1.0f;
}

/* ======================================================================
   Voices: one per instrument
   ====================================================================== */

typedef struct {
  uint8_t on, kind;
  float amp;
  float e1, k1, e2, k2, e3, k3;
  float p1, p2, p3, p4, i1, i2, i3, i4;
  float pe, pk, pd, pe2, pk2, pd2;
  float lp, lc, lck, lcf, atk, atki;
  float c1;
  int32_t t, next, nb, per;
  uint32_t mp[6], mi[6];
  Svf fa, fb;
} Voice;

static Voice vox[NINST];
static float pan_l[NINST], pan_r[NINST], send_g[NINST], disp_k[NINST];
static volatile float disp[NINST];   /* display level of each drum, 0..1 */

static void svf_clear(Svf *f) { f->s1 = 0.0f; f->s2 = 0.0f; }

static void trigger(int n, int vel) {
  Voice *v = &vox[n];
  const Inst *d = &INST[n];
  const Kit *K = &KITS[g_kit];
  int j;
  if (vel <= 0) {                       /* choke */
    if (v->on) { v->k1 = decay_k(0.02f); v->k2 = v->k1; v->k3 = v->k1; }
    return;
  }
  float vv = (float)vel * (1.0f / 127.0f);
  float P = K->pitch * g_tune;
  float Pm = 1.0f + (P - 1.0f) * 0.5f;
  int was = v->on;
  if (!was) { svf_clear(&v->fa); svf_clear(&v->fb); v->p1 = v->p2 = v->p3 = v->p4 = 0.0f; }
  v->amp = d->lvl * (vv * vv * 0.75f + vv * 0.25f);

  switch (d->type) {
  case T_KICK:
    v->i1 = d->f * P * INV_SR;
    v->pd = d->a * P * (0.55f + 0.45f * vv) * INV_SR; v->pe = 1.0f; v->pk = decay_k(d->b);
    v->pd2 = 900.0f * P * vv * INV_SR; v->pe2 = 1.0f; v->pk2 = decay_k(0.004f);
    v->e1 = 1.0f; v->k1 = decay_k(d->d * K->kick * (0.8f + 0.2f * vv));
    v->e2 = 1.8f * d->c * K->click * vv; v->k2 = decay_k(0.006f);
    svf_set(&v->fa, 3500.0f, 0.8f);
    v->c1 = 1.3f + 0.5f * vv;
    break;
  case T_SNARE:
    v->i1 = d->f * P * INV_SR; v->i2 = d->f * 1.62f * P * INV_SR;
    v->pd = 0.22f * vv; v->pe = 1.0f; v->pk = decay_k(0.010f);
    v->e1 = d->c; v->k1 = decay_k(d->a * K->decay);
    v->e2 = K->noise; v->k2 = decay_k(d->d * K->decay * (0.6f + 0.5f * vv));
    v->e3 = 0.5f * K->click * vv; v->k3 = decay_k(0.0012f);
    svf_set(&v->fa, d->b * P, 0.65f);
    v->c1 = 0.06f + 0.14f * vv;
    break;
  case T_STICK:
    v->i1 = d->f * P * INV_SR; v->i2 = d->a * P * INV_SR;
    v->e1 = 1.0f; v->k1 = decay_k(d->d);
    v->e2 = 0.7f; v->k2 = decay_k(d->d * 2.2f);
    v->e3 = 0.8f; v->k3 = decay_k(0.0010f);
    svf_set(&v->fa, 4000.0f * P, 1.0f);
    break;
  case T_CLAP:
  case T_TAMB:
    if (d->type == T_CLAP) svf_set(&v->fa, d->f * P, 1.4f);
    else {
      svf_set(&v->fa, d->f, 0.7f);
      for (j = 0; j < 6; j++) v->mi[j] = (uint32_t)(MET[j] * 5.3f * Pm * 97391.55f);
    }
    v->per = (int32_t)(d->a * SR);
    v->t = 0; v->next = v->per; v->nb = 3;
    v->e1 = 1.0f; v->k1 = decay_k(d->type == T_CLAP ? 0.004f : 0.006f);
    v->k2 = decay_k(d->d * K->decay);
    break;
  case T_HAT:
    for (j = 0; j < 6; j++) {
      v->mi[j] = (uint32_t)(MET[j] * d->f * Pm * 97391.55f);
      if (!was) v->mp[j] = (uint32_t)(noise() * 2.0e9f);
    }
    svf_set(&v->fa, d->a * Pm, 0.7f);
    svf_set(&v->fb, 12000.0f * Pm, 0.8f);
    v->c1 = 1.0f - K->metal;
    v->e1 = 1.0f; v->k1 = decay_k(d->d * K->decay * (0.7f + 0.5f * vv));
    if (n == HHC || n == HHP) {
      if (vox[HHO].on) vox[HHO].k1 = decay_k(0.018f);
    }
    break;
  case T_TOM: {
    float f = d->f * P;
    v->i1 = f * INV_SR; v->i2 = f * 1.593f * INV_SR;
    v->pd = d->a * (0.35f + 0.65f * vv); v->pe = 1.0f; v->pk = decay_k(0.055f);
    v->e1 = 1.0f; v->k1 = decay_k(d->d * K->decay * (0.85f + 0.15f * vv));
    v->e2 = 0.42f; v->k2 = decay_k(d->d * K->decay * 0.3f);
    v->e3 = 2.6f * d->b * K->click * (0.4f + 0.6f * vv); v->k3 = decay_k(0.010f);
    svf_set(&v->fa, 1200.0f + f * 6.0f, 0.7f);
    break; }
  case T_CYM: {
    int kind = (int)d->c;
    float dec = d->d * K->cym * (0.7f + 0.4f * vv);
    v->kind = (uint8_t)kind;
    for (j = 0; j < 6; j++) {
      v->mi[j] = (uint32_t)(MET2[j] * d->f * Pm * 97391.55f);
      if (!was) v->mp[j] = (uint32_t)(noise() * 2.0e9f);
    }
    svf_set(&v->fa, d->a * Pm, 0.65f);
    v->c1 = (1.0f - K->metal) * 0.9f + 0.1f;
    v->e1 = 1.0f; v->k1 = decay_k(dec);
    v->e2 = (kind == 3) ? 0.3f : 1.2f * vv; v->k2 = decay_k(kind == 3 ? 0.03f : 0.06f);
    v->lc = 0.75f; v->lcf = 0.15f + 0.20f * vv; v->lck = decay_k(dec * 0.5f);
    if (!was) { v->atk = 0.0f; v->lp = 0.0f; }
    v->atki = 1.0f / ((kind == 3 ? 0.0006f : 0.0035f) * SR);
    if (kind == 3) {
      v->i3 = 3150.0f * Pm * INV_SR;
      v->e3 = 0.6f * (0.6f + 0.4f * vv); v->k3 = decay_k(0.11f);
      svf_set(&v->fb, 4300.0f * Pm, 4.0f);
    } else if (kind == 1) {
      svf_set(&v->fb, 2800.0f * Pm, 1.2f);
    }
    break; }
  case T_BELL: {
    float f = d->f * Pm, dec = d->d * K->cym;
    v->i1 = f * INV_SR; v->i2 = f * 1.501f * INV_SR;
    v->i3 = f * 2.013f * INV_SR; v->i4 = f * 2.761f * INV_SR;
    v->e1 = 1.0f; v->k1 = decay_k(dec);
    v->e2 = 0.7f; v->k2 = decay_k(dec * 0.7f);
    v->e3 = 0.5f; v->k3 = decay_k(dec * 0.45f);
    v->pe = 0.5f; v->pk = decay_k(0.002f);
    break; }
  case T_COW:
    v->mi[0] = (uint32_t)(d->f * P * 97391.55f);
    v->mi[1] = (uint32_t)(d->a * P * 97391.55f);
    svf_set(&v->fa, d->a * P * 1.1f, 1.8f);
    v->e1 = 1.0f; v->k1 = decay_k(0.012f);
    v->e2 = 0.45f; v->k2 = decay_k(d->d * K->decay);
    break;
  case T_SHAK:
    svf_set(&v->fa, d->f, 0.7f);
    if (!was) v->atk = 0.0f;
    v->atki = 1.0f / (d->a * SR);
    v->e1 = 1.0f; v->k1 = decay_k(d->d);
    break;
  }
  v->on = 1;
  float dv = 0.35f + 0.65f * vv;
  if (disp[n] < dv) disp[n] = dv;
}

#define OFF_IF(cond) do { if (cond) v->on = 0; } while (0)

static void r_kick(Voice *v, float *o, int n) {
  float p = v->p1, i1 = v->i1, pd = v->pd, pe = v->pe, pk = v->pk;
  float pd2 = v->pd2, pe2 = v->pe2, pk2 = v->pk2;
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, dr = v->c1;
  for (int i = 0; i < n; i++) {
    p += i1 + pd * pe + pd2 * pe2;
    pe *= pk; pe2 *= pk2;
    if (p >= 1.0f) p -= 1.0f;
    float x = sin2pi(p) * e1 * dr + svf_bpn(&v->fa, noise()) * e2;
    e1 *= k1; e2 *= k2;
    o[i] = sat(x);
  }
  v->p1 = p; v->pe = pe; v->pe2 = pe2; v->e1 = e1; v->e2 = e2;
  OFF_IF(e1 < 0.001f);
}

static void r_snare(Voice *v, float *o, int n) {
  float p1 = v->p1, p2 = v->p2, i1 = v->i1, i2 = v->i2, pd = v->pd, pe = v->pe, pk = v->pk;
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, e3 = v->e3, k3 = v->k3, br = v->c1;
  for (int i = 0; i < n; i++) {
    float f = 1.0f + pd * pe; pe *= pk;
    p1 += i1 * f; if (p1 >= 1.0f) p1 -= 1.0f;
    p2 += i2 * f; if (p2 >= 1.0f) p2 -= 1.0f;
    float tone = (sin2pi(p1) + 0.55f * sin2pi(p2)) * e1;
    float z = noise(), lp;
    float bp = svf_run(&v->fa, z, &lp);
    float hp = z - v->fa.k * bp - lp;
    float wires = (v->fa.k * bp + lp * 0.25f + hp * br) * e2;
    float x = tone + wires + z * e3;
    e1 *= k1; e2 *= k2; e3 *= k3;
    o[i] = sat(x * 0.9f);
  }
  v->p1 = p1; v->p2 = p2; v->pe = pe; v->e1 = e1; v->e2 = e2; v->e3 = e3;
  OFF_IF(e2 < 0.001f && e1 < 0.001f);
}

static void r_stick(Voice *v, float *o, int n) {
  float p1 = v->p1, p2 = v->p2, i1 = v->i1, i2 = v->i2;
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, e3 = v->e3, k3 = v->k3;
  for (int i = 0; i < n; i++) {
    p1 += i1; if (p1 >= 1.0f) p1 -= 1.0f;
    p2 += i2; if (p2 >= 1.0f) p2 -= 1.0f;
    o[i] = sin2pi(p1) * e1 + sin2pi(p2) * e2 + svf_bpn(&v->fa, noise()) * e3;
    e1 *= k1; e2 *= k2; e3 *= k3;
  }
  v->p1 = p1; v->p2 = p2; v->e1 = e1; v->e2 = e2; v->e3 = e3;
  OFF_IF(e2 < 0.001f);
}

/* clap and tambourine: a few quick bursts, then a tail */
static void r_burst(Voice *v, float *o, int n, int tamb) {
  float e1 = v->e1, k1 = v->k1;
  for (int i = 0; i < n; i++) {
    if (v->t == v->next) {
      e1 = 1.0f;
      if (--v->nb <= 0) { k1 = v->k2; v->next = -1; }
      else v->next += v->per + (int32_t)(noise() * 40.0f);
    }
    v->t++;
    float x;
    if (tamb) {
      x = svf_hp(&v->fa, metal(v->mp, v->mi) * 0.45f + noise() * 0.55f) * 1.3f;
    } else {
      float z = noise(), lp;
      float bp = svf_run(&v->fa, z, &lp);
      x = (v->fa.k * bp + (z - v->fa.k * bp - lp) * 0.1f) * 1.6f;
    }
    o[i] = x * e1;
    e1 *= k1;
  }
  v->e1 = e1; v->k1 = k1;
  OFF_IF(v->nb <= 0 && e1 < 0.001f);
}

static void r_hat(Voice *v, float *o, int n) {
  float e1 = v->e1, k1 = v->k1, c = v->c1, m1 = 1.0f - v->c1;
  for (int i = 0; i < n; i++) {
    float x = metal(v->mp, v->mi) * m1 + noise() * c;
    float h = svf_hp(&v->fa, x);
    o[i] = (svf_lp(&v->fb, h) + 0.25f * h) * e1;
    e1 *= k1;
  }
  v->e1 = e1;
  OFF_IF(e1 < 0.001f);
}

static void r_tom(Voice *v, float *o, int n) {
  float p1 = v->p1, p2 = v->p2, i1 = v->i1, i2 = v->i2, pd = v->pd, pe = v->pe, pk = v->pk;
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, e3 = v->e3, k3 = v->k3;
  for (int i = 0; i < n; i++) {
    float f = 1.0f + pd * pe; pe *= pk;
    p1 += i1 * f; if (p1 >= 1.0f) p1 -= 1.0f;
    p2 += i2 * f; if (p2 >= 1.0f) p2 -= 1.0f;
    float x = sin2pi(p1) * e1 + sin2pi(p2) * e2 + svf_bpn(&v->fa, noise()) * e3;
    e1 *= k1; e2 *= k2; e3 *= k3;
    o[i] = sat(x * 1.25f);
  }
  v->p1 = p1; v->p2 = p2; v->pe = pe; v->e1 = e1; v->e2 = e2; v->e3 = e3;
  OFF_IF(e1 < 0.001f);
}

static void r_cym(Voice *v, float *o, int n) {
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, e3 = v->e3, k3 = v->k3;
  float lp = v->lp, lc = v->lc, lck = v->lck, lcf = v->lcf, atk = v->atk, atki = v->atki;
  float c = v->c1, m1 = 1.0f - v->c1, p3 = v->p3, i3 = v->i3;
  int kind = v->kind;
  for (int i = 0; i < n; i++) {
    float m = metal(v->mp, v->mi);
    if (kind == 1) { m *= 2.2f; if (m > 1.0f) m = 1.0f; else if (m < -1.0f) m = -1.0f; }
    float h = svf_hp(&v->fa, m * m1 + noise() * c);
    lp += lc * (h - lp);
    lc = lcf + (lc - lcf) * lck;
    float y = lp;
    if (kind == 1) y += 0.6f * svf_bpn(&v->fb, h);
    else if (kind == 3) {
      p3 += i3; if (p3 >= 1.0f) p3 -= 1.0f;
      y += (0.25f * sin2pi(p3) + svf_bpn(&v->fb, m * 1.2f + noise() * 0.3f)) * e3;
      e3 *= k3;
    }
    o[i] = y * (e1 + e2) * atk;
    e1 *= k1; e2 *= k2;
    if (atk < 1.0f) { atk += atki; if (atk > 1.0f) atk = 1.0f; }
  }
  v->e1 = e1; v->e2 = e2; v->e3 = e3; v->lp = lp; v->lc = lc; v->atk = atk; v->p3 = p3;
  OFF_IF(e1 < 0.001f && e2 < 0.001f);
}

static void r_bell(Voice *v, float *o, int n) {
  float p1 = v->p1, p2 = v->p2, p3 = v->p3, p4 = v->p4;
  float i1 = v->i1, i2 = v->i2, i3 = v->i3, i4 = v->i4;
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2, e3 = v->e3, k3 = v->k3, pe = v->pe, pk = v->pk;
  for (int i = 0; i < n; i++) {
    p1 += i1; if (p1 >= 1.0f) p1 -= 1.0f;
    p2 += i2; if (p2 >= 1.0f) p2 -= 1.0f;
    p3 += i3; if (p3 >= 1.0f) p3 -= 1.0f;
    p4 += i4; if (p4 >= 1.0f) p4 -= 1.0f;
    o[i] = sin2pi(p1) * e1 + sin2pi(p2) * e2 + (sin2pi(p3) + 0.6f * sin2pi(p4)) * e3 + noise() * pe;
    e1 *= k1; e2 *= k2; e3 *= k3; pe *= pk;
  }
  v->p1 = p1; v->p2 = p2; v->p3 = p3; v->p4 = p4;
  v->e1 = e1; v->e2 = e2; v->e3 = e3; v->pe = pe;
  OFF_IF(e1 < 0.001f);
}

static void r_cow(Voice *v, float *o, int n) {
  float e1 = v->e1, k1 = v->k1, e2 = v->e2, k2 = v->k2;
  for (int i = 0; i < n; i++) {
    v->mp[0] += v->mi[0]; v->mp[1] += v->mi[1];
    float s = (float)(v->mp[0] >> 31) + (float)(v->mp[1] >> 31) - 1.0f;
    o[i] = svf_bpn(&v->fa, s) * (e1 + e2) * 1.4f;
    e1 *= k1; e2 *= k2;
  }
  v->e1 = e1; v->e2 = e2;
  OFF_IF(e2 < 0.001f);
}

static void r_shak(Voice *v, float *o, int n) {
  float e1 = v->e1, k1 = v->k1, atk = v->atk, atki = v->atki;
  for (int i = 0; i < n; i++) {
    o[i] = svf_hp(&v->fa, noise()) * e1 * atk;
    e1 *= k1;
    if (atk < 1.0f) { atk += atki; if (atk > 1.0f) atk = 1.0f; }
  }
  v->e1 = e1; v->atk = atk;
  OFF_IF(e1 < 0.001f);
}

/* ======================================================================
   Room: 8-line feedback delay network with two diffusers
   ====================================================================== */

static float mixL[NBLK], mixR[NBLK], mixS[NBLK], vbuf[NBLK];

#define RV_TOT 5196
static const uint16_t RV_LEN[8] = { 421, 487, 557, 613, 677, 743, 811, 887 };
static float rv_buf[RV_TOT];
static uint16_t rv_off[8], rv_pos[8];
static volatile float rv_g[8];
static float rv_lp[8];
static volatile float rv_damp = 0.45f;
#define AP1_N 113
#define AP2_N 263
static float ap1[AP1_N], ap2[AP2_N];
static int ap1_i, ap2_i;
static float rv_hp;     /* low cut of the room input */

static void room_set(void) {
  const Kit *K = &KITS[set_kit];
  for (int j = 0; j < 8; j++)
    rv_g[j] = exp2_f(-9.965784f * (float)RV_LEN[j] / (K->t60 * SR));
  rv_damp = K->damp;
}

/* adds the room of mixS to mixL and mixR */
static void room_block(int n) {
  float g[8], lp[8], *ln[8];
  int pos[8], j;
  for (j = 0; j < 8; j++) {
    g[j] = rv_g[j] * 0.35355339f; lp[j] = rv_lp[j]; pos[j] = rv_pos[j]; ln[j] = rv_buf + rv_off[j];
  }
  float dm = rv_damp, wet = g_wet * 0.5f;
  int i1 = ap1_i, i2 = ap2_i;
  float hz = rv_hp;
  for (int i = 0; i < n; i++) {
    float x = mixS[i] + 1.0e-18f, d, w, o[8];
    hz += 0.035f * (x - hz);            /* keep the low end of the kit dry */
    x -= hz;
    d = ap1[i1]; w = x + 0.6f * d; ap1[i1] = w; x = d - 0.6f * w; if (++i1 >= AP1_N) i1 = 0;
    d = ap2[i2]; w = x + 0.6f * d; ap2[i2] = w; x = d - 0.6f * w; if (++i2 >= AP2_N) i2 = 0;
    for (j = 0; j < 8; j++) { lp[j] += dm * (ln[j][pos[j]] - lp[j]); o[j] = lp[j]; }
    mixL[i] += (o[0] - o[2] + o[4] - o[6]) * wet;
    mixR[i] += (o[1] - o[3] + o[5] - o[7]) * wet;
    float a0 = o[0] + o[1], a1 = o[0] - o[1], a2 = o[2] + o[3], a3 = o[2] - o[3];
    float a4 = o[4] + o[5], a5 = o[4] - o[5], a6 = o[6] + o[7], a7 = o[6] - o[7];
    float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
    float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    ln[0][pos[0]] = (b0 + b4) * g[0] + x; ln[1][pos[1]] = (b1 + b5) * g[1] - x;
    ln[2][pos[2]] = (b2 + b6) * g[2] + x; ln[3][pos[3]] = (b3 + b7) * g[3] - x;
    ln[4][pos[4]] = (b0 - b4) * g[4] + x; ln[5][pos[5]] = (b1 - b5) * g[5] - x;
    ln[6][pos[6]] = (b2 - b6) * g[6] + x; ln[7][pos[7]] = (b3 - b7) * g[7] - x;
    for (j = 0; j < 8; j++) if (++pos[j] >= RV_LEN[j]) pos[j] = 0;
  }
  for (j = 0; j < 8; j++) { rv_lp[j] = lp[j]; rv_pos[j] = (uint16_t)pos[j]; }
  ap1_i = i1; ap2_i = i2; rv_hp = hz;
}

/* ======================================================================
   The solo: generated as a list of events, 192 units per beat
   ====================================================================== */

#define BT    192
#define BAR   768
#define ST(s) ((s) * 48)
#define EV_MAX 1024

typedef struct { uint16_t t; uint8_t i, v; } Ev;
static Ev ev[EV_MAX];

enum { SEC_COUNT, SEC_GROOVE, SEC_RIDE, SEC_SNARE, SEC_TOMS, SEC_BREAK, SEC_DOUBLE,
       SEC_CLIMAX, SEC_FINALE, NSEC_NAMES };
static const char *SEC_NAME[NSEC_NAMES] = {
  "COUNT IN", "GROOVE", "RIDE GROOVE", "SNARE SOLO", "TOM TOUR", "BREAKDOWN",
  "DOUBLE KICK", "CLIMAX", "FINALE"
};

typedef struct { uint8_t n, bars, countin; uint8_t start[12], id[12]; } SecTab;

static int g_n, g_bar, g_end, g_fkick;
static uint32_t g_rng;

static uint32_t g_rand(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 9; }
static int rr(int n) { return (int)(g_rand() % (uint32_t)n); }
static int chance(int pct) { return rr(100) < pct; }

static void put(int t, int inst, int vel) {
  if (g_n >= EV_MAX) return;
  int tt = g_bar + t;
  if (vel > 0) {
    tt += rr(3) - 1;
    vel += rr(9) - 4;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
  }
  if (tt < 0) tt = 0;
  if (tt > 65535) tt = 65535;
  ev[g_n].t = (uint16_t)tt; ev[g_n].i = (uint8_t)inst; ev[g_n].v = (uint8_t)vel;
  g_n++;
}

static const uint8_t TOMS[6] = { TH, THM, TLM, TL, FTH, FTL };
static const uint8_t RUN7[7] = { SNARE, TH, THM, TLM, TL, FTH, FTL };

enum { F_SN16, F_DN16, F_FLAM8, F_DN6, F_LIN6, F_TRIP, F_DN32 };

static void fill(int t0, int t1, int kind, int v0, int v1) {
  int sub = 48;
  if (kind == F_DN6 || kind == F_LIN6) sub = 32;
  else if (kind == F_DN32) sub = 24;
  else if (kind == F_FLAM8) sub = 96;
  else if (kind == F_TRIP) sub = 64;
  int n = (t1 - t0) / sub;
  if (n < 1) return;
  for (int k = 0; k < n; k++) {
    int t = t0 + k * sub;
    int vel = v0 + (v1 - v0) * k / (n > 1 ? n - 1 : 1);
    int inst;
    if (kind == F_SN16) {
      inst = SNARE;
      vel += (t % BT == 0) ? 10 : -8;
    } else if (kind == F_LIN6 || kind == F_TRIP) {
      int g = k / 3, G = n / 3, r = k % 3;
      if (G < 1) G = 1;
      if (r == 2) inst = KICK;
      else inst = RUN7[1 + g * 5 / G + r];
    } else if (kind == F_FLAM8) {
      inst = RUN7[k * 7 / n];
      put(t - 10, inst, vel / 3);
    } else {
      inst = RUN7[k * 7 / n];
    }
    put(t, inst, vel);
  }
  if (g_fkick && kind != F_LIN6 && kind != F_TRIP)
    for (int t = ((t0 + BT - 1) / BT) * BT; t < t1; t += BT) put(t, KICK, 100);
}

/* variations of a take */
static struct { uint16_t kA, kB; uint8_t hat16, open, ghost, fill; } gA;
static struct { uint16_t k1, k2; uint8_t bell, rim, ghost, fill, fstart, cym; } gB;
static struct { uint16_t acc; uint8_t toms, sext3; } gS;
static struct { uint8_t rot, crash; const char *rhy; } gT;
static struct { uint8_t cow; } gK;
static struct { uint8_t style, b3; } gD;
static struct { uint8_t v; } gC;

static const uint16_t KPA[6] = { 0x0101, 0x0501, 0x0105, 0x0505, 0x0509, 0x0141 };
static const uint16_t KPB[6] = { 0x0509, 0x0485, 0x0C41, 0x0449, 0x4901, 0x0905 };
static const uint16_t KEXT[4] = { 0x0000, 0x4000, 0x8000, 0x2000 };
static const uint16_t ACC[4] = { 0x4949, 0x5249, 0x2525, 0x4929 };
static const char *RHY[4] = {
  "x.xx.xx.x.xx.xx.", "xx.x.xx.xx.x.xxx", "x..xx..xx.x.xxxx", "x.x.xxx.x.x.xxxx"
};

static void count_in(int i, int n) {
  (void)i; (void)n;
  for (int b = 0; b < 4; b++) put(b * BT, STICK, b == 0 ? 112 : 96);
}

static void groove_a(int i, int n) {
  int last = (i == n - 1), end = last ? 12 : 16;
  uint16_t kp = (i & 1) ? gA.kB : gA.kA;
  int open = gA.open && (i & 1) && !last;
  if (i == 0) put(0, CRASH, 118);
  for (int s = 0; s < end; s++) {
    if (!(i == 0 && s == 0) && (gA.hat16 || !(s & 1)) && !(open && s == 15)) {
      if (open && s == 14) put(ST(s), HHO, 90);
      else put(ST(s), HHC, (s % 4 == 0) ? 98 : ((s & 1) ? 50 : 74));
    }
    if (kp & (1u << s)) put(ST(s), KICK, s == 0 ? 116 : 102);
    if (s == 4 || s == 12) put(ST(s), SNARE, 118);
    else if ((s == 3 || s == 7 || s == 9 || s == 15) && chance(gA.ghost)) put(ST(s), SNARE, 30 + rr(14));
  }
  if (last) fill(ST(12), ST(16), gA.fill, 78, 114);
}

static void groove_b(int i, int n) {
  int last = (i == n - 1), end = last ? gB.fstart : 16;
  uint16_t kp = (i & 1) ? gB.k2 : gB.k1;
  if (i == 0) put(0, gB.cym, 120);
  if (i == 2 && chance(50)) put(0, SPLASH, 100);
  for (int s = 0; s < end; s++) {
    if (!(s & 1) && !(i == 0 && s == 0)) {
      int on = (s % 4 == 0);
      put(ST(s), (gB.bell && !on) ? BELL : RIDE, on ? 100 : 82);
    }
    if (s == 4 || s == 12) { put(ST(s), gB.rim ? SNARE2 : SNARE, 120); put(ST(s), HHP, 72); }
    else if ((s == 3 || s == 6 || s == 9 || s == 11 || s == 15) && chance(gB.ghost))
      put(ST(s), SNARE, 28 + rr(16));
    if (kp & (1u << s)) put(ST(s), KICK, s == 0 ? 116 : 104);
  }
  if (last) fill(ST(end), ST(16), gB.fill, 86, 122);
}

static void snare_solo(int i, int n) {
  (void)n;
  for (int b = 0; b < 4; b++) if (!(i == 3 && b == 3)) put(b * BT, HHP, 68);
  if (i == 0) {
    put(0, CRASH, 112); put(0, KICK, 108);
    for (int s = 0; s < 16; s++) {
      if (gS.acc & (1u << s)) put(ST(s), SNARE, 118);
      else put(ST(s), SNARE, 30 + rr(12));
    }
  } else if (i == 1) {                  /* paradiddles, accents around the toms */
    put(0, KICK, 105); put(ST(8), KICK, 100);
    for (int s = 0; s < 16; s++) {
      if (s % 4 == 0) put(ST(s), gS.toms ? TOMS[(s / 4) + (s >= 12 ? 1 : 0)] : SNARE2, 116);
      else put(ST(s), SNARE, 34 + rr(12));
    }
  } else if (i == 2) {                  /* sextuplets with cross accents */
    put(0, KICK, 105); put(2 * BT, KICK, 100);
    for (int k = 0; k < 24; k++) {
      int acc = gS.sext3 ? (k % 3 == 0) : (k % 4 == 0);
      put(k * 32, acc ? SNARE2 : SNARE, acc ? 116 : 38 + rr(10));
    }
  } else {                              /* 32nd crescendo, then flams */
    for (int k = 0; k < 24; k++) put(k * 24, SNARE, 28 + k * 3);
    put(0, KICK, 80); put(BT, KICK, 90); put(2 * BT, KICK, 100);
    put(ST(12) - 10, SNARE, 45); put(ST(12), SNARE2, 122); put(ST(12), KICK, 112);
    put(ST(14) - 10, FTH, 45); put(ST(14), FTL, 122); put(ST(14), KICK, 112);
  }
}

static void tom_tour(int i, int n) {
  (void)n;
  if (i == 0) {
    put(0, gT.crash ? CRASH2 : CRASH, 120); put(0, KICK, 118);
    for (int s = 0; s < 16; s++) {
      if (!(s & 1)) put(ST(s), TOMS[(s / 4 + ((s & 2) ? 1 : 0) + gT.rot) % 5], (s % 4 == 0) ? 112 : 96);
      else put(ST(s), SNARE, 60 + rr(12));
    }
    for (int b = 1; b < 4; b++) put(b * BT, KICK, 100);
  } else if (i == 1) {                  /* triplets: hand, hand, foot */
    for (int k = 0; k < 12; k++) {
      int g = k / 3, r = k % 3;
      if (r == 2) put(k * 64, KICK, 108);
      else put(k * 64, TOMS[g + r], r == 0 ? 114 : 100);
    }
  } else if (i == 2) {                  /* a tom melody, high to low with turns */
    if (chance(50)) put(0, CRASH, 112);
    for (int s = 0; s < 16; s++) {
      if (gT.rhy[s] != 'x') continue;
      int d = s * 6 / 16 + rr(3) - 1;
      if (d < 0) d = 0;
      if (d > 5) d = 5;
      int acc = (s % 4 == 0);
      put(ST(s), (acc && chance(30)) ? SNARE2 : TOMS[d], acc ? 116 : 98);
    }
    for (int b = 0; b < 4; b++) put(b * BT, KICK, 102);
  } else {
    fill(0, BAR, F_DN6, 84, 124);
  }
}

static void breakdown(int i, int n) {
  (void)n;
  if (i == 0) {
    put(0, CRASH, 122); put(0, KICK, 120);
    for (int b = 1; b < 4; b++) put(b * BT, HHP, 70);
    put(ST(4), STICK, 86); put(ST(12), STICK, 86);
    put(ST(10), KICK, 72);
    put(ST(15), FTL, 50);
    if (gK.cow) for (int b = 0; b < 4; b++) put(b * BT + 96, COWB, 80);
  } else {
    static const uint8_t SUB[4] = { 96, 64, 48, 32 };
    int idx = 0;
    for (int b = 0; b < 4; b++) {
      put(b * BT, KICK, 64 + b * 14);
      put(b * BT, HHP, 66);
      for (int t = 0; t < BT; t += SUB[b]) { put(b * BT + t, SNARE, 40 + idx * 80 / 14); idx++; }
    }
  }
}

static void double_kick(int i, int n) {
  (void)n;
  for (int s = 0; s < 16; s++) {
    if (gD.style == 1 && (s & 3) == 1) continue;   /* gallop */
    put(ST(s), KICK, (s & 1) ? 94 : 112);
  }
  if (i == 0) {
    for (int b = 0; b < 4; b++) {
      put(b * BT, (b & 1) ? CHINA : CRASH, 116);
      if (b & 1) put(b * BT, SNARE2, 124);
    }
  } else if (i == 1) {
    put(0, CRASH2, 118);
    for (int s = 0; s < 16; s += 2) put(ST(s), BELL, (s % 4 == 0) ? 112 : 92);
    put(ST(4), SNARE2, 124); put(ST(12), SNARE2, 124);
  } else if (i == 2) {
    put(0, CRASH, 120);
    for (int s = 0; s < 16; s += 2) put(ST(s), (s % 4 == 0) ? FTL : SNARE, (s % 4 == 0) ? 112 : 108);
    put(ST(8), CRASH2, 110);
  } else {
    g_fkick = 0;
    if (gD.b3) {
      put(0, CRASH, 118);
      put(ST(4), SNARE2, 124);
      fill(ST(8), ST(16), F_DN6, 96, 126);
    } else {
      fill(0, BAR, F_DN6, 90, 126);
    }
    g_fkick = 1;
  }
}

static void climax(int i, int n) {
  (void)n;
  if (i == 0) {                         /* six-stroke: R L R L K K */
    static const uint8_t PAIR[4][2] = { { SNARE, TH }, { TH, THM }, { TLM, TL }, { FTH, FTL } };
    put(0, CRASH, 122); put(0, KICK, 118);
    for (int k = 0; k < 24; k++) {
      int g = k / 6, r = k % 6;
      if (r >= 4) put(k * 32, KICK, 106);
      else put(k * 32, PAIR[g][r & 1], r == 0 ? 116 : 98);
    }
  } else if (i == 1) {                  /* crash on every beat, toms between */
    for (int b = 0; b < 4; b++) {
      put(b * BT, (b & 1) ? CRASH2 : CRASH, 118);
      put(b * BT, KICK, 118);
      for (int s = 1; s < 4; s++) put(b * BT + s * 48, TOMS[b + s - 1], gC.v ? 104 : 96);
    }
  } else if (i == 2) {                  /* three over four */
    for (int s = 0; s < 16; s++) {
      if (s % 3 == 0 && s < 15) {
        put(ST(s), ((s / 3) & 1) ? CHINA : CRASH, 118);
        put(ST(s), KICK, 118);
        put(ST(s), SNARE2, 120);
      } else {
        put(ST(s), s == 15 ? SNARE : TOMS[s * 6 / 16], 88);
      }
    }
  } else {
    g_fkick = 0;
    fill(0, BAR, F_DN32, 90, 127);
    g_fkick = 1;
    for (int t = 0; t < BAR; t += 96) put(t, KICK, 110);
  }
}

static void stab(int t, int cym) {
  put(t, cym, 124); put(t, KICK, 122); put(t, SNARE2, 122);
  put(t + 72, cym, 0);                  /* choke */
}

static void finale(int i, int n) {
  (void)n;
  if (i == 0) {
    stab(0, CRASH); stab(ST(3), CRASH2); stab(ST(6), CHINA);
    for (int k = 0; k < 6; k++) put(ST(10 + k), TOMS[k], 112);
    put(ST(12), KICK, 110); put(ST(14), KICK, 110);
  } else if (i == 1) {
    put(0, CRASH, 122); put(0, KICK, 120);
    for (int k = 0; k < 24; k++) put(k * 24, (k & 1) ? FTL : SNARE, 70 + k * 2);
    for (int t = 96; t < 3 * BT; t += 96) put(t, KICK, 100);
    put(3 * BT, KICK, 118);
    for (int k = 0; k < 6; k++) put(3 * BT + k * 32, TOMS[k], 118);
  } else {
    put(0, CRASH, 127); put(0, CRASH2, 127); put(0, CHINA, 112);
    put(0, KICK, 127); put(0, KICK2, 127); put(0, FTL, 124); put(0, SNARE2, 127);
  }
}

typedef void (*BarFn)(int, int);

static void gen_solo(int take, int countin, SecTab *st) {
  static const struct { uint8_t id, bars; BarFn fn; } PLAN[] = {
    { SEC_COUNT, 1, count_in }, { SEC_GROOVE, 4, groove_a }, { SEC_RIDE, 4, groove_b },
    { SEC_SNARE, 4, snare_solo }, { SEC_TOMS, 4, tom_tour }, { SEC_BREAK, 2, breakdown },
    { SEC_DOUBLE, 4, double_kick }, { SEC_CLIMAX, 4, climax }, { SEC_FINALE, 3, finale },
  };
  static const uint8_t FA[4] = { F_SN16, F_DN16, F_FLAM8, F_DN16 };
  static const uint8_t FB[4] = { F_DN16, F_DN6, F_LIN6, F_DN6 };

  g_rng = (uint32_t)take * 2654435761u ^ 0x5BD1E995u;
  for (int k = 0; k < 4; k++) g_rand();

  gA.kA = KPA[rr(6)]; gA.kB = (uint16_t)(gA.kA | KEXT[rr(4)]);
  gA.hat16 = (uint8_t)chance(30); gA.open = (uint8_t)chance(60);
  gA.ghost = (uint8_t)(15 + rr(25)); gA.fill = FA[rr(4)];
  gB.k1 = KPB[rr(6)]; gB.k2 = KPB[rr(6)];
  gB.bell = (uint8_t)chance(40); gB.rim = (uint8_t)chance(60);
  gB.ghost = (uint8_t)(25 + rr(30)); gB.fill = FB[rr(4)];
  gB.fstart = chance(50) ? 8 : 12; gB.cym = chance(50) ? CRASH2 : CRASH;
  if (gB.fstart == 12 && gB.fill != F_DN16) gB.fstart = 8;
  gS.acc = ACC[rr(4)]; gS.toms = (uint8_t)chance(65); gS.sext3 = (uint8_t)chance(50);
  gT.rot = (uint8_t)rr(2); gT.crash = (uint8_t)chance(50); gT.rhy = RHY[rr(4)];
  gK.cow = (uint8_t)chance(30);
  gD.style = (uint8_t)(chance(30) ? 1 : 0); gD.b3 = (uint8_t)chance(50);
  gC.v = (uint8_t)chance(50);
  if (take == 1) {   /* the first take is the reference performance */
    gA.kA = 0x0501; gA.kB = 0x4501; gA.hat16 = 0; gA.open = 1; gA.ghost = 25; gA.fill = F_DN16;
    gB.k1 = 0x0509; gB.k2 = 0x0485; gB.bell = 0; gB.rim = 1; gB.fill = F_DN6; gB.fstart = 8;
    gS.acc = 0x4949; gS.toms = 1; gK.cow = 0; gD.style = 0;
  }

  g_n = 0; g_fkick = 1;
  st->n = 0; st->countin = (uint8_t)(countin ? 1 : 0);
  int bar = 0;
  for (unsigned p = 0; p < sizeof PLAN / sizeof PLAN[0]; p++) {
    if (PLAN[p].id == SEC_COUNT && !countin) continue;
    st->start[st->n] = (uint8_t)bar; st->id[st->n] = PLAN[p].id; st->n++;
    for (int i = 0; i < PLAN[p].bars; i++) {
      g_bar = bar * BAR;
      PLAN[p].fn(i, PLAN[p].bars);
      bar++;
    }
  }
  st->bars = (uint8_t)bar;
  g_end = bar * BAR;
  for (int a = 1; a < g_n; a++) {          /* insertion sort, almost sorted already */
    Ev e = ev[a];
    int b = a - 1;
    while (b >= 0 && ev[b].t > e.t) { ev[b + 1] = ev[b]; b--; }
    ev[b + 1] = e;
  }
}

/* ======================================================================
   Sequencer state shared by audio and loop
   ====================================================================== */

static volatile int seq_on, ev_n, ev_i, seq_end;
static volatile float seq_pos;
static volatile int nx_ready, nx_n, nx_end;
static volatile uint8_t start_req, start_ack, stop_req, stop_ack;
static volatile uint32_t restarts, frames_total;

#define QN 64
static volatile uint8_t q_inst[QN], q_vel[QN];
static volatile uint32_t q_head, q_tail;

static void queue_hit(int inst, int vel) {
  uint32_t h = q_head;
  if (h - q_tail >= QN) return;
  q_inst[h % QN] = (uint8_t)inst;
  q_vel[h % QN] = (uint8_t)vel;
  q_head = h + 1;
}

static float warp(int t) {
  int p = t % 96;
  float s = g_swing;
  float w = (p < 48) ? (float)p * 2.0f * s : 96.0f * s + (float)(p - 48) * 2.0f * (1.0f - s);
  return (float)(t - p) + w;
}

/* ======================================================================
   Audio
   ====================================================================== */

static float c_env, c_gt = 1.0f, c_g = 1.0f, vol_s;

static void render_voices(int off, int n) {
  for (int k = 0; k < NINST; k++) {
    Voice *v = &vox[k];
    if (!v->on) continue;
    switch (INST[k].type) {
    case T_KICK:  r_kick(v, vbuf, n); break;
    case T_SNARE: r_snare(v, vbuf, n); break;
    case T_STICK: r_stick(v, vbuf, n); break;
    case T_CLAP:  r_burst(v, vbuf, n, 0); break;
    case T_TAMB:  r_burst(v, vbuf, n, 1); break;
    case T_HAT:   r_hat(v, vbuf, n); break;
    case T_TOM:   r_tom(v, vbuf, n); break;
    case T_CYM:   r_cym(v, vbuf, n); break;
    case T_BELL:  r_bell(v, vbuf, n); break;
    case T_COW:   r_cow(v, vbuf, n); break;
    default:      r_shak(v, vbuf, n); break;
    }
    float a = v->amp, gl = a * pan_l[k], gr = a * pan_r[k], gs = a * send_g[k];
    float *L = mixL + off, *R = mixR + off, *S = mixS + off;
    for (int i = 0; i < n; i++) {
      float x = vbuf[i];
      L[i] += x * gl; R[i] += x * gr; S[i] += x * gs;
    }
  }
}

static void audio_block(int32_t *out, int frames) {
  int i;
  while (q_tail != q_head) {
    uint32_t t = q_tail;
    trigger(q_inst[t % QN], q_vel[t % QN]);
    q_tail = t + 1;
  }
  if (start_req != start_ack) {
    start_ack = start_req;
    ev_i = 0; seq_pos = 0.0f; seq_on = 1;
  }
  if (stop_req != stop_ack) { stop_ack = stop_req; seq_on = 0; }

  for (i = 0; i < frames; i++) { mixL[i] = 0.0f; mixR[i] = 0.0f; mixS[i] = 0.0f; }

  float pos = seq_pos, upb = g_upb;
  i = 0;
  while (i < frames) {
    int n = frames - i;
    if (seq_on) {
      while (ev_i < ev_n && warp(ev[ev_i].t) <= pos) {
        trigger(ev[ev_i].i, ev[ev_i].v);
        ev_i = ev_i + 1;
      }
      if (ev_i >= ev_n && pos >= (float)seq_end) {
        if (loop_on && nx_ready) {
          pos -= (float)seq_end;
          ev_n = nx_n; seq_end = nx_end; ev_i = 0; nx_ready = 0;
          restarts = restarts + 1;
          continue;
        }
        seq_on = 0;
      } else {
        float nt = (ev_i < ev_n) ? warp(ev[ev_i].t) : (float)seq_end;
        int ns = (int)((nt - pos) / upb) + 1;
        if (ns < n) n = ns;
        if (n < 1) n = 1;
      }
    }
    render_voices(i, n);
    if (seq_on) pos += (float)n * upb;
    i += n;
  }
  seq_pos = pos;

  /* master: room, bus compressor, drive, volume, soft limit */
  room_block(frames);
  float dr = g_drv, dr1 = 1.0f - dr, drg = 1.0f + 3.0f * dr, vt = g_vol;
  float env = c_env, gt = c_gt, gc = c_g, vs = vol_s;
  for (i = 0; i < frames; i++) {
    float l = mixL[i], r = mixR[i];
    float a = fabsf(l), b = fabsf(r);
    if (b > a) a = b;
    env += (a > env ? 0.004f : 0.00015f) * (a - env);
    if ((i & 15) == 0) gt = (env > 0.5f) ? powf(env * 2.0f, -0.6667f) : 1.0f;
    gc += 0.02f * (gt - gc);
    float g = gc * 1.1f;
    l *= g; r *= g;
    l = l * dr1 + sat(l * drg) * dr;
    r = r * dr1 + sat(r * drg) * dr;
    vs += 0.002f * (vt - vs);
    l = sat(l * vs);
    r = sat(r * vs);
    out[2 * i] = (int32_t)(l * (0.89f * (float)FM1_AUDIO_MAX));
    out[2 * i + 1] = (int32_t)(r * (0.89f * (float)FM1_AUDIO_MAX));
  }
  c_env = env; c_gt = gt; c_g = gc; vol_s = vs;
  if (c_env < 1.0e-6f) c_env = 0.0f;

  for (i = 0; i < NINST; i++) {
    float x = disp[i] * disp_k[i];
    disp[i] = (x < 0.01f) ? 0.0f : x;
  }
  frames_total = frames_total + (uint32_t)frames;
}

void audio(int32_t *out, int frames) {
  int done = 0;
  while (done < frames) {
    int n = frames - done;
    if (n > NBLK) n = NBLK;
    audio_block(out + 2 * done, n);
    done += n;
  }
}

/* ======================================================================
   Screen
   ====================================================================== */

static uint16_t rgb(int r, int g, int b) {
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#define C_BG    0x0841u   /* about (8, 8, 8) */
#define C_PANEL 0x18E6u   /* (24, 28, 48) */

static uint16_t c_dim, c_text, c_acc, c_warm, c_green;

/* 5x7 font, ASCII 32 to 90, one byte per column, bit 0 at the top */
static const uint8_t FONT[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};

static uint16_t tbuf[18 * 24];

static void draw_char(int x, int y, char ch, int s, uint16_t fg, uint16_t bg) {
  int c = (unsigned char)ch;
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c < 32 || c > 90) c = 32;
  const uint8_t *g = FONT[c - 32];
  int w = 6 * s, h = 8 * s;
  for (int yy = 0; yy < h; yy++) {
    int row = yy / s;
    for (int xx = 0; xx < w; xx++) {
      int col = xx / s;
      int on = (col < 5 && row < 7) && ((g[col] >> row) & 1);
      tbuf[yy * w + xx] = on ? fg : bg;
    }
  }
  fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, tbuf, (uint32_t)(w * h));
}

static void draw_text(int x, int y, const char *s, int sc, uint16_t fg, uint16_t bg) {
  while (*s && x + 6 * sc <= 240) { draw_char(x, y, *s++, sc, fg, bg); x += 6 * sc; }
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int itoa_s(char *b, int v) {
  char t[12];
  int n = 0, k = 0;
  if (v < 0) { b[k++] = '-'; v = -v; }
  do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) b[k++] = t[--n];
  b[k] = 0;
  return k;
}

/* text in a field of `w` characters; align 0 left, 1 centre */
static void draw_field(int x, int y, const char *s, int w, int sc, int align, uint16_t fg, uint16_t bg) {
  char b[24];
  int n = slen(s), k;
  if (n > w) n = w;
  int lead = align ? (w - n) / 2 : 0;
  for (k = 0; k < w; k++) b[k] = ' ';
  for (k = 0; k < n; k++) b[lead + k] = s[k];
  b[w] = 0;
  draw_text(x, y, b, sc, fg, bg);
}

static const char *PAD_NAME[12] = {
  "CRASH", "HIHAT", "RIDE", "CRASH2", "TOM 1", "TOM 2", "TOM 3", "FLOOR",
  "KICK", "SNARE", "CLAP", "PERC"
};
static const uint8_t PADC[12][3] = {
  {255, 200, 50}, {255, 200, 50}, {255, 200, 50}, {255, 200, 50},
  {255, 120, 40}, {255, 120, 40}, {255, 120, 40}, {255, 120, 40},
  {255, 60, 90}, {60, 200, 255}, {190, 120, 255}, {90, 230, 120}
};

static uint16_t pad_col(int p, int f) {   /* f: 0..255 */
  return rgb(PADC[p][0] * f / 255, PADC[p][1] * f / 255, PADC[p][2] * f / 255);
}

static void draw_pad(int p, int q) {
  int x = 7 + (p & 3) * 58, y = 87 + (p >> 2) * 28;
  int f = 34 + q * 44;
  uint16_t col = pad_col(p, f > 255 ? 255 : f);
  fm1_lcd_fill_rect((uint32_t)x + 1, (uint32_t)y + 1, 50, 22, col);
  const char *nm = PAD_NAME[p];
  draw_text(x + 26 - slen(nm) * 3, y + 8, nm, 1, q >= 3 ? C_BG : pad_col(p, 230), col);
}

static void draw_step(int k, int hi) {
  uint16_t c;
  if (hi) c = (k & 3) ? c_acc : c_warm;
  else c = (k & 3) ? rgb(34, 38, 52) : rgb(66, 70, 92);
  fm1_lcd_fill_rect((uint32_t)(9 + k * 14), 73, 12, 8, c);
}

/* ======================================================================
   User interface state
   ====================================================================== */

static SecTab sec_cur, sec_nx;
static int nx_take;
static uint32_t seen_restarts;
static int ui_ready;
static int8_t pad_q[12];
static int step_drawn = -1, prog_drawn = 0, pad_rr = 0;
static int ui_state = -1, ui_loop = -1, ui_bar = -100, ui_sec = -100, mark_dirty = 1;
static int ui_par[7];
static int hit_inst = -1;
static uint32_t hit_until;
static int8_t led_cur[FM1_NLED];

static void led(uint32_t id, int lvl) {
  if (led_cur[id] != lvl) { led_cur[id] = (int8_t)lvl; fm1_led_level(id, lvl); }
}

static void apply_settings(void) {
  g_upb = (float)set_bpm * (192.0f / 60.0f) * INV_SR;
  g_swing = (float)set_swing * 0.01f;
  g_tune = exp2_f((float)set_tune * (1.0f / 12.0f));
  g_wet = (float)set_room * 0.03f;
  g_drv = (float)set_drive * 0.01f;
  g_kit = set_kit;
}

static void start_solo(void) {
  gen_solo(set_take, 1, &sec_cur);
  ev_n = g_n; seq_end = g_end; nx_ready = 0;
  mark_dirty = 1;
  start_req = (uint8_t)(start_req + 1);
}

static int running(void) { return seq_on || start_req != start_ack; }

static void press_play(void) {
  if (stop_req != stop_ack) return;
  if (running()) stop_req = (uint8_t)(stop_req + 1);
  else start_solo();
}

static void ui_static(void) {
  static const char *title = "DRUM SOLO MACHINE";
  fm1_lcd_fill_rect(0, 0, 240, 240, C_BG);
  for (int k = 0; title[k]; k++) {
    int t = k * 255 / 16;
    draw_char(18 + k * 12, 5, title[k], 2, rgb(255, 120 + t * 100 / 255, 40 + t * 30 / 255), C_BG);
  }
  fm1_lcd_fill_rect(8, 23, 224, 1, rgb(110, 60, 20));
  fm1_lcd_fill_rect(8, 61, 224, 5, C_PANEL);
  for (int k = 0; k < 16; k++) draw_step(k, 0);
  for (int p = 0; p < 12; p++) {
    int x = 7 + (p & 3) * 58, y = 87 + (p >> 2) * 28;
    fm1_lcd_fill_rect((uint32_t)x, (uint32_t)y, 52, 24, pad_col(p, 110));
    draw_pad(p, 0);
    pad_q[p] = 0;
  }
  static const char *L1[4] = { "TEMPO", "SWING", "TUNE", "ROOM" };
  static const char *L2[3] = { "KIT", "DRIVE", "TAKE" };
  for (int k = 0; k < 4; k++) draw_text(6 + k * 60, 175, L1[k], 1, c_dim, C_BG);
  for (int k = 0; k < 3; k++) draw_text(6 + k * 80, 205, L2[k], 1, c_dim, C_BG);
}

static void draw_param(int k, int v) {
  char b[16];
  if (k < 4) {
    int n = 0;
    if (k == 2 && v > 0) b[n++] = '+';
    n += itoa_s(b + n, v);
    if (k == 1) { b[n++] = '%'; b[n] = 0; }
    draw_field(6 + k * 60, 184, b, 4, 2, 0, c_text, C_BG);
  } else if (k == 4) {
    draw_field(6, 214, KITS[v].name, 6, 2, 0, c_warm, C_BG);
  } else {
    itoa_s(b, v);
    draw_field(6 + (k - 4) * 80, 214, b, 6, 2, 0, c_warm, C_BG);
  }
}

static void draw_markers(void) {
  fm1_lcd_fill_rect(8, 67, 224, 2, C_BG);
  int bars = sec_cur.bars ? sec_cur.bars : 1;
  for (int k = 1; k < sec_cur.n; k++) {
    int x = 8 + sec_cur.start[k] * 224 / bars;
    if (x > 230) x = 230;
    fm1_lcd_fill_rect((uint32_t)x, 67, 2, 2, c_dim);
  }
}

static void ui_update(void) {
  int on = seq_on;
  float pos = seq_pos;
  int bar = on ? (int)(pos * (1.0f / BAR)) : -1;
  char b[24];
  int k;

  /* section of the solo now playing */
  int sec = -1;
  if (on) for (k = 0; k < sec_cur.n; k++) if (bar >= sec_cur.start[k]) sec = sec_cur.id[k];

  /* status line */
  int state = on ? (sec == SEC_COUNT ? 2 : 1) : 0;
  if (state != ui_state) {
    static const char *SN[3] = { "STOPPED", "PLAYING", "COUNT-IN" };
    draw_field(8, 29, SN[state], 8, 1, 0, state ? c_green : c_dim, C_BG);
    ui_state = state;
  }
  if (loop_on != ui_loop) {
    draw_field(105, 29, loop_on ? "LOOP" : "", 4, 1, 0, c_acc, C_BG);
    ui_loop = loop_on;
  }
  int total = sec_cur.bars - sec_cur.countin;
  int shown = (on && bar >= sec_cur.countin) ? bar - sec_cur.countin + 1 : 0;
  if (shown * 256 + total != ui_bar) {
    int n = 0;
    b[n++] = 'B'; b[n++] = 'A'; b[n++] = 'R'; b[n++] = ' ';
    if (shown) { if (shown < 10) b[n++] = ' '; n += itoa_s(b + n, shown); }
    else { b[n++] = '-'; b[n++] = '-'; }
    b[n++] = '/';
    itoa_s(b + n, total);
    draw_field(172, 29, b, 10, 1, 0, c_text, C_BG);
    ui_bar = shown * 256 + total;
  }

  /* big line: drum name after a key press, else the section */
  int what;
  if (hit_inst >= 0 && (int32_t)(hit_until - frames_total) > 0) what = 100 + hit_inst;
  else { hit_inst = -1; what = on ? sec : -1; }
  if (what != ui_sec) {
    if (what >= 100) draw_field(6, 41, INST[what - 100].name, 19, 2, 1, pad_col(INST[what - 100].pad, 255), C_BG);
    else if (what >= 0) draw_field(6, 41, SEC_NAME[what], 19, 2, 1, c_text, C_BG);
    else draw_field(6, 41, "PRESS PLAY", 19, 2, 1, c_dim, C_BG);
    ui_sec = what;
  }

  if (mark_dirty) { draw_markers(); mark_dirty = 0; }

  /* progress bar */
  int w = 0;
  if (on && seq_end > 0) { w = (int)(pos * 224.0f / (float)seq_end); if (w > 224) w = 224; if (w < 0) w = 0; }
  if (w < prog_drawn) { fm1_lcd_fill_rect(8, 61, 224, 5, C_PANEL); prog_drawn = 0; }
  if (w > prog_drawn) { fm1_lcd_fill_rect((uint32_t)(8 + prog_drawn), 61, (uint32_t)(w - prog_drawn), 5, c_acc); prog_drawn = w; }

  /* step of the bar */
  int step = on ? ((int)(pos * (1.0f / 48.0f))) & 15 : -1;
  if (step != step_drawn) {
    if (step_drawn >= 0) draw_step(step_drawn, 0);
    if (step >= 0) draw_step(step, 1);
    step_drawn = step;
  }

  /* pads: at most three redraws per call */
  float lv[12];
  for (k = 0; k < 12; k++) lv[k] = 0.0f;
  for (k = 0; k < NINST; k++) { float x = disp[k]; if (x > lv[INST[k].pad]) lv[INST[k].pad] = x; }
  int budget = 3;
  for (k = 0; k < 12 && budget > 0; k++) {
    int p = (pad_rr + k) % 12;
    int q = (int)(lv[p] * 5.99f);
    if (q != pad_q[p]) { draw_pad(p, q); pad_q[p] = (int8_t)q; budget--; }
  }
  pad_rr = (pad_rr + 1) % 12;

  /* parameters: at most two per call */
  int vals[7] = { set_bpm, set_swing, set_tune, set_room, set_kit, set_drive, set_take };
  budget = 2;
  for (k = 0; k < 7 && budget > 0; k++)
    if (vals[k] != ui_par[k]) { draw_param(k, vals[k]); ui_par[k] = vals[k]; budget--; }
}

/* ======================================================================
   setup and loop
   ====================================================================== */

void setup(void) {
  int k;
  for (k = 0; k < NINST; k++) {
    float a = ((float)INST[k].pan + 100.0f) * (0.25f * PI_F / 100.0f);
    pan_l[k] = sinf(a + 0.5f * PI_F);
    pan_r[k] = sinf(a);
    send_g[k] = (float)INST[k].send * 0.01f;
    float td = INST[k].d;
    if (INST[k].type == T_KICK || INST[k].type == T_TOM) td *= 0.6f;
    if (td < 0.06f) td = 0.06f;
    if (td > 0.35f) td = 0.35f;
    disp_k[k] = exp2_f(-1.442695f * ((float)NBLK * INV_SR) / td);
  }
  int off = 0;
  for (k = 0; k < 8; k++) { rv_off[k] = (uint16_t)off; off += RV_LEN[k]; }
  c_dim = rgb(110, 116, 136); c_text = rgb(228, 230, 238);
  c_acc = rgb(60, 200, 255); c_warm = rgb(255, 170, 50); c_green = rgb(90, 235, 120);
  for (k = 0; k < 7; k++) ui_par[k] = -999;
  apply_settings();
  room_set();
  vol_s = 0.0f;
  led(FM1_BTN_PLAY, FM1_LED_ON);
  led(FM1_BTN_SEQ, FM1_LED_ON);
}

void loop(void) {
  int k;
  if (!ui_ready) {
    ui_static();
    gen_solo(set_take, 1, &sec_cur);   /* fills the section markers */
    mark_dirty = 1;
    ui_ready = 1;
  }

  /* buttons */
  uint32_t rel = 0;
  uint32_t pressed = fm1_input_edges(&rel);
  if (pressed & (1u << FM1_BTN_PLAY)) press_play();
  if (pressed & (1u << FM1_BTN_SEQ)) loop_on = !loop_on;

  /* note keys play the drums */
  uint32_t notes = fm1_input_note_edges();
  for (k = 0; k < NINST; k++) {
    if (notes & (1u << k)) {
      queue_hit(k, 100 + (int)((frames_total >> 3) % 13u));
      hit_inst = k; hit_until = frames_total + 53000u;
    }
  }

  /* MIDI */
  uint32_t pk;
  for (k = 0; k < 32 && fm1_midi_take(&pk); k++) {
    uint32_t st = FM1_MIDI_STATUS(pk), d1 = FM1_MIDI_DATA1(pk), d2 = FM1_MIDI_DATA2(pk);
    if ((st & 0xF0u) == 0x90u && d2 > 0u) {
      int n = (int)d1 - 35;
      if (n >= 0 && n < NINST) queue_hit(n, (int)d2);
    } else if (st == 0xFAu || st == 0xFBu) {
      if (!running()) press_play();
    } else if (st == 0xFCu) {
      if (running()) press_play();
    }
  }

  /* encoders */
  int32_t d;
  int changed = 0, kitch = 0;
  if ((d = fm1_enc_take(FM1_ENC_KNOB1)) != 0) { set_bpm += d; changed = 1; }
  if ((d = fm1_enc_take(FM1_ENC_KNOB2)) != 0) { set_swing += d; changed = 1; }
  if ((d = fm1_enc_take(FM1_ENC_KNOB3)) != 0) { set_tune += d; changed = 1; }
  if ((d = fm1_enc_take(FM1_ENC_KNOB4)) != 0) { set_room += 2 * d; changed = 1; }
  if ((d = fm1_enc_take(FM1_ENC_SELECT)) != 0) { set_kit = ((set_kit + d) % 4 + 4) % 4; changed = 1; kitch = 1; }
  if ((d = fm1_enc_take(FM1_ENC_ALGORITHM)) != 0) { set_drive += 2 * d; changed = 1; }
  if ((d = fm1_enc_take(FM1_ENC_PRESETS)) != 0) { set_take = ((set_take - 1 + d) % 99 + 99) % 99 + 1; }
  if (changed) {
    if (set_bpm < 60) set_bpm = 60;
    if (set_bpm > 200) set_bpm = 200;
    if (set_swing < 50) set_swing = 50;
    if (set_swing > 75) set_swing = 75;
    if (set_tune < -12) set_tune = -12;
    if (set_tune > 12) set_tune = 12;
    if (set_room < 0) set_room = 0;
    if (set_room > 100) set_room = 100;
    if (set_drive < 0) set_drive = 0;
    if (set_drive > 100) set_drive = 100;
    apply_settings();
    if (kitch) room_set();
  }

  /* MASTER is the volume */
  float m = 1.0f; /* MASTER is the FM-1's own volume (on the speaker): the full level here */
  g_vol = m * m;

  /* loop mode: prepare the next take while the last one rings out */
  if (loop_on && seq_on && !nx_ready && ev_i >= ev_n) {
    nx_take = set_take % 99 + 1;
    gen_solo(nx_take, 0, &sec_nx);
    nx_n = g_n; nx_end = g_end;
    nx_ready = 1;
  }
  if (restarts != seen_restarts) {
    seen_restarts = restarts;
    set_take = nx_take;
    sec_cur = sec_nx;
    mark_dirty = 1;
  }

  /* LEDs */
  int on = seq_on;
  int blink;
  if (on) blink = ((int)seq_pos % BT) < BT / 2;
  else blink = ((frames_total / 11025u) & 1u) == 0u;
  led(FM1_BTN_PLAY, on ? (blink ? FM1_LED_ON : FM1_LED_OFF) : FM1_LED_ON);
  led(FM1_BTN_SEQ, loop_on ? (blink ? FM1_LED_ON : FM1_LED_OFF) : FM1_LED_ON);
  for (k = 0; k < NINST; k++)
    led(FM1_KEY_NOTE0 + (uint32_t)k, disp[k] > 0.1f ? FM1_LED_ON : FM1_LED_OFF);

  ui_update();
}
