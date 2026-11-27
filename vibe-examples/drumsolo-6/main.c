#include "fm1.h"
#include <math.h>

/* =========================================================
   FM-1 DRUM STUDIO
   Procedural stereo drums and evolving automatic drum solo
   ========================================================= */

#define SR          44100
#define PI          3.14159265358979323846f
#define NDRUM       12
#define NSINE       1024
#define DLYSIZE     8192
#define TILE_W      60
#define TILE_H      54

#define RGB(r,g,b) ((uint16_t)( \
    (((r) & 248) << 8) | \
    (((g) & 252) << 3) | \
    ((b) >> 3)))

#define BLACK       RGB(8,10,15)
#define PANEL       RGB(19,24,33)
#define WHITE       RGB(235,240,248)
#define GREY        RGB(105,119,139)
#define GOLD        RGB(222,173,67)
#define CYAN        RGB(44,211,225)
#define GREEN       RGB(74,225,129)
#define ORANGE      RGB(255,136,51)
#define RED         RGB(242,71,63)

enum {
    KICK, SNARE, RIM, TOM_H, TOM_M, TOM_L,
    HAT_C, HAT_O, CRASH, RIDE, CLAP, COWBELL
};

enum {
    ROCK, FUNK, METAL, JAZZ
};

/* ---------------------------------------------------------
   Shared controls
   --------------------------------------------------------- */

static volatile int playing = 0;
static volatile int bpm = 118;
static volatile int style = ROCK;
static volatile int tuning = 0;
static volatile int decay = 100;
static volatile int ambience = 28;
static volatile int intensity = 72;
static volatile int swing = 12;
static volatile int fx_on = 1;

static volatile uint32_t fill_request = 0;
static volatile uint32_t key_request[27];
static uint32_t key_seen[27];

static volatile uint32_t sample_clock = 0;
static volatile uint32_t key_until[27];
static volatile uint32_t drum_until[NDRUM];
static volatile uint32_t beat_until = 0;
static volatile int shown_bar = 0;
static volatile int shown_step = 0;
static volatile int shown_fill = 0;

static uint32_t fill_seen = 0;

static const uint8_t key_drum[27] = {
    KICK, RIM, SNARE, CLAP, TOM_L, TOM_M,
    HAT_C, TOM_H, HAT_O, CRASH, RIDE, COWBELL,
    KICK, RIM, SNARE, CLAP, TOM_L, TOM_M,
    HAT_C, TOM_H, HAT_O, CRASH, RIDE, COWBELL,
    KICK, SNARE, CRASH
};

static const uint8_t drum_key[NDRUM] = {
    0, 2, 1, 7, 5, 4, 6, 8, 9, 10, 3, 11
};

static const char *drum_name[NDRUM] = {
    "KICK", "SNARE", "RIM", "HI TOM",
    "MID TOM", "LO TOM", "CH HAT", "OP HAT",
    "CRASH", "RIDE", "CLAP", "COWBELL"
};

static const char *style_name[4] = {
    "ROCK", "FUNK", "METAL", "JAZZ"
};

/* ---------------------------------------------------------
   Utility functions
   --------------------------------------------------------- */

static int clampi(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static uint32_t random_state = 0xA341316Cu;

static uint32_t random_u32(void)
{
    uint32_t x = random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_state = x;
    return x;
}

static float random01(void)
{
    return (float)(random_u32() >> 8)
           * (1.0f / 16777216.0f);
}

static float noise_sample(void)
{
    return random01() * 2.0f - 1.0f;
}

static int chance(int percent)
{
    return (int)(random_u32() % 100u) < percent;
}

static int timer_active(uint32_t until, uint32_t now)
{
    return (int32_t)(until - now) > 0;
}

static float softclip(float x)
{
    float a = x < 0.0f ? -x : x;
    return x / (1.0f + a);
}

/* ---------------------------------------------------------
   Oscillator table

   Sine lookup avoids calling sinf for every audio sample.
   --------------------------------------------------------- */

static float sine_table[NSINE];

static float osc_sine(float p)
{
    int i = (int)(p * (float)NSINE);
    return sine_table[i & (NSINE - 1)];
}

static float advance_phase(float p, float hz)
{
    p += hz * (1.0f / 44100.0f);
    if (p >= 1.0f) p -= 1.0f;
    return p;
}

static float envelope_coef(float seconds)
{
    if (seconds < 0.001f) seconds = 0.001f;
    return expf(-1.0f / (seconds * 44100.0f));
}

/* ---------------------------------------------------------
   Drum synthesis engine
   --------------------------------------------------------- */

typedef struct {
    float env;
    float env2;
    float pitch_env;

    float coef;
    float coef2;
    float pitch_coef;

    float phase1;
    float phase2;
    float phase3;

    float freq;
    float velocity;

    float lp;
    float lp2;

    float pan_l;
    float pan_r;

    uint32_t age;
    int active;
} DrumVoice;

static DrumVoice voice[NDRUM];

static float delay_l[DLYSIZE];
static float delay_r[DLYSIZE];
static uint32_t delay_pos = 0;

static float volume_smooth = 0.0f;

static const float drum_pan[NDRUM] = {
     0.00f, -0.10f, -0.17f,
    -0.28f,  0.15f,  0.58f,
    -0.70f, -0.63f, -0.48f,
     0.75f,  0.20f,  0.38f
};

static const float drum_length[NDRUM] = {
    0.24f, 0.17f, 0.045f,
    0.26f, 0.32f, 0.41f,
    0.038f, 0.31f, 0.95f,
    0.52f, 0.12f, 0.11f
};

static const float drum_freq[NDRUM] = {
    48.0f, 180.0f, 810.0f,
    165.0f, 121.0f, 84.0f,
    4200.0f, 4200.0f, 3200.0f,
    4800.0f, 1000.0f, 562.0f
};

static void hit_drum(int d, float velocity, int key)
{
    DrumVoice *v;
    float length;
    float tune;
    float pan;
    uint32_t now = sample_clock;
    uint32_t duration;

    if (d < 0 || d >= NDRUM) return;

    v = &voice[d];

    length = drum_length[d] * ((float)decay * 0.01f);
    tune = powf(2.0f, (float)tuning / 12.0f);
    pan = drum_pan[d];

    v->active = 1;
    v->age = 0;
    v->env = 1.0f;
    v->env2 = 1.0f;
    v->pitch_env = 1.0f;

    v->coef = envelope_coef(length * 0.25f);
    v->coef2 = envelope_coef(length * 0.10f);
    v->pitch_coef = envelope_coef(0.018f);

    v->phase1 = 0.0f;
    v->phase2 = random01();
    v->phase3 = random01();

    v->freq = drum_freq[d] * tune;
    v->velocity = clampf(velocity, 0.05f, 1.0f);

    v->lp = 0.0f;
    v->lp2 = 0.0f;

    v->pan_l = 0.75f - pan * 0.25f;
    v->pan_r = 0.75f + pan * 0.25f;

    /* Closed hi-hat chokes the open hi-hat. */
    if (d == HAT_C) {
        voice[HAT_O].coef = 0.993f;
        voice[HAT_O].coef2 = 0.985f;
    }

    /* Different acoustic envelope shapes. */
    if (d == KICK) {
        v->pitch_coef = envelope_coef(0.023f);
        v->coef2 = envelope_coef(0.004f);
    }

    if (d == SNARE) {
        v->coef2 = envelope_coef(length * 0.17f);
        v->pitch_coef = envelope_coef(0.009f);
    }

    if (d >= TOM_H && d <= TOM_L) {
        v->pitch_coef = envelope_coef(0.035f);
        v->coef2 = envelope_coef(0.012f);
    }

    if (key < 0) key = drum_key[d];

    duration = (uint32_t)(length * 44100.0f);

    if (duration < 3500u) duration = 3500u;

    key_until[key] = now + duration;

    /* Shorter visual impact flash than the audible decay. */
    drum_until[d] = now + 4200u;
}

static float render_drum(int d, DrumVoice *v, float n)
{
    float s = 0.0f;
    float f;
    float a;
    float b;
    float c;
    float metal;
    float burst;

    if (!v->active) return 0.0f;

    v->age++;

    switch (d) {

    case KICK:
        /* Deep membrane with falling pitch and beater click. */
        f = v->freq + 135.0f * v->pitch_env;

        v->phase1 = advance_phase(v->phase1, f);
        v->phase2 = advance_phase(v->phase2, f * 1.51f);

        a = osc_sine(v->phase1);
        b = osc_sine(v->phase2);

        s = 0.94f * a * v->env
          + 0.13f * b * v->env2
          + 0.11f * n * v->env2;

        s *= 0.98f;
        break;

    case SNARE:
        /* Two resonating membrane modes and snare wires. */
        f = v->freq + 70.0f * v->pitch_env;

        v->phase1 = advance_phase(v->phase1, f);
        v->phase2 = advance_phase(v->phase2, f * 1.63f);

        v->lp += 0.14f * (n - v->lp);
        a = n - v->lp * 0.73f;

        s = 0.67f * a * v->env
          + 0.34f * osc_sine(v->phase1) * v->env2
          + 0.18f * osc_sine(v->phase2) * v->env2;

        s *= 0.83f;
        break;

    case RIM:
        v->phase1 = advance_phase(v->phase1, v->freq);
        v->phase2 = advance_phase(v->phase2,
                                  v->freq * 1.83f);

        s = (0.50f * osc_sine(v->phase1)
           + 0.32f * osc_sine(v->phase2)
           + 0.18f * n) * v->env * 0.65f;
        break;

    case TOM_H:
    case TOM_M:
    case TOM_L:
        f = v->freq * (1.0f + 0.65f * v->pitch_env);

        v->phase1 = advance_phase(v->phase1, f);
        v->phase2 = advance_phase(v->phase2, f * 1.59f);
        v->phase3 = advance_phase(v->phase3, f * 2.14f);

        s = 0.77f * osc_sine(v->phase1) * v->env
          + 0.24f * osc_sine(v->phase2) * v->env
          + 0.09f * osc_sine(v->phase3) * v->env2
          + 0.10f * n * v->env2;

        s *= 0.79f;
        break;

    case HAT_C:
    case HAT_O:
    case CRASH:
    case RIDE:
        /* Inharmonic metallic modes and filtered noise. */
        v->phase1 = advance_phase(v->phase1, v->freq);
        v->phase2 = advance_phase(v->phase2,
                                  v->freq * 1.317f);
        v->phase3 = advance_phase(v->phase3,
                                  v->freq * 1.731f);

        a = osc_sine(v->phase1);
        b = osc_sine(v->phase2);
        c = osc_sine(v->phase3);

        metal = a * b + b * c * 0.6f;

        v->lp += 0.36f * (n - v->lp);
        v->lp2 += 0.13f * (n - v->lp2);

        a = n - v->lp;
        b = n - v->lp2;

        if (d == HAT_C) {
            s = (a * 0.77f + metal * 0.23f)
                * v->env * 0.39f;
        }
        else if (d == HAT_O) {
            s = (a * 0.76f + metal * 0.24f)
                * v->env * 0.37f;
        }
        else if (d == CRASH) {
            s = (b * 0.83f + metal * 0.17f)
                * v->env * 0.48f;
        }
        else {
            s = (metal * 0.49f + b * 0.36f)
                * v->env * 0.34f;

            s += osc_sine(v->phase1)
                 * v->env2 * 0.17f;
        }
        break;

    case CLAP:
        /* Multiple closely spaced handclap bursts. */
        v->lp += 0.09f * (n - v->lp);
        v->lp2 += 0.58f * (n - v->lp2);

        a = v->lp2 - v->lp;

        if (v->age < 1320u) {
            burst = 1.0f -
                    (float)(v->age % 440u) / 440.0f;
            burst *= burst;
        } else {
            burst = v->env * 0.45f;
        }

        s = a * burst * 0.90f;
        break;

    case COWBELL:
        v->phase1 = advance_phase(v->phase1, v->freq);
        v->phase2 = advance_phase(v->phase2,
                                  v->freq * 1.493f);

        a = v->phase1 < 0.5f ? 1.0f : -1.0f;
        b = v->phase2 < 0.5f ? 1.0f : -1.0f;

        v->lp += 0.38f * (a + b - v->lp);

        s = v->lp * v->env * 0.22f;
        break;
    }

    v->env *= v->coef;
    v->env2 *= v->coef2;
    v->pitch_env *= v->pitch_coef;

    if (v->env < 0.0007f) {
        v->active = 0;
    }

    return s * v->velocity;
}

/* ---------------------------------------------------------
   Automatic drummer

   16 steps per bar.
   Grooves, fills, ghost notes and velocity variations.
   --------------------------------------------------------- */

static int seq_step = 0;
static int seq_bar = 0;
static int seq_was_playing = 0;
static int seq_fill = 0;
static int force_fill = 0;
static int samples_to_step = 0;

static int extra_timer = -1;
static int extra_drum = SNARE;
static float extra_velocity = 0.0f;

static void auto_hit(int d, float vel)
{
    float human = 0.88f + random01() * 0.24f;
    float power = 0.74f + (float)intensity * 0.0032f;

    hit_drum(d, vel * human * power, -1);
}

static void extra_hit(int d, float vel, int samples)
{
    extra_drum = d;
    extra_velocity = vel;
    extra_timer = samples;
}

static void sequence_tick(void)
{
    int s = seq_step;
    int current_style = style;
    int energy = intensity;
    int base_interval;
    int swing_amount;
    int fill_tom;

    base_interval = (SR * 60) / (bpm * 4);

    if (fill_seen != fill_request) {
        fill_seen = fill_request;
        force_fill = 1;
    }

    if (s == 0) {
        seq_fill = 0;

        if ((seq_bar & 3) == 3)
            seq_fill = 1;

        if (force_fill) {
            seq_fill = 1;
            force_fill = 0;
        }

        if (seq_bar == 0 || (seq_bar & 3) == 0) {
            auto_hit(CRASH, 0.91f);
        }
    }

    shown_bar = seq_bar;
    shown_step = s;
    shown_fill = seq_fill;

    if ((s & 3) == 0) {
        beat_until = sample_clock + 4200u;
    }

    /* ---------------- ROCK ---------------- */

    if (current_style == ROCK) {

        if ((s & 1) == 0) {
            if (s == 14 && chance(42))
                auto_hit(HAT_O, 0.66f);
            else
                auto_hit(HAT_C,
                         (s & 3) == 0 ? 0.69f : 0.40f);
        }

        if (s == 0 || s == 8 || s == 10)
            auto_hit(KICK, 0.94f);

        if ((s == 3 || s == 14) && chance(energy))
            auto_hit(KICK, 0.75f);

        if (s == 4 || s == 12)
            auto_hit(SNARE, 0.96f);

        if ((s == 7 || s == 15) && chance(energy / 2))
            auto_hit(SNARE, 0.27f);
    }

    /* ---------------- FUNK ---------------- */

    else if (current_style == FUNK) {

        if ((s & 1) == 0 || chance(energy)) {
            if (s == 6 || (s == 14 && chance(65)))
                auto_hit(HAT_O, 0.58f);
            else
                auto_hit(HAT_C,
                         (s & 3) == 0 ? 0.66f : 0.29f);
        }

        if (s == 0 || s == 3 || s == 6 || s == 10)
            auto_hit(KICK, s == 0 ? 1.0f : 0.78f);

        if (s == 4 || s == 12)
            auto_hit(SNARE, 0.96f);

        if ((s == 2 || s == 7 || s == 9 || s == 15)
            && chance(energy + 10))
            auto_hit(SNARE, 0.23f + random01() * 0.15f);

        if (s == 11 && chance(45))
            auto_hit(COWBELL, 0.34f);

        if (s == 13 && chance(35))
            auto_hit(RIM, 0.38f);
    }

    /* ---------------- METAL ---------------- */

    else if (current_style == METAL) {

        if ((s & 1) == 0)
            auto_hit(RIDE, (s & 3) == 0 ? 0.78f : 0.48f);

        if ((s & 1) == 0 || chance(energy))
            auto_hit(KICK, (s & 1) == 0 ? 1.0f : 0.72f);

        if (s == 4 || s == 12)
            auto_hit(SNARE, 1.0f);

        if (s == 7 && chance(energy))
            auto_hit(SNARE, 0.54f);

        if (s == 8 && chance(energy / 2))
            auto_hit(CRASH, 0.82f);

        if (s == 15 && chance(energy))
            extra_hit(KICK, 0.86f, base_interval / 2);
    }

    /* ---------------- JAZZ ---------------- */

    else {

        if ((s & 3) == 0)
            auto_hit(RIDE, 0.68f);

        if (s == 6 || s == 14)
            auto_hit(RIDE, 0.45f);

        if (s == 4 || s == 12)
            auto_hit(HAT_C, 0.58f);

        if (s == 0 || s == 8)
            auto_hit(KICK, 0.35f);

        if (s == 4 || s == 12)
            auto_hit(SNARE, 0.53f);

        if ((s & 1) && chance(energy / 2))
            auto_hit(SNARE, 0.18f + random01() * 0.34f);

        if (s == 10 && chance(energy / 2))
            auto_hit(TOM_H, 0.44f);

        if (s == 15 && chance(50))
            extra_hit(SNARE, 0.24f, base_interval / 2);
    }

    /* ---------------- DRUM FILLS ---------------- */

    if (seq_fill && s >= 8) {

        int fill_type = (seq_bar >> 2) & 3;

        if (fill_type == 0) {

            /* Snare roll into descending toms. */
            if (s < 12)
                auto_hit(SNARE, 0.58f + 0.07f * (s - 8));
            else {
                fill_tom = TOM_H + clampi(s - 12, 0, 2);
                auto_hit(fill_tom, 0.89f);
            }

            if (chance(energy))
                extra_hit(s < 12 ? SNARE : TOM_L,
                          0.47f, base_interval / 2);
        }

        else if (fill_type == 1) {

            /* Alternating snare and toms. */
            if ((s & 1) == 0)
                auto_hit(SNARE, 0.87f);
            else
                auto_hit(TOM_H + (s % 3), 0.91f);

            if (s == 15)
                extra_hit(SNARE, 0.76f, base_interval / 2);
        }

        else if (fill_type == 2) {

            /* Big floor tom and double kick finale. */
            auto_hit(TOM_H + ((s - 8) % 3), 0.95f);

            if (s >= 12)
                auto_hit(KICK, 0.88f);

            if (s == 15)
                extra_hit(KICK, 0.92f, base_interval / 2);
        }

        else {

            /* Rapid snare crescendo. */
            auto_hit(SNARE, 0.42f + 0.08f * (s - 8));

            extra_hit(SNARE,
                      0.29f + 0.07f * (s - 8),
                      base_interval / 2);

            if (s == 15)
                auto_hit(TOM_L, 0.95f);
        }

        if (s == 15)
            auto_hit(KICK, 0.92f);
    }

    /* Alternating long and short sixteenth notes. */
    swing_amount = swing;

    if (current_style == JAZZ && swing_amount < 24)
        swing_amount = 24;

    if ((s & 1) == 0)
        samples_to_step = base_interval
                        + base_interval * swing_amount / 100;
    else
        samples_to_step = base_interval
                        - base_interval * swing_amount / 100;

    seq_step++;

    if (seq_step >= 16) {
        seq_step = 0;
        seq_bar++;

        if (seq_bar >= 1024)
            seq_bar = 0;
    }
}

/* ---------------------------------------------------------
   AUDIO CALLBACK

   No drawing, storage or allocation.
   --------------------------------------------------------- */

void audio(int32_t *out, int frames)
{
    int i;
    int d;
    int k;

    float left;
    float right;
    float n;
    float s;

    float wet_l;
    float wet_r;
    float room;

    float target_volume;

    uint32_t read_l;
    uint32_t read_r;

    target_volume = 1.0f; /* MASTER is the FM-1's own volume (on the speaker): the full level here */

    target_volume *= target_volume;

    room = fx_on ? (float)ambience * 0.0045f : 0.0f;

    /* Consume manual drum requests from loop(). */
    for (k = 0; k < 27; k++) {

        uint32_t req = key_request[k];

        if (req != key_seen[k]) {
            key_seen[k] = req;

            if (k < 12)
                hit_drum(key_drum[k], 0.78f, k);
            else if (k < 24)
                hit_drum(key_drum[k], 1.0f, k);
            else
                hit_drum(key_drum[k], 0.42f, k);
        }
    }

    /* Transport transitions. */
    if (playing && !seq_was_playing) {

        seq_step = 0;
        seq_bar = 0;
        seq_fill = 0;
        samples_to_step = 0;
        extra_timer = -1;

        force_fill = 0;
        fill_seen = fill_request;

        seq_was_playing = 1;
    }

    if (!playing && seq_was_playing) {
        seq_was_playing = 0;
        extra_timer = -1;
        shown_fill = 0;
    }

    for (i = 0; i < frames; i++) {

        sample_clock++;

        if (seq_was_playing) {

            if (samples_to_step <= 0)
                sequence_tick();

            samples_to_step--;

            if (extra_timer >= 0) {
                if (extra_timer == 0)
                    auto_hit(extra_drum, extra_velocity);

                extra_timer--;
            }
        }

        left = 0.0f;
        right = 0.0f;
        n = noise_sample();

        for (d = 0; d < NDRUM; d++) {

            if (!voice[d].active) continue;

            s = render_drum(d, &voice[d], n);

            left += s * voice[d].pan_l;
            right += s * voice[d].pan_r;
        }

        /* Small stereo room with cross-feedback. */
        read_l = (delay_pos - 1613u) & (DLYSIZE - 1u);
        read_r = (delay_pos - 2381u) & (DLYSIZE - 1u);

        wet_l = delay_l[read_l];
        wet_r = delay_r[read_r];

        delay_l[delay_pos] = left + wet_r * 0.23f;
        delay_r[delay_pos] = right + wet_l * 0.23f;

        delay_pos = (delay_pos + 1u) & (DLYSIZE - 1u);

        left += wet_l * room;
        right += wet_r * room;

        /* Smooth master volume to avoid zipper noise. */
        volume_smooth +=
            (target_volume - volume_smooth) * 0.0015f;

        left = softclip(left * 1.45f);
        right = softclip(right * 1.45f);

        left *= volume_smooth * 0.94f;
        right *= volume_smooth * 0.94f;

        left = clampf(left, -0.96f, 0.96f);
        right = clampf(right, -0.96f, 0.96f);

        out[i * 2] =
            (int32_t)(left * (float)FM1_AUDIO_MAX);

        out[i * 2 + 1] =
            (int32_t)(right * (float)FM1_AUDIO_MAX);
    }
}

/* ---------------------------------------------------------
   5 x 7 bitmap font

   Characters A-Z and 0-9.
   --------------------------------------------------------- */

static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, /* A */
    {30,17,17,30,17,17,30}, /* B */
    {14,17,16,16,16,17,14}, /* C */
    {30,17,17,17,17,17,30}, /* D */
    {31,16,16,30,16,16,31}, /* E */
    {31,16,16,30,16,16,16}, /* F */
    {14,17,16,23,17,17,15}, /* G */
    {17,17,17,31,17,17,17}, /* H */
    {31,4,4,4,4,4,31},    /* I */
    {7,2,2,2,18,18,12},   /* J */
    {17,18,20,24,20,18,17},/* K */
    {16,16,16,16,16,16,31},/* L */
    {17,27,21,21,17,17,17},/* M */
    {17,25,21,19,17,17,17},/* N */
    {14,17,17,17,17,17,14},/* O */
    {30,17,17,30,16,16,16},/* P */
    {14,17,17,17,21,18,13},/* Q */
    {30,17,17,30,20,18,17},/* R */
    {15,16,16,14,1,1,30},  /* S */
    {31,4,4,4,4,4,4},     /* T */
    {17,17,17,17,17,17,14},/* U */
    {17,17,17,17,17,10,4}, /* V */
    {17,17,17,21,21,27,17},/* W */
    {17,17,10,4,10,17,17}, /* X */
    {17,17,10,4,4,4,4},   /* Y */
    {31,1,2,4,8,16,31},    /* Z */

    {14,17,19,21,25,17,14},/* 0 */
    {4,12,4,4,4,4,14},    /* 1 */
    {14,17,1,2,4,8,31},   /* 2 */
    {30,1,1,14,1,1,30},   /* 3 */
    {2,6,10,18,31,2,2},   /* 4 */
    {31,16,16,30,1,1,30},  /* 5 */
    {14,16,16,30,17,17,14},/* 6 */
    {31,1,2,4,8,8,8},    /* 7 */
    {14,17,17,14,17,17,14},/* 8 */
    {14,17,17,15,1,1,14}  /* 9 */
};

static uint8_t glyph(char ch, int row)
{
    if (ch >= 'A' && ch <= 'Z')
        return font[ch - 'A'][row];

    if (ch >= '0' && ch <= '9')
        return font[26 + ch - '0'][row];

    if (ch == '-')
        return row == 3 ? 14 : 0;

    if (ch == '+')
        return row == 3 ? 14 :
               (row == 2 || row == 4 ? 4 : 0);

    if (ch == ':')
        return row == 2 || row == 5 ? 4 : 0;

    if (ch == '/')
        return (uint8_t)(1u << clampi(row - 1, 0, 4));

    return 0;
}

/* Shared small graphics buffer, only used in loop/setup. */
static uint16_t gfx[240 * 20];

static void draw_text(int x, int y, const char *str,
                      int scale, uint16_t fg, uint16_t bg)
{
    int len = 0;
    int w;
    int h;
    int xx;
    int yy;
    int ch;
    int bit;

    while (str[len]) len++;

    w = len * 6 * scale;
    h = 8 * scale;

    if (w <= 0 || h <= 0) return;
    if (x < 0 || y < 0) return;
    if (x + w > 240 || y + h > 240) return;
    if (w * h > 240 * 20) return;

    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {

            ch = xx / (6 * scale);
            bit = (xx / scale) % 6;

            if (bit < 5 && yy / scale < 7 &&
                (glyph(str[ch], yy / scale) &
                 (1u << (4 - bit)))) {

                gfx[yy * w + xx] = fg;
            }
            else {
                gfx[yy * w + xx] = bg;
            }
        }
    }

    fm1_lcd_blit(x, y, w, h, gfx, w * h);
}

static void number3(char *s, int n)
{
    n = clampi(n, 0, 999);

    s[0] = (char)('0' + n / 100);
    s[1] = (char)('0' + (n / 10) % 10);
    s[2] = (char)('0' + n % 10);
    s[3] = 0;
}

static void number2(char *s, int n)
{
    n = clampi(n, 0, 99);

    s[0] = (char)('0' + n / 10);
    s[1] = (char)('0' + n % 10);
    s[2] = 0;
}

/* ---------------------------------------------------------
   Drum kit visualization

   Each instrument is drawn into a small rectangular tile.
   Only tiles whose hit state changed are transferred.
   --------------------------------------------------------- */

static const uint8_t kit_layout[12] = {
    CRASH, TOM_H, TOM_M, RIDE,
    HAT_C, SNARE, TOM_L, HAT_O,
    RIM,   KICK,  CLAP,  COWBELL
};

static int drum_tile[NDRUM];

static uint16_t tile[TILE_W * TILE_H];
static int old_hit[NDRUM];

static void pixel(int x, int y, uint16_t c)
{
    if (x >= 0 && x < TILE_W &&
        y >= 0 && y < TILE_H) {

        tile[y * TILE_W + x] = c;
    }
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
    int xx;
    int yy;

    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            pixel(xx, yy, c);
}

static void circle(int cx, int cy, int r, uint16_t c)
{
    int x;
    int y;
    int r2 = r * r;

    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r2)
                pixel(cx + x, cy + y, c);
}

static void ring(int cx, int cy, int r, uint16_t c)
{
    int x;
    int y;
    int q;
    int outer = r * r;
    int inner = (r - 1) * (r - 1);

    for (y = -r; y <= r; y++) {
        for (x = -r; x <= r; x++) {
            q = x * x + y * y;

            if (q <= outer && q >= inner)
                pixel(cx + x, cy + y, c);
        }
    }
}

static void tile_text(int x, int y, const char *s,
                      uint16_t color)
{
    int r;
    int c;

    while (*s) {

        for (r = 0; r < 7; r++) {
            for (c = 0; c < 5; c++) {

                if (glyph(*s, r) & (1u << (4 - c)))
                    pixel(x + c, y + r, color);
            }
        }

        x += 6;
        s++;
    }
}

static void draw_drum(int d, int hit)
{
    int i;
    int pos = drum_tile[d];
    int cx = 30;
    int cy = 21;
    int r = 17;

    int len = 0;
    int cymbal;

    uint16_t body;
    uint16_t rim;
    uint16_t head;
    uint16_t dark;

    for (i = 0; i < TILE_W * TILE_H; i++)
        tile[i] = BLACK;

    cymbal = d == HAT_C || d == HAT_O ||
             d == CRASH || d == RIDE;

    if (cymbal) {

        if (d == CRASH) r = 21;
        if (d == RIDE) r = 20;
        if (d == HAT_C || d == HAT_O) r = 16;

        body = hit ? RGB(255,229,125) : RGB(172,121,40);
        rim = hit ? WHITE : GOLD;
        dark = hit ? GOLD : RGB(104,75,31);

        circle(cx, cy, r, body);

        ring(cx, cy, r, rim);
        ring(cx, cy, r - 4, dark);
        ring(cx, cy, r - 8, dark);

        circle(cx, cy, 5, rim);
        circle(cx, cy, 2, RGB(90,74,45));

        if (d == HAT_O)
            rect(cx - 8, cy + r - 2, 16, 2, ORANGE);

        if (hit) {
            ring(cx, cy, r - 2, WHITE);
            pixel(cx - r - 2, cy, GOLD);
            pixel(cx + r + 2, cy, GOLD);
        }
    }

    else if (d == COWBELL) {

        body = hit ? ORANGE : RGB(115,130,148);

        rect(14, 8, 32, 27, body);
        rect(17, 11, 26, 19, hit ? GOLD : PANEL);
        rect(12, 32, 36, 5, GREY);
        rect(23, 4, 14, 4, GREY);
    }

    else if (d == CLAP) {

        body = hit ? CYAN : RGB(64,103,127);

        circle(22, 23, 12, body);
        circle(38, 23, 12, body);

        ring(22, 23, 12, hit ? WHITE : GREY);
        ring(38, 23, 12, hit ? WHITE : GREY);

        if (hit) {
            rect(29, 3, 2, 7, WHITE);
            rect(29, 37, 2, 5, WHITE);
        }
    }

    else {

        if (d == KICK) r = 21;
        if (d == SNARE) r = 18;
        if (d == RIM) r = 13;
        if (d == TOM_H) r = 14;
        if (d == TOM_M) r = 16;
        if (d == TOM_L) r = 19;

        if (d == KICK)
            body = hit ? ORANGE : RGB(113,44,37);
        else if (d == SNARE || d == RIM)
            body = hit ? CYAN : RGB(66,102,134);
        else
            body = hit ? GREEN : RGB(35,104,101);

        rim = hit ? WHITE : RGB(155,169,184);
        head = hit ? body : RGB(52,65,79);

        circle(cx, cy + 2, r, body);
        circle(cx, cy, r, rim);
        circle(cx, cy, r - 3, head);

        ring(cx, cy, r - 5,
             hit ? WHITE : RGB(84,101,117));

        /* Tension rods. */
        rect(cx - 1, cy - r, 3, 3, GREY);
        rect(cx - 1, cy + r - 2, 3, 3, GREY);
        rect(cx - r, cy - 1, 3, 3, GREY);
        rect(cx + r - 2, cy - 1, 3, 3, GREY);

        if (d == KICK) {
            circle(cx, cy, 8,
                   hit ? GOLD : RGB(22,27,35));

            circle(cx, cy, 3,
                   hit ? WHITE : GREY);
        }

        if (d == SNARE) {
            ring(cx, cy, 7,
                 hit ? WHITE : RGB(115,135,150));
        }

        if (hit && d != KICK) {
            circle(cx, cy, 4, WHITE);
        }
    }

    while (drum_name[d][len]) len++;

    tile_text((TILE_W - len * 6) / 2,
              45, drum_name[d],
              hit ? WHITE : GREY);

    fm1_lcd_blit(
        (pos % 4) * TILE_W,
        42 + (pos / 4) * TILE_H,
        TILE_W,
        TILE_H,
        tile,
        TILE_W * TILE_H
    );
}

/* ---------------------------------------------------------
   Header, transport and parameter display
   --------------------------------------------------------- */

static int old_bpm = -1;
static int old_style = -1;
static int old_playing = -1;
static int old_fill = -1;
static int old_step = -1;

static int parameter = 0;
static int old_parameter = -1;
static int old_value = -999;
static int old_fx = -1;

static const char *parameter_name[7] = {
    "TEMPO", "STYLE", "TUNE", "DECAY",
    "ROOM", "POWER", "SWING"
};

static int parameter_value(void)
{
    switch (parameter) {
    case 0: return bpm;
    case 1: return style;
    case 2: return tuning;
    case 3: return decay;
    case 4: return ambience;
    case 5: return intensity;
    case 6: return swing;
    }

    return 0;
}

static void update_header(void)
{
    char n[4];
    int p = playing;
    int st = style;
    int fill = shown_fill;

    if (old_bpm != bpm) {

        number3(n, bpm);
        draw_text(164, 3, n, 2, CYAN, BLACK);
        draw_text(202, 7, "BPM", 1, GREY, BLACK);

        old_bpm = bpm;
    }

    if (old_style != st) {

        fm1_lcd_fill_rect(4, 22, 46, 9, BLACK);
        draw_text(4, 22, style_name[st], 1,
                  GOLD, BLACK);

        old_style = st;
    }

    if (old_playing != p || old_fill != fill) {

        fm1_lcd_fill_rect(62, 22, 108, 9, BLACK);

        if (!p)
            draw_text(62, 22, "READY", 1, GREY, BLACK);
        else if (fill)
            draw_text(62, 22, "DRUM FILL", 1,
                      ORANGE, BLACK);
        else
            draw_text(62, 22, "SOLO PLAY", 1,
                      GREEN, BLACK);

        old_playing = p;
        old_fill = fill;
    }

    if (old_fx != fx_on) {

        draw_text(195, 22, fx_on ? "FX ON " : "FX OFF",
                  1, fx_on ? CYAN : GREY, BLACK);

        old_fx = fx_on;
    }
}

static void update_steps(void)
{
    int s = playing ? shown_step : -1;
    int i;
    uint16_t c;

    if (old_step == s) return;

    for (i = 0; i < 16; i++) {

        if (old_step >= -1 &&
            i != old_step && i != s)
            continue;

        if (i == s)
            c = shown_fill ? ORANGE : GREEN;
        else if ((i & 3) == 0)
            c = RGB(67,83,103);
        else
            c = RGB(30,40,52);

        fm1_lcd_fill_rect(4 + i * 14, 34, 11, 4, c);
    }

    old_step = s;
}

static void update_parameter(void)
{
    int val = parameter_value();
    int bar = 0;
    char n[5];

    if (old_parameter == parameter &&
        old_value == val)
        return;

    fm1_lcd_fill_rect(0, 207, 240, 33, PANEL);

    draw_text(5, 211, parameter_name[parameter],
              1, GREY, PANEL);

    if (parameter == 1) {

        draw_text(177, 211, style_name[style],
                  1, GOLD, PANEL);

        bar = (style + 1) * 25;
    }

    else if (parameter == 2) {

        n[0] = tuning < 0 ? '-' : '+';

        number2(n + 1, tuning < 0 ? -tuning : tuning);

        draw_text(213, 211, n, 1, WHITE, PANEL);

        bar = (tuning + 12) * 100 / 24;
    }

    else {

        number3(n, val);

        draw_text(213, 211, n, 1, WHITE, PANEL);

        switch (parameter) {
        case 0:
            bar = (bpm - 50) * 100 / 170;
            break;

        case 3:
            bar = (decay - 40) * 100 / 140;
            break;

        case 4:
            bar = ambience;
            break;

        case 5:
            bar = intensity;
            break;

        case 6:
            bar = swing * 100 / 45;
            break;
        }
    }

    bar = clampi(bar, 0, 100);

    fm1_lcd_fill_rect(5, 222, 230, 5,
                      RGB(41,52,66));

    if (bar > 0)
        fm1_lcd_fill_rect(5, 222, bar * 230 / 100,
                          5, CYAN);

    draw_text(5, 231, "PLAY SOLO  SEQ FILL  FX ROOM",
              1, GREY, PANEL);

    old_parameter = parameter;
    old_value = val;
}

/* ---------------------------------------------------------
   LEDs
   --------------------------------------------------------- */

static int led_cache[FM1_NLED];

static void set_led(int id, int level)
{
    if (id < 0 || id >= (int)FM1_NLED) return;

    if (led_cache[id] != level) {

        fm1_led_level((uint32_t)id, level);
        led_cache[id] = level;
    }
}

static void update_leds(void)
{
    int k;
    uint32_t now = sample_clock;

    set_led(FM1_BTN_PLAY,
            !playing ? FM1_LED_ON :
            timer_active(beat_until, now) ?
            FM1_LED_ON : FM1_LED_OFF);

    set_led(FM1_LED_PLAY_RED, FM1_LED_OFF);

    set_led(FM1_BTN_FX,
            fx_on ? FM1_LED_BREATH : FM1_LED_ON);

    set_led(FM1_BTN_SEQ,
            shown_fill ? FM1_LED_BREATH : FM1_LED_ON);

    set_led(FM1_BTN_HOME, FM1_LED_ON);
    set_led(FM1_BTN_OCT_DOWN, FM1_LED_ON);
    set_led(FM1_BTN_OCT_UP, FM1_LED_ON);

    for (k = 0; k < 27; k++) {

        set_led(FM1_KEY_NOTE0 + k,
                timer_active(key_until[k], now) ?
                FM1_LED_ON : FM1_LED_OFF);
    }
}

/* ---------------------------------------------------------
   Controls
   --------------------------------------------------------- */

static void defaults(void)
{
    bpm = 118;
    style = ROCK;
    tuning = 0;
    decay = 100;
    ambience = 28;
    intensity = 72;
    swing = 12;
    fx_on = 1;
    parameter = 0;
}

static void read_controls(void)
{
    uint32_t buttons;
    uint32_t notes;
    int k;
    int delta;

    buttons = fm1_input_edges(0);
    notes = fm1_input_note_edges();

    for (k = 0; k < 27; k++) {

        if (notes & (1u << k))
            key_request[k]++;
    }

    if (buttons & (1u << FM1_BTN_PLAY)) {
        playing = !playing;
    }

    if (buttons & (1u << FM1_BTN_FX)) {
        fx_on = !fx_on;
        parameter = 4;
    }

    if (buttons & (1u << FM1_BTN_SEQ)) {

        if (playing)
            fill_request++;
        else {
            playing = 1;
        }
    }

    if (buttons & (1u << FM1_BTN_HOME)) {

        playing = 0;
        defaults();
    }

    if (buttons & (1u << FM1_BTN_OCT_DOWN)) {
        tuning = clampi(tuning - 1, -12, 12);
        parameter = 2;
    }

    if (buttons & (1u << FM1_BTN_OCT_UP)) {
        tuning = clampi(tuning + 1, -12, 12);
        parameter = 2;
    }

    delta = (int)fm1_enc_take(FM1_ENC_SELECT);

    if (delta) {
        bpm = clampi(bpm + delta, 50, 220);
        parameter = 0;
    }

    delta = (int)fm1_enc_take(FM1_ENC_ALGORITHM);

    if (delta) {
        style = (style + delta) % 4;

        if (style < 0)
            style += 4;

        parameter = 1;
    }

    delta = (int)fm1_enc_take(FM1_ENC_KNOB1);

    if (delta) {
        tuning = clampi(tuning + delta, -12, 12);
        parameter = 2;
    }

    delta = (int)fm1_enc_take(FM1_ENC_KNOB2);

    if (delta) {
        decay = clampi(decay + delta * 4, 40, 180);
        parameter = 3;
    }

    delta = (int)fm1_enc_take(FM1_ENC_KNOB3);

    if (delta) {
        ambience = clampi(ambience + delta * 3, 0, 100);
        parameter = 4;
    }

    delta = (int)fm1_enc_take(FM1_ENC_KNOB4);

    if (delta) {
        intensity = clampi(intensity + delta * 3, 10, 100);
        parameter = 5;
    }

    delta = (int)fm1_enc_take(FM1_ENC_PRESETS);

    if (delta) {
        swing = clampi(swing + delta, 0, 45);
        parameter = 6;
    }
}

/* ---------------------------------------------------------
   SETUP
   --------------------------------------------------------- */

void setup(void)
{
    int i;

    defaults();

    playing = 0;

    for (i = 0; i < NSINE; i++) {

        sine_table[i] =
            sinf(2.0f * PI * (float)i / (float)NSINE);
    }

    for (i = 0; i < NDRUM; i++) {

        voice[i].active = 0;
        old_hit[i] = 0;
        drum_until[i] = 0;

        drum_tile[kit_layout[i]] = i;
    }

    for (i = 0; i < 27; i++) {

        key_request[i] = 0;
        key_seen[i] = 0;
        key_until[i] = 0;
    }

    for (i = 0; i < (int)FM1_NLED; i++) {
        led_cache[i] = -1;
        set_led(i, FM1_LED_OFF);
    }

    fm1_lcd_fill_rect(0, 0, 240, 240, BLACK);

    draw_text(4, 3, "DRUM STUDIO", 2, WHITE, BLACK);

    for (i = 0; i < NDRUM; i++)
        draw_drum(i, 0);

    old_step = -2;

    update_header();
    update_steps();
    update_parameter();
    update_leds();
}

/* ---------------------------------------------------------
   MAIN LOOP

   Draw at most one changed drum tile per iteration.
   --------------------------------------------------------- */

void loop(void)
{
    static int scan_drum = 0;
    static uint32_t last_ui = 0;

    uint32_t now;
    int i;
    int d;
    int hit;

    read_controls();

    now = sample_clock;

    update_leds();

    /* Look for one drum needing a visual update. */
    for (i = 0; i < NDRUM; i++) {

        d = (scan_drum + i) % NDRUM;

        hit = timer_active(drum_until[d], now);

        if (hit != old_hit[d]) {

            draw_drum(d, hit);
            old_hit[d] = hit;

            scan_drum = (d + 1) % NDRUM;
            break;
        }
    }

    /* Limit secondary screen updates to about 30 Hz. */
    if ((uint32_t)(now - last_ui) >= 1470u) {

        last_ui = now;

        update_header();
        update_steps();
        update_parameter();
    }
}
