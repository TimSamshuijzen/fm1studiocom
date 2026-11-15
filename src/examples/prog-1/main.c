/* main.c - PROG-1: a progressive rock band in an FM-1.
 *
 * An original piece in the spirit of early-70s progressive rock, played by a
 * tonewheel organ through a rotating speaker, a picked FM bass, a lead synth
 * with a ping-pong echo, drums and a reverb. The band starts at once.
 *
 * The piece: INTRO, MARCH (4/4), MARCH with the lead, HITS (three hits and a
 * rest), RIFF (7/8, 2+2+3; the first bar without drums), RIFF with the lead,
 * TURN (E7 into the A minor), SOLO, BREAK with tom fills, and then again from
 * the MARCH with the lead.
 *
 *   PLAY/STOP   start or stop the band (it starts from the INTRO)
 *   FX          rotating speaker fast or slow (each part also sets it)
 *   OCT- OCT+   octave of the note keys
 *   Note keys   play the organ with the band (a MIDI keyboard too)
 *   SELECT      tempo, 80 to 184 BPM
 *   ALGORITHM   the part to jump to after the current bar
 *   PRESETS     organ drawbars: ROCK, JAZZ, FLUTE, FULL, CHURCH
 *   KNOB 1      DRIVE  organ overdrive
 *   KNOB 2      LEAD   brightness of the lead synth
 *   KNOB 3      SPACE  reverb
 *   KNOB 4      DRUMS  drum level
 *   MASTER      volume
 *
 * The screen: the sound and tempo; the part, metre and bar; a light for each
 * 16th note of the bar; an oscilloscope; a level meter for each instrument
 * (OUT turns red when the mix is driven into the soft limiter); the knobs.
 * The note keys light up with the organ, bass and lead notes.
 */
#include "fm1.h"
#include <math.h>
#include <string.h>

#define RATE ((float)FM1_AUDIO_RATE)
#define BIT(n) (1u << (n))
#define PI 3.14159265f
#define COUNT(a) (int)(sizeof(a) / sizeof(a)[0])

/* ======================================================================
 * Small tools
 * ====================================================================== */

#define SINE_SIZE 4096
static float sine_table[SINE_SIZE];

/* phase in cycles; negative values work too */ static float sine(float phase) {
    return sine_table[(int)(phase * SINE_SIZE) & (SINE_SIZE - 1)]; }

static uint32_t noise_state = 0x1234567u; static float noise(void) { /* -1 to 1 */
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return (float)(int32_t)noise_state * (1.0f / 2147483648.0f); }

static float note_hz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }

/* The factor for each sample that decays to 1/1000 in seconds. */ static float decay_factor(float seconds) { return expf(-6.9f / (seconds * RATE)); }

static int clamp(int value, int low, int high) { return value < low ? low : value > high ? high : value; }

/* ======================================================================
 * Organ: tonewheels and drawbars
 * ====================================================================== */

#define ORGAN_VOICES 16
#define PARTIALS 7
#define KEY_BAND (-1) /* a voice that the band plays, not a key */

/* Multiples of the 16' pitch (half the note): 16', 8', 5 1/3', 4', 2 2/3', 2', 1'.
 * They are whole numbers, so all the partials of a voice wrap with its one phase. */ static const float partial_mult[PARTIALS] = { 1, 2, 3, 4, 6, 8, 16 };

typedef struct {
    const char *name;
    uint8_t drawbar[PARTIALS]; /* 0 to 8, as on the organ */
    int percussion;            /* a decaying 2 2/3' at the start of each note */
} registration_t;

static const registration_t registrations[] = {
    { "ROCK", { 8, 8, 8, 4, 0, 2, 0 }, 0 },
    { "JAZZ", { 8, 8, 0, 0, 0, 0, 0 }, 1 },
    { "FLUTE", { 0, 8, 0, 6, 0, 0, 3 }, 0 },
    { "FULL", { 8, 8, 8, 8, 8, 8, 8 }, 0 },
    { "CHURCH", { 6, 8, 4, 8, 3, 6, 4 }, 0 }, };
#define REGISTRATIONS COUNT(registrations)

typedef struct {
    int note;      /* MIDI note; -1: the voice is free */
    int key;       /* the key that holds it, or KEY_BAND */
    int gate;      /* 1 while it is held */
    int gate_left; /* samples until the gate closes; 0: until the key is up */
    float phase, step;
    float env, click, perc, level;
    uint32_t age;
} organ_voice_t;

static organ_voice_t organ[ORGAN_VOICES]; static float drawbar_gain[PARTIALS]; static int registration; static uint32_t voice_clock;

static void set_registration(int r) {
    float sum = 0.0f;
    registration = r;
    for (int p = 0; p < PARTIALS; p++) {
        int bar = registrations[r].drawbar[p];
        drawbar_gain[p] = bar ? powf(2.0f, (bar - 8) * 0.5f) : 0.0f; /* 3 dB a step */
        sum += drawbar_gain[p] * drawbar_gain[p];
    }
    for (int p = 0; p < PARTIALS; p++) drawbar_gain[p] *= 0.7f / sqrtf(sum); /* each the same loudness */ }

static void organ_on(int note, float level, int gate_samples, int key) {
    organ_voice_t *o = 0;
    for (int v = 0; v < ORGAN_VOICES && !o; v++) /* a free voice */
        if (organ[v].note < 0) o = &organ[v];
    if (!o) /* else the quietest released voice */
        for (int v = 0; v < ORGAN_VOICES; v++)
            if (!organ[v].gate && (!o || organ[v].env < o->env)) o = &organ[v];
    if (!o) /* else the oldest voice */
        for (int v = 0; v < ORGAN_VOICES; v++)
            if (!o || organ[v].age < o->age) o = &organ[v];
    o->note = note;
    o->key = key;
    o->gate = 1;
    o->gate_left = gate_samples;
    o->step = note_hz((float)note) * 0.5f / RATE;
    o->level = level;
    o->click = 1.0f;
    o->perc = 1.0f;
    o->age = ++voice_clock;
}

static void organ_release(int key) {
    for (int v = 0; v < ORGAN_VOICES; v++)
        if (organ[v].note >= 0 && organ[v].key == key) organ[v].gate = 0; }

static float organ_sample(void) {
    float out = 0.0f;
    int percussion = registrations[registration].percussion;
    for (int v = 0; v < ORGAN_VOICES; v++) {
        organ_voice_t *o = &organ[v];
        if (o->note < 0) continue;
        if (o->gate_left > 0 && --o->gate_left == 0) o->gate = 0;
        if (o->gate) {
            o->env += (1.0f - o->env) * 0.02f; /* about 1 ms */
        } else {
            o->env *= 0.9965f; /* about 40 ms */
            if (o->env < 0.0005f) {
                o->note = -1;
                o->env = 0.0f;
                continue;
            }
        }
        float s = 0.0f;
        for (int p = 0; p < PARTIALS; p++)
            if (drawbar_gain[p] > 0.0f) s += sine(o->phase * partial_mult[p]) * drawbar_gain[p];
        if (percussion) {
            s += sine(o->phase * 6.0f) * o->perc * 0.6f;
            o->perc *= 0.99985f;
        }
        s += noise() * o->click * 0.3f; /* the key click */
        o->click *= 0.92f;
        o->phase += o->step;
        if (o->phase >= 1.0f) o->phase -= 1.0f;
        out += s * o->env * o->level;
    }
    return out;
}

/* ---- Rotating speaker: a horn for the highs, a drum for the lows ---- */

#define LESLIE_SIZE 256
static float leslie_line[LESLIE_SIZE];
static int leslie_pos;
static float leslie_low, horn_rot, drum_rot, horn_hz = 0.8f, drum_hz = 0.7f; static int leslie_fast;

/* The value delay samples (with a fraction) before position pos of a ring. */ static float ring_read(const float *ring, int size, int pos, float delay) {
    float p = (float)pos - delay;
    if (p < 0.0f) p += (float)size;
    int i = (int)p;
    int j = i + 1 == size ? 0 : i + 1;
    return ring[i] + (ring[j] - ring[i]) * (p - (float)i); }

static void leslie(float in, float *left, float *right) {
    horn_hz += ((leslie_fast ? 6.8f : 0.8f) - horn_hz) * 0.00004f;  /* the horn speeds up in about 0.6 s */
    drum_hz += ((leslie_fast ? 5.9f : 0.7f) - drum_hz) * 0.000012f; /* the heavy drum in about 2 s */
    horn_rot += horn_hz / RATE;
    if (horn_rot >= 1.0f) horn_rot -= 1.0f;
    drum_rot += drum_hz / RATE;
    if (drum_rot >= 1.0f) drum_rot -= 1.0f;

    leslie_low += (in - leslie_low) * 0.1f; /* the crossover, about 700 Hz */
    leslie_line[leslie_pos] = in - leslie_low;
    /* The distance of the horn changes the pitch, its direction the level */
    float hs = sine(horn_rot), hc = sine(horn_rot + 0.25f);
    float hl = ring_read(leslie_line, LESLIE_SIZE, leslie_pos, 12.0f + 8.0f * hs) * (0.72f + 0.28f * hc);
    float hr = ring_read(leslie_line, LESLIE_SIZE, leslie_pos, 12.0f - 8.0f * hs) * (0.72f - 0.28f * hc);
    if (++leslie_pos == LESLIE_SIZE) leslie_pos = 0;
    float ds = sine(drum_rot);
    *left = hl + leslie_low * (0.85f + 0.15f * ds);
    *right = hr + leslie_low * (0.85f - 0.15f * ds); }

/* ======================================================================
 * Bass: two FM operators, picked
 * ====================================================================== */

static struct {
    int note, on, attack, gate_left;
    float carrier, modulator, step, index, env, level; } bass;

static void bass_on(int note, float level, int gate_samples) {
    bass.note = note;
    bass.step = note_hz((float)note) / RATE;
    bass.index = 2.6f; /* bright at the pick, then darker */
    bass.level = level;
    bass.gate_left = gate_samples;
    bass.attack = 1;
    bass.on = 1;
}

static float bass_sample(void) {
    if (!bass.on) return 0.0f;
    if (bass.gate_left > 0) {
        bass.gate_left--;
        if (bass.attack) {
            bass.env += (1.0f - bass.env) * 0.06f;
            if (bass.env > 0.98f) bass.attack = 0;
        } else {
            bass.env += (0.55f - bass.env) * 0.0003f;
        }
    } else {
        bass.env *= 0.997f; /* about 50 ms */
        if (bass.env < 0.0005f) {
            bass.on = 0;
            bass.env = 0.0f;
            return 0.0f;
        }
    }
    bass.index = 0.7f + (bass.index - 0.7f) * 0.9995f;
    float s = sine(bass.carrier + sine(bass.modulator) * bass.index);
    bass.carrier += bass.step;
    bass.modulator += bass.step;
    if (bass.carrier >= 1.0f) bass.carrier -= 1.0f;
    if (bass.modulator >= 1.0f) bass.modulator -= 1.0f;
    return tanhf(s * bass.env * 1.8f) * bass.level; }

/* ======================================================================
 * Lead: three saws through a resonant low pass, with glide and vibrato
 * ====================================================================== */

enum { LEAD_OFF, LEAD_ATTACK, LEAD_SUSTAIN, LEAD_RELEASE };

static struct {
    int stage, gate_left, held; /* held: samples since the note began, for the vibrato */
    float phase1, phase2, phase3;
    float pitch, target; /* MIDI notes: the pitch glides to the target */
    float env, fenv, level, vibrato;
    float ic1, ic2; /* the filter */
} lead;
static float lead_bright = 0.6f;

static void lead_on(int note, float level, int gate_samples) {
    if (lead.stage == LEAD_OFF) lead.pitch = (float)note; /* no glide from silence */
    if (lead.stage == LEAD_OFF || lead.stage == LEAD_RELEASE) lead.stage = LEAD_ATTACK;
    lead.target = (float)note;
    lead.fenv = 1.0f; /* each note opens the filter */
    lead.held = 0;
    lead.level = level;
    lead.gate_left = gate_samples;
}

/* A saw with the step at the wrap rounded off (polyBLEP), so it does not alias much. */ static float saw(float *phase, float dt) {
    float t = *phase, s = 2.0f * t - 1.0f;
    if (t < dt) {
        t /= dt;
        s -= t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        s -= t * t + t + t + 1.0f;
    }
    *phase += dt;
    if (*phase >= 1.0f) *phase -= 1.0f;
    return s;
}

static float lead_sample(void) {
    if (lead.stage == LEAD_OFF) return 0.0f;
    if (lead.gate_left > 0 && --lead.gate_left == 0) lead.stage = LEAD_RELEASE;
    switch (lead.stage) {
    case LEAD_ATTACK:
        lead.env += (1.05f - lead.env) * 0.004f;
        if (lead.env >= 1.0f) {
            lead.env = 1.0f;
            lead.stage = LEAD_SUSTAIN;
        }
        break;
    case LEAD_SUSTAIN:
        lead.env += (0.8f - lead.env) * 0.00005f;
        break;
    default:
        lead.env *= 0.99965f; /* about 0.45 s */
        if (lead.env < 0.0005f) {
            lead.stage = LEAD_OFF;
            lead.env = lead.ic1 = lead.ic2 = 0.0f;
            return 0.0f;
        }
    }
    lead.pitch += (lead.target - lead.pitch) * 0.0025f; /* glide */

    /* A vibrato that comes in slowly on a long note */
    float vibrato = 0.0f;
    if (lead.held < 200000) lead.held++;
    if (lead.held > 8000) {
        float depth = (lead.held - 8000) * (1.0f / 30000.0f);
        vibrato = sine(lead.vibrato) * (depth > 1.0f ? 1.0f : depth) * 0.25f;
    }
    lead.vibrato += 5.5f / RATE;
    if (lead.vibrato >= 1.0f) lead.vibrato -= 1.0f;

    float hz = note_hz(lead.pitch + vibrato);
    float dt = hz / RATE;
    float s = saw(&lead.phase1, dt * 1.004f) + saw(&lead.phase2, dt * 0.996f) + 0.5f * saw(&lead.phase3, dt * 0.5f);

    /* The low pass: a state variable filter (trapezoidal), its cutoff follows
     * the filter envelope, the LEAD knob and the pitch */
    lead.fenv *= 0.99985f;
    float cutoff = (250.0f + 3200.0f * lead_bright * lead_bright)
        * (1.0f + 4.0f * lead.fenv * (0.25f + lead_bright)) * (0.6f + hz * (1.0f / 1100.0f));
    if (cutoff > 11000.0f) cutoff = 11000.0f;
    float w = PI * cutoff / RATE, w2 = w * w;
    float g = w * (15.0f - w2) / (15.0f - 6.0f * w2); /* tan(w) */
    const float k = 0.9f; /* 1 / Q */
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = s - lead.ic2;
    float v1 = a1 * lead.ic1 + a2 * v3;
    float v2 = lead.ic2 + a2 * lead.ic1 + a3 * v3;
    lead.ic1 = 2.0f * v1 - lead.ic1;
    lead.ic2 = 2.0f * v2 - lead.ic2;
    return tanhf(v2 * 0.9f) * lead.env * lead.level; }

/* ======================================================================
 * Drums
 * ====================================================================== */

typedef struct { float phase, hz, drop, pitch_env, amp, decay, level, click; } drum_t; /* kick and toms */
typedef struct { float amp, decay, level, cut, lp1, lp2; } cymbal_t;                    /* hats and crash */

static drum_t kick, toms[3];
static cymbal_t hat, crash;
static struct { float phase, tone, noise_amp, level, lp; } snare;

static void drum_hit(drum_t *d, float hz, float drop, float seconds, float level) {
    d->hz = hz;
    d->drop = drop;
    d->pitch_env = d->amp = d->click = 1.0f;
    d->decay = decay_factor(seconds);
    d->level = level;
}

static float drum_sample(drum_t *d, float pitch_fall) {
    if (d->amp < 0.0003f) return 0.0f;
    float s = sine(d->phase) * d->amp + noise() * d->click * 0.3f;
    d->phase += d->hz * (1.0f + d->drop * d->pitch_env) / RATE;
    if (d->phase >= 1.0f) d->phase -= 1.0f;
    d->pitch_env *= pitch_fall;
    d->amp *= d->decay;
    d->click *= 0.9f;
    return s * d->level;
}

static void cymbal_hit(cymbal_t *c, float seconds, float level, float cut) {
    c->amp = 1.0f;
    c->decay = decay_factor(seconds);
    c->level = level;
    c->cut = cut;
}

static float cymbal_sample(cymbal_t *c) { /* noise through two high passes */
    if (c->amp < 0.0003f) return 0.0f;
    float n = noise();
    c->lp1 += (n - c->lp1) * c->cut;
    float h1 = n - c->lp1;
    c->lp2 += (h1 - c->lp2) * c->cut;
    float h2 = h1 - c->lp2;
    c->amp *= c->decay;
    return h2 * c->amp * c->level;
}

static void kick_hit(float level) { drum_hit(&kick, 46.0f, 2.6f, 0.5f, level); } static void tom_hit(int t, float level) {
    static const float hz[3] = { 196.0f, 147.0f, 110.0f };
    drum_hit(&toms[t], hz[t], 0.5f, 0.55f, level); } static void hat_hit(int open, float level) { cymbal_hit(&hat, open ? 0.45f : 0.06f, level, 0.55f); } static void crash_hit(float level) { cymbal_hit(&crash, 2.2f, level, 0.3f); } static void snare_hit(float level) {
    snare.tone = snare.noise_amp = 1.0f;
    snare.level = level;
}

static float snare_sample(void) {
    if (snare.noise_amp < 0.0003f) return 0.0f;
    float t = sine(snare.phase) * snare.tone;
    snare.phase += 190.0f / RATE;
    if (snare.phase >= 1.0f) snare.phase -= 1.0f;
    snare.tone *= 0.9989f;
    float n = noise();
    snare.lp += (n - snare.lp) * 0.3f;
    float s = t * 0.6f + (n - snare.lp) * snare.noise_amp * 0.9f;
    snare.noise_amp *= 0.99955f;
    return s * snare.level;
}

/* ======================================================================
 * Effects: a ping-pong echo for the lead, and a reverb
 * ====================================================================== */

#define ECHO_SIZE 24000 /* more than 3 16ths at 80 BPM */
static float echo_line[2][ECHO_SIZE]; static int echo_pos; static float echo_tone;

/* A small Schroeder reverb: four combs and two allpasses for each side */
#define COMBS 4
#define ALLPASSES 2
static const int comb_len[COMBS] = { 1116, 1188, 1277, 1356 }; static const int allpass_len[ALLPASSES] = { 556, 441 }; static float comb_buf[2][COMBS][1400], allpass_buf[2][ALLPASSES][600]; static int comb_pos[2][COMBS], allpass_pos[2][ALLPASSES]; static float comb_damp[2][COMBS];

static float reverb(int side, float in) {
    float out = 0.0f;
    for (int i = 0; i < COMBS; i++) {
        float *buf = comb_buf[side][i];
        int *pos = &comb_pos[side][i];
        float y = buf[*pos];
        comb_damp[side][i] = y * 0.6f + comb_damp[side][i] * 0.4f;
        buf[*pos] = in + comb_damp[side][i] * 0.84f;
        if (++*pos >= comb_len[i] + side * 23) *pos = 0; /* the right side a bit longer */
        out += y;
    }
    for (int i = 0; i < ALLPASSES; i++) {
        float *buf = allpass_buf[side][i];
        int *pos = &allpass_pos[side][i];
        float b = buf[*pos];
        buf[*pos] = out + b * 0.5f;
        out = b - out;
        if (++*pos >= allpass_len[i] + side * 23) *pos = 0;
    }
    return out;
}

/* ======================================================================
 * The piece
 * ====================================================================== */

enum { PART_INTRO, PART_MARCH, PART_RIFF, PART_SOLO, PART_BREAK, PART_HITS, PART_TURN, PART_COUNT };

typedef struct { int8_t bass; int8_t tones[4]; } chord_t; /* MIDI notes */ typedef struct { uint8_t bar, step, note, len; } event_t; /* a lead note; len in 16ths */

typedef struct {
    const char *name, *metre;
    int bars, steps; /* steps: 16th notes in a bar */
    const chord_t *chords; /* one for each bar */
    const event_t *lead;
    int lead_count;
    int leslie_fast;
} part_t;

#define CH_EM { 40, { 55, 59, 64, 67 } }
#define CH_CMAJ7 { 36, { 55, 59, 64, 67 } }
#define CH_AM { 45, { 57, 60, 64, 69 } }
#define CH_B { 35, { 54, 59, 63, 66 } }
#define CH_D { 38, { 54, 57, 62, 66 } }
#define CH_C { 36, { 55, 60, 64, 67 } }
#define CH_F { 41, { 57, 60, 65, 69 } }
#define CH_G { 43, { 55, 59, 62, 67 } }
#define CH_B7 { 47, { 54, 57, 63, 66 } }
#define CH_E7 { 40, { 56, 59, 62, 64 } }
#define ROOT(n) { n, { 0, 0, 0, 0 } }

static const chord_t intro_chords[] = { CH_EM, CH_CMAJ7, CH_AM, CH_B }; static const chord_t march_chords[] = { CH_EM, CH_EM, CH_D, CH_D, CH_C, CH_C, CH_B, CH_B }; static const chord_t riff_chords[] = { ROOT(40), ROOT(40), ROOT(36), ROOT(36), ROOT(38), ROOT(38), ROOT(35), ROOT(35) }; static const chord_t solo_chords[] = { CH_AM, CH_F, CH_C, CH_G, CH_AM, CH_F, CH_B7, CH_B7 }; static const chord_t break_chords[] = { CH_B7, CH_B7 }; static const chord_t hits_chords[] = { CH_B }; static const chord_t turn_chords[] = { CH_E7 };

/* The riff in 7/8: an eighth note each, above the root of the bar */ static const int8_t riff[2][7] = { { 0, 0, 3, 0, 5, 3, 7 }, { 0, 0, 3, 0, 10, 8, 7 } };

/* Organ stabs in the MARCH: 3 + 3 + 2 against the straight bass */
#define MARCH_STABS (BIT(0) | BIT(3) | BIT(6) | BIT(8) | BIT(11) | BIT(14))

/* Bass in the SOLO: above the root, -1 is no note */ static const int8_t solo_bass[16] = { 0, -1, -1, 0, -1, -1, 7, -1, 12, -1, 0, -1, -1, -1, 7, -1 };

static const event_t march_lead[] = {
    { 0, 0, 71, 6 }, { 0, 6, 76, 2 }, { 0, 8, 79, 4 }, { 0, 12, 78, 4 },
    { 1, 0, 76, 4 }, { 1, 4, 74, 2 }, { 1, 6, 76, 2 }, { 1, 8, 71, 8 },
    { 2, 0, 69, 6 }, { 2, 6, 74, 2 }, { 2, 8, 78, 4 }, { 2, 12, 76, 4 },
    { 3, 0, 74, 4 }, { 3, 4, 73, 2 }, { 3, 6, 74, 2 }, { 3, 8, 69, 8 },
    { 4, 0, 67, 4 }, { 4, 4, 72, 4 }, { 4, 8, 76, 4 }, { 4, 12, 79, 4 },
    { 5, 0, 78, 4 }, { 5, 4, 76, 4 }, { 5, 8, 74, 4 }, { 5, 12, 72, 4 },
    { 6, 0, 71, 4 }, { 6, 4, 75, 4 }, { 6, 8, 78, 4 }, { 6, 12, 81, 4 },
    { 7, 0, 83, 12 },
};

static const event_t riff_lead[] = { { 0, 0, 76, 28 }, { 2, 0, 79, 28 }, { 4, 0, 81, 28 }, { 6, 0, 78, 24 } };

/* TURN: a run down to the leading tone G#, which goes up to the A of the SOLO */ static const event_t turn_lead[] = { { 0, 8, 76, 2 }, { 0, 10, 74, 2 }, { 0, 12, 71, 2 }, { 0, 14, 68, 2 } };

static const event_t solo_lead[] = {
    { 0, 0, 69, 2 }, { 0, 2, 72, 2 }, { 0, 4, 76, 2 }, { 0, 6, 81, 4 }, { 0, 10, 79, 2 }, { 0, 12, 76, 4 },
    { 1, 0, 77, 6 }, { 1, 6, 76, 2 }, { 1, 8, 72, 2 }, { 1, 10, 69, 2 }, { 1, 12, 72, 4 },
    { 2, 0, 76, 2 }, { 2, 2, 79, 2 }, { 2, 4, 84, 6 }, { 2, 10, 83, 2 }, { 2, 12, 79, 4 },
    { 3, 0, 74, 4 }, { 3, 4, 71, 2 }, { 3, 6, 74, 2 }, { 3, 8, 79, 8 },
    { 4, 0, 81, 2 }, { 4, 2, 83, 2 }, { 4, 4, 84, 4 }, { 4, 8, 83, 2 }, { 4, 10, 81, 2 }, { 4, 12, 76, 4 },
    { 5, 0, 77, 2 }, { 5, 2, 79, 2 }, { 5, 4, 81, 4 }, { 5, 8, 79, 2 }, { 5, 10, 77, 2 }, { 5, 12, 72, 4 },
    { 6, 0, 75, 4 }, { 6, 4, 78, 4 }, { 6, 8, 81, 4 }, { 6, 12, 83, 4 },
    { 7, 0, 83, 8 }, { 7, 8, 81, 2 }, { 7, 10, 78, 2 }, { 7, 12, 75, 4 }, };

static const part_t parts[PART_COUNT] = {
    [PART_INTRO] = { "INTRO", "4/4", 4, 16, intro_chords, 0, 0, 0 },
    [PART_MARCH] = { "MARCH", "4/4", 8, 16, march_chords, march_lead, COUNT(march_lead), 1 },
    [PART_RIFF] = { "RIFF", "7/8", 8, 14, riff_chords, riff_lead, COUNT(riff_lead), 1 },
    [PART_SOLO] = { "SOLO", "4/4", 8, 16, solo_chords, solo_lead, COUNT(solo_lead), 0 },
    [PART_BREAK] = { "BREAK", "4/4", 2, 16, break_chords, 0, 0, 0 },
    [PART_HITS] = { "HITS", "4/4", 1, 16, hits_chords, 0, 0, 1 },
    [PART_TURN] = { "TURN", "4/4", 1, 16, turn_chords, turn_lead, COUNT(turn_lead), 0 }, };

typedef struct { int part, lead, drums_from; } slot_t; /* drums_from: the first bar with drums */ static const slot_t arrangement[] = {
    { PART_INTRO, 0, 0 }, { PART_MARCH, 0, 0 }, { PART_MARCH, 1, 0 },
    { PART_HITS, 0, 0 }, { PART_RIFF, 0, 1 }, { PART_RIFF, 1, 0 },
    { PART_TURN, 1, 0 }, { PART_SOLO, 1, 0 }, { PART_BREAK, 0, 0 }, };
#define SLOTS COUNT(arrangement)
#define LOOP_START 2 /* after the BREAK: the MARCH with the lead */

/* Where ALGORITHM jumps to: the RIFF and the SOLO come with their way in */ static const struct { int slot, part; } jumps[] = {
    { 0, PART_INTRO }, { 2, PART_MARCH }, { 3, PART_RIFF }, { 6, PART_SOLO }, { 8, PART_BREAK }, };

/* ---- Sequencer ---- */

static int playing;
static volatile int play_request; /* set by loop(), done by audio() */ static int slot, bar, step; static int next_slot = -1, next_jump = -1; /* a jump chosen with ALGORITHM, at the next bar */ static int tempo = 138; static float samples_per_step, countdown;

static int accented(int part, int s) { return part == PART_RIFF ? s == 0 || s == 4 || s == 8 : s % 4 == 0; }

static int gate_of(float steps) {
    int g = (int)(steps * samples_per_step);
    return g > 0 ? g : 1;
}

static void organ_chord(const chord_t *c, float level, int gate) {
    for (int t = 0; t < 4; t++) organ_on(c->tones[t], level, gate, KEY_BAND); }

static void slot_started(void) { leslie_fast = parts[arrangement[slot].part].leslie_fast; }

static void set_playing(int on) {
    playing = on;
    if (on) {
        slot = bar = step = 0;
        countdown = 0.0f;
        next_slot = next_jump = -1;
        slot_started();
    } else {
        organ_release(KEY_BAND);
        if (lead.stage == LEAD_ATTACK || lead.stage == LEAD_SUSTAIN) lead.stage = LEAD_RELEASE;
        lead.gate_left = 0;
        bass.gate_left = 0;
    }
}

static void play_step(void) {
    const slot_t *sl = &arrangement[slot];
    const part_t *p = &parts[sl->part];
    const chord_t *c = &p->chords[bar];
    const int s = step, last_bar = bar == p->bars - 1;

    if (bar == sl->drums_from && s == 0 && sl->part != PART_INTRO && sl->part != PART_HITS) crash_hit(1.0f);

    switch (sl->part) {
    case PART_INTRO:
        if (s == 0) {
            organ_chord(c, 0.9f, gate_of(p->steps - 0.3f));
            bass_on(c->bass, 0.8f, gate_of(p->steps - 0.5f));
        }
        if (bar == 0 && s == 0) crash_hit(0.5f);
        if (bar == 3 && s == 0) leslie_fast = 1; /* the speaker runs up into the MARCH */
        if (bar == 3 && s >= 8) {
            tom_hit((s - 8) / 3, 0.55f + (s - 8) * 0.05f);
        } else if (bar >= 2) {
            if (s % 4 == 0) kick_hit(0.8f);
            if (s % 2 == 0) hat_hit(0, s % 4 == 0 ? 0.5f : 0.3f);
        }
        break;

    case PART_MARCH:
        if (s % 2 == 0) bass_on(c->bass + (s == 14 ? 12 : 0), s % 4 == 0 ? 1.0f : 0.75f, gate_of(1.4f));
        if (MARCH_STABS & BIT(s)) organ_chord(c, s % 8 == 0 ? 1.0f : 0.8f, gate_of(s % 8 == 0 ? 2.5f : 1.4f));
        if (s == 0 || s == 6 || s == 8) kick_hit(1.0f);
        if (s == 4 || s == 12) snare_hit(1.0f);
        if (last_bar && s >= 13) snare_hit(0.5f + (s - 13) * 0.2f);
        if (s % 2 == 0) hat_hit(s == 14, s % 4 == 0 ? 0.55f : 0.35f);
        break;

    case PART_RIFF: {
        int with_drums = bar >= sl->drums_from;
        if (s % 2 == 0) {
            int e = s / 2; /* the eighth note, 0 to 6 */
            int n = c->bass + riff[bar & 1][e];
            float level = e == 0 || e == 2 || e == 4 ? 1.0f : 0.75f;
            bass_on(n, level, gate_of(1.5f));
            organ_on(n + 24, level, gate_of(1.5f), KEY_BAND);
            organ_on(n + 31, level * 0.8f, gate_of(1.5f), KEY_BAND);
            organ_on(n + 36, level * 0.6f, gate_of(1.5f), KEY_BAND);
            if (with_drums) hat_hit(0, level * 0.5f);
        }
        if (!with_drums) break;
        if (last_bar && s >= 8) {
            tom_hit((s - 8) / 2, 0.6f + (s - 8) * 0.06f);
        } else {
            if (s == 0 || s == 8) kick_hit(1.0f);
            if (s == 4 || s == 12) snare_hit(1.0f);
        }
        break;
    }

    case PART_SOLO:
        if (s == 0) organ_chord(c, 0.85f, gate_of(p->steps - 0.2f));
        if (solo_bass[s] >= 0) bass_on(c->bass + solo_bass[s], s == 0 ? 1.0f : 0.8f, gate_of(1.6f));
        if (s == 0 || s == 3 || s == 8 || s == 10) kick_hit(s == 0 ? 1.0f : 0.8f);
        if (s == 4 || s == 12) snare_hit(1.0f);
        else if (last_bar && s >= 8) snare_hit(0.35f + (s - 8) * 0.08f);
        hat_hit(0, s % 2 == 0 ? 0.4f : 0.22f);
        break;

    case PART_BREAK:
        if (bar == 0) {
            if (s == 0) {
                organ_chord(c, 1.0f, gate_of(2 * p->steps - 0.3f));
                bass_on(c->bass, 1.0f, gate_of(3.0f));
                kick_hit(1.0f);
            }
            if (s >= 4) tom_hit((s - 4) / 4, 0.5f + (s - 4) * 0.04f);
        } else {
            if (s == 0) leslie_fast = 1;
            snare_hit(0.25f + s * 0.045f);
            if (s % 4 == 0) {
                kick_hit(1.0f);
                bass_on(c->bass - 12, 1.0f, gate_of(1.5f));
            }
        }
        break;

    case PART_HITS: /* three hits of the whole band, then a rest */
        if (s == 0 || s == 3 || s == 6) {
            organ_chord(c, 1.0f, gate_of(1.2f));
            bass_on(c->bass, 1.0f, gate_of(1.2f));
            kick_hit(1.0f);
            snare_hit(0.8f);
            crash_hit(s == 6 ? 1.0f : 0.8f);
        }
        break;

    case PART_TURN: /* E7, the way into the A minor of the SOLO; the speaker slows down */
        if (s == 0) {
            organ_chord(c, 0.9f, gate_of(p->steps - 0.3f));
            bass_on(c->bass, 1.0f, gate_of(6.0f));
            kick_hit(1.0f);
        }
        if (s == 12) bass_on(c->bass + 4, 0.9f, gate_of(3.5f)); /* G#, up to the A */
        if (s >= 4) tom_hit((s - 4) / 4, 0.45f + (s - 4) * 0.04f);
        break;
    }

    if (sl->lead)
        for (int e = 0; e < p->lead_count; e++)
            if (p->lead[e].bar == bar && p->lead[e].step == s)
                lead_on(p->lead[e].note, 0.9f, gate_of(p->lead[e].len * 0.94f)); }

static void advance(void) {
    const part_t *p = &parts[arrangement[slot].part];
    if (++step < p->steps) return;
    step = 0;
    if (next_slot >= 0) {
        slot = next_slot;
        next_slot = next_jump = -1;
        bar = 0;
        slot_started();
        return;
    }
    if (++bar < p->bars) return;
    bar = 0;
    if (++slot == SLOTS) slot = LOOP_START;
    slot_started();
}

/* ======================================================================
 * Audio
 * ====================================================================== */

static int knob[FM1_NENC] = { [FM1_ENC_KNOB1] = 5, [FM1_ENC_KNOB2] = 7, [FM1_ENC_KNOB3] = 5, [FM1_ENC_KNOB4] = 9 }; static int octave; static float gain; /* follows MASTER smoothly */

enum { M_ORGAN, M_BASS, M_LEAD, M_KICK, M_SNARE, M_HATS, M_OUT, METERS }; static float meter[METERS];

#define SCOPE_RING 2048
static float scope_ring[SCOPE_RING];
static uint32_t scope_pos, audio_blocks;

static uint32_t last_keys;

static void read_keys(void) {
    uint32_t keys = fm1_in.notes;
    uint32_t pressed = fm1_input_note_edges() | (keys & ~last_keys);
    uint32_t released = last_keys & ~keys;
    for (int n = 0; n < (int)FM1_NOTE_KEYS; n++) {
        if ((pressed >> n) & 1u) {
            organ_release(n);
            /* A press that already ended still sounds for a moment */
            organ_on(53 + 12 * octave + n, 1.0f, (keys >> n) & 1u ? 0 : (int)(0.25f * RATE), n);
        } else if ((released >> n) & 1u) {
            organ_release(n);
        }
    }
    last_keys = keys;
}

static void read_midi(void) {
    uint32_t packet;
    while (fm1_midi_take(&packet)) {
        uint32_t type = FM1_MIDI_STATUS(packet) & 0xF0u;
        int note = (int)FM1_MIDI_DATA1(packet);
        uint32_t velocity = FM1_MIDI_DATA2(packet);
        if (type == 0x90u && velocity > 0) {
            organ_release(1000 + note);
            organ_on(note, 0.5f + velocity / 254.0f, 0, 1000 + note);
        } else if (type == 0x80u || type == 0x90u) {
            organ_release(1000 + note);
        }
    }
}

void audio(int32_t *out, int frames) {
    if (play_request) {
        play_request = 0;
        set_playing(!playing);
    }
    samples_per_step = RATE * 60.0f / (tempo * 4.0f);
    int echo_delay = (int)(3.0f * samples_per_step); /* a dotted eighth */
    if (echo_delay > ECHO_SIZE - 1) echo_delay = ECHO_SIZE - 1;
    read_keys();
    read_midi();

    float drive = 0.6f + knob[FM1_ENC_KNOB1] * 0.35f, drive_norm = 1.0f / tanhf(drive);
    lead_bright = knob[FM1_ENC_KNOB2] / 12.0f;
    float space = knob[FM1_ENC_KNOB3] / 12.0f * 1.5f;
    float drums = knob[FM1_ENC_KNOB4] / 12.0f;
    float master = fm1_master() / (float)FM1_MASTER_MAX;
    float target_gain = master * master * 0.5f;
    float sum[M_OUT] = { 0 };
    float peak = 0.0f;

    for (int i = 0; i < frames; i++) {
        if (playing) {
            countdown -= 1.0f;
            if (countdown <= 0.0f) {
                play_step();
                advance();
                countdown += samples_per_step;
            }
        }

        float org = tanhf(organ_sample() * 0.35f * drive) * drive_norm;
        float ol, orr;
        leslie(org, &ol, &orr);
        float b = bass_sample();
        float ld = lead_sample();
        float k = tanhf(drum_sample(&kick, 0.9988f) * 1.6f);
        float sn = snare_sample();
        float h = cymbal_sample(&hat), cr = cymbal_sample(&crash);
        float t0 = drum_sample(&toms[0], 0.9997f), t1 = drum_sample(&toms[1], 0.9997f);
        float t2 = drum_sample(&toms[2], 0.9997f);
        float dl = (k + sn * 0.55f + h * 0.35f + cr * 0.6f + t0 * 0.35f + t1 * 0.5f + t2 * 0.65f) * drums;
        float dr = (k + sn * 0.45f + h * 0.55f + cr * 0.4f + t0 * 0.65f + t1 * 0.5f + t2 * 0.35f) * drums;

        /* Ping-pong echo: the lead goes into the left line, which feeds the right
         * line, which feeds the left line again, a little darker each time */
        int back = echo_pos - echo_delay;
        if (back < 0) back += ECHO_SIZE;
        float el = echo_line[0][back], er = echo_line[1][back];
        echo_tone += (er - echo_tone) * 0.35f;
        echo_line[0][echo_pos] = ld + echo_tone * 0.45f;
        echo_line[1][echo_pos] = el;
        if (++echo_pos == ECHO_SIZE) echo_pos = 0;

        float send = (ol + orr) * 0.3f + ld * 0.6f + (sn + t0 + t1 + t2) * 0.4f * drums + (el + er) * 0.2f;
        float rl = reverb(0, send * 0.03f), rr = reverb(1, send * 0.03f);

        float left = ol * 0.6f + b * 0.55f + ld * 0.4f + el * 0.25f + dl * 0.75f + rl * space;
        float right = orr * 0.6f + b * 0.55f + ld * 0.4f + er * 0.25f + dr * 0.75f + rr * space;

        sum[M_ORGAN] += fabsf(org);
        sum[M_BASS] += fabsf(b);
        sum[M_LEAD] += fabsf(ld);
        sum[M_KICK] += fabsf(k * drums);
        sum[M_SNARE] += fabsf(sn * drums);
        sum[M_HATS] += fabsf((h + cr) * drums);
        if (fabsf(left) > peak) peak = fabsf(left);
        if (fabsf(right) > peak) peak = fabsf(right);

        /* tanhf: a soft limiter instead of clipping */
        gain += (target_gain - gain) * 0.002f;
        out[2 * i] = (int32_t)(tanhf(left * 0.5f) * gain * FM1_AUDIO_MAX);
        out[2 * i + 1] = (int32_t)(tanhf(right * 0.5f) * gain * FM1_AUDIO_MAX);

        scope_ring[scope_pos] = tanhf((left + right) * 0.5f);
        scope_pos = (scope_pos + 1) & (SCOPE_RING - 1);
    }

    for (int m = 0; m < METERS; m++) {
        float level = m == M_OUT ? peak * 0.5f : sum[m] / frames * 1.6f;
        meter[m] = level > meter[m] * 0.96f ? level : meter[m] * 0.96f;
    }
    audio_blocks++;
}

/* ======================================================================
 * Screen
 * ====================================================================== */

#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define BLACK RGB(0, 0, 0)
#define DARK RGB(40, 40, 52)
#define DIM RGB(120, 120, 135)
#define WHITE RGB(255, 255, 255)
#define ORANGE RGB(255, 140, 40)
#define RED RGB(255, 50, 50)

static const uint16_t part_colour[PART_COUNT] = {
    RGB(170, 120, 255), RGB(255, 140, 40), RGB(255, 70, 70), RGB(70, 210, 255), RGB(255, 220, 60),
    RGB(255, 255, 255), RGB(120, 230, 140), };

/* The layout, top to bottom */
#define TITLE_Y 3
#define PART_Y 17
#define STRIP_Y 31
#define SCOPE_MID 80
#define SCOPE_AMP 34
#define METER_Y 120
#define KNOB_Y 199
#define STATUS_Y 230

/* fm1_lcd_fill_rect, but a rectangle without area is fine */ static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour) {
    if (w > 0 && h > 0) fm1_lcd_fill_rect(x, y, w, h, colour); }

/* A 3 x 5 pixel font, drawn at twice the size. Each octal digit is a row, the
 * top row first; in a row, 4 is the left pixel and 1 the right one. */ static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-./:";
static const uint16_t font[] = {
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011152, /* A to J */
    055655, 044447, 057755, 065555, 025552, 065644, 025563, 065655, 034216, 072222, /* K to T */
    055557, 055552, 055775, 055255, 055222, 071247,                                 /* U to Z */
    075557, 026227, 061247, 061216, 055711, 074616, 034757, 071222, 075757, 075716, /* 0 to 9 */
    002720, 000700, 000002, 011244, 002020,                                         /* + - . / : */
};

/* Draws text in a field of width characters (8 x 10 pixels each), blank after the text. */ static void draw_text(uint32_t x, uint32_t y, const char *text, int width, uint16_t colour) {
    for (int i = 0; i < width; i++, x += 8) {
        char c = *text ? *text++ : ' ';
        fill(x, y, 8, 10, BLACK);
        const char *found = c == ' ' ? 0 : strchr(font_chars, c);
        if (!found) continue;
        uint16_t glyph = font[found - font_chars];
        for (int row = 0; row < 5; row++) {
            int bits = (glyph >> (3 * (4 - row))) & 7;
            for (int column = 0; column < 3; column++)
                if ((bits >> (2 - column)) & 1) fill(x + column * 2, y + row * 2, 2, 2, colour);
        }
    }
}

/* Writes value (0 to 999) and returns the end of the text. */ static char *put_number(char *t, int value) {
    if (value >= 100) *t++ = (char)('0' + value / 100);
    if (value >= 10) *t++ = (char)('0' + value / 10 % 10);
    *t++ = (char)('0' + value % 10);
    *t = 0;
    return t;
}

static void draw_title(void) {
    char text[4];
    draw_text(4, TITLE_Y, "PROG-1", 6, ORANGE);
    draw_text(68, TITLE_Y, registrations[registration].name, 6, WHITE);
    put_number(text, tempo);
    draw_text(180, TITLE_Y, text, 3, WHITE);
    draw_text(208, TITLE_Y, "BPM", 3, DIM); }

static void draw_part(int part, int b, int on, int fast) {
    const part_t *p = &parts[part];
    char text[8];
    draw_text(4, PART_Y, p->name, 5, on ? part_colour[part] : DIM);
    draw_text(52, PART_Y, p->metre, 3, WHITE);
    draw_text(88, PART_Y, "BAR", 3, DIM);
    char *end = put_number(text, b + 1);
    *end++ = '/';
    put_number(end, p->bars);
    draw_text(120, PART_Y, text, 5, WHITE);
    draw_text(204, PART_Y, fast ? "FAST" : "SLOW", 4, fast ? WHITE : DIM); }

/* A light for each 16th note of the bar */ static void draw_strip(int part, int s, int on) {
    const part_t *p = &parts[part];
    uint32_t w = FM1_LCD_WIDTH / (uint32_t)p->steps;
    for (int i = 0; i < p->steps; i++) {
        uint16_t colour = on && i == s ? part_colour[part] : accented(part, i) ? DIM : DARK;
        fill(i * w + 1, STRIP_Y, w - 2, 8, colour);
    }
}

static void draw_status(int on, int next) {
    if (!on) {
        draw_text(4, STATUS_Y, "PLAY: START THE BAND", 20, WHITE);
    } else if (next >= 0) {
        char text[16] = "NEXT: ";
        strcat(text, parts[jumps[next].part].name);
        draw_text(4, STATUS_Y, text, 20, part_colour[jumps[next].part]);
    } else {
        draw_text(4, STATUS_Y, "", 20, DIM);
    }
}

static const char *const meter_names[METERS] = { "ORGAN", "BASS", "LEAD", "KICK", "SNARE", "HATS", "OUT" }; static const uint16_t meter_colour[METERS] = {
    RGB(255, 140, 40), RGB(255, 90, 90), RGB(70, 210, 255), RGB(255, 255, 255), RGB(220, 220, 220),
    RGB(150, 150, 165), RGB(90, 230, 120), }; static int shown_meter[METERS];

static void draw_meter(int m, int width) {
    uint32_t y = METER_Y + 11 * m + 2;
    uint16_t colour = m == M_OUT && width >= 176 ? RED : meter_colour[m];
    fill(56, y, width, 6, colour);
    fill(56 + width, y, 180 - width, 6, DARK); }

static void draw_knob(int k) {
    static const char *const names[4] = { "DRIVE", "LEAD", "SPACE", "DRUMS" };
    int value = knob[FM1_ENC_KNOB1 + k];
    uint32_t x = 6 + k * 60;
    char text[4];
    draw_text(x, KNOB_Y, names[k], 6, DIM);
    fill(x, KNOB_Y + 12, value * 4, 5, ORANGE);
    fill(x + value * 4, KNOB_Y + 12, 48 - value * 4, 5, DARK);
    put_number(text, value);
    draw_text(x, KNOB_Y + 19, text, 3, WHITE); }

/* ---- Oscilloscope ---- */

static uint8_t scope_top[FM1_LCD_WIDTH], scope_bottom[FM1_LCD_WIDTH]; static uint16_t scope_colour; static uint32_t scope_block;

/* Draws the latest samples, one for each column; a column only when it changes. */ static void draw_scope(uint16_t colour) {
    const uint32_t mask = SCOPE_RING - 1;
    uint32_t start = (scope_pos - FM1_LCD_WIDTH) & mask;
    for (uint32_t back = 0; back < 1024; back++) { /* start at a rising zero crossing */
        uint32_t i = (scope_pos - FM1_LCD_WIDTH - back) & mask;
        if (scope_ring[(i - 1) & mask] < 0.0f && scope_ring[i] >= 0.0f) {
            start = i;
            break;
        }
    }
    int previous = SCOPE_MID - (int)(scope_ring[(start - 1) & mask] * SCOPE_AMP);
    for (uint32_t x = 0; x < FM1_LCD_WIDTH; x++) {
        int y = SCOPE_MID - (int)(scope_ring[(start + x) & mask] * SCOPE_AMP);
        int top = y < previous ? y : previous;
        int bottom = (y > previous ? y : previous) + 2;
        previous = y;
        if (top == scope_top[x] && bottom == scope_bottom[x] && colour == scope_colour) continue;
        fill(x, scope_top[x], 1, scope_bottom[x] - scope_top[x], BLACK);
        if (x % 4 == 0) fill(x, SCOPE_MID, 1, 1, DARK); /* a dotted zero line */
        fill(x, top, 1, bottom - top, colour);
        scope_top[x] = (uint8_t)top;
        scope_bottom[x] = (uint8_t)bottom;
    }
    scope_colour = colour;
}

/* ======================================================================
 * Program
 * ====================================================================== */

static int shown_registration = -1, shown_tempo = -1, shown_slot = -1, shown_bar = -1, shown_step = -1; static int shown_playing = -1, shown_leslie = -1, shown_next = -2, shown_part = -1; static int shown_knob[4] = { -1, -1, -1, -1 }; static uint64_t shown_leds;

/* The note key that shows a MIDI note: in the range F3 to G5, moved by octaves. */ static int key_of(int note) {
    int n = note - 53;
    while (n < 0) n += 12;
    while (n >= (int)FM1_NOTE_KEYS) n -= 12;
    return n;
}

static void update_leds(int part) {
    uint64_t leds = fm1_in.buttons;
    if (playing) leds |= 1ull << FM1_BTN_PLAY;
    if (leslie_fast) leds |= 1ull << FM1_BTN_FX;
    if (octave < 0) leds |= 1ull << FM1_BTN_OCT_DOWN;
    if (octave > 0) leds |= 1ull << FM1_BTN_OCT_UP;
    if (playing && accented(part, step)) leds |= 1ull << FM1_BTN_SEQ; /* blinks with the beat */
    leds |= (uint64_t)fm1_in.notes << FM1_KEY_NOTE0;
    /* The keys light up with the music */
    for (int v = 0; v < ORGAN_VOICES; v++)
        if (organ[v].note >= 0 && organ[v].gate) leds |= 1ull << (FM1_KEY_NOTE0 + key_of(organ[v].note));
    if (bass.on && bass.gate_left > 0) leds |= 1ull << (FM1_KEY_NOTE0 + key_of(bass.note));
    if (lead.stage == LEAD_ATTACK || lead.stage == LEAD_SUSTAIN)
        leds |= 1ull << (FM1_KEY_NOTE0 + key_of((int)lead.target));
    uint64_t changed = leds ^ shown_leds;
    for (uint32_t id = 0; id < FM1_NKEY; id++)
        if ((changed >> id) & 1u) fm1_led_key(id, (int)((leds >> id) & 1u));
    shown_leds = leds;
}

static void read_buttons(void) {
    uint32_t pressed = fm1_input_edges(0);
    if (pressed & BIT(FM1_BTN_PLAY)) play_request = 1;
    if (pressed & BIT(FM1_BTN_FX)) leslie_fast = !leslie_fast;
    if ((pressed & BIT(FM1_BTN_OCT_DOWN)) && octave > -2) octave--;
    if ((pressed & BIT(FM1_BTN_OCT_UP)) && octave < 2) octave++; }

static void read_knobs(void) {
    for (uint32_t e = 0; e < FM1_NENC; e++) {
        int turned = fm1_enc_take(e);
        if (!turned) continue;
        if (e == FM1_ENC_SELECT) {
            tempo = clamp(tempo + 2 * turned, 80, 184);
            continue;
        }
        int high = e == FM1_ENC_ALGORITHM ? COUNT(jumps) - 1 : e == FM1_ENC_PRESETS ? REGISTRATIONS - 1 : 12;
        int value = clamp(knob[e] + turned, 0, high);
        if (value == knob[e]) continue;
        knob[e] = value;
        if (e == FM1_ENC_PRESETS) set_registration(value);
        if (e == FM1_ENC_ALGORITHM && playing) {
            next_slot = jumps[value].slot;
            next_jump = value;
        }
    }
}

void setup(void) {
    for (int i = 0; i < SINE_SIZE; i++) sine_table[i] = sinf(i * 2.0f * PI / SINE_SIZE);
    for (int v = 0; v < ORGAN_VOICES; v++) organ[v].note = -1;
    set_registration(0);
    samples_per_step = RATE * 60.0f / (tempo * 4.0f);

    fill(0, 0, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, BLACK);
    for (int m = 0; m < METERS; m++) {
        draw_text(4, METER_Y + 11 * m, meter_names[m], 6, DIM);
        shown_meter[m] = -1;
    }
    for (uint32_t id = 0; id < FM1_NKEY; id++) fm1_led_key(id, 0);
    set_playing(1);
}

void loop(void) {
    read_buttons();
    read_knobs();

    int part = arrangement[slot].part;
    int b = bar, s = step, on = playing, fast = leslie_fast, next = next_jump;

    if (registration != shown_registration || tempo != shown_tempo) {
        draw_title();
        shown_registration = registration;
        shown_tempo = tempo;
    }
    if (slot != shown_slot || b != shown_bar || on != shown_playing || fast != shown_leslie) {
        draw_part(part, b, on, fast);
        shown_bar = b;
        shown_leslie = fast;
    }
    if (part != shown_part) fill(0, STRIP_Y, FM1_LCD_WIDTH, 8, BLACK); /* the strip gets other cells */
    if (part != shown_part || s != shown_step || on != shown_playing) {
        draw_strip(part, s, on);
        shown_step = s;
    }
    if (on != shown_playing || next != shown_next) {
        draw_status(on, next);
        shown_next = next;
    }
    shown_slot = slot;
    shown_part = part;
    shown_playing = on;

    for (int k = 0; k < 4; k++)
        if (knob[FM1_ENC_KNOB1 + k] != shown_knob[k]) {
            draw_knob(k);
            shown_knob[k] = knob[FM1_ENC_KNOB1 + k];
        }

    for (int m = 0; m < METERS; m++) {
        int width = (int)(meter[m] * 45.0f);
        width = clamp(width, 0, 45) * 4;
        if (width != shown_meter[m]) {
            draw_meter(m, width);
            shown_meter[m] = width;
        }
    }

    /* The oscilloscope, at about 43 pictures a second (every 4 audio blocks) */
    if (audio_blocks - scope_block >= 4) {
        scope_block = audio_blocks;
        draw_scope(on ? part_colour[part] : DIM);
    }

    update_leds(part);
}