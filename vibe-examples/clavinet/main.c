/* CLAVINET - a funk clavinet for the M-VAVE FM-1 (FM-1 Studio).
 *
 * Sound: each note is a string (a delay loop) that a tangent strikes with a
 * short pulse. Two pickups read the string (comb filters), as on a D6. After
 * that: a tone filter, an auto-wah, a soft overdrive and a small stereo room.
 * PLAY starts and stops a 28-bar funk solo in E, with a drum groove.
 *
 * audio() makes all sound and runs the solo. loop() reads the controls,
 * sets the LEDs and draws only what changed.
 */
#include "fm1.h"
#include <math.h>

#define SR        44100.0f
#define PI_F      3.14159265f
#define BLK       FM1_AUDIO_BLOCK

/* ------------------------------------------------------------------ */
/* Values that loop() writes and audio() reads                        */
/* ------------------------------------------------------------------ */

static volatile float p_mute    = 0.2f;   /* 0 = long strings, 1 = fully damped */
static volatile float p_tone_g  = 0.3f;   /* tone filter: tan(pi * fc / SR) */
static volatile float p_wah_sens = 2.0f;
static volatile float p_drv_pre = 2.0f;
static volatile float p_drv_post = 0.6f;
static volatile float p_pk0 = 1.5f, p_pk1 = -0.65f, p_pk2 = -0.85f; /* pickups */
static volatile float p_master  = 0.5f;
static volatile int   p_wah_on  = 0;
static volatile int   p_drums   = 1;
static volatile int   p_step_len = 6361;  /* samples in a 16th note */

/* Events from loop() to audio(): a small ring. */
#define Q_LEN 32u
#define Q_NOTE_ON  0x0100u
#define Q_NOTE_OFF 0x0200u
#define Q_START    0x0300u
#define Q_STOP     0x0400u
static volatile uint16_t q_buf[Q_LEN];
static volatile uint32_t q_head, q_tail;

/* What audio() tells loop(). */
static volatile int32_t s_pos = -1;       /* bar * 16 + step of the solo, -1 = stopped */

/* ------------------------------------------------------------------ */
/* The strings                                                        */
/* ------------------------------------------------------------------ */

#define NVOICE   8
#define DLEN     1024u
#define DMASK    (DLEN - 1u)
#define EXC_MAX  16
#define NOTE_MIN 29     /* F1 */
#define NOTE_MAX 96     /* C7 */

enum { V_OFF = 0, V_HELD = 1, V_REL = 2 };
enum { SRC_KEY = 0, SRC_SEQ = 1 };

typedef struct {
  float    buf[DLEN];
  float    exc[EXC_MAX];
  float    g, g_rel;        /* loop gain: held, released */
  float    b;               /* loop filter: held (released is B_REL) */
  float    ap_a, ap_x, ap_y;/* allpass for the fine tuning */
  float    lp_z;
  uint32_t w;               /* write position */
  uint32_t d_int, tap1, tap2;
  int32_t  exc_pos, exc_len;
  int32_t  off_cnt;         /* samples to the release (solo notes), 0 = none */
  int32_t  life;            /* samples until the voice is free */
  uint32_t age;
  volatile uint8_t note, state, src;
} voice_t;

static voice_t voices[NVOICE];
static uint32_t voice_clock;
static float note_per[128];   /* period in samples */
static float note_amp[128];   /* strike level for an even loudness */
static float note_t60[128];   /* decay time in seconds */

#define B_REL   0.48f
#define T60_REL 0.045f

/* exp(-x) for x >= 0, without the math library (audio() must be fast). */
static float exp_neg(float x)
{
  return 1.0f / (1.0f + x + 0.5f * x * x + 0.16667f * x * x * x);
}

static void voice_release(voice_t *v)
{
  if (v->state == V_HELD) {
    v->state = V_REL;
    v->off_cnt = 0;
    v->life = (int32_t)(0.2f * SR);
  }
}

static void voice_start(int note, float vel, int32_t off_cnt, uint8_t src)
{
  voice_t *v = 0;
  int i;
  uint32_t best;
  float per, mute, b, rem, fr, t60, m1, wf, amp;
  uint32_t di;
  int wl;

  if (note < NOTE_MIN) note = NOTE_MIN;
  if (note > NOTE_MAX) note = NOTE_MAX;

  /* The same note is the same string. */
  for (i = 0; i < NVOICE; i++)
    if (voices[i].state != V_OFF && voices[i].note == note) { v = &voices[i]; break; }
  if (!v)
    for (i = 0; i < NVOICE; i++)
      if (voices[i].state == V_OFF) { v = &voices[i]; break; }
  if (!v) {
    best = 0xFFFFFFFFu;
    for (i = 0; i < NVOICE; i++)
      if (voices[i].state == V_REL && voices[i].age < best) { best = voices[i].age; v = &voices[i]; }
  }
  if (!v) {
    best = 0xFFFFFFFFu;
    for (i = 0; i < NVOICE; i++)
      if (voices[i].age < best) { best = voices[i].age; v = &voices[i]; }
  }

  per = note_per[note];
  mute = p_mute;
  b = 0.14f + 0.30f * mute;
  if (per < 200.0f) b *= (per < 50.0f ? 50.0f : per) * (1.0f / 200.0f);  /* high strings stay bright */
  rem = per - b;
  di = (uint32_t)(rem - 0.3f);
  fr = rem - (float)di;
  v->ap_a = (1.0f - fr) / (1.0f + fr);
  v->d_int = di;
  v->b = b;

  m1 = 1.0f - mute;
  t60 = 0.07f + note_t60[note] * m1 * m1 * m1;
  v->g = exp_neg(6.91f * per / (SR * t60));
  v->g_rel = exp_neg(6.91f * per / (SR * T60_REL));
  v->life = (int32_t)(t60 * 2.0f * SR);

  v->tap1 = (uint32_t)(0.29f * per + 0.5f);
  v->tap2 = (uint32_t)(0.11f * per + 0.5f);
  if (v->tap2 < 1u) v->tap2 = 1u;
  if (v->tap1 <= v->tap2) v->tap1 = v->tap2 + 1u;

  /* A silent string: clear the part of the loop that is read next. */
  for (i = 1; i <= (int)di + 2; i++) v->buf[(v->w - (uint32_t)i) & DMASK] = 0.0f;
  v->ap_x = v->ap_y = v->lp_z = 0.0f;

  /* The strike: a short bump. A hard strike is shorter (brighter) and louder. */
  wf = 5.0f + 9.0f * (1.0f - vel);
  wl = (int)(wf + 0.5f);
  if (wl < 4) wl = 4;
  if (wl > EXC_MAX) wl = EXC_MAX;
  amp = vel * vel * (8.0f / (float)wl) * note_amp[note];
  for (i = 0; i < wl; i++) {
    float u = ((float)i + 0.5f) / (float)wl;
    float q = 4.0f * u * (1.0f - u);
    v->exc[i] = amp * q * q;
  }
  v->exc_pos = 0;
  v->exc_len = wl;

  v->off_cnt = off_cnt;
  v->age = ++voice_clock;
  v->note = (uint8_t)note;
  v->src = src;
  v->state = V_HELD;
}

static void voice_run(voice_t *v, float *out, int n, float c0, float c1, float c2)
{
  float *buf = v->buf;
  uint32_t w = v->w, di = v->d_int, t1 = v->tap1, t2 = v->tap2;
  float a = v->ap_a, ax = v->ap_x, ay = v->ap_y, lz = v->lp_z;
  float g = (v->state == V_HELD) ? v->g : v->g_rel;
  float b = (v->state == V_HELD) ? v->b : B_REL;
  int ep = v->exc_pos, el = v->exc_len;
  int i;

  for (i = 0; i < n; i++) {
    float x = buf[(w - di) & DMASK];
    float ap = a * (x - ay) + ax;
    float f, y;
    ax = x; ay = ap;
    f = ap + b * (lz - ap);
    lz = ap;
    y = g * f;
    if (ep < el) y += v->exc[ep++];
    buf[w & DMASK] = y;
    out[i] += c0 * y + c1 * buf[(w - t1) & DMASK] + c2 * buf[(w - t2) & DMASK];
    w++;
  }
  v->w = w & DMASK;
  v->ap_x = ax; v->ap_y = ay; v->lp_z = lz;
  v->exc_pos = ep;
}

static void voices_render(float *out, int n)
{
  float c0 = p_pk0, c1 = p_pk1, c2 = p_pk2;
  int i;
  for (i = 0; i < NVOICE; i++) {
    voice_t *v = &voices[i];
    int left = n, done = 0;
    if (v->state == V_OFF) continue;
    while (left > 0) {
      int m = left;
      if (v->state == V_HELD && v->off_cnt > 0 && v->off_cnt < m) m = v->off_cnt;
      voice_run(v, out + done, m, c0, c1, c2);
      if (v->state == V_HELD && v->off_cnt > 0) {
        v->off_cnt -= m;
        if (v->off_cnt <= 0) voice_release(v);
      }
      done += m; left -= m;
    }
    v->life -= n;
    if (v->life <= 0) v->state = V_OFF;
  }
}

/* ------------------------------------------------------------------ */
/* Drums                                                              */
/* ------------------------------------------------------------------ */

static float k_ph, k_fenv, k_aenv;
static float sn_env, sn_tenv, sn_ph, sn_lp, sn_dec;
static float h_env, h_dec, h_z1, h_z2;
static uint32_t rng = 0x1234567u;

static float frand(void)   /* -1 to 1 */
{
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static float fsin(float ph)  /* sin(2 pi ph), ph from 0 to 1, a fast approximation */
{
  float x = 2.0f * ph - 1.0f;
  float ax = x < 0.0f ? -x : x;
  float y = 4.0f * x * (1.0f - ax);
  float ay = y < 0.0f ? -y : y;
  y = 0.225f * (y * ay - y) + y;
  return -y;
}

static void drums_render(float *dr, float *ht, int n)
{
  int i;
  if (k_aenv < 0.0005f && sn_env < 0.0005f && sn_tenv < 0.0005f && h_env < 0.0005f) return;
  for (i = 0; i < n; i++) {
    float nz = frand();
    float o = 0.0f;
    if (k_aenv > 0.0005f) {
      k_ph += (47.0f + 135.0f * k_fenv) * (1.0f / SR);
      if (k_ph >= 1.0f) k_ph -= 1.0f;
      o += fsin(k_ph) * k_aenv;
      k_fenv *= 0.99918f;
      k_aenv *= 0.99975f;
    }
    if (sn_env > 0.0005f || sn_tenv > 0.0005f) {
      float hp;
      sn_lp += 0.22f * (nz - sn_lp);
      hp = nz - sn_lp;
      sn_ph += (176.0f + 70.0f * sn_tenv) * (1.0f / SR);
      if (sn_ph >= 1.0f) sn_ph -= 1.0f;
      o += hp * sn_env * 0.75f + fsin(sn_ph) * sn_tenv * 0.45f;
      sn_env *= sn_dec;
      sn_tenv *= 0.99915f;
    }
    dr[i] += o;
    if (h_env > 0.0005f) {
      float d1 = nz - h_z1, d2;
      h_z1 = nz;
      d2 = d1 - h_z2;
      h_z2 = d1;
      ht[i] += d2 * h_env;
      h_env *= h_dec;
    }
  }
}

/* ------------------------------------------------------------------ */
/* The solo                                                           */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t step, note, len, vel; } ev_t;  /* len: 4 = one 16th */
#define EV_END {255, 0, 0, 0}

enum {
  nD2 = 38, nE2 = 40, nG2 = 43, nA2 = 45, nAS2 = 46, nB2 = 47,
  nD3 = 50, nE3 = 52, nG3 = 55, nGS3 = 56, nA3 = 57, nAS3 = 58, nB3 = 59,
  nCS4 = 61, nD4 = 62, nE4 = 64, nFS4 = 66, nG4 = 67, nGS4 = 68, nA4 = 69,
  nAS4 = 70, nB4 = 71, nD5 = 74, nDS5 = 75, nE5 = 76, nG5 = 79
};

/* The riff: low E, octave pops, the E7#9 stab on 2 and 4. */
static const ev_t pat_a[] = {
  {0,nE2,6,118},{2,nE3,2,84},{3,nE2,2,62},{4,nD4,3,108},{4,nG4,3,110},
  {6,nE3,2,92},{7,nD3,2,88},{8,nE3,3,104},{10,nG2,2,96},{11,nA2,3,102},
  {12,nD4,2,104},{12,nG4,2,108},{13,nE2,2,60},{14,nB2,2,94},{15,nD3,3,100}, EV_END };
/* The answer: a slide into the stab, then down the scale. */
static const ev_t pat_b[] = {
  {0,nE2,6,118},{2,nE3,2,84},{3,nE2,2,62},{4,nD4,3,108},{4,nG4,3,110},
  {6,nCS4,2,88},{6,nFS4,2,90},{7,nD4,4,108},{7,nG4,4,112},{9,nE3,2,96},
  {10,nD3,2,92},{11,nB2,2,94},{12,nA2,2,102},{13,nG2,2,92},{14,nE2,3,100},
  {15,nD2,3,98}, EV_END };
/* The turnaround: G A A# B. */
static const ev_t pat_c[] = {
  {0,nE2,6,118},{2,nE3,2,84},{3,nE2,2,62},{4,nD4,3,108},{4,nG4,3,110},
  {6,nE3,2,92},{7,nD3,2,88},{8,nE3,3,104},{10,nG2,3,100},{12,nA2,3,106},
  {13,nA2,2,60},{14,nAS2,3,104},{15,nB2,4,112}, EV_END };
/* The riff, then a blues run up into the solo. */
static const ev_t pat_d[] = {
  {0,nE2,6,118},{2,nE3,2,84},{3,nE2,2,62},{4,nD4,3,108},{4,nG4,3,110},
  {6,nE3,2,90},{8,nE3,2,96},{9,nG3,2,98},{10,nA3,2,100},{11,nAS3,2,100},
  {12,nB3,2,106},{13,nD4,2,106},{14,nE4,2,110},{15,nG4,3,114}, EV_END };
/* Solo lines. */
static const ev_t pat_l1[] = {
  {0,nE2,6,112},{2,nB4,2,100},{3,nD5,2,104},{4,nE5,3,116},{6,nE5,2,94},
  {7,nD5,2,100},{8,nB4,2,106},{8,nE3,2,78},{9,nD5,3,108},{10,nE2,2,80},
  {11,nB4,2,98},{12,nA4,3,108},{13,nG4,2,96},{14,nA4,3,104},{15,nD3,2,84}, EV_END };
static const ev_t pat_l2[] = {
  {0,nE2,6,112},{3,nAS4,2,94},{3,nDS5,2,96},{4,nB4,4,110},{4,nE5,4,114},
  {6,nB4,2,90},{6,nE5,2,92},{7,nA4,2,98},{7,nD5,2,100},{8,nE3,2,80},
  {9,nA4,2,94},{9,nD5,2,96},{10,nB4,3,106},{10,nE5,3,110},{12,nG4,2,104},
  {13,nE4,2,98},{14,nD4,2,96},{15,nE4,4,106}, EV_END };
static const ev_t pat_l3[] = {
  {0,nE2,6,112},{1,nE5,2,98},{2,nG5,3,114},{4,nE5,2,106},{5,nD5,2,98},
  {6,nB4,2,102},{7,nAS4,2,96},{8,nA4,3,108},{8,nE2,2,74},{9,nG4,2,96},
  {10,nA4,2,100},{11,nG4,2,98},{12,nE4,3,108},{13,nD4,2,96},{14,nE4,2,100},
  {15,nG4,3,104}, EV_END };
static const ev_t pat_l4[] = {
  {0,nE2,6,112},{2,nE4,2,100},{3,nE4,2,84},{4,nG4,3,112},{6,nE4,2,96},
  {7,nG4,2,100},{8,nA4,4,114},{8,nE3,2,78},{10,nG4,2,96},{11,nA4,2,100},
  {12,nAS4,2,108},{13,nB4,2,104},{14,nD5,2,110},{15,nE5,4,118}, EV_END };
static const ev_t pat_l5[] = {
  {0,nE2,6,116},{0,nE5,2,108},{2,nE5,2,90},{3,nE4,2,96},{4,nE5,3,112},
  {6,nD5,2,98},{7,nE5,2,104},{8,nE3,2,84},{9,nE4,2,94},{10,nG4,2,100},
  {11,nGS4,2,104},{12,nB4,3,112},{14,nD5,2,102},{15,nB4,2,98}, EV_END };
static const ev_t pat_l6[] = {
  {0,nE2,4,116},{2,nGS3,2,100},{2,nD4,2,102},{2,nG4,2,104},{3,nE2,2,62},
  {5,nGS3,2,96},{5,nD4,2,98},{5,nG4,2,100},{7,nGS3,3,104},{7,nD4,3,106},
  {7,nG4,3,108},{8,nE2,2,100},{10,nG3,2,90},{10,nCS4,2,92},{10,nFS4,2,94},
  {11,nGS3,4,108},{11,nD4,4,110},{11,nG4,4,112},{13,nE3,2,92},{14,nD3,2,92},
  {15,nB2,3,98}, EV_END };
static const ev_t pat_l7[] = {
  {0,nE2,4,116},{0,nE3,2,92},{1,nG3,2,94},{2,nA3,2,96},{3,nB3,2,98},
  {4,nD4,2,102},{5,nE4,2,102},{6,nG4,2,104},{7,nA4,2,106},{8,nB4,2,108},
  {9,nD5,2,110},{10,nE5,2,114},{11,nG5,3,118},{12,nE5,2,110},{13,nD5,2,108},
  {14,nB4,2,106},{15,nG4,3,108}, EV_END };
/* The break: three hits, then a run down to the 1. */
static const ev_t pat_k[] = {
  {0,nE2,3,120},{0,nE3,3,112},{0,nD4,3,112},{0,nG4,3,116},
  {3,nE2,3,112},{3,nE3,3,106},{3,nD4,3,106},{3,nG4,3,110},
  {6,nE2,5,120},{6,nE3,5,112},{6,nD4,5,114},{6,nG4,5,118},
  {10,nE3,2,100},{11,nD3,2,98},{12,nB2,2,100},{13,nAS2,2,98},{14,nA2,2,100},
  {15,nG2,3,104}, EV_END };

enum { PA, PB, PC, PD, PL1, PL2, PL3, PL4, PL5, PL6, PL7, PK };
static const ev_t *const pats[] = {
  pat_a, pat_b, pat_c, pat_d, pat_l1, pat_l2, pat_l3, pat_l4, pat_l5, pat_l6, pat_l7, pat_k };

enum { DR_NORMAL, DR_FILL, DR_BREAK };
/* One bit for each 16th of the bar. The three values: normal bar, fill, break. */
static const uint16_t drum_kick[3]  = { (1<<0)|(1<<7)|(1<<10), (1<<0)|(1<<7)|(1<<10), (1<<0)|(1<<3)|(1<<6)|(1<<10) };
static const uint16_t drum_snare[3] = { (1<<4)|(1<<12),        (1<<4)|(1<<12)|(1<<14), (1<<6)|(1<<12)|(1<<14)|(1<<15) };
static const uint16_t drum_ghost[3] = { (1<<9)|(1<<15),        (1<<9)|(1<<11)|(1<<13)|(1<<15), (1<<13) };
static const uint16_t drum_open[3]  = { (1<<14),               0,                      0 };

typedef struct { uint8_t pat; int8_t tr; uint8_t drum; } bar_t;
#define NBARS 28
static const bar_t song[NBARS] = {
  /* theme */
  {PA, 0, DR_NORMAL}, {PB, 0, DR_NORMAL}, {PA, 0, DR_NORMAL}, {PD, 0, DR_FILL},
  /* chorus 1: a 12-bar funk blues in E */
  {PL1, 0, DR_NORMAL}, {PL2, 0, DR_NORMAL}, {PL3, 0, DR_NORMAL}, {PL4, 0, DR_FILL},
  {PA, 5, DR_NORMAL}, {PB, 5, DR_NORMAL}, {PA, 0, DR_NORMAL}, {PB, 0, DR_FILL},
  {PA, 7, DR_NORMAL}, {PB, 5, DR_NORMAL}, {PC, 0, DR_NORMAL}, {PK, 0, DR_BREAK},
  /* chorus 2 */
  {PL5, 0, DR_NORMAL}, {PL6, 0, DR_NORMAL}, {PL5, 0, DR_NORMAL}, {PL7, 0, DR_FILL},
  {PL1, 5, DR_NORMAL}, {PL2, 5, DR_NORMAL}, {PA, 0, DR_NORMAL}, {PL3, 0, DR_FILL},
  {PA, 7, DR_NORMAL}, {PB, 5, DR_NORMAL}, {PC, 0, DR_NORMAL}, {PK, 0, DR_BREAK}
};

#define SWING 0.08f

static int seq_run, seq_bar, seq_step;
static int32_t seq_cnt;
static const ev_t *seq_ev;

static void seq_fire(void)
{
  const bar_t *bar = &song[seq_bar];
  int step = seq_step;
  int32_t base = p_step_len;
  int d = bar->drum;
  uint16_t bit = (uint16_t)(1u << step);

  while (seq_ev->step == step) {
    float vel = (float)seq_ev->vel * (1.0f / 127.0f) * (1.0f + 0.04f * frand());
    int32_t len = (base * (int32_t)seq_ev->len) / 4;
    if (vel > 1.0f) vel = 1.0f;
    voice_start((int)seq_ev->note + bar->tr, vel, len, SRC_SEQ);
    seq_ev++;
  }

  if (p_drums) {
    if (drum_kick[d] & bit) { k_ph = 0.0f; k_fenv = 1.0f; k_aenv = 0.50f; }
    if (drum_snare[d] & bit) {
      sn_env = 0.44f + 0.04f * frand(); sn_tenv = 0.6f; sn_ph = 0.0f; sn_dec = 0.99966f;
    } else if (drum_ghost[d] & bit) {
      sn_env = 0.17f + 0.03f * frand(); sn_tenv = 0.12f; sn_ph = 0.0f; sn_dec = 0.9993f;
    }
    if (drum_open[d] & bit) { h_env = 0.070f; h_dec = 0.99975f; }
    else {
      h_env = (step & 3) == 0 ? 0.085f : ((step & 1) == 0 ? 0.062f : 0.034f);
      h_env *= 1.0f + 0.15f * frand();
      h_dec = 0.9982f;
    }
  }

  s_pos = seq_bar * 16 + step;

  /* A little swing: the 16ths on the beat are longer. */
  seq_cnt += (int32_t)((float)base * ((step & 1) ? 1.0f - SWING : 1.0f + SWING));

  seq_step++;
  if (seq_step >= 16) {
    seq_step = 0;
    seq_bar++;
    if (seq_bar >= NBARS) seq_bar = 0;
    seq_ev = pats[song[seq_bar].pat];
  }
}

static void seq_stop_notes(void)
{
  int i;
  for (i = 0; i < NVOICE; i++)
    if (voices[i].src == SRC_SEQ) voice_release(&voices[i]);
}

/* ------------------------------------------------------------------ */
/* audio()                                                            */
/* ------------------------------------------------------------------ */

#define RLEN  2048u
#define RMASK (RLEN - 1u)
static float room_l[RLEN], room_r[RLEN];
static uint32_t room_w;
static float room_lp_l, room_lp_r;

static float mix_cl[BLK], mix_dr[BLK], mix_ht[BLK];
static float hp_z, tn_1, tn_2, wah_1, wah_2, wah_a1 = 1.0f, wah_a2, wah_a3;
static float env_f, wah_mix, master_s, body_z;

#define VOICE_GAIN 0.085f
#define HP_C       0.010f   /* high-pass at 70 Hz */
#define BODY_LEAK  0.0142f  /* the corner of the integrator: 100 Hz */
#define BODY_EDGE  0.6f     /* how much of the strike click stays */
#define TONE_K     0.75f   /* 1 / Q of the tone filter */
#define WAH_K      0.28f

static float soft_clip(float x)   /* +-1.5 in gives +-1 out */
{
  if (x > 1.5f) x = 1.5f;
  if (x < -1.5f) x = -1.5f;
  return x - 0.148148f * x * x * x;
}

static void audio_block(int32_t *out, int n)
{
  int i, pos;
  float tg, ta1, ta2, ta3, sens, pre, post, wtarget, mtarget;

  /* Events from loop(). */
  while (q_tail != q_head) {
    uint16_t e = q_buf[q_tail % Q_LEN];
    uint16_t cmd = (uint16_t)(e & 0xFF00u);
    int note = e & 0x7F;
    q_tail++;
    if (cmd == Q_NOTE_ON) {
      voice_start(note, 0.84f + 0.05f * frand(), 0, SRC_KEY);
    } else if (cmd == Q_NOTE_OFF) {
      for (i = 0; i < NVOICE; i++)
        if (voices[i].state == V_HELD && voices[i].src == SRC_KEY && voices[i].note == note)
          voice_release(&voices[i]);
    } else if (cmd == Q_START) {
      seq_stop_notes();
      seq_bar = 0; seq_step = 0; seq_cnt = 0;
      seq_ev = pats[song[0].pat];
      seq_run = 1;
    } else if (cmd == Q_STOP) {
      seq_run = 0;
      seq_stop_notes();
      s_pos = -1;
    }
  }

  for (i = 0; i < n; i++) { mix_cl[i] = 0.0f; mix_dr[i] = 0.0f; mix_ht[i] = 0.0f; }

  /* The strings and the drums, in parts that end where a 16th starts. */
  pos = 0;
  while (pos < n) {
    int m = n - pos;
    if (seq_run) {
      if (seq_cnt <= 0) seq_fire();
      if (m > seq_cnt) m = seq_cnt;
      if (m < 1) m = 1;
    }
    voices_render(mix_cl + pos, m);
    drums_render(mix_dr + pos, mix_ht + pos, m);
    if (seq_run) seq_cnt -= m;
    pos += m;
  }

  /* Tone, wah, drive, room, master. */
  tg = p_tone_g;
  ta1 = 1.0f / (1.0f + tg * (tg + TONE_K));
  ta2 = tg * ta1;
  ta3 = tg * ta2;
  sens = p_wah_sens;
  pre = p_drv_pre;
  post = p_drv_post;
  wtarget = p_wah_on ? 1.0f : 0.0f;
  mtarget = p_master;

  for (i = 0; i < n; i++) {
    float x = mix_cl[i] * VOICE_GAIN;
    float v1, v2, v3, a, l, r, tl, tr, send;

    /* The pickups give pulses at the edges of the wave. A leaky integrator
     * makes the wave itself of them: a narrow pulse wave, the clavinet tone. */
    body_z += x - BODY_LEAK * body_z;
    x = body_z + BODY_EDGE * x;

    /* no deep bass, no DC */
    hp_z += HP_C * (x - hp_z);
    x = x - hp_z + 1.0e-9f;

    /* tone: a low-pass with a small peak */
    v3 = x - tn_2;
    v1 = ta1 * tn_1 + ta2 * v3;
    v2 = tn_2 + ta2 * tn_1 + ta3 * v3;
    tn_1 = 2.0f * v1 - tn_1;
    tn_2 = 2.0f * v2 - tn_2;
    x = v2;

    /* auto-wah: the level of the notes opens the filter */
    a = x < 0.0f ? -x : x;
    env_f += (a > env_f ? 0.010f : 0.0004f) * (a - env_f);
    wah_mix += 0.002f * (wtarget - wah_mix);
    if (wah_mix > 0.001f) {
      if ((i & 7) == 0) {
        float e = env_f * sens, fc, wx, wg;
        if (e > 1.0f) e = 1.0f;
        fc = 330.0f + 2900.0f * e * (0.3f + 0.7f * e);
        wx = fc * (PI_F / SR);
        wg = wx * (1.0f + 0.33333f * wx * wx);
        wah_a1 = 1.0f / (1.0f + wg * (wg + WAH_K));
        wah_a2 = wg * wah_a1;
        wah_a3 = wg * wah_a2;
      }
      v3 = x - wah_2;
      v1 = wah_a1 * wah_1 + wah_a2 * v3;
      v2 = wah_2 + wah_a2 * wah_1 + wah_a3 * v3;
      wah_1 = 2.0f * v1 - wah_1;
      wah_2 = 2.0f * v2 - wah_2;
      x += wah_mix * ((0.72f * v2 + 0.65f * v1) - x);
    }

    /* drive */
    x = soft_clip(x * pre) * post;

    /* a small stereo room for the clavinet */
    send = x;
    tl = room_l[(room_w - 1361u) & RMASK];
    tr = room_r[(room_w - 1879u) & RMASK];
    room_lp_l += 0.35f * (tl - room_lp_l);
    room_lp_r += 0.35f * (tr - room_lp_r);
    room_l[room_w] = send + 0.30f * room_lp_r;
    room_r[room_w] = send + 0.30f * room_lp_l;
    l = x + mix_dr[i] + 0.8f * mix_ht[i]
      + 0.20f * room_lp_l + 0.10f * room_l[(room_w - 613u) & RMASK];
    r = x + mix_dr[i] + 1.2f * mix_ht[i]
      + 0.20f * room_lp_r + 0.10f * room_r[(room_w - 887u) & RMASK];
    room_w = (room_w + 1u) & RMASK;

    master_s += 0.002f * (mtarget - master_s);
    l = soft_clip(0.88f * l) * master_s;
    r = soft_clip(0.88f * r) * master_s;
    out[2 * i]     = (int32_t)(l * (0.92f * (float)FM1_AUDIO_MAX));
    out[2 * i + 1] = (int32_t)(r * (0.92f * (float)FM1_AUDIO_MAX));
  }
}

void audio(int32_t *out, int frames)
{
  while (frames > 0) {
    int n = frames > BLK ? BLK : frames;
    audio_block(out, n);
    out += 2 * n;
    frames -= n;
  }
}

/* ------------------------------------------------------------------ */
/* Screen                                                             */
/* ------------------------------------------------------------------ */

#define C_BG     0x1881
#define C_WOOD   0x79E2
#define C_CREAM  0xFFDB
#define C_ORANGE 0xFD20
#define C_GREY   0x9CD3
#define C_DIM    0x632C
#define C_DARK   0x3186
#define C_WHITE  0xFFFF
#define C_GREEN  0x47E8

/* 5 x 7 font, ASCII 32 to 90, 5 columns for each character, bit 0 is the top. */
static const uint8_t font[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},
  {0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

#define TBUF_N 3840u
static uint16_t tbuf[TBUF_N];

/* Draws text with a background. sc is the size: a character is 6*sc by 8*sc. */
static void draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int sc)
{
  int len = 0, w, h, row, c, fx, k;
  uint16_t *p = tbuf;
  while (s[len]) len++;
  while (len > 0 && (x + len * 6 * sc > 240 || (uint32_t)(len * 6 * sc * 8 * sc) > TBUF_N)) len--;
  if (len <= 0 || y + 8 * sc > 240 || x < 0 || y < 0) return;
  w = len * 6 * sc;
  h = 8 * sc;
  for (row = 0; row < h; row++) {
    int fy = row / sc;
    for (c = 0; c < len; c++) {
      int ch = (unsigned char)s[c];
      const uint8_t *gl;
      if (ch < 32 || ch > 90) ch = 32;
      gl = font[ch - 32];
      for (fx = 0; fx < 6; fx++) {
        uint16_t col = (fx < 5 && fy < 7 && ((gl[fx] >> fy) & 1)) ? fg : bg;
        for (k = 0; k < sc; k++) *p++ = col;
      }
    }
  }
  fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, tbuf, (uint32_t)(w * h));
}

/* ------------------------------------------------------------------ */
/* The controls                                                       */
/* ------------------------------------------------------------------ */

#define OCT_MIN (-2)
#define OCT_MAX 1
#define TEMPO_MIN 70
#define TEMPO_MAX 140
#define NPRESET 4
#define NPICKUP 4

typedef struct { const char *name; uint8_t pickup, bright, wah, wah_on, drive, mute; } preset_t;
static const preset_t presets[NPRESET] = {
  { "SUPER",    2, 14, 12, 0,  8,  2 },
  { "WAH FUNK", 0, 13, 14, 1,  6,  5 },
  { "MUTED",    1, 15, 12, 0,  8, 12 },
  { "NASAL",    3, 16, 12, 0, 10,  3 }
};
static const char *const pickup_name[NPICKUP] = { "NECK", "BRIDGE", "BOTH", "PHASE" };
/* How much each pickup gives: a is the neck pickup, b is the bridge pickup. */
static const float pickup_a[NPICKUP] = { 1.00f, 0.00f, 0.65f, 1.10f };
static const float pickup_b[NPICKUP] = { 0.00f, 1.50f, 0.85f, -1.10f };

static int ui_preset, ui_pickup, ui_bright, ui_wah, ui_wah_on, ui_drive, ui_mute;
static int ui_tempo = 104, ui_oct = 0, ui_drums = 1, ui_playing = 0;

static uint8_t key_note[FM1_NOTE_KEYS];   /* the note that each held key plays, 0 = none */
static uint8_t led_now[FM1_NLED];
static int shown_step = -1, shown_bar = -1;

/* What must be drawn. loop() draws one item for each call. */
enum {
  DI_BG0, DI_BG1, DI_BG2, DI_BG3, DI_TITLE,
  DI_LAB0, DI_LAB1, DI_LAB2, DI_LAB3, DI_LAB4, DI_LAB5, DI_LAB6,
  DI_VAL0, DI_VAL1, DI_VAL2, DI_VAL3, DI_VAL4, DI_VAL5, DI_VAL6,
  DI_OCT, DI_DRUMS, DI_STEPS, DI_STATUS, DI_COUNT
};
static uint32_t dirty;
#define MARK(i) (dirty |= 1u << (i))

static const char *const row_label[7] = { "PRESET", "PICKUP", "BRIGHT", "WAH", "DRIVE", "MUTE", "TEMPO" };
static const char *const row_tag[7]   = { "PR", "AL", "K1", "K2", "K3", "K4", "SE" };
#define ROW_Y(r)  (40 + 21 * (r))
#define VAL_X     96
#define OCT_Y     187
#define STEP_Y    208
#define STATUS_Y  222

static void led_set(uint32_t id, int level)
{
  if (led_now[id] != (uint8_t)level) {
    led_now[id] = (uint8_t)level;
    fm1_led_level(id, level);
  }
}

static void q_push(uint16_t e)
{
  if (q_head - q_tail < Q_LEN) {
    q_buf[q_head % Q_LEN] = e;
    q_head++;
  }
}

static void apply_params(void)
{
  float br = (float)ui_bright * 0.05f;
  float dr = (float)ui_drive * 0.05f;
  float fc = 900.0f * powf(9.0f, br);
  float x = PI_F * fc / SR;
  float pre = 1.0f + 7.0f * dr * dr;
  float a = pickup_a[ui_pickup], b = pickup_b[ui_pickup];

  p_tone_g = sinf(x) / sinf(x + 0.5f * PI_F);
  p_drv_pre = pre;
  p_drv_post = powf(pre, -0.7f);
  p_wah_sens = 1.0f + 4.0f * (float)ui_wah * 0.05f;
  p_wah_on = ui_wah_on;
  p_mute = (float)ui_mute * 0.05f;
  p_pk0 = a + b; p_pk1 = -a; p_pk2 = -b;
  p_drums = ui_drums;
  p_step_len = (int)(SR * 15.0f / (float)ui_tempo);
}

static void load_preset(int n)
{
  const preset_t *p = &presets[n];
  ui_preset = n;
  ui_pickup = p->pickup; ui_bright = p->bright; ui_wah = p->wah;
  ui_wah_on = p->wah_on; ui_drive = p->drive; ui_mute = p->mute;
  apply_params();
  MARK(DI_VAL0); MARK(DI_VAL1); MARK(DI_VAL2); MARK(DI_VAL3); MARK(DI_VAL4); MARK(DI_VAL5);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Copies s to d and adds spaces up to n characters. */
static void pad(char *d, const char *s, int n)
{
  int i = 0;
  while (s[i] && i < n) { d[i] = s[i]; i++; }
  while (i < n) d[i++] = ' ';
  d[n] = 0;
}

static void draw_bar(int y, int val)   /* val: 0 to 20 */
{
  uint32_t w = (uint32_t)val * 5u;
  if (w > 0u)   fm1_lcd_fill_rect(VAL_X, (uint32_t)y + 2u, w, 12u, C_ORANGE);
  if (w < 100u) fm1_lcd_fill_rect(VAL_X + w, (uint32_t)y + 2u, 100u - w, 12u, C_DARK);
}

static void draw_step(int i, int on)
{
  uint16_t col = on ? C_ORANGE : ((i & 3) == 0 ? C_GREY : C_DARK);
  fm1_lcd_fill_rect(8u + 14u * (uint32_t)i, STEP_Y, 12u, 9u, col);
}

static void draw_item(int it)
{
  char t[24];
  if (it <= DI_BG3) {
    if (it == DI_BG0) {
      fm1_lcd_fill_rect(0, 0, 240, 32, C_WOOD);
      fm1_lcd_fill_rect(0, 32, 240, 2, C_ORANGE);
      fm1_lcd_fill_rect(0, 34, 240, 26, C_BG);
    } else {
      fm1_lcd_fill_rect(0, 60u * (uint32_t)(it - DI_BG0), 240, 60, C_BG);
    }
  } else if (it == DI_TITLE) {
    draw_text(48, 5, "CLAVINET", C_CREAM, C_WOOD, 3);
  } else if (it >= DI_LAB0 && it <= DI_LAB6) {
    int r = it - DI_LAB0;
    draw_text(2, ROW_Y(r) + 4, row_tag[r], C_DIM, C_BG, 1);
    draw_text(20, ROW_Y(r), row_label[r], C_GREY, C_BG, 2);
  } else if (it >= DI_VAL0 && it <= DI_VAL6) {
    int r = it - DI_VAL0, y = ROW_Y(r);
    switch (r) {
      case 0: pad(t, presets[ui_preset].name, 11); draw_text(VAL_X, y, t, C_WHITE, C_BG, 2); break;
      case 1: pad(t, pickup_name[ui_pickup], 11);  draw_text(VAL_X, y, t, C_WHITE, C_BG, 2); break;
      case 2: draw_bar(y, ui_bright); break;
      case 3:
        draw_bar(y, ui_wah);
        draw_text(202, y, ui_wah_on ? "ON " : "OFF", ui_wah_on ? C_GREEN : C_DIM, C_BG, 2);
        break;
      case 4: draw_bar(y, ui_drive); break;
      case 5: draw_bar(y, ui_mute); break;
      default:
        t[0] = ui_tempo >= 100 ? (char)('0' + ui_tempo / 100) : ' ';
        t[1] = (char)('0' + (ui_tempo / 10) % 10);
        t[2] = (char)('0' + ui_tempo % 10);
        t[3] = ' '; t[4] = 'B'; t[5] = 'P'; t[6] = 'M'; t[7] = 0;
        draw_text(VAL_X, y, t, C_WHITE, C_BG, 2);
        break;
    }
  } else if (it == DI_OCT) {
    t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
    t[4] = ui_oct < 0 ? '-' : '+';
    t[5] = (char)('0' + (ui_oct < 0 ? -ui_oct : ui_oct));
    t[6] = 0;
    draw_text(20, OCT_Y, t, C_CREAM, C_BG, 2);
  } else if (it == DI_DRUMS) {
    draw_text(120, OCT_Y, "DRUMS", C_GREY, C_BG, 2);
    draw_text(192, OCT_Y, ui_drums ? "ON " : "OFF", ui_drums ? C_GREEN : C_DIM, C_BG, 2);
  } else if (it == DI_STEPS) {
    int i;
    for (i = 0; i < 16; i++) draw_step(i, i == shown_step);
  } else if (it == DI_STATUS) {
    if (ui_playing) {
      draw_text(6, STATUS_Y, "SOLO BAR   /28     ", C_ORANGE, C_BG, 2);
      shown_bar = -1;
    } else {
      draw_text(6, STATUS_Y, "PRESS PLAY FOR SOLO", C_CREAM, C_BG, 2);
    }
  }
}

static void update_leds(uint32_t notes)
{
  uint32_t lit = notes;
  int32_t pos = s_pos;
  int base = 53 + 12 * ui_oct;   /* note key 0 is F3 */
  int i;

  /* PLAY: on = stopped, press to start. It blinks with the beat while the solo plays. */
  if (!ui_playing) led_set(FM1_BTN_PLAY, FM1_LED_ON);
  else led_set(FM1_BTN_PLAY, (pos < 0 || (pos & 3) < 2) ? FM1_LED_ON : FM1_LED_OFF);

  /* OCT: on while there is an octave to go to. */
  led_set(FM1_BTN_OCT_DOWN, ui_oct > OCT_MIN ? FM1_LED_ON : FM1_LED_OFF);
  led_set(FM1_BTN_OCT_UP,   ui_oct < OCT_MAX ? FM1_LED_ON : FM1_LED_OFF);

  /* FX (wah) and SEQ (drums): on = can be switched on, breathing = it is on. */
  led_set(FM1_BTN_FX,  ui_wah_on ? FM1_LED_BREATH : FM1_LED_ON);
  led_set(FM1_BTN_SEQ, ui_drums  ? FM1_LED_BREATH : FM1_LED_ON);

  /* Note keys: lit while their note sounds (your keys and the notes of the solo). */
  for (i = 0; i < NVOICE; i++) {
    if (voices[i].state == V_HELD && voices[i].src == SRC_SEQ) {
      int k = (int)voices[i].note - base;
      if (k >= 0 && k < (int)FM1_NOTE_KEYS) lit |= 1u << k;
    }
  }
  for (i = 0; i < (int)FM1_NOTE_KEYS; i++)
    led_set(FM1_KEY_NOTE0 + (uint32_t)i, (lit >> i) & 1u ? FM1_LED_ON : FM1_LED_OFF);
}

/* ------------------------------------------------------------------ */
/* setup() and loop()                                                 */
/* ------------------------------------------------------------------ */

void setup(void)
{
  int i;
  for (i = 0; i < 128; i++) {
    float f = 440.0f * powf(2.0f, (float)(i - 69) * (1.0f / 12.0f));
    float per = SR / f;
    float a = powf(per * (1.0f / 128.0f), 0.3f);
    if (a < 0.75f) a = 0.75f;
    if (a > 1.6f) a = 1.6f;
    note_per[i] = per;
    note_amp[i] = a;
    note_t60[i] = 6.0f * powf(2.0f, -(float)(i - 40) * (1.0f / 36.0f));
  }
  seq_ev = pats[song[0].pat];
  load_preset(0);
  p_master = 0.0f;
  dirty = (1u << DI_COUNT) - 1u;   /* draw all */
}

void loop(void)
{
  uint32_t pressed = fm1_input_edges(0);
  uint32_t nedge = fm1_input_note_edges();
  uint32_t notes = fm1_in.notes;
  int32_t d, pos;
  int i, v;
  float m;

  /* ---- note keys ---- */
  for (i = 0; i < (int)FM1_NOTE_KEYS; i++) {
    uint32_t bit = 1u << i;
    if ((nedge & bit) || ((notes & bit) && !key_note[i])) {
      int note = 53 + 12 * ui_oct + i;
      if (key_note[i]) q_push((uint16_t)(Q_NOTE_OFF | key_note[i]));
      key_note[i] = (uint8_t)note;
      q_push((uint16_t)(Q_NOTE_ON | (uint16_t)note));
    }
    if (key_note[i] && !(notes & bit)) {
      q_push((uint16_t)(Q_NOTE_OFF | key_note[i]));
      key_note[i] = 0;
    }
  }

  /* ---- buttons ---- */
  if (pressed & (1u << FM1_BTN_PLAY)) {
    ui_playing = !ui_playing;
    q_push(ui_playing ? Q_START : Q_STOP);
    if (ui_playing) s_pos = 0;
    MARK(DI_STATUS);
  }
  if ((pressed & (1u << FM1_BTN_OCT_DOWN)) && ui_oct > OCT_MIN) { ui_oct--; MARK(DI_OCT); }
  if ((pressed & (1u << FM1_BTN_OCT_UP))   && ui_oct < OCT_MAX) { ui_oct++; MARK(DI_OCT); }
  if (pressed & (1u << FM1_BTN_FX))  { ui_wah_on = !ui_wah_on; apply_params(); MARK(DI_VAL3); }
  if (pressed & (1u << FM1_BTN_SEQ)) { ui_drums = !ui_drums;   apply_params(); MARK(DI_DRUMS); }

  /* ---- encoders ---- */
  d = fm1_enc_take(FM1_ENC_PRESETS);
  if (d) { v = clampi(ui_preset + (int)d, 0, NPRESET - 1); if (v != ui_preset) load_preset(v); }
  d = fm1_enc_take(FM1_ENC_ALGORITHM);
  if (d) { v = clampi(ui_pickup + (int)d, 0, NPICKUP - 1); if (v != ui_pickup) { ui_pickup = v; apply_params(); MARK(DI_VAL1); } }
  d = fm1_enc_take(FM1_ENC_KNOB1);
  if (d) { v = clampi(ui_bright + (int)d, 0, 20); if (v != ui_bright) { ui_bright = v; apply_params(); MARK(DI_VAL2); } }
  d = fm1_enc_take(FM1_ENC_KNOB2);
  if (d) { v = clampi(ui_wah + (int)d, 0, 20); if (v != ui_wah) { ui_wah = v; apply_params(); MARK(DI_VAL3); } }
  d = fm1_enc_take(FM1_ENC_KNOB3);
  if (d) { v = clampi(ui_drive + (int)d, 0, 20); if (v != ui_drive) { ui_drive = v; apply_params(); MARK(DI_VAL4); } }
  d = fm1_enc_take(FM1_ENC_KNOB4);
  if (d) { v = clampi(ui_mute + (int)d, 0, 20); if (v != ui_mute) { ui_mute = v; apply_params(); MARK(DI_VAL5); } }
  d = fm1_enc_take(FM1_ENC_SELECT);
  if (d) { v = clampi(ui_tempo + (int)d, TEMPO_MIN, TEMPO_MAX); if (v != ui_tempo) { ui_tempo = v; apply_params(); MARK(DI_VAL6); } }

  /* ---- MASTER ---- */
  m = 1.0f; /* MASTER is the FM-1's own volume (on the speaker): the full level here */
  p_master = m * m;

  /* ---- LEDs ---- */
  update_leds(notes);

  /* ---- screen: one item for each call ---- */
  if (dirty) {
    for (i = 0; i < DI_COUNT; i++) {
      if (dirty & (1u << i)) { dirty &= ~(1u << i); draw_item(i); break; }
    }
    return;
  }
  pos = ui_playing ? s_pos : -1;
  v = pos < 0 ? -1 : (int)(pos & 15);
  if (v != shown_step) {
    if (shown_step >= 0) draw_step(shown_step, 0);
    if (v >= 0) draw_step(v, 1);
    shown_step = v;
    return;
  }
  if (pos >= 0 && (int)(pos >> 4) != shown_bar) {
    char t[3];
    shown_bar = (int)(pos >> 4);
    t[0] = (char)('0' + (shown_bar + 1) / 10);
    t[1] = (char)('0' + (shown_bar + 1) % 10);
    t[2] = 0;
    draw_text(6 + 12 * 9, STATUS_Y, t, C_WHITE, C_BG, 2);
  }
}
