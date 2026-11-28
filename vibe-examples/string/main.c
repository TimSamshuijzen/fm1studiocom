#include "fm1.h"
#include <math.h>

/* =========================================================
   STRINGSCAPE
   Eight-voice physical modelling synthesizer
   M-VAVE FM-1 / FM-1 Studio
   ========================================================= */

#define SR           44100.0f
#define PI           3.14159265358979323846f
#define VOICES       8
#define KEYS         27
#define DELAY_SIZE   2048
#define REV_SIZE     4096
#define EVENT_SIZE   64

#define STR_X        12
#define STR_Y        76
#define STR_W        8
#define STR_H        146

/* ---------- Utility functions ---------- */

static float limitf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int limiti(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static uint16_t rgb(int r, int g, int b)
{
    r = limiti(r, 0, 255);
    g = limiti(g, 0, 255);
    b = limiti(b, 0, 255);

    return (uint16_t)(
        ((r >> 3) << 11) |
        ((g >> 2) << 5) |
        (b >> 3)
    );
}

static uint32_t random_state = 0x7341AC29u;

static float noise(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;

    return (float)(random_state & 65535u)
           * (2.0f / 65535.0f) - 1.0f;
}

static float softclip(float x)
{
    /* Smooth saturation with bounded output. */

    float a = fabsf(x);

    if (a >= 3.0f)
        return x < 0.0f ? -1.0f : 1.0f;

    return x * (27.0f + x * x)
             / (27.0f + 9.0f * x * x);
}

/* ---------- Instruments ---------- */

typedef struct {
    const char *name;

    float damping;
    float decay;
    float excitation;
    float hammer;

    float body_f1;
    float body_f2;
    float body_r1;
    float body_r2;
    float body_mix;

    int red;
    int green;
    int blue;
} Instrument;

static const Instrument instruments[4] = {
    {
        "HARP",
        0.18f, 1.35f, 0.68f, 0.0f,
        190.0f, 410.0f,
        0.992f, 0.984f, 0.13f,
        255, 195, 65
    },
    {
        "NYLON GUITAR",
        0.43f, 0.78f, 0.48f, 0.0f,
        105.0f, 225.0f,
        0.993f, 0.988f, 0.23f,
        255, 125, 55
    },
    {
        "KOTO",
        0.12f, 0.95f, 0.85f, 0.0f,
        310.0f, 680.0f,
        0.989f, 0.981f, 0.16f,
        85, 235, 185
    },
    {
        "HAMMERED DULCIMER",
        0.065f, 1.55f, 0.93f, 1.0f,
        440.0f, 920.0f,
        0.995f, 0.989f, 0.19f,
        100, 175, 255
    }
};

/* ---------- User interface state ---------- */

static int instrument = 0;
static int knob[4] = {35, 65, 38, 42};

static int octave = 0;
static int pentatonic = 0;
static int arp_enabled = 0;
static int fx_enabled = 1;

/*
   Packed control word.
   Written by loop(), read by audio().

   Each knob uses seven bits.
   Instrument uses two bits.
   ARP and FX use one bit each.
*/

static volatile uint32_t control_word;
static volatile uint32_t held_notes;
static volatile int scale_octave_word;

static volatile uint32_t audio_clock;
static volatile uint32_t panic_request;

static volatile uint8_t string_level[KEYS];
static volatile uint8_t string_colour[KEYS];

static void publish_controls(void)
{
    uint32_t w = 0;

    w |= (uint32_t)knob[0];
    w |= (uint32_t)knob[1] << 7;
    w |= (uint32_t)knob[2] << 14;
    w |= (uint32_t)knob[3] << 21;
    w |= (uint32_t)instrument << 28;
    w |= (uint32_t)arp_enabled << 30;
    w |= (uint32_t)fx_enabled << 31;

    scale_octave_word =
        (octave + 2) | (pentatonic << 3);

    control_word = w;
}

/* ---------- Note mapping ---------- */

static const int pentatonic_notes[5] = {
    0, 2, 4, 7, 9
};

static int key_to_midi(int key, int mode)
{
    int oct = (mode & 7) - 2;
    int scale = (mode >> 3) & 1;
    int midi;

    if (scale) {
        /* C2, D2, E2, G2, A2, C3 ... D7 */

        midi = 36
             + 12 * (key / 5)
             + pentatonic_notes[key % 5];
    } else {
        /* Original FM-1 keyboard: F3 ... G5 */

        midi = 53 + key;
    }

    midi += 12 * oct;

    return limiti(midi, 12, 122);
}

static float midi_frequency(int midi)
{
    return 440.0f *
        powf(2.0f, ((float)midi - 69.0f) / 12.0f);
}

/* ---------- Note event queue ---------- */

/*
   Single producer: loop()
   Single consumer: audio()

   The event is written before the write index
   is published.
*/

static volatile uint32_t events[EVENT_SIZE];
static volatile uint32_t event_write;
static volatile uint32_t event_read;

static void queue_note(int key)
{
    uint32_t w = event_write;
    uint32_t next = (w + 1u) & (EVENT_SIZE - 1u);

    if (next != event_read) {
        events[w] = (uint32_t)key;
        event_write = next;
    }
}

/* ---------- Physical string model ---------- */

typedef struct {
    float buffer[DELAY_SIZE];

    int active;
    int key;
    int instrument;
    int length;
    int position;

    float frequency;
    float feedback;
    float damping;

    float previous;
    float ap_input;
    float ap_output;
    float ap_coefficient;

    float pan_l;
    float pan_r;

    float energy;
    float peak;
    float age_gain;

    uint32_t age;
} StringVoice;

static StringVoice voices[VOICES];

static int decay_setting = -1;
static uint32_t voice_serial;

static float decay_feedback(
    float frequency,
    int decay,
    int inst
)
{
    /*
       T60 is the approximate time required
       for amplitude to fall by 60 dB.
    */

    float t = (float)decay * 0.01f;

    float seconds =
        (0.25f + 14.0f * t * t)
        * instruments[inst].decay;

    float gain = expf(
        -6.907755f / (seconds * frequency)
    );

    return limitf(gain, 0.70f, 0.99997f);
}

static int choose_voice(int key)
{
    int i;
    int quietest = 0;

    float lowest = 100.0f;

    /* Retrigger an existing string first. */

    for (i = 0; i < VOICES; ++i) {
        if (voices[i].active &&
            voices[i].key == key)
            return i;
    }

    /* Otherwise use a free voice. */

    for (i = 0; i < VOICES; ++i) {
        if (!voices[i].active)
            return i;
    }

    /* Steal the quietest voice. */

    for (i = 0; i < VOICES; ++i) {
        float e = voices[i].energy;

        if (e < lowest) {
            lowest = e;
            quietest = i;
        }
    }

    return quietest;
}

static void pluck(
    int key,
    int midi,
    int inst,
    int position_setting,
    int decay,
    float velocity
)
{
    int i;
    int offset;
    int length;

    float filtered = 0.0f;

    float position =
        (float)position_setting * 0.01f;

    float frequency = midi_frequency(midi);

    const Instrument *p = &instruments[inst];

    StringVoice *v = &voices[choose_voice(key)];

    float damping =
        p->damping * (1.15f - 0.65f * position);

    damping = limitf(damping, 0.025f, 0.48f);

    /*
       Compensate for the fractional delay
       introduced by the string damping filter.
    */

    float period = SR / frequency - damping;

    period = limitf(
        period, 2.0f, (float)(DELAY_SIZE - 2)
    );

    length = (int)period;

    float fraction = period - (float)length;

    v->ap_coefficient =
        (1.0f - fraction) / (1.0f + fraction);

    /*
       Plucking near the centre gives a round sound.
       Plucking near the bridge gives more upper
       harmonics.
    */

    float pluck_point = 0.48f - 0.42f * position;

    offset = limiti(
        (int)(pluck_point * (float)length),
        1,
        length - 1
    );

    float brightness =
        0.13f
        + 0.72f * position
        + 0.12f * p->excitation;

    brightness = limitf(brightness, 0.08f, 0.98f);

    /* Initial string displacement. */

    for (i = 0; i < length; ++i) {
        float n = noise();

        filtered += brightness * (n - filtered);

        v->buffer[i] = filtered;
    }

    /*
       Excitation comb filter represents
       the position of the pluck.
       Work backwards so source samples
       have not yet been overwritten.
    */

    for (i = length - 1; i >= offset; --i) {
        v->buffer[i] -=
            0.82f * v->buffer[i - offset];
    }

    /*
       Nylon strings have softer attacks.
       Dulcimer strings receive a concentrated
       hammer excitation.
    */

    if (p->hammer > 0.5f) {
        int impact = length / 10 + 1;

        for (i = 0; i < impact; ++i) {
            float t = (float)i / (float)impact;

            v->buffer[i] +=
                0.75f * (1.0f - t) * noise();
        }
    }

    /* Remove DC and normalise the excitation. */

    float mean = 0.0f;
    float maximum = 0.001f;

    for (i = 0; i < length; ++i)
        mean += v->buffer[i];

    mean /= (float)length;

    for (i = 0; i < length; ++i) {
        v->buffer[i] -= mean;

        float a = fabsf(v->buffer[i]);

        if (a > maximum)
            maximum = a;
    }

    float gain = velocity * 0.88f / maximum;

    for (i = 0; i < length; ++i)
        v->buffer[i] *= gain;

    /* Equal-power stereo positioning. */

    float pan =
        0.12f + 0.76f * (float)key / 26.0f;

    v->pan_l = sqrtf(1.0f - pan);
    v->pan_r = sqrtf(pan);

    v->key = key;
    v->instrument = inst;
    v->frequency = frequency;

    v->length = length;
    v->position = 0;

    v->feedback =
        decay_feedback(frequency, decay, inst);

    v->damping = damping;

    v->previous = 0.0f;
    v->ap_input = 0.0f;
    v->ap_output = 0.0f;

    v->energy = 0.8f * velocity;
    v->peak = 0.0f;

    v->age_gain = 0.0f;
    v->age = ++voice_serial;

    v->active = 1;
}

static inline float string_sample(StringVoice *v)
{
    float x = v->buffer[v->position];

    /*
       Frequency-dependent string energy loss.
       High harmonics disappear faster than
       the fundamental.
    */

    float low =
        x * (1.0f - v->damping)
        + v->previous * v->damping;

    v->previous = x;

    /* Fractional-delay all-pass filter. */

    float y =
        v->ap_coefficient * low
        + v->ap_input
        - v->ap_coefficient * v->ap_output;

    v->ap_input = low;
    v->ap_output = y;

    v->buffer[v->position] = y * v->feedback;

    ++v->position;

    if (v->position >= v->length)
        v->position = 0;

    /*
       Very short attack ramp to reduce clicks
       when a string is retriggered.
    */

    if (v->age_gain < 1.0f) {
        v->age_gain += 0.065f;

        if (v->age_gain > 1.0f)
            v->age_gain = 1.0f;
    }

    float result = x * v->age_gain;

    float a = fabsf(result);

    if (a > v->peak)
        v->peak = a;

    return result;
}

/* ---------- Instrument body resonances ---------- */

typedef struct {
    float a;
    float b;
    float gain;
    float y1;
    float y2;
} Resonator;

static Resonator body[4][2];

static void resonator_setup(
    Resonator *r,
    float frequency,
    float radius
)
{
    r->a =
        2.0f * radius *
        cosf(2.0f * PI * frequency / SR);

    r->b = -radius * radius;

    r->gain = 1.0f - radius;

    r->y1 = 0.0f;
    r->y2 = 0.0f;
}

static inline float resonate(
    Resonator *r,
    float input
)
{
    float y =
        input * r->gain
        + r->a * r->y1
        + r->b * r->y2;

    r->y2 = r->y1;
    r->y1 = y;

    return y;
}

/* ---------- Stereo hall reverb ---------- */

/*
   Four-line feedback delay network.
   Mutually different delay lengths reduce
   obvious periodic echoes.

   Approximately 64 KB of static memory.
*/

static float rev_buffer[4][REV_SIZE];

static const int rev_length[4] = {
    1559, 2137, 2909, 3889
};

static int rev_pos[4];
static float rev_lp[4];

static float wet_smooth;
static float rev_feedback = 0.78f;

static inline void reverb_sample(
    float in_l,
    float in_r,
    float *out_l,
    float *out_r
)
{
    float a = rev_buffer[0][rev_pos[0]];
    float b = rev_buffer[1][rev_pos[1]];
    float c = rev_buffer[2][rev_pos[2]];
    float d = rev_buffer[3][rev_pos[3]];

    /* High-frequency absorption in the hall. */

    rev_lp[0] += 0.32f * (a - rev_lp[0]);
    rev_lp[1] += 0.32f * (b - rev_lp[1]);
    rev_lp[2] += 0.32f * (c - rev_lp[2]);
    rev_lp[3] += 0.32f * (d - rev_lp[3]);

    a = rev_lp[0];
    b = rev_lp[1];
    c = rev_lp[2];
    d = rev_lp[3];

    /* Orthogonal Hadamard feedback matrix. */

    float f = rev_feedback * 0.5f;

    rev_buffer[0][rev_pos[0]] =
        0.30f * in_l + f * (a + b + c + d);

    rev_buffer[1][rev_pos[1]] =
        0.30f * in_r + f * (a - b + c - d);

    rev_buffer[2][rev_pos[2]] =
        0.22f * in_l + f * (a + b - c - d);

    rev_buffer[3][rev_pos[3]] =
        0.22f * in_r + f * (a - b - c + d);

    if (++rev_pos[0] >= rev_length[0])
        rev_pos[0] = 0;

    if (++rev_pos[1] >= rev_length[1])
        rev_pos[1] = 0;

    if (++rev_pos[2] >= rev_length[2])
        rev_pos[2] = 0;

    if (++rev_pos[3] >= rev_length[3])
        rev_pos[3] = 0;

    *out_l = 0.5f * (a + b - c + d);
    *out_r = 0.5f * (a - b + c + d);
}

/* ---------- Bidirectional strummer ---------- */

static int arp_key = -1;
static int arp_direction = 1;
static int arp_remaining;
static int arp_was_on;

static uint32_t arp_previous_mask;

static int arp_next(uint32_t mask)
{
    int k;

    if (!mask)
        return -1;

    if (arp_key < 0) {
        arp_direction = 1;

        for (k = 0; k < KEYS; ++k)
            if (mask & (1u << k))
                return k;
    }

    if (arp_direction > 0) {
        for (k = arp_key + 1; k < KEYS; ++k)
            if (mask & (1u << k))
                return k;

        arp_direction = -1;

        for (k = arp_key - 1; k >= 0; --k)
            if (mask & (1u << k))
                return k;
    } else {
        for (k = arp_key - 1; k >= 0; --k)
            if (mask & (1u << k))
                return k;

        arp_direction = 1;

        for (k = arp_key + 1; k < KEYS; ++k)
            if (mask & (1u << k))
                return k;
    }

    /* Repeat a single held note. */

    for (k = 0; k < KEYS; ++k)
        if (mask & (1u << k))
            return k;

    return -1;
}

static int arp_interval(int speed)
{
    float t = (float)speed * 0.01f;

    /*
       550 ms at minimum speed.
       28 ms at maximum speed.
    */

    float seconds =
        0.028f + 0.522f * (1.0f - t) * (1.0f - t);

    return (int)(seconds * SR);
}

/* ---------- Emergency silence ---------- */

static uint32_t panic_seen;

static void silence_audio(void)
{
    int i;
    int j;

    for (i = 0; i < VOICES; ++i) {
        voices[i].active = 0;
        voices[i].energy = 0.0f;
    }

    for (i = 0; i < 4; ++i) {
        body[i][0].y1 = 0.0f;
        body[i][0].y2 = 0.0f;

        body[i][1].y1 = 0.0f;
        body[i][1].y2 = 0.0f;

        rev_lp[i] = 0.0f;

        for (j = 0; j < rev_length[i]; ++j)
            rev_buffer[i][j] = 0.0f;
    }

    for (i = 0; i < KEYS; ++i)
        string_level[i] = 0;

    event_read = event_write;

    arp_key = -1;
    arp_remaining = 0;
    arp_previous_mask = 0;
}

/* =========================================================
   AUDIO CALLBACK
   No LCD, LED, storage or MASTER calls here.
   ========================================================= */

void audio(int32_t *out, int frames)
{
    int i;
    int j;

    uint32_t w = control_word;

    int position = w & 127u;
    int decay = (w >> 7) & 127u;
    int hall = (w >> 14) & 127u;
    int speed = (w >> 21) & 127u;
    int inst = (w >> 28) & 3u;
    int arp = (w >> 30) & 1u;
    int fx = (w >> 31) & 1u;

    int mode = scale_octave_word;

    uint32_t mask = held_notes;

    if (panic_seen != panic_request) {
        panic_seen = panic_request;
        silence_audio();

        mask = 0;
    }

    if (arp != arp_was_on) {
        arp_key = -1;
        arp_remaining = 0;
        arp_direction = 1;

        arp_was_on = arp;
    }

    /* Consume pending manual plucks. */

    int event_count = 0;

    while (event_read != event_write &&
           event_count < VOICES) {

        uint32_t r = event_read;

        int key = (int)events[r];

        event_read =
            (r + 1u) & (EVENT_SIZE - 1u);

        if (!arp && key >= 0 && key < KEYS) {
            pluck(
                key,
                key_to_midi(key, mode),
                inst,
                position,
                decay,
                0.95f
            );
        }

        ++event_count;
    }

    /* Allow the decay knob to affect ringing strings. */

    if (decay_setting != decay) {
        decay_setting = decay;

        for (i = 0; i < VOICES; ++i) {
            StringVoice *v = &voices[i];

            if (v->active) {
                v->feedback = decay_feedback(
                    v->frequency,
                    decay,
                    v->instrument
                );
            }
        }
    }

    if (!mask) {
        arp_key = -1;
        arp_remaining = 0;
    } else if (!arp_previous_mask) {
        arp_remaining = 0;
    }

    arp_previous_mask = mask;

    float hall_amount = (float)hall * 0.01f;

    float wet_target =
        fx ? 0.62f * hall_amount : 0.0f;

    rev_feedback =
        0.58f + 0.365f * hall_amount;

    int interval = arp_interval(speed);

    for (i = 0; i < VOICES; ++i)
        voices[i].peak = 0.0f;

    /* ---------- Sample processing ---------- */

    for (i = 0; i < frames; ++i) {
        float left = 0.0f;
        float right = 0.0f;

        float body_in[4] = {
            0.0f, 0.0f, 0.0f, 0.0f
        };

        /* Sample-accurate strumming clock. */

        if (arp && mask) {
            if (arp_remaining <= 0) {
                arp_key = arp_next(mask);

                if (arp_key >= 0) {
                    float velocity =
                        arp_direction > 0
                        ? 0.85f : 0.70f;

                    pluck(
                        arp_key,
                        key_to_midi(arp_key, mode),
                        inst,
                        position,
                        decay,
                        velocity
                    );
                }

                arp_remaining = interval;
            }

            --arp_remaining;
        }

        /* Mix up to eight vibrating strings. */

        for (j = 0; j < VOICES; ++j) {
            StringVoice *v = &voices[j];

            if (!v->active)
                continue;

            float s = string_sample(v);

            left += s * v->pan_l;
            right += s * v->pan_r;

            body_in[v->instrument] += s;
        }

        /*
           Each instrument has its own resonant body.
           Old strings retain their instrument when
           ALGORITHM is changed.
        */

        float body_l = 0.0f;
        float body_r = 0.0f;

        for (j = 0; j < 4; ++j) {
            float a = resonate(
                &body[j][0],
                body_in[j]
            );

            float b = resonate(
                &body[j][1],
                body_in[j]
            );

            float mix = instruments[j].body_mix;

            body_l += mix * (a + 0.55f * b);
            body_r += mix * (0.70f * a + b);
        }

        left = (left + body_l) * 0.36f;
        right = (right + body_r) * 0.36f;

        /* Stereo hall. */

        float wet_l;
        float wet_r;

        reverb_sample(
            left,
            right,
            &wet_l,
            &wet_r
        );

        wet_smooth +=
            0.0015f * (wet_target - wet_smooth);

        left += wet_l * wet_smooth;
        right += wet_r * wet_smooth;

        /*
           Soft saturation prevents overload
           during loud eight-note chords.
           MASTER is deliberately not applied.
        */

        left = softclip(left * 1.4f);
        right = softclip(right * 1.4f);

        out[2 * i] =
            (int32_t)(left * (float)FM1_AUDIO_MAX);

        out[2 * i + 1] =
            (int32_t)(right * (float)FM1_AUDIO_MAX);
    }

    /* ---------- Publish string activity ---------- */

    uint8_t levels[KEYS];
    uint8_t colours[KEYS];

    for (i = 0; i < KEYS; ++i) {
        levels[i] = 0;
        colours[i] = (uint8_t)inst;
    }

    for (i = 0; i < VOICES; ++i) {
        StringVoice *v = &voices[i];

        if (!v->active)
            continue;

        /*
           Peak envelope follows the string's actual
           audio amplitude, not a separate timer.
        */

        if (v->peak > v->energy) {
            v->energy = v->peak;
        } else {
            v->energy =
                0.88f * v->energy
                + 0.12f * v->peak;
        }

        if (v->energy < 0.0007f) {
            v->active = 0;
            v->energy = 0.0f;
            continue;
        }

        /*
           Square-root mapping keeps quiet strings
           visible during their long decay.
        */

        int level = (int)(
            sqrtf(limitf(v->energy, 0.0f, 1.0f))
            * 255.0f
        );

        level = limiti(level, 1, 255);

        if (level > levels[v->key]) {
            levels[v->key] = (uint8_t)level;
            colours[v->key] =
                (uint8_t)v->instrument;
        }
    }

    for (i = 0; i < KEYS; ++i) {
        string_colour[i] = colours[i];
        string_level[i] = levels[i];
    }

    audio_clock += (uint32_t)frames;
}

/* =========================================================
   SMALL 5 x 7 BITMAP FONT

   Each glyph is five columns.
   Least significant bit is the top pixel.
   ========================================================= */

static const uint8_t font[36][5] = {
    /* A-Z */
    {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},

    /* 0-9 */
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E}
};

static uint8_t glyph(char ch, int column)
{
    if (ch >= 'A' && ch <= 'Z')
        return font[ch - 'A'][column];

    if (ch >= '0' && ch <= '9')
        return font[26 + ch - '0'][column];

    if (ch == '-')
        return 0x08;

    if (ch == '+')
        return column == 2 ? 0x3E : 0x08;

    if (ch == ':')
        return column == 2 ? 0x24 : 0;

    if (ch == '.')
        return column == 2 ? 0x40 : 0;

    if (ch == '/')
        return (uint8_t)(0x40u >> column);

    return 0;
}

/* ---------- Small display drawing buffer ---------- */

/*
   Reused for all LCD operations in loop().
   No full-screen framebuffer.
*/

static uint16_t tile[240 * 24];

static uint16_t background;
static uint16_t panel_background;
static uint16_t muted;
static uint16_t white;

static void clear_tile(
    int w,
    int h,
    uint16_t colour
)
{
    int i;

    for (i = 0; i < w * h; ++i)
        tile[i] = colour;
}

static void tile_pixel(
    int w,
    int h,
    int x,
    int y,
    uint16_t colour
)
{
    if (x >= 0 && x < w &&
        y >= 0 && y < h)
        tile[y * w + x] = colour;
}

static void tile_rect(
    int w,
    int h,
    int x,
    int y,
    int rw,
    int rh,
    uint16_t colour
)
{
    int a;
    int b;

    for (b = y; b < y + rh; ++b)
        for (a = x; a < x + rw; ++a)
            tile_pixel(w, h, a, b, colour);
}

static void tile_text(
    int w,
    int h,
    int x,
    int y,
    const char *text,
    int scale,
    uint16_t colour
)
{
    int c;
    int r;
    int dx;
    int dy;

    while (*text) {
        for (c = 0; c < 5; ++c) {
            uint8_t bits = glyph(*text, c);

            for (r = 0; r < 7; ++r) {
                if (!(bits & (1u << r)))
                    continue;

                for (dy = 0; dy < scale; ++dy)
                    for (dx = 0; dx < scale; ++dx)
                        tile_pixel(
                            w, h,
                            x + c * scale + dx,
                            y + r * scale + dy,
                            colour
                        );
            }
        }

        x += 6 * scale;
        ++text;
    }
}

static int text_length(const char *s)
{
    int n = 0;

    while (*s++)
        ++n;

    return n;
}

static void number_text(int n, char *s)
{
    n = limiti(n, 0, 100);

    if (n >= 100) {
        s[0] = '1';
        s[1] = '0';
        s[2] = '0';
        s[3] = 0;
    } else if (n >= 10) {
        s[0] = (char)('0' + n / 10);
        s[1] = (char)('0' + n % 10);
        s[2] = 0;
    } else {
        s[0] = (char)('0' + n);
        s[1] = 0;
    }
}

static uint16_t accent_colour(int inst)
{
    const Instrument *p = &instruments[inst];

    return rgb(p->red, p->green, p->blue);
}

/* ---------- Display header ---------- */

static int old_instrument = -1;
static int old_knob[4] = {-1, -1, -1, -1};

static int old_octave = 99;
static int old_scale = -1;
static int old_arp = -1;
static int old_fx = -1;

static void draw_title(void)
{
    const char *name = instruments[instrument].name;

    int width = text_length(name) * 12 - 2;

    clear_tile(240, 24, panel_background);

    tile_text(
        240, 24,
        (240 - width) / 2,
        4,
        name,
        2,
        accent_colour(instrument)
    );

    fm1_lcd_blit(
        0, 0, 240, 24,
        tile, 240 * 24
    );
}

static const char *knob_names[4] = {
    "PLUCK", "RING", "HALL", "STRUM"
};

static void draw_knob(int k)
{
    char value[4];

    uint16_t accent = accent_colour(instrument);

    number_text(knob[k], value);

    clear_tile(60, 24, panel_background);

    tile_text(
        60, 24,
        5, 1,
        knob_names[k],
        1,
        muted
    );

    tile_text(
        60, 24,
        5, 10,
        value,
        1,
        white
    );

    /* Small horizontal value meter. */

    tile_rect(
        60, 24,
        5, 21, 49, 2,
        rgb(32, 42, 55)
    );

    int width = 49 * knob[k] / 100;

    if (width > 0) {
        tile_rect(
            60, 24,
            5, 21, width, 2,
            accent
        );
    }

    fm1_lcd_blit(
        k * 60, 25, 60, 24,
        tile, 60 * 24
    );
}

static void draw_status(void)
{
    char oct[7] = "OCT  0";

    oct[4] =
        octave < 0 ? '-' :
        octave > 0 ? '+' : ' ';

    int n = octave < 0 ? -octave : octave;

    oct[5] = (char)('0' + n);

    uint16_t accent = accent_colour(instrument);

    clear_tile(240, 20, background);

    tile_text(
        240, 20, 7, 6,
        oct, 1, white
    );

    tile_text(
        240, 20, 57, 6,
        pentatonic ? "PENTA" : "CHROM",
        1,
        pentatonic ? accent : muted
    );

    tile_text(
        240, 20, 111, 6,
        arp_enabled ? "ARP ON" : "ARP OFF",
        1,
        arp_enabled ? accent : muted
    );

    tile_text(
        240, 20, 183, 6,
        fx_enabled ? "FX ON" : "FX OFF",
        1,
        fx_enabled ? accent : muted
    );

    fm1_lcd_blit(
        0, 52, 240, 20,
        tile, 240 * 20
    );
}

/* ---------- String animation ---------- */

static uint16_t string_tile[STR_W * STR_H];

static int displayed_level[KEYS];
static int displayed_colour[KEYS];
static int displayed_phase[KEYS];

static uint32_t last_string_update;
static int string_cursor;

/*
   Fast approximate sine.

   Used only by the display.
   The visual vibration is deliberately slower
   than the audio-frequency vibration so the
   motion can be seen on the LCD.
*/

static float visual_sine(float x)
{
    while (x >= 2.0f * PI)
        x -= 2.0f * PI;

    while (x < 0.0f)
        x += 2.0f * PI;

    if (x > PI)
        x -= 2.0f * PI;

    float y =
        1.2732395f * x
        - 0.4052847f * x * fabsf(x);

    return 0.225f * (y * fabsf(y) - y) + y;
}

static void draw_string(
    int key,
    int level,
    int inst,
    int phase
)
{
    int x;
    int y;

    const Instrument *p = &instruments[inst];

    float energy = (float)level / 255.0f;

    float swing =
        visual_sine(
            (float)phase * (2.0f * PI / 32.0f)
        );

    uint16_t idle = rgb(48, 57, 67);

    uint16_t glow = rgb(
        (int)((float)p->red * energy * 0.24f),
        (int)((float)p->green * energy * 0.24f),
        (int)((float)p->blue * energy * 0.24f)
    );

    uint16_t bright = rgb(
        (int)(48.0f + (float)p->red * energy),
        (int)(55.0f + (float)p->green * energy),
        (int)(65.0f + (float)p->blue * energy)
    );

    int centre = STR_W / 2;

    for (y = 0; y < STR_H; ++y) {
        float t =
            (float)y / (float)(STR_H - 1);

        /*
           Both ends remain fixed.
           The centre has the largest displacement.
           Higher modes add a small ripple.
        */

        float shape = 4.0f * t * (1.0f - t);

        float ripple =
            visual_sine(
                t * 2.0f * PI
                + (float)phase * 0.27f
            );

        float displacement =
            energy * shape *
            (2.75f * swing + 0.45f * ripple);

        float xpos = (float)centre + displacement;

        for (x = 0; x < STR_W; ++x) {
            float distance =
                fabsf((float)x - xpos);

            uint16_t colour = background;

            if (level > 0) {
                if (distance < 1.65f)
                    colour = glow;

                if (distance < 0.65f)
                    colour = bright;
            } else {
                if (x == centre)
                    colour = idle;
            }

            /* Bridge and upper string anchor. */

            if (y < 3 || y >= STR_H - 3) {
                colour = background;

                if (x >= centre - 1 &&
                    x <= centre + 1)
                    colour = level > 0
                        ? bright : muted;
            }

            string_tile[y * STR_W + x] = colour;
        }
    }

    fm1_lcd_blit(
        STR_X + key * STR_W,
        STR_Y,
        STR_W,
        STR_H,
        string_tile,
        STR_W * STR_H
    );

    displayed_level[key] = level;
    displayed_colour[key] = inst;
    displayed_phase[key] = phase;
}

static int update_one_string(void)
{
    int i;

    uint32_t now = audio_clock;

    /*
       Throttle transfers to approximately one
       string region per audio block.

       Silent strings are never redrawn unless
       their previous image was vibrating.
    */

    if ((uint32_t)(now - last_string_update) < 220u)
        return 0;

    for (i = 0; i < KEYS; ++i) {
        int k = (string_cursor + i) % KEYS;

        int level = string_level[k];
        int inst = string_colour[k] & 3;

        if (level < 6)
            level = 0;

        /*
           Use a different visual oscillation
           speed for each string.
        */

        int phase = (int)(
            (now / (uint32_t)(390 - k * 7))
            & 31u
        );

        int redraw = 0;

        if (level == 0) {
            if (displayed_level[k] != 0)
                redraw = 1;
        } else {
            int diff =
                level - displayed_level[k];

            if (diff < 0)
                diff = -diff;

            if (displayed_level[k] == 0 ||
                diff >= 6 ||
                inst != displayed_colour[k] ||
                phase != displayed_phase[k])
                redraw = 1;
        }

        if (redraw) {
            draw_string(k, level, inst, phase);

            string_cursor = (k + 1) % KEYS;
            last_string_update = now;

            return 1;
        }
    }

    return 0;
}

/* ---------- Footer ---------- */

static int old_voice_count = -1;

static void draw_footer(int count)
{
    char s[12] = "VOICES 0/8";

    s[7] = (char)('0' + limiti(count, 0, 8));

    clear_tile(240, 14, panel_background);

    tile_text(
        240, 14,
        6, 3,
        "STRINGSCAPE",
        1,
        muted
    );

    tile_text(
        240, 14,
        172, 3,
        s,
        1,
        white
    );

    /* Eight tiny voice indicators. */

    int i;

    for (i = 0; i < VOICES; ++i) {
        uint16_t c = i < count
            ? accent_colour(instrument)
            : rgb(34, 42, 50);

        tile_rect(
            240, 14,
            104 + i * 7,
            4, 5, 5,
            c
        );
    }

    fm1_lcd_blit(
        0, 226, 240, 14,
        tile, 240 * 14
    );
}

/* ---------- LEDs ---------- */

static int led_cache[FM1_NLED];

static void set_led(int id, int level)
{
    if (led_cache[id] == level)
        return;

    led_cache[id] = level;

    fm1_led_level(
        (uint32_t)id,
        level
    );
}

static void update_leds(void)
{
    int k;
    int count = 0;

    uint32_t now = audio_clock;

    int blink =
        ((now / 11025u) & 1u) == 0u;

    set_led(
        FM1_BTN_OCT_DOWN,
        octave > -2 ? FM1_LED_ON : FM1_LED_OFF
    );

    set_led(
        FM1_BTN_OCT_UP,
        octave < 2 ? FM1_LED_ON : FM1_LED_OFF
    );

    set_led(
        FM1_BTN_ARP,
        arp_enabled
            ? (blink ? FM1_LED_ON : FM1_LED_OFF)
            : FM1_LED_ON
    );

    set_led(
        FM1_BTN_SCL,
        pentatonic
            ? (blink ? FM1_LED_ON : FM1_LED_OFF)
            : FM1_LED_ON
    );

    set_led(
        FM1_BTN_FX,
        fx_enabled
            ? (blink ? FM1_LED_ON : FM1_LED_OFF)
            : FM1_LED_ON
    );

    set_led(FM1_BTN_HOME, FM1_LED_ON);

    /* Unused buttons remain dark. */

    set_led(FM1_BTN_ENV, FM1_LED_OFF);
    set_led(FM1_BTN_LFO, FM1_LED_OFF);
    set_led(FM1_BTN_EDIT, FM1_LED_OFF);
    set_led(FM1_BTN_GLO, FM1_LED_OFF);
    set_led(FM1_BTN_SAVE, FM1_LED_OFF);
    set_led(FM1_BTN_SEQ, FM1_LED_OFF);
    set_led(FM1_BTN_PLAY, FM1_LED_OFF);
    set_led(FM1_BTN_REC, FM1_LED_OFF);
    set_led(FM1_LED_PLAY_RED, FM1_LED_OFF);

    for (k = 0; k < KEYS; ++k) {
        int a = string_level[k];

        int level = FM1_LED_OFF;

        if (a >= 145)
            level = FM1_LED_ON;
        else if (a >= 50)
            level = FM1_LED_MID;
        else if (a >= 7)
            level = FM1_LED_GLOW;

        if (a >= 7)
            ++count;

        set_led(FM1_KEY_NOTE0 + k, level);
    }

    count = limiti(count, 0, VOICES);

    if (count != old_voice_count) {
        draw_footer(count);
        old_voice_count = count;
    }
}

/* =========================================================
   SETUP
   ========================================================= */

void setup(void)
{
    int i;

    background = rgb(8, 13, 21);
    panel_background = rgb(15, 22, 32);
    muted = rgb(110, 125, 143);
    white = rgb(225, 235, 245);

    for (i = 0; i < VOICES; ++i)
        voices[i].active = 0;

    for (i = 0; i < 4; ++i) {
        resonator_setup(
            &body[i][0],
            instruments[i].body_f1,
            instruments[i].body_r1
        );

        resonator_setup(
            &body[i][1],
            instruments[i].body_f2,
            instruments[i].body_r2
        );
    }

    for (i = 0; i < FM1_NLED; ++i)
        led_cache[i] = -1;

    publish_controls();

    /* Only setup clears the entire display. */

    fm1_lcd_fill_rect(
        0, 0, 240, 240, background
    );

    draw_title();

    for (i = 0; i < 4; ++i) {
        draw_knob(i);
        old_knob[i] = knob[i];
    }

    draw_status();

    fm1_lcd_fill_rect(
        9, 74, 222, 2,
        rgb(57, 66, 78)
    );

    fm1_lcd_fill_rect(
        9, 222, 222, 2,
        rgb(57, 66, 78)
    );

    for (i = 0; i < KEYS; ++i) {
        draw_string(i, 0, instrument, 0);
    }

    draw_footer(0);

    old_voice_count = 0;

    old_instrument = instrument;
    old_octave = octave;
    old_scale = pentatonic;
    old_arp = arp_enabled;
    old_fx = fx_enabled;

    update_leds();

    /* Discard stale input edges. */

    fm1_input_edges(0);
    fm1_input_note_edges();

    for (i = 0; i < FM1_NENC; ++i)
        fm1_enc_take((uint32_t)i);
}

/* =========================================================
   MAIN LOOP
   Controls, LEDs and incremental screen updates.
   ========================================================= */

void loop(void)
{
    int i;
    int changed = 0;

    uint32_t buttons = fm1_input_edges(0);
    uint32_t pressed = fm1_input_note_edges();

    uint32_t notes =
        fm1_in.notes & 0x07FFFFFFu;

    if (buttons & (1u << FM1_BTN_OCT_DOWN)) {
        octave = limiti(octave - 1, -2, 2);
    }

    if (buttons & (1u << FM1_BTN_OCT_UP)) {
        octave = limiti(octave + 1, -2, 2);
    }

    if (buttons & (1u << FM1_BTN_SCL)) {
        pentatonic = !pentatonic;
    }

    if (buttons & (1u << FM1_BTN_ARP)) {
        arp_enabled = !arp_enabled;
    }

    if (buttons & (1u << FM1_BTN_FX)) {
        fx_enabled = !fx_enabled;
    }

    if (buttons & (1u << FM1_BTN_HOME)) {
        ++panic_request;

        pressed = 0;
        notes = 0;
    }

    /* Instrument selection. */

    int32_t delta =
        fm1_enc_take(FM1_ENC_ALGORITHM);

    if (delta) {
        instrument =
            (instrument + (int)(delta % 4) + 4)
            % 4;
    }

    /* Four sound parameters. */

    for (i = 0; i < 4; ++i) {
        int32_t d = fm1_enc_take(
            FM1_ENC_KNOB1 + (uint32_t)i
        );

        if (d) {
            d = limiti((int)d, -100, 100);

            knob[i] = limiti(
                knob[i] + (int)d * 2,
                0,
                100
            );
        }
    }

    /*
       SELECT and PRESETS have no function.
       Consume their movements.
    */

    fm1_enc_take(FM1_ENC_SELECT);
    fm1_enc_take(FM1_ENC_PRESETS);

    publish_controls();

    held_notes = notes;

    /* Manual notes ring independently of key release. */

    if (!arp_enabled) {
        for (i = 0; i < KEYS; ++i) {
            if (pressed & (1u << i))
                queue_note(i);
        }
    }

    update_leds();

    /*
       At most one changed header region
       is transferred per loop iteration.
       Input polling therefore remains responsive.
    */

    if (instrument != old_instrument) {
        old_instrument = instrument;

        draw_title();

        for (i = 0; i < 4; ++i)
            old_knob[i] = -1;

        old_scale = -1;
        old_voice_count = -1;

        changed = 1;
    }

    if (!changed) {
        for (i = 0; i < 4; ++i) {
            if (knob[i] != old_knob[i]) {
                draw_knob(i);
                old_knob[i] = knob[i];

                changed = 1;
                break;
            }
        }
    }

    if (!changed) {
        if (octave != old_octave ||
            pentatonic != old_scale ||
            arp_enabled != old_arp ||
            fx_enabled != old_fx) {

            draw_status();

            old_octave = octave;
            old_scale = pentatonic;
            old_arp = arp_enabled;
            old_fx = fx_enabled;

            changed = 1;
        }
    }

    if (!changed)
        update_one_string();
}
