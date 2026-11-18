/*
 * NEON AFTERGLOW
 * Original synthwave composition for M-VAVE FM-1
 *
 * A minor / 100 BPM / 48 bars / 115.2 seconds
 *
 * PLAY       Start / stop / restart
 * NOTE KEYS  Four-voice live lead
 * OCT +/-    Keyboard octave
 * MASTER     Overall volume
 *
 * KNOB1      Drums
 * KNOB2      Bass
 * KNOB3      Pads
 * KNOB4      Arpeggio
 * ALGORITHM  Song lead
 * SELECT     Live lead
 * PRESETS    Stereo echo
 */

#include "fm1.h"
#include <math.h>

/* =========================================================
 * CONSTANTS
 * ========================================================= */

#define BPM             100
#define SONG_BARS       48
#define STEP_SAMPLES    6615
#define TABLE_SIZE      1024
#define DELAY_SIZE      (STEP_SAMPLES * 3)
#define LIVE_VOICES     4
#define PHASE_HZ        97391.54866f
#define TWO_PI          6.28318530718f

#define BG              0x0843
#define WHITE           0xFFFF
#define PINK            0xFA9F
#define CYAN            0x07FF
#define GREEN           0x57F6
#define TRACK           0x2948
#define DIM             0x8495

enum {
    DRUMS, BASS, PADS, ARP, LEAD, LIVE, ECHO
};

enum {
    INTRO, VERSE1, CHORUS1, VERSE2, CHORUS2, OUTRO
};

enum {
    AM, FMAJ, CMAJ, GMAJ, DM, EM
};

/* =========================================================
 * CONTROL DATA: loop() -> audio()
 * ========================================================= */

static volatile uint32_t ctl_keys = 0;
static volatile uint32_t ctl_play = 0;
static volatile uint32_t ctl_master = 0;
static volatile int32_t ctl_octave = 0;

static volatile uint32_t ctl_mix[7] = {
    100, 100, 100, 100, 100, 100, 70
};

/* Audio -> screen */

static volatile uint32_t display_status = 0;
static volatile uint32_t display_level[6];
static volatile uint32_t audio_blocks = 0;

/* =========================================================
 * OSCILLATORS
 * ========================================================= */

static float wt_sine[TABLE_SIZE];
static float wt_bass[TABLE_SIZE];
static float wt_pad[TABLE_SIZE];
static float wt_arp[TABLE_SIZE];
static float wt_lead[TABLE_SIZE];

static uint32_t note_inc[128];

static inline float absf_fast(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float wave(const float *table, uint32_t phase)
{
    return table[phase >> 22];
}

static inline float osc(
    const float *table,
    uint32_t *phase,
    uint32_t inc)
{
    *phase += inc;
    return wave(table, *phase);
}

static inline float smooth(
    float value, float target, float speed)
{
    return value + (target - value) * speed;
}

static inline float clip(float x, float limit)
{
    if (x > limit) return limit;
    if (x < -limit) return -limit;
    return x;
}

static inline float limiter(float x)
{
    if (x > 0.75f)
        x = 0.75f + (x - 0.75f) * 0.20f;

    if (x < -0.75f)
        x = -0.75f + (x + 0.75f) * 0.20f;

    return clip(x, 0.95f);
}

static void make_tables(void)
{
    int i;

    for (i = 0; i < TABLE_SIZE; ++i) {
        float p = TWO_PI * (float)i / (float)TABLE_SIZE;

        float s1 = sinf(p);
        float s2 = sinf(p * 2.0f);
        float s3 = sinf(p * 3.0f);
        float s4 = sinf(p * 4.0f);
        float s5 = sinf(p * 5.0f);

        wt_sine[i] = s1;

        wt_bass[i] =
            0.62f * s1 +
            0.22f * s2 +
            0.10f * s3 +
            0.06f * s4;

        wt_pad[i] =
            0.55f * s1 +
            0.20f * s2 +
            0.13f * s3 +
            0.07f * s4 +
            0.05f * s5;

        wt_arp[i] =
            0.70f * s1 +
            0.21f * s2 +
            0.09f * s4;

        wt_lead[i] =
            0.54f * s1 +
            0.13f * s2 +
            0.22f * s3 +
            0.04f * s4 +
            0.07f * s5;
    }

    for (i = 0; i < 128; ++i) {
        float hz = 440.0f *
            powf(2.0f, ((float)i - 69.0f) / 12.0f);

        note_inc[i] = (uint32_t)(hz * PHASE_HZ);
    }
}

/* =========================================================
 * COMPOSITION
 * ========================================================= */

/* Bass roots */
static const uint8_t roots[6] = {
    33, 29, 36, 31, 38, 40
};

/* Pad voicings */
static const uint8_t chords[6][3] = {
    {57, 60, 64},  /* Am */
    {57, 60, 65},  /* F  */
    {55, 60, 64},  /* C  */
    {55, 59, 62},  /* G  */
    {57, 62, 65},  /* Dm */
    {55, 59, 64}   /* Em */
};

static const uint8_t verse_chords[4] = {
    AM, FMAJ, DM, EM
};

static const uint8_t chorus_chords[4] = {
    AM, FMAJ, CMAJ, GMAJ
};

static const uint8_t outro_chords[4] = {
    AM, FMAJ, GMAJ, AM
};

/* 
 * Melody notation:
 * Positive = MIDI note
 * -1       = hold previous note
 *  0       = release
 *
 * Each row is one bar, with 16 sixteenth notes.
 */

static const int8_t verse_melody[4][16] = {
    {
        0,0,0,0, 69,-1,-1,0,
        72,-1,74,-1, 76,-1,0,0
    },
    {
        77,-1,-1,76, 72,-1,-1,0,
        69,-1,-1,0, 0,0,0,0
    },
    {
        0,0,74,-1, 77,-1,76,-1,
        74,-1,69,-1, 0,0,0,0
    },
    {
        71,-1,-1,0, 74,-1,76,-1,
        79,-1,76,-1, 71,-1,0,0
    }
};

static const int8_t chorus_melody[8][16] = {
    {
        76,-1,76,79, 81,-1,-1,79,
        76,-1,72,-1, 74,-1,76,-1
    },
    {
        77,-1,-1,76, 72,-1,69,-1,
        72,-1,76,-1, 77,-1,79,-1
    },
    {
        79,-1,-1,76, 84,-1,-1,83,
        79,-1,76,-1, 74,-1,76,-1
    },
    {
        74,-1,71,-1, 79,-1,-1,77,
        74,-1,-1,71, 74,-1,76,-1
    },
    {
        81,-1,-1,84, 83,-1,79,-1,
        81,-1,76,-1, 72,-1,76,-1
    },
    {
        77,-1,81,-1, 84,-1,-1,81,
        77,-1,76,-1, 72,-1,69,-1
    },
    {
        76,-1,79,-1, 84,-1,86,-1,
        84,-1,79,-1, 76,-1,74,-1
    },
    {
        79,-1,-1,77, 74,-1,71,-1,
        74,-1,76,-1, 79,-1,0,0
    }
};

static const int8_t outro_melody[4][16] = {
    {
        81,-1,-1,-1, 79,-1,76,-1,
        72,-1,-1,-1, 0,0,0,0
    },
    {
        77,-1,-1,-1, 76,-1,72,-1,
        69,-1,-1,-1, 0,0,0,0
    },
    {
        71,-1,-1,-1, 74,-1,79,-1,
        76,-1,-1,-1, 0,0,0,0
    },
    {
        69,-1,-1,-1, -1,-1,-1,-1,
        0,0,0,0, 0,0,0,0
    }
};

static int section_of(int bar)
{
    if (bar < 4)  return INTRO;
    if (bar < 16) return VERSE1;
    if (bar < 24) return CHORUS1;
    if (bar < 32) return VERSE2;
    if (bar < 44) return CHORUS2;
    return OUTRO;
}

static int local_bar_of(int bar)
{
    switch (section_of(bar)) {
        case INTRO:   return bar;
        case VERSE1:  return bar - 4;
        case CHORUS1: return bar - 16;
        case VERSE2:  return bar - 24;
        case CHORUS2: return bar - 32;
        default:      return bar - 44;
    }
}

/* =========================================================
 * DRUM SYNTHESIS
 * ========================================================= */

static uint32_t noise_state = 0x91D473ABu;
static float last_noise = 0.0f;

static uint32_t kick_phase;
static uint32_t snare_phase;

static float kick_env;
static float kick_pitch;
static float snare_env;
static float hat_env;
static float hat_decay;

static inline float noise_sample(void)
{
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;

    return ((float)(noise_state >> 16) - 32768.0f)
        * (1.0f / 32768.0f);
}

static void kick_trigger(float accent)
{
    kick_phase = 0;
    kick_env = accent;
    kick_pitch = 145.0f * PHASE_HZ;
}

static void snare_trigger(float accent)
{
    snare_phase = 0;
    snare_env = accent;
}

static void hat_trigger(float accent, int open)
{
    hat_env = accent;
    hat_decay = open ? 0.9987f : 0.9945f;
}

static inline float drum_sample(void)
{
    float d = 0.0f;

    if (kick_env > 0.0001f) {
        kick_pitch = smooth(
            kick_pitch, 48.0f * PHASE_HZ, 0.0024f);

        d += 0.37f * kick_env *
            osc(wt_sine, &kick_phase,
                (uint32_t)kick_pitch);

        kick_env *= 0.99948f;
    } else {
        kick_env = 0.0f;
    }

    if (snare_env > 0.0001f ||
        hat_env > 0.0001f) {

        float n = noise_sample();
        float high = (n - last_noise) * 0.5f;
        last_noise = n;

        if (snare_env > 0.0001f) {
            float tone = osc(
                wt_sine, &snare_phase,
                (uint32_t)(185.0f * PHASE_HZ));

            d += snare_env *
                (0.23f * n + 0.09f * tone);

            snare_env *= 0.99912f;
        } else {
            snare_env = 0.0f;
        }

        if (hat_env > 0.0001f) {
            d += 0.13f * high * hat_env;
            hat_env *= hat_decay;
        } else {
            hat_env = 0.0f;
        }
    }

    return d;
}

/* =========================================================
 * BACKING SYNTHESIZERS
 * ========================================================= */

static uint32_t bass_phase, bass_inc;
static uint32_t arp_phase, arp_inc;
static uint32_t lead_phase, lead_inc;

static int bass_gate, arp_gate, lead_gate;

static float bass_env;
static float arp_env;
static float lead_env;

static uint32_t pad_phase_l[3];
static uint32_t pad_phase_r[3];
static uint32_t pad_inc_l[3];
static uint32_t pad_inc_r[3];

static float pad_env;
static float pad_target;

static void set_pad(int chord)
{
    int i;

    for (i = 0; i < 3; ++i) {
        float inc = (float)note_inc[chords[chord][i]];

        pad_inc_l[i] = (uint32_t)(inc * 0.9970f);
        pad_inc_r[i] = (uint32_t)(inc * 1.0030f);
    }
}

/* =========================================================
 * LIVE KEYBOARD SYNTH
 * ========================================================= */

typedef struct {
    int key;
    uint32_t phase;
    uint32_t inc;
    float env;
} LiveVoice;

static LiveVoice live[LIVE_VOICES];

static void update_live(uint32_t keys, int octave)
{
    int i, k;

    /* Release voices whose notes are no longer held. */

    for (i = 0; i < LIVE_VOICES; ++i) {
        if (live[i].key >= 0) {
            if (!(keys & (1u << live[i].key)))
                live[i].key = -1;
        }
    }

    /* Allocate available voices to held keys. */

    for (k = 0; k < 27; ++k) {
        int found = 0;
        int best = -1;
        float quietest = 2.0f;

        if (!(keys & (1u << k)))
            continue;

        for (i = 0; i < LIVE_VOICES; ++i) {
            if (live[i].key == k)
                found = 1;

            if (live[i].key < 0 &&
                live[i].env < quietest) {

                quietest = live[i].env;
                best = i;
            }
        }

        if (!found && best >= 0) {
            live[best].key = k;

            if (live[best].env < 0.0001f)
                live[best].phase = 0;
        }
    }

    /* The lowest physical key is F3, MIDI 53. */

    for (i = 0; i < LIVE_VOICES; ++i) {
        if (live[i].key >= 0) {
            int note = 53 + live[i].key + octave * 12;

            if (note < 0)   note = 0;
            if (note > 127) note = 127;

            live[i].inc = note_inc[note];
        }
    }
}

static inline float live_sample(void)
{
    float result = 0.0f;
    int i;

    for (i = 0; i < LIVE_VOICES; ++i) {
        LiveVoice *v = &live[i];

        if (v->key >= 0) {
            v->env = smooth(v->env, 1.0f, 0.0045f);
        } else {
            v->env *= 0.99945f;
        }

        if (v->env > 0.0001f) {
            result += 0.105f * v->env *
                osc(wt_lead, &v->phase, v->inc);
        } else {
            v->env = 0.0f;
        }
    }

    return result;
}

/* =========================================================
 * SEQUENCER
 * ========================================================= */

static int playing;
static int next_bar;
static int next_step;
static int step_left;

static uint32_t seen_play;

static float backing_gain;
static float master_gain;

static void stop_song(int finished)
{
    playing = 0;

    bass_gate = 0;
    arp_gate = 0;
    lead_gate = 0;
    pad_target = 0.0f;

    display_status =
        (display_status & 0xFFFFu) |
        ((finished ? 2u : 0u) << 16);
}

static void start_song(void)
{
    playing = 1;
    next_bar = 0;
    next_step = 0;
    step_left = 0;

    kick_env = 0.0f;
    snare_env = 0.0f;
    hat_env = 0.0f;

    bass_env = 0.0f;
    arp_env = 0.0f;
    lead_env = 0.0f;
    pad_env = 0.0f;

    bass_gate = 0;
    arp_gate = 0;
    lead_gate = 0;

    pad_target = 0.0f;
    backing_gain = 0.0f;

    bass_phase = 0;
    arp_phase = 0;
    lead_phase = 0;
}

static void sequence_event(int bar, int s)
{
    int sec = section_of(bar);
    int local = local_bar_of(bar);
    int chorus = sec == CHORUS1 || sec == CHORUS2;
    int verse = sec == VERSE1 || sec == VERSE2;
    int chord;
    int drums_on;
    int bass_on;
    int arp_on;
    int melody = 0;

    static const uint8_t arp_pattern[8] = {
        0, 1, 2, 1, 0, 2, 1, 2
    };

    display_status =
        (1u << 16) |
        ((uint32_t)bar << 4) |
        (uint32_t)s;

    if (verse)
        chord = verse_chords[local & 3];
    else if (sec == OUTRO)
        chord = outro_chords[local & 3];
    else
        chord = chorus_chords[local & 3];

    /* Pads */

    if (s == 0)
        set_pad(chord);

    pad_target = chorus ? 1.0f : 0.78f;

    if (sec == INTRO)
        pad_target = 0.85f;

    if (bar == SONG_BARS - 1)
        pad_target = 0.85f *
            (float)(16 - s) / 16.0f;

    /* Arrangement */

    drums_on = bar >= 2 && bar < 47;
    bass_on = bar >= 2 && bar < 47;
    arp_on = bar >= 1 && bar < 47;

    /* Drum patterns */

    if (drums_on) {
        if (chorus) {
            if ((s & 3) == 0)
                kick_trigger(1.0f);

            if (s == 14 && (local & 1))
                kick_trigger(0.70f);
        } else {
            if (s == 0 || s == 6 || s == 8)
                kick_trigger(0.90f);

            if (s == 14 && sec != OUTRO)
                kick_trigger(0.65f);
        }

        if (s == 4 || s == 12)
            snare_trigger(chorus ? 1.0f : 0.86f);

        if (chorus || !(s & 1)) {
            int open = (s == 6 || s == 14);

            float accent = (s & 1) ? 0.28f : 0.66f;

            if (open)
                accent = 0.85f;

            hat_trigger(accent, open);
        }

        /* Small fill before each chorus. */

        if ((bar == 15 || bar == 31) && s >= 12) {
            if (s == 13 || s == 15)
                snare_trigger(0.55f);
        }
    }

    /* Bass line */

    if (bass_on && !(s & 1)) {
        int note = roots[chord];

        if (chorus) {
            if (s == 2 || s == 10 || s == 14)
                note += 12;
            else if (s == 6 || s == 12)
                note += 7;

            bass_gate = STEP_SAMPLES + 2200;
        } else {
            if (s == 6 || s == 14)
                note += 7;
            else if (s == 10)
                note += 12;

            bass_gate = STEP_SAMPLES + 3900;
        }

        bass_inc = note_inc[note];
    }

    /* Arpeggio */

    if (arp_on && (chorus || !(s & 1))) {
        int n = chorus ? s : s / 2;

        int note =
            chords[chord][arp_pattern[n & 7]] + 12;

        if (chorus && (s & 7) == 7)
            note += 12;

        arp_inc = note_inc[note];

        arp_gate = chorus ? 3600 : 7400;
    }

    /* Composed lead melody */

    if (chorus)
        melody = chorus_melody[local & 7][s];

    else if (verse)
        melody = verse_melody[local & 3][s];

    else if (sec == OUTRO)
        melody = outro_melody[local & 3][s];

    else
        melody = 0;

    if (melody > 0) {
        lead_inc = note_inc[melody];
        lead_gate = 1;
    } else if (melody == 0) {
        lead_gate = 0;
    }
}

static inline void sequencer_tick(void)
{
    if (!playing)
        return;

    if (step_left == 0) {
        if (next_bar >= SONG_BARS) {
            stop_song(1);
            return;
        }

        sequence_event(next_bar, next_step);

        step_left = STEP_SAMPLES;
        ++next_step;

        if (next_step >= 16) {
            next_step = 0;
            ++next_bar;
        }
    }

    --step_left;
}

/* =========================================================
 * STEREO ECHO
 *
 * 450 ms feedback delay, with 300 ms second tap.
 * 16-bit buffer keeps memory usage low.
 * ========================================================= */

static int16_t delay_buffer[DELAY_SIZE];
static int delay_pos;

/* =========================================================
 * AUDIO ENGINE
 * ========================================================= */

static float meter_hold[6];

void audio(int32_t *out, int frames)
{
    float gain[7];
    float peak[6] = {0};
    float target_master;
    uint32_t command;
    int i, f;

    command = ctl_play;

    if (command != seen_play) {
        uint32_t difference = command - seen_play;
        seen_play = command;

        if (difference & 1u) {
            if (playing)
                stop_song(0);
            else
                start_song();
        }
    }

    for (i = 0; i < 7; ++i)
        gain[i] = (float)ctl_mix[i] * 0.01f;

    target_master = (float)ctl_master *
        (1.0f / (float)FM1_MASTER_MAX);

    update_live(ctl_keys, (int)ctl_octave);

    for (f = 0; f < frames; ++f) {
        float d = 0.0f;
        float b = 0.0f;
        float pl = 0.0f;
        float pr = 0.0f;
        float a = 0.0f;
        float m = 0.0f;
        float k;
        float left, right;
        float duck;
        float echo_a, echo_b;
        float send;
        float pad_peak;
        int tap;

        /* Exact sample-timed sixteenth notes. */

        sequencer_tick();

        backing_gain = smooth(
            backing_gain,
            playing ? 1.0f : 0.0f,
            0.0025f);

        if (playing || backing_gain > 0.0001f) {

            /* DRUMS */

            d = drum_sample() * gain[DRUMS];

            /* Kick-controlled sidechain ducking. */

            duck = 1.0f - 0.38f * kick_env;

            /* BASS */

            bass_env = smooth(
                bass_env,
                bass_gate > 0 ? 1.0f : 0.0f,
                bass_gate > 0 ? 0.007f : 0.0012f);

            if (bass_gate > 0)
                --bass_gate;

            if (bass_env > 0.0001f) {
                b = osc(wt_bass,
                    &bass_phase, bass_inc);

                b *= bass_env * 0.23f *
                    gain[BASS] * duck;
            }

            /* STEREO PADS */

            pad_env = smooth(
                pad_env, pad_target, 0.00013f);

            if (pad_env > 0.0001f) {
                for (i = 0; i < 3; ++i) {
                    pl += osc(wt_pad,
                        &pad_phase_l[i], pad_inc_l[i]);

                    pr += osc(wt_pad,
                        &pad_phase_r[i], pad_inc_r[i]);
                }

                pl *= 0.066f * pad_env *
                    gain[PADS] * duck;

                pr *= 0.066f * pad_env *
                    gain[PADS] * duck;
            }

            /* ARPEGGIO */

            arp_env = smooth(
                arp_env,
                arp_gate > 0 ? 1.0f : 0.0f,
                arp_gate > 0 ? 0.012f : 0.0011f);

            if (arp_gate > 0)
                --arp_gate;

            if (arp_env > 0.0001f) {
                a = osc(wt_arp,
                    &arp_phase, arp_inc);

                a *= 0.105f * arp_env *
                    gain[ARP];
            }

            /* AUTOMATIC LEAD */

            lead_env = smooth(
                lead_env,
                lead_gate ? 1.0f : 0.0f,
                lead_gate ? 0.0035f : 0.00065f);

            if (lead_env > 0.0001f) {
                m = osc(wt_lead,
                    &lead_phase, lead_inc);

                m *= 0.165f * lead_env *
                    gain[LEAD];
            }

            d  *= backing_gain;
            b  *= backing_gain;
            pl *= backing_gain;
            pr *= backing_gain;
            a  *= backing_gain;
            m  *= backing_gain;
        }

        /* Live keyboard always remains playable. */

        k = live_sample() * gain[LIVE];

        /* Stereo delay: no large buffer clears in audio(). */

        tap = delay_pos + STEP_SAMPLES;

        if (tap >= DELAY_SIZE)
            tap -= DELAY_SIZE;

        echo_a = (float)delay_buffer[tap] *
            (1.0f / 32768.0f);

        echo_b = (float)delay_buffer[delay_pos] *
            (1.0f / 32768.0f);

        send =
            a * 0.75f +
            m * 0.80f +
            k * 0.55f +
            echo_b * gain[ECHO] * 0.35f;

        send = clip(send, 0.98f);

        delay_buffer[delay_pos] =
            (int16_t)(send * 32767.0f);

        ++delay_pos;

        if (delay_pos >= DELAY_SIZE)
            delay_pos = 0;

        /* Stereo mix */

        left =
            d + b + pl +
            a * 0.75f +
            m * 0.90f +
            k * 0.88f +
            echo_a * gain[ECHO] * 0.60f;

        right =
            d + b + pr +
            a * 1.0f +
            m * 0.84f +
            k * 0.95f +
            echo_b * gain[ECHO] * 0.60f;

        master_gain = smooth(
            master_gain, target_master, 0.002f);

        left = limiter(left) * master_gain;
        right = limiter(right) * master_gain;

        /* Signed 24-bit stereo output. */

        out[f * 2] =
            (int32_t)(left * (float)FM1_AUDIO_MAX);

        out[f * 2 + 1] =
            (int32_t)(right * (float)FM1_AUDIO_MAX);

        /* Meter peak detection */

        if (absf_fast(d) > peak[DRUMS])
            peak[DRUMS] = absf_fast(d);

        if (absf_fast(b) > peak[BASS])
            peak[BASS] = absf_fast(b);

        pad_peak = absf_fast(pl);

        if (absf_fast(pr) > pad_peak)
            pad_peak = absf_fast(pr);

        if (pad_peak > peak[PADS])
            peak[PADS] = pad_peak;

        if (absf_fast(a) > peak[ARP])
            peak[ARP] = absf_fast(a);

        if (absf_fast(m) > peak[LEAD])
            peak[LEAD] = absf_fast(m);

        if (absf_fast(k) > peak[LIVE])
            peak[LIVE] = absf_fast(k);
    }

    /* Publish low-resolution meter values once per block. */

    {
        static const float meter_scale[6] = {
            2.9f, 4.8f, 6.0f, 10.0f, 6.6f, 5.0f
        };

        for (i = 0; i < 6; ++i) {
            float v = peak[i] *
                meter_scale[i] * master_gain;

            if (v > 1.0f)
                v = 1.0f;

            if (v > meter_hold[i])
                meter_hold[i] = v;
            else
                meter_hold[i] *= 0.955f;

            display_level[i] =
                (uint32_t)(meter_hold[i] * 24.0f);
        }
    }

    ++audio_blocks;
}

/* =========================================================
 * BITMAP FONT
 *
 * Five columns per character, seven pixels high.
 * ========================================================= */

static const uint8_t font_digits[10][5] = {
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

static const uint8_t font_letters[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}  /* Z */
};

static const uint8_t glyph_blank[5] = {
    0,0,0,0,0
};

static const uint8_t glyph_slash[5] = {
    0x20,0x10,0x08,0x04,0x02
};

static const uint8_t glyph_plus[5] = {
    0x08,0x08,0x3E,0x08,0x08
};

static const uint8_t glyph_minus[5] = {
    0x08,0x08,0x08,0x08,0x08
};

static const uint8_t glyph_colon[5] = {
    0x00,0x36,0x36,0x00,0x00
};

static const uint8_t *get_glyph(char c)
{
    if (c >= 'A' && c <= 'Z')
        return font_letters[c - 'A'];

    if (c >= '0' && c <= '9')
        return font_digits[c - '0'];

    if (c == '/') return glyph_slash;
    if (c == '+') return glyph_plus;
    if (c == '-') return glyph_minus;
    if (c == ':') return glyph_colon;

    return glyph_blank;
}

/* Small text rendering buffer: no full framebuffer. */

static uint16_t text_pixels[240 * 14];

static void draw_text(
    int x, int y,
    const char *str,
    int scale,
    uint16_t fg,
    uint16_t bg)
{
    int len = 0;
    int w, h;
    int px, py, j;
    const uint8_t *g;

    if (scale < 1 || scale > 2)
        return;

    while (str[len])
        ++len;

    w = len * 6 * scale;
    h = 7 * scale;

    if (w <= 0 || w > 240 ||
        x < 0 || y < 0 ||
        x + w > 240 || y + h > 240)
        return;

    for (j = 0; j < w * h; ++j)
        text_pixels[j] = bg;

    for (j = 0; j < len; ++j) {
        g = get_glyph(str[j]);

        for (py = 0; py < h; ++py) {
            for (px = 0; px < 5 * scale; ++px) {
                if (g[px / scale] &
                    (1u << (py / scale))) {

                    int xx = j * 6 * scale + px;

                    text_pixels[py * w + xx] = fg;
                }
            }
        }
    }

    fm1_lcd_blit(
        (uint32_t)x,
        (uint32_t)y,
        (uint32_t)w,
        (uint32_t)h,
        text_pixels,
        (uint32_t)(w * h));
}

/* =========================================================
 * SCREEN
 * ========================================================= */

static const char *section_names[6] = {
    "INTRO       ",
    "VERSE 1     ",
    "CHORUS 1    ",
    "VERSE 2     ",
    "CHORUS 2    ",
    "OUTRO       "
};

static const char *meter_names[6] = {
    "DRUMS", "BASS", "PADS", "ARP", "LEAD", "LIVE"
};

static const uint16_t meter_colors[6] = {
    0xFDA8,
    0xFA9F,
    0x9C1F,
    0x07FF,
    0xFB2F,
    0x57F6
};

static int old_level[6];

static uint32_t old_status = 0xFFFFFFFFu;
static uint32_t last_ui_block = 0;

static int old_octave = 99;
static int meter_index = 0;

static void draw_static_screen(void)
{
    int i;

    fm1_lcd_fill_rect(0, 0, 240, 240, BG);

    draw_text(10, 9, "NEON AFTERGLOW",
              2, PINK, BG);

    draw_text(10, 30, "A MINOR / 100 BPM",
              1, CYAN, BG);

    fm1_lcd_fill_rect(10, 45, 220, 1, TRACK);

    for (i = 0; i < 6; ++i) {
        int y = 108 + i * 18;

        draw_text(10, y, meter_names[i],
                  2, WHITE, BG);

        fm1_lcd_fill_rect(84, y + 3,
                          144, 9, TRACK);

        old_level[i] = 0;
    }

    fm1_lcd_fill_rect(10, 217, 220, 1, TRACK);

    draw_text(10, 226, "PLAY: START/STOP",
              1, DIM, BG);
}

static void draw_status(uint32_t status)
{
    int mode;
    int bar;
    int beat;
    int section;
    int previous_bar;

    char line[] = "BAR 01/48  BEAT 1";

    /* Only update when section, bar, beat or mode changes. */

    status &= ~3u;

    if (status == old_status)
        return;

    mode = (int)(status >> 16);
    bar = (int)((status >> 4) & 0xFFu);
    beat = (int)((status & 15u) / 4u) + 1;

    if (bar >= SONG_BARS)
        bar = SONG_BARS - 1;

    section = section_of(bar);

    previous_bar =
        (int)((old_status >> 4) & 0xFFu);

    if (old_status == 0xFFFFFFFFu ||
        section_of(previous_bar) != section) {

        draw_text(10, 52, section_names[section],
                  2, WHITE, BG);
    }

    if (old_status == 0xFFFFFFFFu ||
        (old_status >> 16) != (status >> 16)) {

        if (mode == 1) {
            draw_text(164, 52, "PLAY  ",
                      2, GREEN, BG);
        } else if (mode == 2) {
            draw_text(164, 52, "END   ",
                      2, PINK, BG);
        } else {
            draw_text(164, 52, "STOP  ",
                      2, DIM, BG);
        }

        fm1_led_key(FM1_BTN_PLAY, mode == 1);
    }

    if (old_status == 0xFFFFFFFFu ||
        (old_status & 0xFFFFu) !=
        (status & 0xFFFFu)) {

        int number = bar + 1;

        line[4] = (char)('0' + number / 10);
        line[5] = (char)('0' + number % 10);
        line[16] = (char)('0' + beat);

        draw_text(10, 80, line, 2, CYAN, BG);
    }

    old_status = status;
}

static void draw_octave(void)
{
    int octave = (int)ctl_octave;

    char str[] = "OCT +0";

    if (octave == old_octave)
        return;

    str[4] = octave < 0 ? '-' : '+';
    str[5] = (char)('0' +
        (octave < 0 ? -octave : octave));

    draw_text(184, 226, str, 1, CYAN, BG);

    old_octave = octave;
}

static void draw_meter(int i)
{
    int level = (int)display_level[i];
    int old = old_level[i];
    int y = 111 + i * 18;

    if (level > 24)
        level = 24;

    if (level < 0)
        level = 0;

    if (level > old) {
        /* Paint just the new portion. */

        fm1_lcd_fill_rect(
            (uint32_t)(84 + old * 6),
            (uint32_t)y,
            (uint32_t)((level - old) * 6),
            9,
            meter_colors[i]);

    } else if (level < old) {
        /* Erase just the missing portion. */

        fm1_lcd_fill_rect(
            (uint32_t)(84 + level * 6),
            (uint32_t)y,
            (uint32_t)((old - level) * 6),
            9,
            TRACK);
    }

    old_level[i] = level;
}

/* =========================================================
 * INPUT
 * ========================================================= */

static void adjust_mix(int channel, int32_t clicks)
{
    int value;
    int maximum = channel == ECHO ? 100 : 150;

    if (!clicks)
        return;

    if (clicks > 30)  clicks = 30;
    if (clicks < -30) clicks = -30;

    value = (int)ctl_mix[channel] + (int)clicks * 5;

    if (value < 0)
        value = 0;

    if (value > maximum)
        value = maximum;

    ctl_mix[channel] = (uint32_t)value;
}

/* =========================================================
 * FM-1 ENTRY POINTS
 * ========================================================= */

void setup(void)
{
    int i;

    make_tables();

    for (i = 0; i < LIVE_VOICES; ++i) {
        live[i].key = -1;
        live[i].env = 0.0f;
        live[i].phase = 0;
        live[i].inc = 0;
    }

    for (i = 0; i < 3; ++i) {
        pad_phase_l[i] =
            (uint32_t)i * 0x31415926u;

        pad_phase_r[i] =
            (uint32_t)i * 0x27182818u + 0x20000000u;
    }

    ctl_master = fm1_master();

    playing = 0;
    display_status = 0;
    master_gain = 0.0f;

    draw_static_screen();
    draw_status(display_status);
    draw_octave();
}

void loop(void)
{
    uint32_t buttons;
    uint32_t block;
    int octave;

    /* Read all controls outside the audio interrupt. */

    ctl_keys = fm1_in.notes & 0x07FFFFFFu;
    ctl_master = fm1_master();

    buttons = fm1_input_edges(0);

    if (buttons & (1u << FM1_BTN_PLAY))
        ++ctl_play;

    octave = (int)ctl_octave;

    if (buttons & (1u << FM1_BTN_OCT_UP))
        ++octave;

    if (buttons & (1u << FM1_BTN_OCT_DOWN))
        --octave;

    if (octave > 2)
        octave = 2;

    if (octave < -2)
        octave = -2;

    ctl_octave = octave;

    /* Mixer encoders */

    adjust_mix(DRUMS,
        fm1_enc_take(FM1_ENC_KNOB1));

    adjust_mix(BASS,
        fm1_enc_take(FM1_ENC_KNOB2));

    adjust_mix(PADS,
        fm1_enc_take(FM1_ENC_KNOB3));

    adjust_mix(ARP,
        fm1_enc_take(FM1_ENC_KNOB4));

    adjust_mix(LEAD,
        fm1_enc_take(FM1_ENC_ALGORITHM));

    adjust_mix(LIVE,
        fm1_enc_take(FM1_ENC_SELECT));

    adjust_mix(ECHO,
        fm1_enc_take(FM1_ENC_PRESETS));

    /* Update only changed screen regions. */

    draw_octave();

    block = audio_blocks;

    if (block != last_ui_block) {
        last_ui_block = block;

        draw_status(display_status);

        /* One meter per update, round-robin. */

        draw_meter(meter_index);

        ++meter_index;

        if (meter_index >= 6)
            meter_index = 0;
    }
}