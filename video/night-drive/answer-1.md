Here is **NIGHT DRIVE**. It has a two-octave up arpeggio, an 8th-note bass, a beat with kick pump, a dotted-8th ping-pong echo and drive. On the screen, a neon sun with sliding stripes sits over a grid road that moves one line per beat, and every held note is a star.

```c
/*
 * NIGHT DRIVE - a synthwave arpeggiator for the M-VAVE FM-1 (FM-1 Studio)
 *
 *   note keys  hold notes: they play as 16th notes, up over two octaves,
 *              over a pulsing 8th-note bass on the lowest held note
 *   PLAY       starts and stops the beat (kick, snare, hats)
 *   KNOB 1     tempo, 80 to 160 BPM
 *   KNOB 2     filter
 *   KNOB 3     echo (dotted 8th, ping-pong)
 *   KNOB 4     drive
 *   MASTER     volume
 *
 * The screen: a neon sun over a grid road. The road and the stripes of the
 * sun move one line per beat. Each held note is a star in the sky; the star
 * of the note that sounds now flashes.
 */
#include <stdint.h>
#include <math.h>
#include "fm1.h"

/* ------------------------------------------------------------ constants */

#define SR          44100.0f
#define INC_PER_HZ  (4294967296.0f / SR)   /* phase increment of 1 Hz */
#define NOTE_KEY0   53                     /* note key 0 is F3 */
#define NKEYS       27
#define KEYMASK     0x7FFFFFFu

#define BPM_MIN     80
#define BPM_MAX     160

/* the echo buffer runs at half rate: two dotted 8ths at 80 BPM (49.7 KB) */
#define ELEN        24832

#define HORIZON     140
#define SUN_CX      120
#define SUN_CY      116
#define SUN_R       50
#define SUN_TOP     (SUN_CY - SUN_R)       /* 66 */
#define SUN_ROWS    (HORIZON - SUN_TOP)    /* 74 */
#define STRIPE_Y    (SUN_CY - 12)          /* the stripes of the sun start here */
#define GROUND_ROWS (240 - HORIZON - 1)    /* 99 */

#define FRAME_SAMPLES 1323u                /* about 30 ms between two frames */
#define LABEL_SAMPLES 66150u               /* a knob value shows for 1.5 s */

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_WHITE     RGB(255, 255, 255)
#define COL_STAR      RGB(150, 230, 255)
#define COL_STAR_DIM  RGB(70, 120, 190)
#define COL_GOLD      RGB(255, 230, 120)
#define COL_PINK      RGB(255, 110, 210)
#define COL_TITLE     RGB(90, 235, 255)
#define COL_LABEL     RGB(255, 225, 90)
#define COL_BPM       RGB(255, 100, 210)
#define COL_BPM_SMALL RGB(200, 150, 255)
#define COL_HORIZON   RGB(255, 160, 235)

/* ---------------------------------------- shared by loop() and audio() */

static volatile uint32_t g_held;          /* held note keys          loop -> audio */
static volatile uint32_t g_play_presses;  /* PLAY presses            loop -> audio */
static volatile int      g_bpm = 110;     /* tempo in BPM            loop -> audio */
static volatile float    g_cut = 1000.0f; /* filter cutoff in Hz     loop -> audio */
static volatile float    g_echo = 0.4f;   /* 0 to 1                  loop -> audio */
static volatile float    g_drive = 0.2f;  /* 0 to 1                  loop -> audio */
static volatile uint32_t g_master = 700;  /* MASTER pot              loop -> audio */

static volatile int      g_drums;         /* the beat plays          audio -> loop */
static volatile uint32_t g_pos;           /* 16ths << 8 | fraction   audio -> loop */
static volatile uint32_t g_samples;       /* samples played          audio -> loop */
static volatile int      g_arp_key = -1;  /* note key the arp plays  audio -> loop */
static volatile int      g_arp_on;        /* its gate is open        audio -> loop */

/* ------------------------------------------------------------ audio state */

static uint32_t note_inc[128];            /* phase increment of each MIDI note */

static float c_adec, c_arel, c_afenv;     /* arp: decay, release, filter envelope */
static float c_bdec, c_brel, c_bfenv;     /* bass: the same */
static float c_kick, c_kpitch, c_stone, c_snoise, c_hat, c_pump;
static uint32_t snare_inc;

static uint32_t count16 = 0xFFFFFFFFu;    /* 16th notes since the clock started */
static uint32_t step_ph;                  /* position in the 16th (2^32 is one) */
static uint32_t arp_pos, prev_held, play_seen;
static int drums_on;

static uint32_t a_ph1, a_ph2, a_inc1, a_inc2;   /* arp: two detuned saws, filter */
static float a_env, a_vel, a_fenv, a_low, a_band;
static int a_att, a_gate;

static uint32_t b_ph, b_inc;                    /* bass: one saw, filter */
static float b_env, b_vel, b_fenv, b_low, b_band;
static int b_att, b_gate;

static uint32_t k_ph, s_ph, rng = 0x12345678u;  /* drums */
static float k_env, k_penv, s_tenv, s_nenv, s_vel, s_lp, h_env, h_lp, pump;

static int16_t echo_buf[ELEN];                  /* echo */
static int e_w, e_tick;
static float e_dly, e_acc, e_oL, e_oR, e_fb, e_sL, e_sR;

static float cut_s = 1000.0f, wet_s, pre_s = 1.0f, vol_s;  /* smoothed controls */

/* -------------------------------------------------------- audio helpers */

/* sine of a 32-bit phase (parabola, about 0.1 % error) */
static inline float fsin(uint32_t ph)
{
  float x = (float)(int32_t)ph * (1.0f / 2147483648.0f);
  float y = 4.0f * x * (1.0f - (x < 0.0f ? -x : x));
  return y * (0.775f + 0.225f * (y < 0.0f ? -y : y));
}

static inline float noise(void)
{
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* saw with polyBLEP edges, less aliasing */
static inline float saw(uint32_t ph, uint32_t inc)
{
  float t = (float)ph * (1.0f / 4294967296.0f);
  float dt = (float)inc * (1.0f / 4294967296.0f);
  float s = 2.0f * t - 1.0f;
  if (t < dt) {
    t /= dt;
    s -= t + t - t * t - 1.0f;
  } else if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    s -= t * t + t + t + 1.0f;
  }
  return s;
}

static inline float echo_read(float p)
{
  int i0 = (int)p;
  float fr = p - (float)i0;
  if (i0 >= ELEN) i0 -= ELEN;
  int i1 = i0 + 1 < ELEN ? i0 + 1 : 0;
  float a = (float)echo_buf[i0];
  return (a + ((float)echo_buf[i1] - a) * fr) * (1.0f / 32000.0f);
}

static float env_coef(float seconds)
{
  return expf(-1.0f / (seconds * SR));
}

/* one 16th note: drums, arp and bass */
static void do_step(uint32_t held)
{
  count16++;
  uint32_t step = count16 & 15u, bar = (count16 >> 4) & 3u;

  if (drums_on) {
    if ((step & 3u) == 0) {                      /* kick on each beat */
      k_env = 1.0f; k_penv = 1.0f; k_ph = 0; pump = 1.0f;
    }
    if (step == 4 || step == 12 || (bar == 3 && step >= 14)) {  /* snare, fill */
      s_vel = step < 14 ? 1.0f : (step == 14 ? 0.45f : 0.7f);
      s_tenv = 1.0f; s_nenv = 1.0f; s_ph = 0;
    }
    if ((step & 3u) == 2) h_env = 1.0f;          /* hats on the off-beats */
    else if (step & 1u) h_env = 0.4f;            /* and soft 16ths */
  }

  if (held) {
    uint8_t keys[NKEYS];
    uint32_t n = 0;
    for (int k = 0; k < NKEYS; k++)
      if (held & (1u << k)) keys[n++] = (uint8_t)k;
    uint32_t idx = arp_pos++ % (2u * n);         /* up, over two octaves */
    int key = keys[idx % n];
    uint32_t inc = note_inc[NOTE_KEY0 + key + 12 * (int)(idx / n)];
    a_inc1 = inc + inc / 290u;                   /* +-6 cents */
    a_inc2 = inc - inc / 290u;
    a_vel = (step & 1u) ? 0.75f : 1.0f;
    a_att = 1; a_gate = 1; a_fenv = 1.0f;
    g_arp_key = key;
    if ((step & 1u) == 0) {                      /* bass on the 8ths */
      int bn = NOTE_KEY0 + keys[0];
      while (bn > 40) bn -= 12;                  /* F1 to E2 */
      b_inc = note_inc[bn];
      b_vel = (step & 3u) == 0 ? 1.0f : 0.8f;
      b_att = 1; b_gate = 1; b_fenv = 1.0f;
    }
  }
}

void audio(int32_t *out, int frames)
{
  uint32_t held = g_held & KEYMASK;
  int restart = 0;

  uint32_t pp = g_play_presses;
  if (pp != play_seen) {                 /* PLAY: the beat on or off */
    play_seen = pp;
    drums_on = !drums_on;
    if (drums_on) restart = 1;           /* start on the first kick */
  }
  if (held && !prev_held && !drums_on) restart = 1;  /* the arp starts now */
  if (!held) {
    a_gate = 0; b_gate = 0; arp_pos = 0;
  }
  prev_held = held;

  float bpm = (float)g_bpm;
  uint32_t step_inc = (uint32_t)(bpm * (4.0f / 60.0f) * INC_PER_HZ);
  float e_target = 992250.0f / bpm;      /* a dotted 8th in half-rate samples */

  cut_s += (g_cut - cut_s) * 0.25f;
  float fa = cut_s * (6.2831853f / SR);  /* filter coefficient of the arp */
  float fb = fa * 0.5f;                  /* and of the bass */
  if (fb < 0.006f) fb = 0.006f;
  float echo = g_echo;
  float fbk = 0.25f + echo * 0.45f;      /* echo feedback */
  wet_s += (echo * 0.75f - wet_s) * 0.25f;
  pre_s += (1.0f + 6.0f * g_drive - pre_s) * 0.25f;
  float pre = pre_s * 0.5f;
  float comp = 1.0f / sqrtf(pre_s);
  float m = (float)g_master * (1.0f / (float)FM1_MASTER_MAX);
  float vol_inc = (m * m * 0.9f - vol_s) / (float)frames;

  for (int i = 0; i < frames; i++) {
    /* clock */
    if (restart) {
      restart = 0;
      count16 = 0xFFFFFFFFu;
      step_ph = 0;
      do_step(held);
    } else {
      uint32_t old = step_ph;
      step_ph += step_inc;
      if (step_ph < old) do_step(held);
    }
    if (a_gate && step_ph >= 0xA0000000u) a_gate = 0;   /* 5/8 of the 16th */
    if (b_gate && (count16 & 1u) && step_ph >= 0x80000000u) b_gate = 0;

    /* arp voice */
    float arp = 0.0f;
    if (a_att || a_env > 0.00003f) {
      if (a_att) {
        a_env += 0.02f;
        if (a_env >= a_vel) a_att = 0;
      } else {
        a_env *= a_gate ? c_adec : c_arel;
      }
      float x = saw(a_ph1, a_inc1) + saw(a_ph2, a_inc2);
      a_ph1 += a_inc1;
      a_ph2 += a_inc2;
      float f = fa * (1.0f + 3.0f * a_fenv);
      if (f > 1.1f) f = 1.1f;
      a_fenv *= c_afenv;
      a_low += f * a_band;                          /* state variable filter */
      a_band += f * (x - a_low - 0.42f * a_band);
      arp = a_low * a_env;
    } else {
      a_env = 0.0f; a_low = 0.0f; a_band = 0.0f;
    }

    /* bass voice */
    float bass = 0.0f;
    if (b_att || b_env > 0.00003f) {
      if (b_att) {
        b_env += 0.01f;
        if (b_env >= b_vel) b_att = 0;
      } else {
        b_env *= b_gate ? c_bdec : c_brel;
      }
      float x = saw(b_ph, b_inc);
      b_ph += b_inc;
      float f = fb * (1.0f + 2.0f * b_fenv);
      if (f > 1.0f) f = 1.0f;
      b_fenv *= c_bfenv;
      b_low += f * b_band;
      b_band += f * (x - b_low - 0.5f * b_band);
      bass = b_low * b_env;
    } else {
      b_env = 0.0f; b_low = 0.0f; b_band = 0.0f;
    }

    /* drums */
    float nz = noise();
    float kick = 0.0f, snare = 0.0f, hat = 0.0f;
    if (k_env > 0.0002f) {
      kick = fsin(k_ph) * k_env;
      k_ph += (uint32_t)((45.0f + 130.0f * k_penv) * INC_PER_HZ);
      k_env *= c_kick;
      k_penv *= c_kpitch;
    }
    if (s_nenv > 0.0002f) {
      s_lp += (nz - s_lp) * 0.3f;
      snare = (fsin(s_ph) * s_tenv * 0.5f + (nz - s_lp) * s_nenv) * s_vel;
      s_ph += snare_inc;
      s_tenv *= c_stone;
      s_nenv *= c_snoise;
    }
    if (h_env > 0.0002f) {
      h_lp += (nz - h_lp) * 0.6f;
      hat = (nz - h_lp) * h_env;
      h_env *= c_hat;
    }
    pump *= c_pump;
    float duck = 1.0f - 0.45f * pump;              /* the synths pump with the kick */

    /* drive: soft clip with level compensation */
    float mix = ((arp * 0.16f + bass * 0.30f) * duck + kick * 0.55f + snare * 0.30f) * pre;
    if (mix > 3.0f) mix = 3.0f;
    else if (mix < -3.0f) mix = -3.0f;
    float mix2 = mix * mix;
    float dry = mix * (27.0f + mix2) / (27.0f + 9.0f * mix2) * comp;

    /* echo: dotted 8th ping-pong (left, then right), at half rate */
    e_acc += arp * duck * 0.2f + snare * 0.08f;
    e_tick ^= 1;
    if (!e_tick) {
      float in = e_acc * 0.5f;
      e_acc = 0.0f;
      e_dly += (e_target - e_dly) * 0.0015f;
      float pl = (float)e_w - e_dly;
      if (pl < 0.0f) pl += (float)ELEN;
      float pr = (float)e_w - 2.0f * e_dly;
      if (pr < 0.0f) pr += (float)ELEN;
      e_oL = echo_read(pl);
      e_oR = echo_read(pr);
      e_fb += (e_oR - e_fb) * 0.5f;
      float w = in + e_fb * fbk;
      if (w > 1.0f) w = 1.0f;
      else if (w < -1.0f) w = -1.0f;
      echo_buf[e_w] = (int16_t)(w * 32000.0f);
      if (++e_w >= ELEN) e_w = 0;
    }
    e_sL += (e_oL - e_sL) * 0.4f;
    e_sR += (e_oR - e_sR) * 0.4f;

    /* out */
    vol_s += vol_inc;
    float L = (dry + hat * 0.07f + e_sL * wet_s) * vol_s;
    float R = (dry + hat * 0.10f + e_sR * wet_s) * vol_s;
    if (L > 0.95f) L = 0.95f;
    else if (L < -0.95f) L = -0.95f;
    if (R > 0.95f) R = 0.95f;
    else if (R < -0.95f) R = -0.95f;
    out[2 * i] = (int32_t)(L * (float)FM1_AUDIO_MAX);
    out[2 * i + 1] = (int32_t)(R * (float)FM1_AUDIO_MAX);
  }

  g_drums = drums_on;
  g_arp_on = a_gate;
  g_pos = (count16 << 8) | (step_ph >> 24);
  g_samples += (uint32_t)frames;
  if (pump < 1e-6f) pump = 0.0f;                    /* no denormals */
  if (e_fb < 1e-9f && e_fb > -1e-9f) e_fb = 0.0f;
  if (e_sL < 1e-9f && e_sL > -1e-9f) e_sL = 0.0f;
  if (e_sR < 1e-9f && e_sR > -1e-9f) e_sR = 0.0f;
}

/* ---------------------------------------------------------------- UI state */

static int ui_bpm = 110, p_filter = 56, p_echo = 40, p_drive = 20;

enum { LBL_TITLE, LBL_TEMPO, LBL_FILTER, LBL_ECHO, LBL_DRIVE };
static int label_kind = LBL_TITLE;
static uint32_t label_until;
static int hud_kind = -1, hud_val = -1, hud_bpm = -1;

static int first_frame = 1;
static uint32_t last_frame;

static int8_t   led_now[FM1_NLED];          /* the LEDs start off */
static uint16_t sky_col[HORIZON];           /* sky colour of each row */
static uint8_t  sun_hw[SUN_ROWS];           /* half width of each sun row */
static uint8_t  sun_state[SUN_ROWS];        /* 1 sun, 0 stripe, 0xFF not drawn */
static uint8_t  ground_state[GROUND_ROWS];  /* 0 no line, 1 half, 2 full line */
static uint8_t  star_state[NKEYS];          /* 0 none, 1 held, 2 sounds now */
static uint16_t rowbuf[240];
static uint16_t txtbuf[11 * 12 * 14];       /* 11 characters at scale 2 */

/* the height of the star of each note key; low notes left, high notes right */
static const uint8_t star_y[NKEYS] = {
  44, 26, 52, 34, 20, 48, 30, 56, 38, 22, 46, 28, 54, 36,
  24, 50, 32, 58, 40, 21, 47, 29, 55, 35, 25, 51, 39
};

/* 5 x 7 font: space, 0-9, A-Z; one byte per column, bit 0 at the top */
static const uint8_t font5x7[37][5] = {
  {0x00,0x00,0x00,0x00,0x00},
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},
  {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

/* ------------------------------------------------------------ UI helpers */

static int clampi(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint16_t rgb(int r, int g, int b)
{
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return RGB(r, g, b);
}

static float cutoff_hz(int p)                /* 0..100 -> 120 Hz to 6 kHz */
{
  return 120.0f * powf(50.0f, (float)p * 0.01f);
}

/* sets an LED only when it changes */
static void led_set(uint32_t id, int level)
{
  if (led_now[id] != level) {
    led_now[id] = (int8_t)level;
    fm1_led_level(id, level);
  }
}

static void show_label(int kind, uint32_t now)
{
  label_kind = kind;
  label_until = now + LABEL_SAMPLES;
}

/* ---------------------------------------------------------------- screen */

/* the sky at (x, y): a gradient with a few faint stars, none behind the sun */
static uint16_t sky_px(int x, int y)
{
  if (y >= 20 && y <= 136) {
    int dx = x - SUN_CX, dy = y - SUN_CY;
    if (dx * dx + dy * dy > (SUN_R + 4) * (SUN_R + 4)) {
      uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
      h ^= h >> 13;
      h *= 0x5BD1E995u;
      h ^= h >> 15;
      if ((h & 511u) == 0) return (h & 512u) ? RGB(170, 160, 220) : RGB(110, 100, 170);
    }
  }
  return sky_col[y];
}

static void draw_text(int x, int y, int s, const char *str, int n, uint16_t col)
{
  int cw = 6 * s, w = n * cw, h = 7 * s, len = 0;
  while (len < n && str[len]) len++;
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      int ci = c / cw, gx = (c % cw) / s;
      uint16_t px = sky_px(x + c, y + r);
      if (ci < len && gx < 5) {
        char ch = str[ci];
        int g = ch >= '0' && ch <= '9' ? 1 + ch - '0' : (ch >= 'A' && ch <= 'Z' ? 11 + ch - 'A' : 0);
        if ((font5x7[g][gx] >> (r / s)) & 1u) px = col;
      }
      txtbuf[r * w + c] = px;
    }
  fm1_lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, txtbuf, (uint32_t)(w * h));
}

/* one row of the ground, ri pixels below the horizon (0 is the first) */
static void draw_ground_row(int ri, int st)
{
  float v = (float)(ri + 1);
  float hz = 1.0f / (1.0f + v * 0.1f);           /* haze near the horizon */
  int br = 8 + (int)(80.0f * hz), bg = 2 + (int)(14.0f * hz), bb = 18 + (int)(80.0f * hz);
  float lb = 0.35f + v * 0.011f;                 /* lines brighter near you */
  if (lb > 1.0f) lb = 1.0f;
  uint16_t back = rgb(br, bg, bb);
  uint16_t vcol = rgb(br + (int)(120.0f * lb), bg + (int)(30.0f * lb), bb + (int)(150.0f * lb));
  for (int x = 0; x < 240; x++) rowbuf[x] = back;

  float sp = v * 0.4f;                           /* lines to the vanishing point */
  if (sp >= 2.5f) {
    int jmax = (int)(120.0f / sp) + 1;
    for (int j = -jmax; j <= jmax; j++) {
      int x = 120 + (int)((float)j * sp + (j < 0 ? -0.5f : 0.5f));
      int x2 = j < 0 ? x - 1 : x + 1;
      if (x >= 0 && x < 240) rowbuf[x] = vcol;
      if (v > 50.0f && x2 >= 0 && x2 < 240) rowbuf[x2] = vcol;
    }
  }
  if (st) {                                      /* a line across */
    int r = (int)(255.0f * lb), g = (int)(60.0f * lb), b = (int)(225.0f * lb);
    if (st == 1) {
      r = (r + br) / 2; g = (g + bg) / 2; b = (b + bb) / 2;
    }
    uint16_t hcol = rgb(r, g, b);
    uint16_t xcol = st == 2 ? rgb(255, 120 + (int)(110.0f * lb), 255) : hcol;
    for (int x = 0; x < 240; x++) rowbuf[x] = rowbuf[x] == vcol ? xcol : hcol;
  }
  fm1_lcd_blit(0, (uint32_t)(HORIZON + 1 + ri), 240, 1, rowbuf, 240);
}

/* the road moves one line per beat: ph is the position in the beat, 0 to 1 */
static void update_ground(float ph)
{
  uint8_t want[GROUND_ROWS];
  for (int r = 0; r < GROUND_ROWS; r++) want[r] = 0;
  for (int i = 0; i < 40; i++) {
    float c = 80.0f / ((float)i + 1.0f - ph);    /* centre of the line, below the horizon */
    if (c < 4.0f) break;
    if (c > 104.0f) continue;
    float th = 0.7f + c * 0.025f;                /* thicker near you */
    float top = c - 0.5f * th, bot = c + 0.5f * th;
    for (int r = (int)top - 1; r <= (int)bot - 1; r++) {
      if (r < 0 || r >= GROUND_ROWS) continue;
      float a = (float)(r + 1), b = a + 1.0f;
      float cov = (bot < b ? bot : b) - (top > a ? top : a);
      uint8_t s = cov >= 0.7f ? 2 : (cov >= 0.25f ? 1 : 0);
      if (s > want[r]) want[r] = s;
    }
  }
  for (int r = 0; r < GROUND_ROWS; r++)
    if (want[r] != ground_state[r]) {
      ground_state[r] = want[r];
      draw_ground_row(r, want[r]);
    }
}

static uint16_t sun_color(int i)               /* yellow, orange, hot pink */
{
  float t = (float)i / (float)(SUN_ROWS - 1);
  if (t < 0.5f) {
    float u = t * 2.0f;
    return rgb(255, 240 - (int)(100.0f * u), 110 - (int)(60.0f * u));
  }
  float u = (t - 0.5f) * 2.0f;
  return rgb(255, 140 - (int)(100.0f * u), 50 + (int)(110.0f * u));
}

/* the stripes in the lower half of the sun slide down, one per beat */
static void update_sun(float ph)
{
  for (int i = 0; i < SUN_ROWS; i++) {
    int y = SUN_TOP + i;
    uint8_t s = 1;
    if (y >= STRIPE_Y) {
      float u = (float)(y - STRIPE_Y);
      float p = u - ph * 9.0f + 9.0f;
      float mm = p - 9.0f * (float)(int)(p / 9.0f);
      if (mm < 1.0f + u * 0.13f) s = 0;
    }
    if (s != sun_state[i]) {
      int hw = sun_hw[i];
      sun_state[i] = s;
      fm1_lcd_fill_rect((uint32_t)(SUN_CX - hw), (uint32_t)y, (uint32_t)(2 * hw + 1), 1,
                        s ? sun_color(i) : sky_col[y]);
    }
  }
}

static void draw_star(int k, int lvl)
{
  uint16_t buf[49];
  int cx = 8 + (k * 224) / 26, cy = star_y[k];
  for (int dy = -3; dy <= 3; dy++)
    for (int dx = -3; dx <= 3; dx++) {
      int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
      uint16_t c = sky_px(cx + dx, cy + dy);
      if (lvl == 1) {                            /* held: a small cross */
        if (ax + ay == 0) c = COL_WHITE;
        else if ((ax == 0 || ay == 0) && ax + ay == 1) c = COL_STAR;
        else if ((ax == 0 || ay == 0) && ax + ay == 2) c = COL_STAR_DIM;
      } else if (lvl == 2) {                     /* sounds now: a sparkle */
        if (ax + ay <= 1) c = COL_WHITE;
        else if (ax == 0 || ay == 0) c = ax + ay == 2 ? COL_GOLD : COL_PINK;
        else if (ax == 1 && ay == 1) c = COL_STAR;
      }
      buf[(dy + 3) * 7 + dx + 3] = c;
    }
  fm1_lcd_blit((uint32_t)(cx - 3), (uint32_t)(cy - 3), 7, 7, buf, 49);
}

static void update_stars(uint32_t held)
{
  int ak = g_arp_on ? g_arp_key : -1;
  for (int k = 0; k < NKEYS; k++) {
    uint8_t want = (held >> k) & 1u ? (k == ak ? 2 : 1) : 0;
    if (want != star_state[k]) {
      star_state[k] = want;
      draw_star(k, want);
    }
  }
}

static void update_hud(uint32_t now)
{
  static const char *const names[] = { "NIGHT DRIVE", "TEMPO", "FILTER", "ECHO", "DRIVE" };
  int kind = (int32_t)(label_until - now) > 0 ? label_kind : LBL_TITLE;
  int val = kind == LBL_TEMPO ? ui_bpm : kind == LBL_FILTER ? p_filter :
            kind == LBL_ECHO ? p_echo : kind == LBL_DRIVE ? p_drive : 0;
  if (kind != hud_kind || val != hud_val) {
    char s[12];
    int n = 0;
    for (const char *p = names[kind]; *p; p++) s[n++] = *p;
    if (kind != LBL_TITLE) {
      s[n++] = ' ';
      if (val >= 100) s[n++] = (char)('0' + val / 100);
      if (val >= 10) s[n++] = (char)('0' + val / 10 % 10);
      s[n++] = (char)('0' + val % 10);
    }
    s[n] = 0;
    draw_text(4, 3, 2, s, 11, kind == LBL_TITLE ? COL_TITLE : COL_LABEL);
    hud_kind = kind;
    hud_val = val;
  }
  if (ui_bpm != hud_bpm) {
    char s[4];
    s[0] = ui_bpm >= 100 ? (char)('0' + ui_bpm / 100) : ' ';
    s[1] = (char)('0' + ui_bpm / 10 % 10);
    s[2] = (char)('0' + ui_bpm % 10);
    s[3] = 0;
    draw_text(180, 3, 2, s, 3, COL_BPM);
    hud_bpm = ui_bpm;
  }
}

/* PLAY: on while the beat is stopped, blinks with the beat while it plays.
 * Held note keys: lit, fully on the first 16th of each beat. */
static void update_leds(uint32_t held)
{
  uint32_t q = (g_pos >> 8) & 3u;               /* the 16th in the beat */
  led_set(FM1_BTN_PLAY, (!g_drums || q < 2u) ? FM1_LED_ON : FM1_LED_OFF);
  int lit = q == 0u ? FM1_LED_ON : FM1_LED_MID;
  for (int k = 0; k < NKEYS; k++)
    led_set(FM1_KEY_NOTE0 + (uint32_t)k, (held >> k) & 1u ? lit : FM1_LED_OFF);
}

static void draw_frame(uint32_t held, uint32_t now)
{
  float ph = (float)(g_pos & 1023u) * (1.0f / 1024.0f);   /* position in the beat */
  update_ground(ph);
  update_sun(ph);
  update_stars(held);
  update_hud(now);
}

/* ---------------------------------------------------------------- program */

void setup(void)
{
  for (int n = 0; n < 128; n++)
    note_inc[n] = (uint32_t)(440.0f * powf(2.0f, (float)(n - 69) / 12.0f) * INC_PER_HZ);
  c_adec = env_coef(0.20f);
  c_arel = env_coef(0.012f);
  c_afenv = env_coef(0.07f);
  c_bdec = env_coef(0.25f);
  c_brel = env_coef(0.015f);
  c_bfenv = env_coef(0.05f);
  c_kick = env_coef(0.18f);
  c_kpitch = env_coef(0.03f);
  c_stone = env_coef(0.06f);
  c_snoise = env_coef(0.13f);
  c_hat = env_coef(0.025f);
  c_pump = env_coef(0.12f);
  snare_inc = (uint32_t)(185.0f * INC_PER_HZ);

  g_bpm = ui_bpm;
  g_cut = cutoff_hz(p_filter);
  g_echo = (float)p_echo * 0.01f;
  g_drive = (float)p_drive * 0.01f;
  g_master = fm1_master();
  cut_s = g_cut;
  wet_s = g_echo * 0.75f;
  pre_s = 1.0f + 6.0f * g_drive;
  e_dly = 992250.0f / (float)ui_bpm;

  for (int y = 0; y < HORIZON; y++) {            /* sky: deep blue to purple to pink */
    float t = (float)y / (float)(HORIZON - 1);
    if (t < 0.6f) {
      float u = t / 0.6f;
      sky_col[y] = rgb(6 + (int)(46.0f * u), 3 + (int)(9.0f * u), 26 + (int)(64.0f * u));
    } else {
      float u = (t - 0.6f) / 0.4f;
      sky_col[y] = rgb(52 + (int)(118.0f * u), 12 + (int)(28.0f * u), 90 + (int)(40.0f * u));
    }
  }
  for (int i = 0; i < SUN_ROWS; i++) {
    int dy = SUN_TOP + i - SUN_CY;
    sun_hw[i] = (uint8_t)sqrtf((float)(SUN_R * SUN_R - dy * dy));
    sun_state[i] = 0xFF;
  }
  for (int i = 0; i < GROUND_ROWS; i++) ground_state[i] = 0xFF;

  for (int y = 0; y < HORIZON; y++) {
    for (int x = 0; x < 240; x++) rowbuf[x] = sky_px(x, y);
    fm1_lcd_blit(0, (uint32_t)y, 240, 1, rowbuf, 240);
  }
  fm1_lcd_fill_rect(0, HORIZON, 240, 1, COL_HORIZON);
  draw_text(218, 10, 1, "BPM", 3, COL_BPM_SMALL);

  led_set(FM1_BTN_PLAY, FM1_LED_ON);             /* the only button with a use */
}

void loop(void)
{
  uint32_t pressed = fm1_input_edges(0);
  uint32_t held = fm1_in.notes & KEYMASK;
  uint32_t now = g_samples;
  int32_t d;

  g_held = held;
  g_master = fm1_master();
  if (pressed & (1u << FM1_BTN_PLAY)) g_play_presses = g_play_presses + 1u;

  if ((d = fm1_enc_take(FM1_ENC_KNOB1)) != 0) {  /* tempo: 1 BPM per click */
    ui_bpm = clampi(ui_bpm + (int)d, BPM_MIN, BPM_MAX);
    g_bpm = ui_bpm;
    show_label(LBL_TEMPO, now);
  }
  if ((d = fm1_enc_take(FM1_ENC_KNOB2)) != 0) {  /* the others: 4 per click */
    p_filter = clampi(p_filter + 4 * (int)d, 0, 100);
    g_cut = cutoff_hz(p_filter);
    show_label(LBL_FILTER, now);
  }
  if ((d = fm1_enc_take(FM1_ENC_KNOB3)) != 0) {
    p_echo = clampi(p_echo + 4 * (int)d, 0, 100);
    g_echo = (float)p_echo * 0.01f;
    show_label(LBL_ECHO, now);
  }
  if ((d = fm1_enc_take(FM1_ENC_KNOB4)) != 0) {
    p_drive = clampi(p_drive + 4 * (int)d, 0, 100);
    g_drive = (float)p_drive * 0.01f;
    show_label(LBL_DRIVE, now);
  }

  update_leds(held);

  if (first_frame || now - last_frame >= FRAME_SAMPLES) {
    first_frame = 0;
    last_frame = now;
    draw_frame(held, now);
  }
}
```

## How to play

1. **Turn MASTER up.** The road already moves at the tempo (110 BPM to start), and the stripes in the lower half of the sun slide down one per beat.
2. **Hold notes.** One key or a chord plays as 16th notes, going up through the held notes and then the same notes one octave higher. A bass pulses in 8ths on the lowest held note, moved down to the F1–E2 range. When the beat is off, the arp starts the moment you press. When the beat is on, it comes in on the next 16th. Let go of all keys to stop.
   - The held keys light up and pulse fully on each beat.
   - Each held note shows as a star in the sky: low notes on the left, high notes on the right. The star of the note that sounds now sparkles.
3. **PLAY** starts the beat on its first kick: a kick on every beat, a snare on 2 and 4, hats on the off-beats with soft 16ths, and a small snare fill at the end of every 4th bar. The synths duck a little on each kick (the "pump"). PLAY's LED is on while the beat is stopped and blinks with the beat while it plays. Press PLAY again to stop the beat.
4. **Knobs** (the top line shows the value for 1.5 s, and the BPM is always at the top right):
   - **KNOB 1, tempo:** 80 to 160 BPM, 1 per click. The echo follows the tempo with a short tape-like slide.
   - **KNOB 2, filter:** from dark and muffled to bright and buzzy, 0 to 100 in steps of 4. It sets the filter for both the arp and the bass.
   - **KNOB 3, echo:** a dotted-8th ping-pong, left and then right. Turning it up gives a louder echo with more repeats. At 0 the echo is off.
   - **KNOB 4, drive:** saturates the whole mix. The level is compensated, so the sound gets dirtier but not much louder.

A good first try: switch the beat on, turn the filter down to about 20, then play **A-C-E → F-A-C → C-E-G → G-B-D**, one bar each, and slowly open the filter.

Notes for a real FM-1: it uses floats only, with no malloc and no printf. The biggest buffer is the 49.7 KB echo, which runs at half rate. The screen redraws only the road rows and sun rows that changed, the stars that changed and the text when it changes. Tell me what you want changed, or paste the log if it doesn't compile.
