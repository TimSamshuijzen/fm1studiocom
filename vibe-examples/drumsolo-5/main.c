#include "fm1.h"
#include <math.h>
#include <stdint.h>

/* =========================================================
   FM-1 DRUM SOLO
   Acoustic-inspired stereo drum synthesizer
   ========================================================= */

#define SR          44100
#define PI          3.14159265358979323846f
#define TABLE_SIZE  1024
#define VOICES      20
#define DRUMS       12

#define RGB(r,g,b) ((uint16_t)( \
    (((r) & 248) << 8) | \
    (((g) & 252) << 3) | \
    ((b) >> 3)))

#define BG       RGB(9,14,22)
#define PANEL    RGB(20,29,40)
#define WHITE    RGB(235,243,250)
#define GREY     RGB(115,137,154)
#define DARK     RGB(37,49,62)
#define GOLD     RGB(235,184,72)
#define CYAN     RGB(65,218,238)
#define GREEN    RGB(74,235,139)
#define RED      RGB(255,85,76)

/* =========================================================
   DRUM TYPES
   ========================================================= */

enum {
    KICK,
    SNARE,
    RIM,
    TOM_LOW,
    TOM_MID,
    TOM_HIGH,
    HAT_CLOSED,
    HAT_OPEN,
    RIDE,
    CRASH,
    COWBELL,
    CLAP
};

/* Primary keys are the first 12 note keys. */

static const uint8_t key_drum[27] = {
    KICK, SNARE, RIM,
    TOM_LOW, TOM_MID, TOM_HIGH,
    HAT_CLOSED, HAT_OPEN,
    RIDE, CRASH, COWBELL, CLAP,

    KICK, SNARE, TOM_LOW,
    TOM_MID, TOM_HIGH,
    HAT_CLOSED, HAT_OPEN,
    RIDE, CRASH, COWBELL, CLAP,

    SNARE, SNARE, KICK, CRASH
};

static const uint8_t key_velocity[27] = {
    115,112,100,112,110,108,
    95,105,105,120,105,110,

    78,72,78,78,78,
    65,70,75,80,75,75,

    42,127,127,127
};

/* =========================================================
   CONTROLS
   Written by loop(), read by audio().
   ========================================================= */

static volatile int tempo      = 120;
static volatile int style      = 0;
static volatile int complexity = 65;
static volatile int tuning     = 50;
static volatile int decay      = 60;
static volatile int punch      = 65;
static volatile int room       = 25;

static volatile uint32_t playing = 0;
static volatile uint32_t click_on = 0;
static volatile uint32_t reset_request = 0;
static volatile uint32_t mute_request = 0;

static const char *style_names[4] = {
    "ROCK", "FUNK", "JAZZ", "METAL"
};

/*
   Each manual key has its own event counter.
   loop() is the only writer.
   audio() consumes the events.
*/

static volatile uint32_t key_request[27];
static uint32_t key_consumed[27];

/* Audio publishes these for the display and LEDs. */

static volatile uint32_t sample_clock = 0;
static volatile uint32_t drum_until[DRUMS];
static volatile uint32_t key_until[27];
static volatile uint32_t beat_until = 0;
static volatile uint32_t shown_step = 0;

/* =========================================================
   MATH AND SOUND UTILITIES
   ========================================================= */

static float sine_table[TABLE_SIZE];
static uint32_t rng = 0x9328AB71u;

static int clampi(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float absf_local(float x)
{
    return x < 0.0f ? -x : x;
}

static uint32_t random_u32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int chance(int percent)
{
    return (int)(random_u32() % 100u) < percent;
}

static float noise_sample(void)
{
    return (float)(random_u32() & 65535u)
         * (1.0f / 32768.0f) - 1.0f;
}

static float osc(uint32_t phase)
{
    return sine_table[phase >> 22];
}

static float square_wave(uint32_t phase)
{
    return (phase & 0x80000000u) ? 1.0f : -1.0f;
}

static uint32_t hz_increment(float hz)
{
    return (uint32_t)(hz *
           (4294967296.0f / 44100.0f));
}

static float envelope_rate(float seconds)
{
    if (seconds < 0.001f)
        seconds = 0.001f;

    return expf(-1.0f / (seconds * 44100.0f));
}

static int time_active(uint32_t until, uint32_t now)
{
    return (int32_t)(until - now) > 0;
}

/* =========================================================
   VOICE ENGINE
   ========================================================= */

typedef struct {
    uint8_t active;
    uint8_t drum;

    uint32_t age;
    uint32_t length;

    uint32_t p1;
    uint32_t p2;
    uint32_t p3;

    uint32_t inc1;
    uint32_t inc2;
    uint32_t inc3;

    float amplitude;
    float env;
    float env2;
    float pitch_env;

    float rate;
    float rate2;
    float pitch_rate;
    float pitch_depth;

    float lowpass;
    float noise_prev;
    float noise_filter;

    float pan_l;
    float pan_r;
    float attack;
} Voice;

static Voice voices[VOICES];

static const float drum_pan[DRUMS] = {
     0.00f, -0.12f, -0.18f,
     0.48f,  0.12f, -0.22f,
    -0.62f, -0.62f,  0.72f,
    -0.48f,  0.55f,  0.05f
};

static const uint16_t flash_ms[DRUMS] = {
    130,120,90,180,160,140,
    85,240,260,320,110,120
};

static int allocate_voice(void)
{
    int i;
    int best = 0;
    float quietest = 100.0f;

    for (i = 0; i < VOICES; i++) {
        float level;

        if (!voices[i].active)
            return i;

        level = voices[i].env * voices[i].amplitude;

        if (level < quietest) {
            quietest = level;
            best = i;
        }
    }

    return best;
}

static void hit_drum(int drum, int velocity, int key)
{
    int i;
    int index;
    Voice *v;

    float freq = 180.0f;
    float seconds = 0.12f;
    float tail = 0.05f;
    float pitch_seconds = 0.025f;
    float pitch_depth = 0.0f;
    float gain = 0.5f;
    float ratio2 = 1.51f;
    float ratio3 = 2.14f;

    float tune_factor =
        0.65f + (float)tuning * 0.007f;

    float decay_factor =
        0.45f + (float)decay * 0.014f;

    float pan = drum_pan[drum];

    uint32_t light_time =
        (uint32_t)flash_ms[drum] * 44u;

    /* A closed hi-hat chokes an open hi-hat. */

    if (drum == HAT_CLOSED || drum == HAT_OPEN) {
        for (i = 0; i < VOICES; i++) {
            if (voices[i].active &&
                voices[i].drum == HAT_OPEN) {

                voices[i].rate = 0.994f;
                voices[i].rate2 = 0.990f;
            }
        }

        if (drum == HAT_CLOSED) {
            drum_until[HAT_OPEN] = sample_clock;

            for (i = 0; i < 27; i++) {
                if (key_drum[i] == HAT_OPEN)
                    key_until[i] = sample_clock;
            }
        }
    }

    switch (drum) {
    case KICK:
        freq = 49.0f;
        seconds = 0.095f;
        tail = 0.009f;
        pitch_seconds = 0.028f;
        pitch_depth = 2.9f;
        gain = 0.96f;
        break;

    case SNARE:
        freq = 181.0f;
        seconds = 0.065f;
        tail = 0.095f;
        pitch_seconds = 0.012f;
        pitch_depth = 0.25f;
        gain = 0.76f;
        ratio2 = 1.67f;
        break;

    case RIM:
        freq = 820.0f;
        seconds = 0.018f;
        tail = 0.012f;
        gain = 0.46f;
        ratio2 = 1.59f;
        break;

    case TOM_LOW:
        freq = 83.0f;
        seconds = 0.19f;
        tail = 0.025f;
        pitch_depth = 0.95f;
        pitch_seconds = 0.047f;
        gain = 0.78f;
        break;

    case TOM_MID:
        freq = 119.0f;
        seconds = 0.15f;
        tail = 0.025f;
        pitch_depth = 0.85f;
        pitch_seconds = 0.040f;
        gain = 0.71f;
        break;

    case TOM_HIGH:
        freq = 168.0f;
        seconds = 0.12f;
        tail = 0.020f;
        pitch_depth = 0.75f;
        pitch_seconds = 0.034f;
        gain = 0.65f;
        break;

    case HAT_CLOSED:
        freq = 317.0f;
        seconds = 0.028f;
        tail = 0.012f;
        gain = 0.40f;
        ratio2 = 1.483f;
        ratio3 = 2.137f;
        break;

    case HAT_OPEN:
        freq = 317.0f;
        seconds = 0.23f;
        tail = 0.085f;
        gain = 0.39f;
        ratio2 = 1.483f;
        ratio3 = 2.137f;
        break;

    case RIDE:
        freq = 683.0f;
        seconds = 0.38f;
        tail = 0.12f;
        gain = 0.34f;
        ratio2 = 2.381f;
        ratio3 = 3.743f;
        break;

    case CRASH:
        freq = 413.0f;
        seconds = 0.49f;
        tail = 0.24f;
        gain = 0.43f;
        ratio2 = 1.913f;
        ratio3 = 3.371f;
        break;

    case COWBELL:
        freq = 540.0f;
        seconds = 0.083f;
        tail = 0.028f;
        gain = 0.36f;
        ratio2 = 1.481f;
        break;

    case CLAP:
        freq = 430.0f;
        seconds = 0.065f;
        tail = 0.045f;
        gain = 0.57f;
        break;
    }

    index = allocate_voice();
    v = &voices[index];

    v->active = 1;
    v->drum = (uint8_t)drum;
    v->age = 0;

    v->length = (uint32_t)(
        (seconds + tail) * decay_factor *
        44100.0f * 6.0f);

    v->p1 = 0;
    v->p2 = random_u32();
    v->p3 = random_u32();

    freq *= tune_factor;

    v->inc1 = hz_increment(freq);
    v->inc2 = hz_increment(freq * ratio2);
    v->inc3 = hz_increment(freq * ratio3);

    v->amplitude =
        gain * (float)velocity / 127.0f;

    v->env = 1.0f;
    v->env2 = 1.0f;
    v->pitch_env = 1.0f;

    v->rate = envelope_rate(seconds * decay_factor);
    v->rate2 = envelope_rate(tail * decay_factor);
    v->pitch_rate = envelope_rate(pitch_seconds);
    v->pitch_depth = pitch_depth;

    v->lowpass = 0.0f;
    v->noise_prev = 0.0f;
    v->noise_filter = 0.0f;

    v->pan_l = 0.72f - pan * 0.28f;
    v->pan_r = 0.72f + pan * 0.28f;

    v->attack = 0.25f + (float)punch * 0.0075f;

    drum_until[drum] = sample_clock + light_time;

    /*
       Use the actual manual key for manual hits.
       Automatic hits light the primary drum key.
    */

    if (key < 0)
        key = drum;

    key_until[key] = sample_clock + light_time;
}

static float render_voice(Voice *v, float noise)
{
    float s = 0.0f;
    float high;
    float band;
    float a;
    float b;
    float c;

    v->lowpass += 0.18f * (noise - v->lowpass);

    high = noise - v->lowpass;

    v->noise_filter +=
        0.22f * (high - v->noise_filter);

    band = v->noise_filter;

    v->p1 += v->inc1 +
        (uint32_t)((float)v->inc1 *
        v->pitch_env * v->pitch_depth);

    v->p2 += v->inc2;
    v->p3 += v->inc3;

    a = osc(v->p1);
    b = osc(v->p2);
    c = osc(v->p3);

    switch (v->drum) {
    case KICK:
        s = a * v->env * 0.94f
          + high * v->env2 * v->attack * 0.32f
          + b * v->env2 * 0.12f;
        break;

    case SNARE:
        s = (a * 0.46f + b * 0.19f) * v->env
          + (high * 0.74f + band * 0.55f)
            * v->env2
          + noise * v->pitch_env
            * 0.14f * v->attack;
        break;

    case RIM:
        s = (a * 0.52f + b * 0.40f) * v->env
          + high * v->env2 * 0.40f;
        break;

    case TOM_LOW:
    case TOM_MID:
    case TOM_HIGH:
        s = (a * 0.86f
          + b * 0.15f
          + c * 0.06f) * v->env
          + high * v->env2
            * v->attack * 0.27f;
        break;

    case HAT_CLOSED:
    case HAT_OPEN:
    {
        float metal =
            square_wave(v->p1) *
            square_wave(v->p2);

        metal += 0.6f *
            square_wave(v->p3) *
            square_wave(v->p1 + v->p2);

        s = (high * 0.78f + metal * 0.20f)
            * v->env
          + band * v->env2 * 0.42f;
        break;
    }

    case RIDE:
        s = (a * 0.29f + b * 0.22f
          + c * 0.14f) * v->env
          + high * v->env2 * 0.63f
          + a * b * v->env2 * 0.35f;
        break;

    case CRASH:
    {
        float metal =
            square_wave(v->p1) *
            square_wave(v->p2) *
            square_wave(v->p3);

        s = (high * 0.88f
          + metal * 0.16f
          + a * b * 0.17f) * v->env
          + band * v->env2 * 0.40f;
        break;
    }

    case COWBELL:
        s = (square_wave(v->p1) * 0.43f
          + square_wave(v->p2) * 0.36f)
            * v->env
          + high * v->env2 * 0.12f;
        break;

    case CLAP:
    {
        float burst = 0.18f;
        uint32_t t = v->age;

        if (t < 140u ||
            (t > 440u && t < 620u) ||
            (t > 880u && t < 1090u) ||
            t > 1320u) {
            burst = 1.0f;
        }

        s = (band * 1.75f + high * 0.35f)
          * v->env * burst
          + high * v->env2 * 0.23f;
        break;
    }
    }

    v->env *= v->rate;
    v->env2 *= v->rate2;
    v->pitch_env *= v->pitch_rate;
    v->age++;

    if (v->age >= v->length ||
        (v->env < 0.001f && v->env2 < 0.001f)) {
        v->active = 0;
    }

    return s * v->amplitude;
}

/* =========================================================
   AUTOMATIC DRUM SOLO
   16 steps per bar, evolving 4-bar phrases
   ========================================================= */

static int seq_step = 0;
static uint32_t seq_bar = 0;
static int32_t samples_to_step = 0;

static uint32_t seen_reset = 0;
static uint32_t seen_mute = 0;
static int previous_playing = 0;

static int roll_delay = -1;
static int roll_drum = SNARE;
static int roll_velocity = 70;

static float click_env = 0.0f;
static uint32_t click_phase = 0;
static uint32_t click_increment = 0;

static void auto_hit(int drum, int velocity)
{
    int human = (int)(random_u32() % 15u) - 7;

    hit_drum(drum,
             clampi(velocity + human, 15, 127),
             -1);
}

static void schedule_roll(int drum, int velocity,
                          int delay_samples)
{
    roll_drum = drum;
    roll_velocity = velocity;
    roll_delay = delay_samples;
}

static void drum_step(int step_samples)
{
    int s = seq_step;
    int cpx = complexity;
    int st = style;

    int fill_bar = ((seq_bar & 3u) == 3u);
    int filling = fill_bar && s >= 8;

    shown_step = (uint32_t)s;

    if ((s & 3) == 0) {
        beat_until = sample_clock + 4410u;

        if (click_on) {
            click_env = 1.0f;
            click_phase = 0;

            click_increment = hz_increment(
                s == 0 ? 1600.0f : 1050.0f);
        }
    }

    if (s == 0 && (seq_bar & 3u) == 0u)
        auto_hit(CRASH, 105);

    if (filling) {
        static const uint8_t fill_drums[8] = {
            SNARE, SNARE, TOM_HIGH, SNARE,
            TOM_MID, TOM_LOW, TOM_LOW, SNARE
        };

        int drum = fill_drums[s - 8];

        auto_hit(drum, (s & 1) ? 79 : 113);

        if (s == 8 || s == 12 || s == 14)
            auto_hit(KICK, 106);

        if (s == 15)
            auto_hit(HAT_OPEN, 92);

        if (chance(cpx / 2)) {
            schedule_roll(
                drum,
                60 + cpx / 3,
                step_samples / 2);
        }
    }
    else {
        switch (st) {
        case 0: /* ROCK */

            if (s == 0 || s == 8 || s == 10 ||
                (s == 6 && chance(55)) ||
                (s == 14 && chance(cpx))) {
                auto_hit(KICK, 114);
            }

            if (s == 4 || s == 12)
                auto_hit(SNARE, 115);

            if ((s & 1) == 0)
                auto_hit(HAT_CLOSED,
                         (s & 3) == 0 ? 94 : 65);

            if (s == 15 && chance(cpx))
                auto_hit(HAT_OPEN, 98);

            if ((s == 3 || s == 11) &&
                chance(cpx / 2))
                auto_hit(SNARE, 43);

            break;

        case 1: /* FUNK */

            if (s == 0 || s == 3 || s == 10 ||
                s == 14 ||
                (s == 7 && chance(55)))
                auto_hit(KICK, 113);

            if (s == 4 || s == 12)
                auto_hit(SNARE, 116);

            if (s == 2 || s == 6 || s == 9 ||
                s == 11 || s == 15) {
                if (chance(25 + cpx / 2))
                    auto_hit(SNARE, 42);
            }

            if ((s & 1) == 0 || chance(cpx / 2))
                auto_hit(
                    HAT_CLOSED,
                    (s & 3) == 0 ? 91 : 53);

            if (s == 7 || s == 15)
                auto_hit(HAT_OPEN, 88);

            if (s == 6 && chance(cpx / 2))
                auto_hit(COWBELL, 74);

            break;

        case 2: /* JAZZ */

            if (s == 0 || s == 4 ||
                s == 7 || s == 8 ||
                s == 12 || s == 15) {
                auto_hit(RIDE,
                         (s & 3) == 0 ? 90 : 67);
            }

            if (s == 4 || s == 12)
                auto_hit(HAT_CLOSED, 70);

            if (s == 0 || s == 8 ||
                (s == 14 && chance(50)))
                auto_hit(KICK, 75);

            if (s == 4 || s == 12 ||
                chance(cpx / 3)) {
                auto_hit(
                    SNARE,
                    (s == 4 || s == 12) ? 89 : 46);
            }

            if (s == 10 && chance(cpx))
                auto_hit(TOM_HIGH, 81);

            break;

        case 3: /* METAL */

            if ((s & 1) == 0 ||
                chance(35 + cpx / 2)) {
                auto_hit(KICK,
                         (s & 1) ? 96 : 120);
            }

            if (s == 4 || s == 12)
                auto_hit(SNARE, 123);

            if ((s & 1) == 0)
                auto_hit(RIDE,
                         (s & 3) ? 77 : 108);

            if (s == 8 && chance(cpx))
                auto_hit(CRASH, 109);

            if (s == 15 && chance(cpx))
                schedule_roll(KICK, 112,
                              step_samples / 2);
            break;
        }

        /* Solo embellishments in every style. */

        if ((seq_bar & 3u) == 1u &&
            (s == 13 || s == 15) &&
            chance(cpx)) {

            auto_hit(TOM_HIGH +
                     0, 88);
        }

        if ((seq_bar & 3u) == 2u &&
            s >= 12 && chance(cpx)) {

            auto_hit(TOM_LOW +
                     (int)(random_u32() % 3u),
                     89 + cpx / 5);
        }

        if ((s == 7 || s == 15) &&
            chance(cpx / 3) &&
            st != 3) {

            schedule_roll(SNARE, 65,
                          step_samples / 2);
        }
    }

    seq_step++;

    if (seq_step >= 16) {
        seq_step = 0;
        seq_bar++;
    }
}

/* =========================================================
   STEREO ROOM AMBIENCE
   Small cross-fed delay network
   ========================================================= */

#define DELAY_L 1499
#define DELAY_R 2137

static float delay_l[DELAY_L];
static float delay_r[DELAY_R];

static int delay_pos_l = 0;
static int delay_pos_r = 0;

static float master_smooth = 0.0f;

static void clear_sound(void)
{
    int i;

    for (i = 0; i < VOICES; i++)
        voices[i].active = 0;

    for (i = 0; i < DELAY_L; i++)
        delay_l[i] = 0.0f;

    for (i = 0; i < DELAY_R; i++)
        delay_r[i] = 0.0f;

    for (i = 0; i < DRUMS; i++)
        drum_until[i] = sample_clock;

    for (i = 0; i < 27; i++)
        key_until[i] = sample_clock;

    roll_delay = -1;
    click_env = 0.0f;
}

static float soft_limit(float x)
{
    return x / (1.0f + 0.75f * absf_local(x));
}

/* =========================================================
   AUDIO INTERRUPT
   ========================================================= */

void audio(int32_t *out, int frames)
{
    int f;
    int i;
    int run = playing != 0;

    int step_samples =
        (SR * 60) / (tempo * 4);

    float swing = style == 2 ? 0.28f :
                  style == 1 ? 0.10f : 0.0f;

    float wet = (float)room * 0.0045f;

    float master_target = 1.0f; /* MASTER is the FM-1's own volume (on the speaker): the full level here */

    master_target *= master_target;

    if (seen_mute != mute_request) {
        seen_mute = mute_request;
        clear_sound();
    }

    if (seen_reset != reset_request ||
        (run && !previous_playing)) {

        seen_reset = reset_request;
        seq_step = 0;
        seq_bar = 0;
        samples_to_step = 0;
        shown_step = 0;
        roll_delay = -1;
    }

    if (!run) {
        roll_delay = -1;
        click_env = 0.0f;
    }

    previous_playing = run;

    /* Manual drum triggers. */

    for (i = 0; i < 27; i++) {
        uint32_t request = key_request[i];

        if (request != key_consumed[i]) {
            key_consumed[i] = request;

            hit_drum(key_drum[i],
                     key_velocity[i], i);
        }
    }

    for (f = 0; f < frames; f++) {
        float l = 0.0f;
        float r = 0.0f;
        float n;
        float dl;
        float dr;

        sample_clock++;

        if (run) {
            if (samples_to_step <= 0) {
                int old_step = seq_step;

                drum_step(step_samples);

                if (old_step & 1) {
                    samples_to_step +=
                        (int32_t)((float)step_samples *
                                  (1.0f - swing));
                }
                else {
                    samples_to_step +=
                        (int32_t)((float)step_samples *
                                  (1.0f + swing));
                }
            }

            samples_to_step--;

            if (roll_delay == 0) {
                auto_hit(roll_drum, roll_velocity);
                roll_delay = -1;
            }
            else if (roll_delay > 0) {
                roll_delay--;
            }
        }

        n = noise_sample();

        for (i = 0; i < VOICES; i++) {
            Voice *v = &voices[i];

            if (v->active) {
                float s = render_voice(v, n);

                l += s * v->pan_l;
                r += s * v->pan_r;
            }
        }

        if (click_env > 0.001f) {
            float tick;

            click_phase += click_increment;

            tick = osc(click_phase) *
                   click_env * 0.17f;

            l += tick;
            r += tick;

            click_env *= 0.996f;
        }

        /* Stereo ambience. */

        dl = delay_l[delay_pos_l];
        dr = delay_r[delay_pos_r];

        delay_l[delay_pos_l] =
            l * 0.28f + dr * 0.35f;

        delay_r[delay_pos_r] =
            r * 0.28f + dl * 0.35f;

        delay_pos_l++;

        if (delay_pos_l >= DELAY_L)
            delay_pos_l = 0;

        delay_pos_r++;

        if (delay_pos_r >= DELAY_R)
            delay_pos_r = 0;

        l += dl * wet;
        r += dr * wet;

        master_smooth +=
            (master_target - master_smooth) * 0.002f;

        l = soft_limit(l * 0.83f);
        r = soft_limit(r * 0.83f);

        l *= master_smooth * 0.74f;
        r *= master_smooth * 0.74f;

        if (l > 0.98f) l = 0.98f;
        if (l < -0.98f) l = -0.98f;
        if (r > 0.98f) r = 0.98f;
        if (r < -0.98f) r = -0.98f;

        out[f * 2] =
            (int32_t)(l * (float)FM1_AUDIO_MAX);

        out[f * 2 + 1] =
            (int32_t)(r * (float)FM1_AUDIO_MAX);
    }
}

/* =========================================================
   SMALL BITMAP FONT
   5 x 7 pixels, uppercase and numbers
   ========================================================= */

static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, /* A */
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},
    {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},
    {17,17,17,21,21,27,17},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31}, /* Z */

    {14,17,19,21,25,17,14}, /* 0 */
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

/*
   Small reusable graphics surface.
   Maximum area: 240 x 40.
   No full-screen framebuffer.
*/

static uint16_t pixels[9600];

static int canvas_w;
static int canvas_h;

static void canvas_begin(int w, int h, uint16_t color)
{
    int i;

    canvas_w = w;
    canvas_h = h;

    for (i = 0; i < w * h; i++)
        pixels[i] = color;
}

static void dot(int x, int y, uint16_t color)
{
    if (x >= 0 && x < canvas_w &&
        y >= 0 && y < canvas_h) {
        pixels[y * canvas_w + x] = color;
    }
}

static void box_fill(int x, int y, int w, int h,
                     uint16_t color)
{
    int xx;
    int yy;

    for (yy = y; yy < y + h; yy++) {
        for (xx = x; xx < x + w; xx++)
            dot(xx, yy, color);
    }
}

static void disc(int cx, int cy, int radius,
                 uint16_t color)
{
    int x;
    int y;

    for (y = -radius; y <= radius; y++) {
        for (x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius)
                dot(cx+x, cy+y, color);
        }
    }
}

static int text_width(const char *s, int scale)
{
    int n = 0;

    while (*s++)
        n++;

    return n ? n * 6 * scale - scale : 0;
}

static void text_draw(int x, int y, const char *s,
                      int scale, uint16_t color)
{
    while (*s) {
        int index = -1;
        int row;
        int col;

        char ch = *s++;

        if (ch >= 'A' && ch <= 'Z')
            index = ch - 'A';
        else if (ch >= '0' && ch <= '9')
            index = 26 + ch - '0';

        if (index >= 0) {
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (font[index][row] &
                        (1u << (4-col))) {

                        box_fill(
                            x + col * scale,
                            y + row * scale,
                            scale, scale, color);
                    }
                }
            }
        }
        else if (ch == '-') {
            box_fill(x, y + 3 * scale,
                     4 * scale, scale, color);
        }
        else if (ch == ':') {
            box_fill(x + 2*scale, y + 2*scale,
                     scale, scale, color);

            box_fill(x + 2*scale, y + 5*scale,
                     scale, scale, color);
        }

        x += 6 * scale;
    }
}

static void centered_text(int y, const char *s,
                          int scale, uint16_t color)
{
    text_draw(
        (canvas_w - text_width(s, scale)) / 2,
        y, s, scale, color);
}

static void number_text(char *s, int value)
{
    int pos = 0;

    value = clampi(value, 0, 999);

    if (value >= 100)
        s[pos++] = '0' + value / 100;

    if (value >= 10)
        s[pos++] = '0' + (value / 10) % 10;

    s[pos++] = '0' + value % 10;
    s[pos] = 0;
}

static void canvas_send(int x, int y)
{
    fm1_lcd_blit(
        (uint32_t)x,
        (uint32_t)y,
        (uint32_t)canvas_w,
        (uint32_t)canvas_h,
        pixels,
        (uint32_t)(canvas_w * canvas_h));
}

/* =========================================================
   DRUM KIT GRAPHICS
   Independent non-overlapping drawing regions.
   ========================================================= */

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
    uint8_t radius;
    const char *label;
} DrumGraphic;

static const DrumGraphic graphics[DRUMS] = {
    { 88,128,64,60,25,"KICK"},
    { 18,125,66,52,21,"SNARE"},
    {  2,181,48,27, 9,"RIM"},
    {166,121,70,61,26,"LOW"},
    {119, 70,57,52,21,"MID"},
    { 61, 70,57,52,20,"HIGH"},
    {  0, 70,59,53,21,"HAT"},
    {  0, 32,58,37,14,"OPEN"},
    {180, 70,60,50,20,"RIDE"},
    { 61, 32,69,37,15,"CRASH"},
    {176, 32,64,37,13,"BELL"},
    { 55,181,48,27, 9,"CLAP"}
};

static int8_t graphic_state[DRUMS];

static void draw_drum(int drum, int active)
{
    const DrumGraphic *g = &graphics[drum];

    int cx = g->w / 2;
    int cy = g->radius + 2;
    int rad = g->radius;
    int cymbal = drum >= HAT_CLOSED &&
                 drum <= CRASH;

    uint16_t edge;
    uint16_t body;
    uint16_t inside;
    uint16_t label;

    canvas_begin(g->w, g->h, BG);

    if (active) {
        edge = WHITE;
        body = cymbal ? GOLD : CYAN;
        inside = cymbal ?
            RGB(255,222,122) :
            RGB(134,242,255);

        label = WHITE;
    }
    else if (cymbal || drum == COWBELL) {
        edge = RGB(142,106,41);
        body = RGB(111,81,32);
        inside = RGB(172,132,53);
        label = GOLD;
    }
    else if (drum == KICK) {
        edge = RGB(91,126,154);
        body = RGB(37,53,68);
        inside = RGB(21,32,44);
        label = GREY;
    }
    else {
        edge = RGB(105,132,151);
        body = RGB(48,71,86);
        inside = RGB(76,101,117);
        label = GREY;
    }

    if (drum == COWBELL || drum == CLAP) {
        int w = rad * 2;

        box_fill(cx - rad, 3,
                 w, rad + 2, edge);

        box_fill(cx - rad + 2, 5,
                 w - 4, rad - 2, body);

        box_fill(cx - rad + 4, 6,
                 w - 8, 3, inside);
    }
    else {
        disc(cx, cy, rad, edge);
        disc(cx, cy, rad - 2, body);
        disc(cx, cy, rad - 5, inside);

        if (cymbal) {
            disc(cx, cy, rad - 8, body);

            if (rad > 12) {
                disc(cx, cy, rad - 10, inside);
            }

            disc(cx, cy, 5, edge);
            disc(cx, cy, 2, WHITE);
        }
        else if (drum == KICK) {
            disc(cx, cy, rad - 10, body);
            disc(cx, cy, 9, inside);
            disc(cx, cy, 6, edge);

            box_fill(cx - 4, cy + rad - 6,
                     8, 7, edge);
        }
        else {
            int k;

            /* Drum tuning lugs. */

            for (k = 0; k < 6; k++) {
                uint32_t p =
                    (uint32_t)k * 715827883u;

                int xx = cx + (int)(
                    osc(p + 0x40000000u) *
                    (float)(rad - 2));

                int yy = cy + (int)(
                    osc(p) * (float)(rad - 2));

                box_fill(xx - 1, yy - 1,
                         3, 3, edge);
            }

            if (active)
                disc(cx, cy, 4, WHITE);
        }
    }

    centered_text(g->h - 7,
                  g->label, 1, label);

    canvas_send(g->x, g->y);

    graphic_state[drum] = (int8_t)active;
}

/* =========================================================
   HEADER, STATUS AND BEAT DISPLAY
   ========================================================= */

static int header_style = -1;
static int header_tempo = -1;
static int header_run = -1;
static int header_click = -1;
static int beat_drawn = -1;

enum {
    PARAM_TEMPO,
    PARAM_STYLE,
    PARAM_COMPLEXITY,
    PARAM_TUNING,
    PARAM_DECAY,
    PARAM_PUNCH,
    PARAM_ROOM
};

static int selected_parameter = PARAM_COMPLEXITY;
static int footer_parameter = -1;
static int footer_value = -1;

static const char *parameter_names[7] = {
    "TEMPO", "STYLE", "COMPLEXITY",
    "TUNING", "DECAY", "PUNCH", "ROOM"
};

static int parameter_value(void)
{
    switch (selected_parameter) {
    case PARAM_TEMPO: return tempo;
    case PARAM_STYLE: return style;
    case PARAM_COMPLEXITY: return complexity;
    case PARAM_TUNING: return tuning;
    case PARAM_DECAY: return decay;
    case PARAM_PUNCH: return punch;
    default: return room;
    }
}

static void draw_header(void)
{
    char num[4];

    canvas_begin(240, 30, PANEL);

    text_draw(7, 5, "DRUM SOLO", 2, WHITE);

    if (playing)
        text_draw(180, 5, "LIVE", 2, GREEN);
    else
        text_draw(174, 5, "READY", 1, GREY);

    text_draw(7, 21, style_names[style], 1, GOLD);

    number_text(num, tempo);

    text_draw(79, 21, num, 1, WHITE);
    text_draw(104, 21, "BPM", 1, GREY);

    if (click_on)
        text_draw(160, 21, "CLICK ON", 1, GREEN);
    else
        text_draw(154, 21, "CLICK OFF", 1, GREY);

    canvas_send(0, 0);

    header_style = style;
    header_tempo = tempo;
    header_run = (int)playing;
    header_click = (int)click_on;
}

static void draw_beats(int step)
{
    int i;

    canvas_begin(126, 21, BG);

    for (i = 0; i < 16; i++) {
        uint16_t col = DARK;

        if ((i & 3) == 0)
            col = RGB(71,88,103);

        if (i == step)
            col = (i & 3) == 0 ? GOLD : CYAN;

        box_fill(i * 8, 2, 6, 10, col);
    }

    text_draw(0, 14, "1", 1, GREY);
    text_draw(32, 14, "2", 1, GREY);
    text_draw(64, 14, "3", 1, GREY);
    text_draw(96, 14, "4", 1, GREY);

    canvas_send(113, 186);

    beat_drawn = step;
}

static void draw_footer(void)
{
    char num[4];

    int value = parameter_value();
    int bar = value;

    canvas_begin(240, 30, PANEL);

    text_draw(6, 4,
              parameter_names[selected_parameter],
              1, WHITE);

    if (selected_parameter == PARAM_STYLE) {
        text_draw(162, 4,
                  style_names[style], 1, GOLD);

        bar = style * 100 / 3;
    }
    else {
        number_text(num, value);
        text_draw(210, 4, num, 1, CYAN);

        if (selected_parameter == PARAM_TEMPO)
            bar = (tempo - 60) * 100 / 140;
    }

    bar = clampi(bar, 0, 100);

    box_fill(6, 15, 228, 8, DARK);
    box_fill(6, 15, bar * 228 / 100, 8, CYAN);

    canvas_send(0, 210);

    footer_parameter = selected_parameter;
    footer_value = value;
}

/* =========================================================
   LED MANAGEMENT
   ========================================================= */

static int8_t led_cache[FM1_NLED];

static void set_led(uint32_t id, int level)
{
    if (led_cache[id] != level) {
        fm1_led_level(id, level);
        led_cache[id] = (int8_t)level;
    }
}

static void update_leds(void)
{
    int i;
    uint32_t now = sample_clock;

    for (i = 0; i < 14; i++) {
        int level = FM1_LED_OFF;

        switch (i) {
        case FM1_BTN_PLAY:
            if (!playing)
                level = FM1_LED_ON;
            else
                level = time_active(beat_until, now) ?
                        FM1_LED_ON : FM1_LED_OFF;
            break;

        case FM1_BTN_OCT_DOWN:
        case FM1_BTN_OCT_UP:
        case FM1_BTN_FX:
        case FM1_BTN_ENV:
        case FM1_BTN_HOME:
        case FM1_BTN_SEQ:
            level = FM1_LED_ON;
            break;

        case FM1_BTN_ARP:
            level = click_on ?
                    FM1_LED_BREATH : FM1_LED_ON;
            break;
        }

        set_led((uint32_t)i, level);
    }

    set_led(FM1_LED_PLAY_RED, FM1_LED_OFF);

    for (i = 0; i < 27; i++) {
        set_led(
            FM1_KEY_NOTE0 + (uint32_t)i,
            time_active(key_until[i], now) ?
            FM1_LED_ON : FM1_LED_OFF);
    }
}

/* =========================================================
   INPUT
   ========================================================= */

static void change_style(int amount)
{
    int v = (style + amount) % 4;

    if (v < 0)
        v += 4;

    style = v;
    selected_parameter = PARAM_STYLE;
}

static void read_controls(void)
{
    uint32_t buttons = fm1_input_edges(0);
    uint32_t notes = fm1_input_note_edges();

    int i;
    int32_t d;

    for (i = 0; i < 27; i++) {
        if (notes & (1u << i))
            key_request[i]++;
    }

    if (buttons & (1u << FM1_BTN_PLAY))
        playing = !playing;

    if (buttons & (1u << FM1_BTN_OCT_DOWN))
        change_style(-1);

    if (buttons & (1u << FM1_BTN_OCT_UP))
        change_style(1);

    if (buttons & (1u << FM1_BTN_ARP))
        click_on = !click_on;

    if (buttons & (1u << FM1_BTN_SEQ))
        reset_request++;

    if (buttons & (1u << FM1_BTN_HOME)) {
        playing = 0;
        mute_request++;
        reset_request++;
    }

    if (buttons & (1u << FM1_BTN_FX)) {
        room = room == 0 ? 35 : 0;
        selected_parameter = PARAM_ROOM;
    }

    if (buttons & (1u << FM1_BTN_ENV)) {
        decay = decay >= 70 ? 35 : 85;
        selected_parameter = PARAM_DECAY;
    }

    d = fm1_enc_take(FM1_ENC_SELECT);

    if (d) {
        tempo = clampi(tempo + d, 60, 200);
        selected_parameter = PARAM_TEMPO;
    }

    d = fm1_enc_take(FM1_ENC_ALGORITHM);

    if (d)
        change_style(d);

    d = fm1_enc_take(FM1_ENC_KNOB1);

    if (d) {
        complexity = clampi(complexity+d*2, 0,100);
        selected_parameter = PARAM_COMPLEXITY;
    }

    d = fm1_enc_take(FM1_ENC_KNOB2);

    if (d) {
        tuning = clampi(tuning+d*2, 0,100);
        selected_parameter = PARAM_TUNING;
    }

    d = fm1_enc_take(FM1_ENC_KNOB3);

    if (d) {
        decay = clampi(decay+d*2, 0,100);
        selected_parameter = PARAM_DECAY;
    }

    d = fm1_enc_take(FM1_ENC_KNOB4);

    if (d) {
        punch = clampi(punch+d*2, 0,100);
        selected_parameter = PARAM_PUNCH;
    }

    d = fm1_enc_take(FM1_ENC_PRESETS);

    if (d) {
        room = clampi(room+d*2, 0,100);
        selected_parameter = PARAM_ROOM;
    }
}

/* =========================================================
   SETUP
   ========================================================= */

void setup(void)
{
    int i;

    for (i = 0; i < TABLE_SIZE; i++) {
        sine_table[i] = sinf(
            2.0f * PI * (float)i /
            (float)TABLE_SIZE);
    }

    for (i = 0; i < FM1_NLED; i++)
        led_cache[i] = -1;

    for (i = 0; i < DRUMS; i++)
        graphic_state[i] = -1;

    fm1_lcd_fill_rect(0, 0, 240, 240, BG);

    draw_header();

    for (i = 0; i < DRUMS; i++)
        draw_drum(i, 0);

    draw_beats(-1);
    draw_footer();

    update_leds();
}

/* =========================================================
   MAIN LOOP
   At most one screen region is transferred per call.
   ========================================================= */

void loop(void)
{
    int i;
    int step;
    uint32_t now;

    read_controls();
    update_leds();

    now = sample_clock;

    if (header_style != style ||
        header_tempo != tempo ||
        header_run != (int)playing ||
        header_click != (int)click_on) {

        draw_header();
        return;
    }

    if (footer_parameter != selected_parameter ||
        footer_value != parameter_value()) {

        draw_footer();
        return;
    }

    /* Prioritize newly struck drums. */

    for (i = 0; i < DRUMS; i++) {
        int active =
            time_active(drum_until[i], now);

        if (active && graphic_state[i] != 1) {
            draw_drum(i, 1);
            return;
        }
    }

    /* Return drums to their normal appearance. */

    for (i = 0; i < DRUMS; i++) {
        int active =
            time_active(drum_until[i], now);

        if (!active && graphic_state[i] != 0) {
            draw_drum(i, 0);
            return;
        }
    }

    step = playing ? (int)shown_step : -1;

    if (step != beat_drawn) {
        draw_beats(step);
        return;
    }
}
