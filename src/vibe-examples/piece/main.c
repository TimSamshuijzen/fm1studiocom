/* A MINOR ODYSSEY - a 90s style demoscene intro for the M-VAVE FM-1.
 *
 * Trance at 128 BPM in A minor, 96 bars (3:00), then it starts again:
 * INTRO 16 bars, BUILD 16, DROP 16, BREAKDOWN 16, DROP II 16, OUTRO 16.
 *
 * PLAY      start / stop (it goes on from where it stopped)
 * FX        mute drums        SCL   mute bass       ENV  mute chords and pad
 * LFO       mute arpeggio     EDIT  mute lead       GLO  mute effects
 * OCT UP    jump to the next part
 * OCT DOWN  jump to the start of this part (in its first bar: the part before)
 * MASTER    volume
 */
#include "fm1.h"
#include <math.h>

#define SR        44100.0f
#define TWO_PI    6.2831853f
#define PHK       (4294967296.0f / SR)   /* Hz to phase increment */
#define STEP_LEN  5168                   /* samples in a 16th note at 128 BPM */
#define GATE_OFF  3600                   /* where a bass note ends in its step */
#define BARS      96
#define STEPS     (BARS * 16)
#define BEAT_LEN  (STEP_LEN * 4)
#define DLY_LEN   (STEP_LEN * 3)         /* dotted 8th */
#define DLY_TAP2  (STEP_LEN * 2)         /* 8th */

/* ---------- shared by audio() and loop() ---------- */
static volatile int      g_playing;
static volatile uint32_t g_jump;     /* step + 1 to jump to, 0 = none */
static volatile uint32_t g_mute;     /* bit 0 drums, 1 bass, 2 chords, 3 arp, 4 lead, 5 fx */
static volatile float    g_vol = 0.5f;
static volatile uint32_t g_clock;    /* samples since the start */
static volatile uint32_t g_step;     /* the 16th note that plays now */
static volatile uint32_t g_kicks;    /* kicks heard so far */
static volatile uint32_t g_trig[6];  /* clock of the last note of each part */

/* ---------- the music ---------- */
static const char *const part_name[6] = {
  "INTRO", "BUILD", "DROP", "BREAKDOWN", "DROP II", "OUTRO"
};
static const float part_col[6][3] = {
  {0.3f, 0.6f, 1.0f}, {0.3f, 1.0f, 0.6f}, {1.0f, 0.3f, 0.8f},
  {0.65f, 0.45f, 1.0f}, {1.0f, 0.8f, 0.2f}, {0.3f, 0.9f, 1.0f}
};
static const uint8_t part_shape[6] = { 0, 1, 2, 1, 2, 0 };

/* one chord per bar: Am F C G */
static const uint8_t chord[4][3] = { {57, 60, 64}, {57, 60, 65}, {55, 60, 64}, {55, 59, 62} };
static const uint8_t bass_root[4] = { 33, 29, 36, 31 };
static const int8_t  bass_off[16] = { -1, 0, 12, 0, -1, 0, 12, 12, -1, 0, 12, 0, -1, 7, 12, 7 };
#define BASS_ACC   0x4444u   /* accents on steps 2, 6, 10, 14 */
#define BASS_SLIDE 0x8080u   /* steps 7 and 15 glide from the note before */
static const uint8_t arp_pat[16] = { 0, 1, 2, 3, 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 4 };

/* the lead melody: 8 bars of (note, length in 16ths) */
static const uint8_t mel_src[][2] = {
  {69,4},{72,2},{76,4},{74,2},{72,2},{71,2},
  {69,6},{72,2},{69,4},{67,4},
  {67,4},{72,2},{76,4},{79,2},{76,2},{74,2},
  {74,8},{71,4},{67,4},
  {69,4},{72,2},{76,4},{81,2},{79,2},{76,2},
  {77,6},{76,2},{72,4},{69,4},
  {76,4},{74,2},{72,4},{74,2},{76,2},{79,2},
  {71,4},{74,4},{76,8}
};
/* the last two bars of DROP II: the melody climbs to the high A */
static const uint8_t mel_end_src[][2] = {
  {76,4},{79,2},{81,4},{79,2},{76,2},{79,2},
  {74,2},{76,2},{79,2},{83,2},{81,8}
};
static uint8_t mel[128];     /* the note that starts on each 16th, 0 = hold */
static uint8_t mel_end[32];

/* ---------- 5 x 7 font, characters 32 to 90 ---------- */
static const uint8_t font[59][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
  {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, /* # */
  {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
  {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* & */
  {0x04,0x04,0x08,0x00,0x00,0x00,0x00}, /* ' */
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
  {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, /* , */
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
  {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, /* / */
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
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* : */
  {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, /* ; */
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
  {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
  {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, /* @ */
  {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}, /* A */
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
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}  /* Z */
};

static float sintab[512];

/* ---------- synth state (audio only) ---------- */
static int      step, pos, was_playing;
static uint32_t clk;
static uint32_t rng = 22222u;

typedef struct { float lo, bp; } svf_t;   /* state-variable filter */

/* drums and effects */
static float k_amp, k_fenv, k_ph, side;
static float oh_amp, ch_amp, hp_prev, cl_amp, cl_lvl, cr_amp;
static svf_t cl_f, ri_f;
static int   riser_on;
static float ri_amp, ri_fc = 0.05f;

/* acid bass */
static uint32_t b_ph;
static float b_f = 55.0f, b_tf = 55.0f, b_amp, b_vel, b_fenv;
static float b_cut = 300.0f, b_envamt, b_envbase = 1000.0f, b_q = 0.25f;
static int   b_gate, b_slide;
static svf_t b_svf;

/* supersaw chords: 3 notes x 4 saws, two saws on each side */
static uint32_t c_ph[12], c_inc[12];
static float c_amp, c_lvl, c_base = 500.0f;
static int   c_pad, c_pad_on;
static svf_t c_fl, c_fr;

/* the soft pad under DROP II: 3 notes x 2 saws */
static uint32_t p_ph[6], p_inc[6];
static float p_amp;
static int   p_on;
static svf_t p_fl, p_fr;

/* arpeggio */
static uint32_t a_ph, a_inc;
static float a_amp, a_lp, a_coef = 0.1f, a_panl = 0.7f, a_panr = 0.7f;

/* lead, with an octave and a harmony a third above in DROP II */
static uint32_t l_ph[3], h_ph[2];
static float l_f = 440.0f, l_tf = 440.0f, l_amp, l_lvl = 1.0f, vib_ph;
static float h_f = 523.0f, h_tf = 523.0f, h_lp[2];
static int   l_gate, l_attack, l_oct, h_on;
static svf_t l_svf;

/* stereo delay */
static int16_t dly[DLY_LEN];
static int     d_w;

static float mgain[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static float vol_s;

static inline float noise(void)
{
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static inline float saw(uint32_t ph)
{
  return (float)(int32_t)ph * (1.0f / 2147483648.0f);
}

static float mtof(int n)
{
  return 440.0f * powf(2.0f, (float)(n - 69) * (1.0f / 12.0f));
}

/* the note a third above in A minor */
static int third_up(int n)
{
  static const uint8_t up[12] = { 3, 3, 3, 4, 3, 3, 3, 3, 4, 3, 4, 3 };
  return n + up[(n + 3) % 12];
}

/* a cutoff in Hz to a filter coefficient, always in the safe range */
static inline float fco(float hz)
{
  if (!(hz >= 20.0f)) hz = 20.0f;          /* also catches NaN */
  else if (hz > 7000.0f) hz = 7000.0f;
  return hz * (TWO_PI / SR);
}

/* Chamberlin state-variable filter; stable for f <= 1 and 0 < q < 2 */
static inline void svf(svf_t *s, float in, float f, float q)
{
  float hp;
  s->lo += f * s->bp;
  hp = in - s->lo - q * s->bp;
  s->bp += f * hp;
}

/* resets a filter that ran away (or holds NaN) */
static void svf_check(svf_t *s)
{
  if (!(s->lo > -100.0f && s->lo < 100.0f && s->bp > -100.0f && s->bp < 100.0f)) {
    s->lo = 0.0f;
    s->bp = 0.0f;
  }
}

static inline float fsafe(float v)
{
  return (v > -100.0f && v < 100.0f) ? v : 0.0f;
}

static inline float softclip(float x)
{
  if (x > 1.5f) x = 1.5f;
  else if (x < -1.5f) x = -1.5f;
  else if (!(x >= -1.5f)) x = 0.0f;         /* NaN */
  return x - 0.148148f * x * x * x;
}

static inline void flush(float *v)
{
  if (!(*v >= 1e-6f)) *v = 0.0f;            /* tiny values and NaN */
}

/* ---------- sequencer ---------- */
static void set_chord(int ch)
{
  /* left: -14 and +7 cents, right: +14 and -7 cents */
  static const float det[4] = { 0.99195f, 1.00405f, 1.00812f, 0.99596f };
  int i, j;
  for (i = 0; i < 3; i++) {
    float f = mtof(chord[ch][i]) * PHK * (1.0f + 0.0007f * (float)i);
    for (j = 0; j < 4; j++) c_inc[i * 4 + j] = (uint32_t)(f * det[j]);
  }
}

static void set_pad(int ch)
{
  int i;
  for (i = 0; i < 3; i++) {
    float f = mtof(chord[ch][i] - 12) * PHK;
    p_inc[i * 2] = (uint32_t)(f * 0.996f);
    p_inc[i * 2 + 1] = (uint32_t)(f * 1.004f);
  }
}

/* called at the start of each 16th note */
static void step_event(void)
{
  int bar = step >> 4, s = step & 15, part = bar >> 4, pb = bar & 15, ch = bar & 3;
  float frac = (float)(pb * 16 + s) * (1.0f / 256.0f);   /* place in the part, 0..1 */
  float cyc = 0.5f - 0.5f * cosf(TWO_PI * (float)((bar & 7) * 16 + s) * (1.0f / 128.0f));
  int kick = 0, ohat = 0, chat = 0, clap = 0, roll = 0, bass = 0;
  int stab = 0, pad = 0, arp = 0, lead = 0, crash = 0;
  float arp_cut = 4000.0f, arp_lvl = 1.0f, stab_lvl = 1.0f;
  float pad_from = 200.0f, pad_oct = 5.0f, pad_lvl = 1.0f;   /* pad filter sweep */
  uint32_t stab_mask = 0x4444u;   /* the offbeats */

  riser_on = 0;
  p_on = 0;
  c_base = 500.0f;
  switch (part) {
  case 0:   /* INTRO: pad and arpeggio at once, kick from bar 5, a hint of the lead */
    kick = pb >= 4; ohat = pb >= 8; arp = 1; pad = 1;
    arp_cut = 400.0f + 3400.0f * frac; arp_lvl = 0.8f;
    pad_from = 400.0f; pad_oct = 2.6f; pad_lvl = 0.85f;
    lead = (pb >= 8 && pb < 10) || (pb >= 12 && pb < 14);
    break;
  case 1:   /* BUILD */
    kick = pb < 15; ohat = 1; clap = pb >= 4 && pb < 12; roll = pb >= 12;
    bass = 1; b_cut = 150.0f + 600.0f * frac; b_envbase = 400.0f + 1200.0f * frac;
    stab = pb >= 8; stab_lvl = 0.55f; c_base = 400.0f + 1100.0f * frac;
    arp = 1; arp_cut = 3800.0f; riser_on = pb >= 8;
    break;
  case 2:   /* DROP */
    kick = ohat = clap = bass = stab = arp = lead = 1;
    crash = pb == 0 && s == 0;
    b_cut = 250.0f + 900.0f * cyc; b_envbase = 1500.0f;
    arp_cut = 5000.0f; c_base = 1600.0f;
    break;
  case 3:   /* BREAKDOWN */
    pad = pb < 15 || s < 8; arp = pb >= 4; arp_cut = 1800.0f; arp_lvl = 0.6f;
    lead = pb >= 8; roll = pb >= 12; riser_on = pb >= 8;
    break;
  case 4:   /* DROP II: everything, the filters open, a pad under it */
    kick = ohat = chat = clap = bass = stab = arp = lead = 1;
    crash = (pb == 0 || pb == 8) && s == 0;
    b_cut = 400.0f + 1500.0f * cyc; b_envbase = 2400.0f;
    arp_cut = 6500.0f; c_base = 6000.0f; stab_lvl = 1.1f; stab_mask = 0x4949u;
    p_on = 1;
    if (s == 0) set_pad(ch);
    break;
  default:  /* OUTRO: pad and arpeggio to the end, the kick stops after bar 12 */
    kick = pb < 12; ohat = pb < 12; clap = pb < 8; bass = pb < 8; stab = pb < 4;
    pad = pb >= 4; arp = 1;
    b_cut = 700.0f - 30.0f * (float)pb; b_envbase = 1000.0f;
    arp_cut = 4500.0f - 250.0f * (float)pb; arp_lvl = 1.0f - 0.015f * (float)pb;
    c_base = 1200.0f; stab_lvl = 0.8f;
    pad_from = 3000.0f; pad_oct = -3.0f; pad_lvl = 0.85f;
    break;
  }

  /* drums */
  if (kick && (s & 3) == 0) {
    k_amp = 1.0f; k_fenv = 1.0f; k_ph = 0.0f; side = 1.0f;
    if (!(g_mute & 1u)) g_kicks++;
    g_trig[0] = clk;
  }
  if (ohat && (s & 3) == 2) oh_amp = 1.0f;
  if (chat && (s & 3) != 2) ch_amp = (s & 1) ? 0.6f : 1.0f;
  if (clap && (s == 4 || s == 12)) { cl_amp = 1.0f; cl_lvl = 1.0f; g_trig[0] = clk; }
  if (roll) {
    int r = pb - 12, every = r == 0 ? 4 : r == 1 ? 2 : 1;
    if (s % every == 0) {
      cl_amp = 1.0f;
      cl_lvl = 0.3f + 0.7f * (float)(r * 16 + s) * (1.0f / 64.0f);
      g_trig[0] = clk;
    }
  }
  if (crash) { cr_amp = 1.0f; g_trig[5] = clk; }

  /* acid bass */
  if (bass && bass_off[s] >= 0) {
    int sl = (BASS_SLIDE >> s) & 1u, acc = (BASS_ACC >> s) & 1u;
    b_tf = mtof(bass_root[ch] + bass_off[s]);
    b_slide = sl && b_gate;
    if (!b_slide) { b_f = b_tf; b_fenv = 1.0f; }
    b_envamt = b_envbase * (acc ? 1.6f : 1.0f);
    b_q = acc ? 0.12f : 0.25f;
    b_vel = acc ? 1.0f : 0.7f;
    b_gate = 1;
    g_trig[1] = clk;
  } else {
    b_gate = 0;
  }

  /* chords: a pad in the breakdown, stabs elsewhere */
  if (pad) {
    if (!c_pad || s == 0) set_chord(ch);
    if (!c_pad) c_amp = 0.0f;
    c_pad = 1; c_pad_on = 1; c_lvl = pad_lvl;
    c_base = pad_from * powf(2.0f, pad_oct * frac);
    if (s == 0) g_trig[2] = clk;
  } else {
    c_pad_on = 0;
    if (stab && ((stab_mask >> s) & 1u)) {
      set_chord(ch);
      c_pad = 0; c_amp = 1.0f; c_lvl = stab_lvl;
      g_trig[2] = clk;
    }
  }

  /* arpeggio */
  if (arp) {
    int k = arp_pat[s];
    int n = chord[ch][k % 3] + 12 * (k / 3) + 12;
    a_inc = (uint32_t)(mtof(n) * PHK);
    a_amp = arp_lvl;
    a_coef = fco(arp_cut);
    if (s & 1) { a_panl = 0.45f; a_panr = 0.85f; } else { a_panl = 0.85f; a_panr = 0.45f; }
    g_trig[3] = clk;
  }

  /* lead */
  if (lead) {
    /* the INTRO plays only the first two bars of the melody */
    int n = (part == 4 && pb >= 14) ? mel_end[(pb - 14) * 16 + s]
          : mel[(part == 0 ? (pb & 1) : (pb & 7)) * 16 + s];
    if (n) {
      l_tf = mtof(n);
      h_tf = mtof(third_up(n));
      if (!l_gate) { l_f = l_tf; h_f = h_tf; }
      l_gate = 1; l_attack = 1;
      g_trig[4] = clk;
    }
    l_oct = part == 4;
    h_on = part == 4;
    l_lvl = part == 0 ? 0.5f : (part == 3 ? 0.75f : 1.0f);
  } else {
    l_gate = 0;
  }

  if ((s & 3) == 0 && (riser_on || arp || lead)) g_trig[5] = clk;
  g_step = (uint32_t)step;
}

/* a bass note ends before its step does, unless the next note glides */
static void gate_off(void)
{
  int s = step & 15;
  if (!((BASS_SLIDE >> ((s + 1) & 15)) & 1u)) b_gate = 0;
}

static void release_all(void)
{
  b_gate = 0; l_gate = 0; c_pad_on = 0; p_on = 0; riser_on = 0;
}

/* ---------- audio ---------- */
void audio(int32_t *out, int frames)
{
  int i, k, playing = g_playing;
  uint32_t mute = g_mute, j = g_jump;
  float tg[6], vt = g_vol, ri_t = 0.0f, vib;

  if (j) { g_jump = 0; step = (int)(j - 1u) % STEPS; pos = 0; g_step = (uint32_t)step; }
  if (playing != was_playing) { was_playing = playing; if (!playing) release_all(); }
  for (k = 0; k < 6; k++) tg[k] = ((mute >> k) & 1u) ? 0.0f : 1.0f;
  if (riser_on) {
    float r = ((float)((((step >> 4) & 15) - 8) * 16 + (step & 15)) +
               (float)pos * (1.0f / STEP_LEN)) * (1.0f / 128.0f);
    if (!(r >= 0.0f)) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    ri_fc = fco(300.0f * powf(2.0f, 5.0f * r));
    ri_t = r * r;
  }
  vib_ph += 5.5f * (float)frames * (1.0f / SR);
  if (!(vib_ph < 1.0f)) vib_ph = 0.0f;
  vib = 1.0f + 0.0035f * sintab[(int)(vib_ph * 512.0f) & 511];

  for (i = 0; i < frames; i++) {
    float L, R, n, hp, x, drums, bass, chL = 0.0f, chR = 0.0f, arp, lead = 0.0f;
    float hl = 0.0f, hr = 0.0f, fxL = 0.0f, fxR = 0.0f, pump, spump, d1, d2;
    int t;

    if (playing) {
      if (pos == 0) step_event();
      else if (pos == GATE_OFF) gate_off();
    }
    for (k = 0; k < 6; k++) mgain[k] += (tg[k] - mgain[k]) * 0.002f;

    /* kick: a sine that falls in pitch */
    {
      float kf = 45.0f + 150.0f * k_fenv;
      k_fenv *= 0.99909f;
      k_ph += kf * (1.0f / SR);
      if (k_ph >= 1.0f) k_ph -= 1.0f;
      x = sintab[(int)(k_ph * 512.0f) & 511] * k_amp * 0.5f;
      k_amp *= 0.99983f;
    }
    /* hats and clap from noise */
    n = noise();
    hp = n - hp_prev; hp_prev = n;
    svf(&cl_f, n, 0.2f, 0.7f);
    drums = x + hp * (oh_amp * 0.07f + ch_amp * 0.045f) + cl_f.bp * cl_amp * cl_lvl * 0.35f;
    oh_amp *= 0.99962f; ch_amp *= 0.9985f; cl_amp *= 0.99972f;
    drums *= mgain[0];

    /* the kick ducks the rest, the stabs most */
    pump = 1.0f - 0.6f * side;
    spump = 1.0f - 0.8f * side;
    side *= 0.99975f;

    /* acid bass: saw into a resonant low-pass, then a soft drive */
    if (b_slide) b_f += (b_tf - b_f) * 0.003f;
    b_ph += (uint32_t)(b_f * PHK);
    b_amp += ((b_gate ? b_vel : 0.0f) - b_amp) * (b_gate ? 0.01f : 0.004f);
    svf(&b_svf, saw(b_ph) * 0.5f, fco(b_cut + b_envamt * b_fenv), b_q);
    x = b_svf.lo * 2.5f;
    bass = x / (1.0f + (x < 0.0f ? -x : x)) * b_amp * 0.42f * pump * mgain[1];
    b_fenv *= 0.99982f;

    /* supersaw chords, wide: other saws on the left and on the right */
    if (c_pad_on || c_amp > 0.0001f) {
      float sl = 0.0f, sr = 0.0f, fc, g;
      for (k = 0; k < 12; k += 4) {
        c_ph[k] += c_inc[k];
        c_ph[k + 1] += c_inc[k + 1];
        c_ph[k + 2] += c_inc[k + 2];
        c_ph[k + 3] += c_inc[k + 3];
        sl += saw(c_ph[k]) + saw(c_ph[k + 1]);
        sr += saw(c_ph[k + 2]) + saw(c_ph[k + 3]);
      }
      if (c_pad) {
        c_amp += ((c_pad_on ? 1.0f : 0.0f) - c_amp) * (c_pad_on ? 0.00006f : 0.00012f);
        fc = fco(c_base);
        g = c_amp * c_lvl * 0.075f;
      } else {
        c_amp *= 0.999924f;
        fc = fco(c_base + 4000.0f * c_amp);
        g = c_amp * c_lvl * 0.2f * spump;
      }
      svf(&c_fl, sl, fc, 0.5f);
      svf(&c_fr, sr, fc, 0.5f);
      g *= mgain[2];
      chL = c_fl.lo * g;
      chR = c_fr.lo * g;
    }

    /* the soft pad under DROP II */
    if (p_on || p_amp > 0.0001f) {
      float g;
      for (k = 0; k < 6; k++) p_ph[k] += p_inc[k];
      p_amp += ((p_on ? 1.0f : 0.0f) - p_amp) * 0.00008f;
      svf(&p_fl, saw(p_ph[0]) + saw(p_ph[2]) + saw(p_ph[4]), 0.171f, 0.8f);
      svf(&p_fr, saw(p_ph[1]) + saw(p_ph[3]) + saw(p_ph[5]), 0.171f, 0.8f);
      g = p_amp * 0.07f * pump * mgain[2];
      chL += p_fl.lo * g;
      chR += p_fr.lo * g;
    }

    /* arpeggio */
    a_ph += a_inc;
    x = (a_ph < 0x80000000u ? 0.5f : -0.5f) + 0.5f * saw(a_ph);
    a_lp += a_coef * (x - a_lp);
    arp = a_lp * a_amp * 0.2f * pump * mgain[3];
    a_amp *= 0.99968f;

    /* lead: two detuned saws; in DROP II an octave and a harmony on top */
    if (l_gate || l_amp > 0.0001f) {
      float inc, s3;
      l_f += (l_tf - l_f) * 0.004f;
      inc = l_f * PHK * vib;
      l_ph[0] += (uint32_t)inc;
      l_ph[1] += (uint32_t)(inc * 1.0079f);
      s3 = saw(l_ph[0]) + saw(l_ph[1]);
      if (l_oct) { l_ph[2] += (uint32_t)(inc * 2.004f); s3 += 0.7f * saw(l_ph[2]); }
      if (l_attack) {
        l_amp += (1.05f - l_amp) * 0.015f;
        if (l_amp >= 1.0f) l_attack = 0;
      } else {
        l_amp += ((l_gate ? 0.7f : 0.0f) - l_amp) * (l_gate ? 0.0002f : 0.0008f);
      }
      svf(&l_svf, s3, 0.42f, 0.5f);
      lead = l_svf.lo * l_amp * l_lvl * 0.11f * mgain[4];
      if (h_on) {
        float hi, g = l_amp * 0.075f * mgain[4];
        h_f += (h_tf - h_f) * 0.004f;
        hi = h_f * PHK * vib;
        h_ph[0] += (uint32_t)(hi * 0.997f);
        h_ph[1] += (uint32_t)(hi * 1.003f);
        h_lp[0] += 0.35f * (saw(h_ph[0]) - h_lp[0]);
        h_lp[1] += 0.35f * (saw(h_ph[1]) - h_lp[1]);
        hl = h_lp[0] * g;
        hr = h_lp[1] * g;
      }
    }

    /* effects: crash, riser, delay */
    if (cr_amp > 0.0001f) {
      x = hp * cr_amp * 0.11f;
      fxL += x;
      fxR += x * 0.8f + n * cr_amp * 0.02f;
      cr_amp *= 0.99998f;
    }
    ri_amp += ((riser_on ? ri_t : 0.0f) - ri_amp) * 0.001f;
    if (ri_amp > 0.0001f) {
      svf(&ri_f, n, ri_fc, 0.35f);
      x = ri_f.bp * ri_amp * 0.3f;
      fxL += x; fxR += x;
    }
    t = d_w + (DLY_LEN - DLY_TAP2);
    if (t >= DLY_LEN) t -= DLY_LEN;
    d1 = (float)dly[d_w] * (1.0f / 16384.0f);
    d2 = (float)dly[t] * (1.0f / 16384.0f);
    x = (arp * 0.5f + (lead + 0.5f * (hl + hr)) * 0.5f) * mgain[5] + d1 * 0.4f;
    if (x > 1.9f) x = 1.9f;
    else if (x < -1.9f) x = -1.9f;
    else if (!(x >= -1.9f)) x = 0.0f;     /* NaN */
    dly[d_w] = (int16_t)(x * 16384.0f);
    if (++d_w >= DLY_LEN) d_w = 0;
    fxL += d2 * 0.5f;
    fxR += d1 * 0.5f;

    /* mix */
    L = drums + bass + chL + arp * a_panl + lead + hl + fxL * mgain[5];
    R = drums + bass + chR + arp * a_panr + lead + hr + fxR * mgain[5];
    vol_s += (vt - vol_s) * 0.001f;
    L = softclip(L * 0.6f) * vol_s;
    R = softclip(R * 0.6f) * vol_s;
    out[2 * i]     = (int32_t)(L * (0.89f * (float)FM1_AUDIO_MAX));
    out[2 * i + 1] = (int32_t)(R * (0.89f * (float)FM1_AUDIO_MAX));

    if (playing && ++pos >= STEP_LEN) {
      pos = 0;
      if (++step >= STEPS) step = 0;
    }
    clk++;
  }

  /* one mistake must not silence the program for good */
  svf_check(&cl_f); svf_check(&ri_f); svf_check(&b_svf); svf_check(&c_fl);
  svf_check(&c_fr); svf_check(&p_fl); svf_check(&p_fr); svf_check(&l_svf);
  a_lp = fsafe(a_lp); h_lp[0] = fsafe(h_lp[0]); h_lp[1] = fsafe(h_lp[1]);
  if (!(vol_s >= 0.0f && vol_s <= 1.0f)) vol_s = 0.0f;
  flush(&k_amp); flush(&k_fenv); flush(&side); flush(&oh_amp); flush(&ch_amp);
  flush(&cl_amp); flush(&cr_amp); flush(&b_fenv); flush(&b_amp); flush(&a_amp);
  flush(&c_amp); flush(&p_amp); flush(&l_amp); flush(&ri_amp);
  g_clock = clk;
}

/* ---------- screen ---------- */
#define REG_X   40           /* the shape stays in this region */
#define REG_Y   33
#define REG_W   160
#define REG_H   168
#define SHP_CX  120          /* the centre of the shape */
#define SHP_CY  117
#define MID_Y0  24           /* the stars fly between these rows */
#define MID_Y1  212
#define SCR_Y   214          /* the scroller */
#define SCR_H   24
#define NSTARS  64

static uint16_t shpbuf[REG_W * REG_H];   /* the rectangle that is redrawn */
static uint16_t strip[240 * SCR_H];
static uint16_t rainbow[64];

static float   st_x[NSTARS], st_y[NSTARS], st_z[NSTARS];
static int16_t st_dx[NSTARS], st_dy[NSTARS];
static uint8_t st_ds[NSTARS];   /* size drawn on the screen, 0 = none */

static float   shp_v[3][12][3];
static uint8_t shp_e[3][30][2];
static int     shp_nv[3], shp_ne[3];
static int     spx[12], spy[12];             /* the vertices on the screen */
static int     ux, uy, uw, uh;               /* the rectangle redrawn this frame */
static int     cur_x0, cur_y0, cur_x1, cur_y1; /* the shape's own rectangle */
static float   pulse;

static const char greet[] =
  "*** A MINOR ODYSSEY *** A TRANCE INTRO FOR THE FM-1 AT 128 BPM ... "
  "27 KEYS, 14 BUTTONS AND ONE BIG HEART ... "
  "GREETINGS FLY OUT TO ALL FM-1 HACKERS, THE CHIPTUNE CREW, THE TRACKER KIDS, "
  "THE PIXEL PUSHERS, THE PARTY ORGANIZERS AND EVERY CODER WHO STILL COUNTS CYCLES ... "
  "PLAY STARTS AND STOPS, FX SCL ENV LFO EDIT GLO MUTE THE PARTS, OCT JUMPS ... "
  "KEEP THE SCENE ALIVE!          ";

static uint32_t vrng = 12345u;
static uint32_t last_draw, last_kicks;
static int   started, drawn, title_key = -1;
static int   led_state[FM1_NKEY];
static float energy = 0.5f, kick_t = 10.0f, ang_x, ang_y, ang_z, scroll_pos, t_anim;

static float frand(void)
{
  vrng = vrng * 1664525u + 1013904223u;
  return (float)(vrng >> 8) * (1.0f / 16777216.0f);
}

static int clampi(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t rgb(float r, float g, float b)
{
  int R = clampi((int)(r * 31.0f), 0, 31);
  int G = clampi((int)(g * 63.0f), 0, 63);
  int B = clampi((int)(b * 31.0f), 0, 31);
  return (uint16_t)((R << 11) | (G << 5) | B);
}

static const uint8_t *glyph(char c)
{
  if (c >= 'a' && c <= 'z') c = (char)(c - 32);
  if (c < 32 || c > 90) c = 32;
  return font[c - 32];
}

/* the part name at the top, 2x size, centred */
static void draw_title(const char *s, float r, float g, float b)
{
  int n = 0, i, x0, row, col;
  while (s[n]) n++;
  for (i = 0; i < 240 * 22; i++) strip[i] = 0;
  x0 = (240 - (n * 12 - 2)) / 2;
  for (row = 0; row < 7; row++) {
    float sh = 1.0f - 0.08f * (float)row;
    uint16_t c = rgb(r * sh, g * sh, b * sh);
    for (i = 0; i < n; i++) {
      const uint8_t *gl = glyph(s[i]);
      for (col = 0; col < 5; col++)
        if (gl[row] & (0x10 >> col)) {
          int p = (4 + row * 2) * 240 + x0 + i * 12 + col * 2;
          strip[p] = c; strip[p + 1] = c; strip[p + 240] = c; strip[p + 241] = c;
        }
    }
  }
  fm1_lcd_blit(0, 0, 240, 22, strip, 240 * 22);
}

/* the sine scroller at the bottom */
static void draw_scroller(int sp, unsigned ph, int cs)
{
  int x, r, total = (int)(sizeof(greet) - 1) * 12;
  for (x = 0; x < 240 * SCR_H; x++) strip[x] = 0;
  for (x = 0; x < 240; x++) {
    int c = (sp + x) % total, ci = c / 12, cx = c - ci * 12, yo, bit;
    const uint8_t *gl;
    uint16_t col;
    if (cx >= 10) continue;
    gl = glyph(greet[ci]);
    bit = 0x10 >> (cx >> 1);
    yo = 5 + (int)(4.0f * sintab[((unsigned)x * 3u + ph) & 511u]);
    col = rainbow[((x >> 2) + cs) & 63];
    for (r = 0; r < 7; r++)
      if (gl[r] & bit) {
        int p = (yo + r * 2) * 240 + x;
        strip[p] = col; strip[p + 240] = col;
      }
  }
  fm1_lcd_blit(0, SCR_Y, 240, SCR_H, strip, 240 * SCR_H);
}

/* a pixel into the redrawn rectangle */
static inline void put(int x, int y, uint16_t c)
{
  int bx = x - ux, by = y - uy;
  if ((unsigned)bx < (unsigned)uw && (unsigned)by < (unsigned)uh) shpbuf[by * uw + bx] = c;
}

static void bline(int x0, int y0, int x1, int y1, uint16_t c)
{
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;
  for (;;) {
    put(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

/* turns the shape, finds where it is, and sets the rectangle to redraw:
 * the shape's rectangle now joined with the one of the frame before */
static void project_shape(float dt, int part)
{
  int sh = part_shape[part], nv = shp_nv[sh], i;
  int x0 = 10000, y0 = 10000, x1 = -10000, y1 = -10000;
  float spin = 0.5f + 0.18f * energy, jump = 0.0f, scale;
  float cX, sX, cY, sY, cZ, sZ;

  ang_y += dt * spin; ang_x += dt * spin * 0.63f; ang_z += dt * spin * 0.29f;
  if (ang_y > TWO_PI) ang_y -= TWO_PI;
  if (ang_x > TWO_PI) ang_x -= TWO_PI;
  if (ang_z > TWO_PI) ang_z -= TWO_PI;
  cX = cosf(ang_x); sX = sinf(ang_x);
  cY = cosf(ang_y); sY = sinf(ang_y);
  cZ = cosf(ang_z); sZ = sinf(ang_z);
  pulse = expf(-kick_t * 8.0f);
  if (kick_t < 0.35f) jump = -12.0f * sinf(3.14159f * kick_t / 0.35f);
  scale = (part == 4 ? 56.0f : 52.0f) * (1.0f + 0.15f * pulse);

  for (i = 0; i < nv; i++) {
    const float *v = shp_v[sh][i];
    float xa = v[0] * cY + v[2] * sY, za = v[2] * cY - v[0] * sY;
    float ya = v[1] * cX - za * sX, zb = v[1] * sX + za * cX;
    float xb = xa * cZ - ya * sZ, yb = xa * sZ + ya * cZ;
    float p = scale * 5.0f / (zb + 5.0f);
    spx[i] = (int)((float)SHP_CX + 0.5f + xb * p);
    spy[i] = (int)((float)SHP_CY + 0.5f + yb * p + jump);
    if (spx[i] < x0) x0 = spx[i];
    if (spx[i] > x1) x1 = spx[i];
    if (spy[i] < y0) y0 = spy[i];
    if (spy[i] > y1) y1 = spy[i];
  }
  x0 = clampi(x0 - 1, REG_X, REG_X + REG_W);
  y0 = clampi(y0 - 1, REG_Y, REG_Y + REG_H);
  x1 = clampi(x1 + 3, REG_X, REG_X + REG_W);
  y1 = clampi(y1 + 3, REG_Y, REG_Y + REG_H);

  ux = x0; uy = y0; uw = x1; uh = y1;          /* uw, uh: right and bottom for now */
  if (cur_x1 > cur_x0 && cur_y1 > cur_y0) {
    if (cur_x0 < ux) ux = cur_x0;
    if (cur_y0 < uy) uy = cur_y0;
    if (cur_x1 > uw) uw = cur_x1;
    if (cur_y1 > uh) uh = cur_y1;
  }
  uw -= ux; uh -= uy;
  if (uw < 0) uw = 0;
  if (uh < 0) uh = 0;
  cur_x0 = x0; cur_y0 = y0; cur_x1 = x1; cur_y1 = y1;
}

/* the edges, and a white dot on each vertex */
static void draw_shape(int part, int playing)
{
  int sh = part_shape[part], i;
  const float *pc = part_col[part];
  float w = 0.7f * pulse, br = playing ? 1.0f : 0.6f;
  uint16_t lc = rgb((pc[0] + (1.0f - pc[0]) * w) * br,
                    (pc[1] + (1.0f - pc[1]) * w) * br,
                    (pc[2] + (1.0f - pc[2]) * w) * br);
  for (i = 0; i < shp_ne[sh]; i++) {
    int a = shp_e[sh][i][0], b = shp_e[sh][i][1];
    bline(spx[a], spy[a], spx[b], spy[b], lc);
  }
  for (i = 0; i < shp_nv[sh]; i++) {
    put(spx[i], spy[i], 0xFFFFu);     put(spx[i] + 1, spy[i], 0xFFFFu);
    put(spx[i], spy[i] + 1, 0xFFFFu); put(spx[i] + 1, spy[i] + 1, 0xFFFFu);
  }
}

static void star_spawn(int i, float z)
{
  st_x[i] = (frand() - 0.5f) * 4.8f;
  st_y[i] = (frand() - 0.5f) * 4.0f;
  st_z[i] = z;
}

/* stars outside the redrawn rectangle go straight to the screen; those in the
 * shape's own rectangle go into shpbuf; the others wait a frame */
static void draw_stars(float dt)
{
  int i;
  for (i = 0; i < NSTARS; i++)
    if (st_ds[i]) {
      fm1_lcd_fill_rect((uint32_t)st_dx[i], (uint32_t)st_dy[i], st_ds[i], st_ds[i], 0);
      st_ds[i] = 0;
    }
  for (i = 0; i < NSTARS; i++) {
    float z, k, br;
    int sx, sy, sz;
    uint16_t c;
    st_z[i] -= dt * 0.22f * energy;
    if (st_z[i] < 0.05f) star_spawn(i, 0.9f + 0.1f * frand());
    z = st_z[i];
    k = 50.0f / z;
    sx = 120 + (int)(st_x[i] * k);
    sy = 117 + (int)(st_y[i] * k);
    sz = z < 0.35f ? 2 : 1;
    if (sx < 0 || sx + sz > 240 || sy < MID_Y0 || sy + sz > MID_Y1) {
      star_spawn(i, 0.9f + 0.1f * frand());
      continue;
    }
    br = 1.2f - z;
    if (br > 1.0f) br = 1.0f;
    c = rgb(br * 0.8f, br * 0.85f, br);
    if (sx + sz <= ux || sx >= ux + uw || sy + sz <= uy || sy >= uy + uh) {
      fm1_lcd_fill_rect((uint32_t)sx, (uint32_t)sy, (uint32_t)sz, (uint32_t)sz, c);
      st_dx[i] = (int16_t)sx; st_dy[i] = (int16_t)sy; st_ds[i] = (uint8_t)sz;
    } else if (sx >= cur_x0 && sx + sz <= cur_x1 && sy >= cur_y0 && sy + sz <= cur_y1) {
      put(sx, sy, c);
      if (sz == 2) { put(sx + 1, sy, c); put(sx, sy + 1, c); put(sx + 1, sy + 1, c); }
    }
  }
}

static float dist2(int k, int i, int j)
{
  float dx = shp_v[k][i][0] - shp_v[k][j][0];
  float dy = shp_v[k][i][1] - shp_v[k][j][1];
  float dz = shp_v[k][i][2] - shp_v[k][j][2];
  return dx * dx + dy * dy + dz * dz;
}

/* scales the vertices to radius 1; the edges are the shortest distances */
static void make_shape(int k, float (*v)[3], int nv)
{
  int i, j, ne = 0;
  float mind = 1e9f;
  for (i = 0; i < nv; i++) {
    float l = sqrtf(v[i][0] * v[i][0] + v[i][1] * v[i][1] + v[i][2] * v[i][2]);
    for (j = 0; j < 3; j++) shp_v[k][i][j] = v[i][j] / l;
  }
  for (i = 0; i < nv; i++)
    for (j = i + 1; j < nv; j++)
      if (dist2(k, i, j) < mind) mind = dist2(k, i, j);
  for (i = 0; i < nv; i++)
    for (j = i + 1; j < nv; j++)
      if (dist2(k, i, j) < mind * 1.05f && ne < 30) {
        shp_e[k][ne][0] = (uint8_t)i;
        shp_e[k][ne][1] = (uint8_t)j;
        ne++;
      }
  shp_nv[k] = nv;
  shp_ne[k] = ne;
}

/* (note, length) pairs to a note on each 16th */
static void expand(const uint8_t (*src)[2], int count, uint8_t *dst, int len)
{
  int i, j, n = 0;
  for (i = 0; i < count; i++) {
    int l = src[i][1];
    if (n + l > len) break;
    dst[n] = src[i][0];
    for (j = 1; j < l; j++) dst[n + j] = 0;
    n += l;
  }
}

/* ---------- setup and loop ---------- */
void setup(void)
{
  int i;
  float v[12][3];
  const float gr = 1.618034f;

  for (i = 0; i < 512; i++) sintab[i] = sinf(TWO_PI * (float)i / 512.0f);
  for (i = 0; i < 64; i++) {
    float a = TWO_PI * (float)i / 64.0f;
    rainbow[i] = rgb(0.55f + 0.45f * sinf(a), 0.55f + 0.45f * sinf(a + 2.094f),
                     0.55f + 0.45f * sinf(a + 4.189f));
  }
  expand(mel_src, (int)(sizeof(mel_src) / sizeof(mel_src[0])), mel, 128);
  expand(mel_end_src, (int)(sizeof(mel_end_src) / sizeof(mel_end_src[0])), mel_end, 32);

  /* cube, octahedron, icosahedron */
  for (i = 0; i < 8; i++) {
    v[i][0] = (i & 1) ? 1.0f : -1.0f;
    v[i][1] = (i & 2) ? 1.0f : -1.0f;
    v[i][2] = (i & 4) ? 1.0f : -1.0f;
  }
  make_shape(0, v, 8);
  for (i = 0; i < 6; i++) {
    v[i][0] = 0.0f; v[i][1] = 0.0f; v[i][2] = 0.0f;
    v[i][i >> 1] = (i & 1) ? 1.0f : -1.0f;
  }
  make_shape(1, v, 6);
  for (i = 0; i < 4; i++) {
    float a = (i & 1) ? 1.0f : -1.0f, b = (i & 2) ? gr : -gr;
    v[i][0] = 0.0f;     v[i][1] = a;        v[i][2] = b;
    v[i + 4][0] = a;    v[i + 4][1] = b;    v[i + 4][2] = 0.0f;
    v[i + 8][0] = b;    v[i + 8][1] = 0.0f; v[i + 8][2] = a;
  }
  make_shape(2, v, 12);

  for (i = 0; i < NSTARS; i++) star_spawn(i, 0.1f + 0.9f * frand());
  set_chord(0);
  set_pad(0);
}

void loop(void)
{
  uint32_t now = g_clock, pressed, kicks;
  int i, playing, cur, part, pb;
  float dt;

  if (!started) {
    started = 1;
    fm1_lcd_fill_rect(0, 0, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, 0);
    for (i = 0; i < (int)FM1_NKEY; i++) led_state[i] = -1;
  }

  /* controls */
  pressed = fm1_input_edges(0);
  if (pressed & (1u << FM1_BTN_PLAY)) g_playing = !g_playing;
  for (i = 0; i < 6; i++)
    if (pressed & (1u << (FM1_BTN_FX + i))) g_mute ^= 1u << i;
  if (pressed & ((1u << FM1_BTN_OCT_UP) | (1u << FM1_BTN_OCT_DOWN))) {
    int st = (int)g_step, p = (st >> 8) % 6;
    if (pressed & (1u << FM1_BTN_OCT_UP)) p = (p + 1) % 6;
    else if ((st & 255) < 16) p = (p + 5) % 6;
    g_jump = (uint32_t)(p * 256) + 1u;
  }
  (void)fm1_input_note_edges();
  {
    float m = (float)fm1_master() * (1.0f / (float)FM1_MASTER_MAX);
    g_vol = m * m;
  }

  playing = g_playing;
  cur = (int)g_step;
  part = (cur >> 8) % 6;
  pb = (cur >> 4) & 15;

  /* LEDs: PLAY on when stopped, blinks with the beat when playing.
   * A part that plays flashes with its notes (or the beat); a muted part is on. */
  {
    int want[FM1_NKEY];
    int beat_on = playing ? (cur & 3) < 2 : ((now / (uint32_t)(BEAT_LEN / 2)) & 1u) == 0;
    for (i = 0; i < (int)FM1_NKEY; i++) want[i] = 0;
    want[FM1_BTN_OCT_DOWN] = 1;
    want[FM1_BTN_OCT_UP] = 1;
    want[FM1_BTN_PLAY] = playing ? beat_on : 1;
    for (i = 0; i < 6; i++) {
      uint32_t since = now - g_trig[i];
      int on;
      if ((g_mute >> i) & 1u) on = 1;
      else if (playing && since < (uint32_t)BEAT_LEN) on = since < 3000u;
      else on = beat_on;
      want[FM1_BTN_FX + i] = on;
    }
    for (i = 0; i < (int)FM1_NKEY; i++)
      if (want[i] != led_state[i]) {
        led_state[i] = want[i];
        fm1_led_key((uint32_t)i, want[i]);
      }
  }

  /* draw at most 60 frames a second, timed by the audio clock */
  if (drawn && now - last_draw < 735u) return;
  dt = (float)(now - last_draw) * (1.0f / SR);
  if (dt > 0.1f) dt = 0.1f;
  last_draw = now;
  drawn = 1;

  {
    static const float e_part[6] = { 1.0f, 2.0f, 5.0f, 0.7f, 7.0f, 2.0f };
    float tgt = playing ? e_part[part] : 0.3f, a = dt * 2.0f;
    if (playing && (part == 1 || part == 3) && pb >= 8)
      tgt += 4.0f * (float)((pb - 8) * 16 + (cur & 15)) * (1.0f / 128.0f);
    energy += (tgt - energy) * (a > 1.0f ? 1.0f : a);
  }
  kicks = g_kicks;
  if (kicks != last_kicks) { last_kicks = kicks; kick_t = 0.0f; }
  else if (kick_t < 10.0f) kick_t += dt;

  {
    int key = playing ? part : (cur == 0 ? 14 : part + 8);
    if (key != title_key) {
      title_key = key;
      if (key == 14) draw_title("PRESS PLAY", 0.9f, 0.9f, 0.9f);
      else if (!playing) draw_title(part_name[part], 0.5f, 0.5f, 0.5f);
      else draw_title(part_name[part], part_col[part][0], part_col[part][1], part_col[part][2]);
    }
  }

  project_shape(dt, part);
  for (i = 0; i < uw * uh; i++) shpbuf[i] = 0;
  draw_stars(dt);
  draw_shape(part, playing);
  if (uw > 0 && uh > 0)
    fm1_lcd_blit((uint32_t)ux, (uint32_t)uy, (uint32_t)uw, (uint32_t)uh, shpbuf, (uint32_t)(uw * uh));

  scroll_pos += dt * 70.0f;
  if (scroll_pos >= (float)((sizeof(greet) - 1) * 12))
    scroll_pos -= (float)((sizeof(greet) - 1) * 12);
  t_anim += dt;
  if (t_anim >= 128.0f) t_anim -= 128.0f;
  draw_scroller((int)scroll_pos, (unsigned)(t_anim * 200.0f), (int)(t_anim * 30.0f));
}
