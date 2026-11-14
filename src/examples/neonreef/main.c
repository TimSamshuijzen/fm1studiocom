/* main.c - NEON REEF - a vertical shooter for the FM-1.
 *
 * You fly up a sea of stars over reefs that float in it. One gun hits all:
 * the swarms in the air and the things that grow on the reefs. At the end of
 * each zone a guardian waits. Shoot it down before it leaves.
 *
 * Computer key   FM-1 key   What
 *   A   D        C4   E4    left, right
 *   W   S        C#4  D4    up, down
 *   J  or  K     B4   C5    fire (hold it)
 *                PLAY       start, pause
 *                MASTER     volume
 *
 * In the air     DARTS weave, MANTAS swoop at you, JELLIES sink and shoot,
 *                DIVERS fall fast, URCHINS (from zone 2) take three hits,
 *                RINGS (from zone 2) turn around a centre.
 *                Shoot all of a swarm for a bonus of 1000.
 * On the reefs   PODS give points. TURRETS shoot at you. A SHELL opens after
 *                four hits and has a pearl of 1000. A BEACON (it blinks, with
 *                a P) lets go of a capsule: catch it for a twin gun that
 *                fires faster. You lose the twin gun with your ship.
 *
 * The sound is in the manner of the sound chips of 1984: three square waves,
 * noise and a drum for the music, and eight more voices for the effects. The
 * tunes are in this file as notes: a melody, and chords from which the bass
 * and the arpeggio are made. The music changes when you have the twin gun,
 * and when a guardian comes.
 *
 * How it is made: the game draws each picture in its own frame buffer, then
 * sends to the screen only the parts of each row that changed. loop() counts
 * the audio blocks as its clock: one picture each 6 blocks, about 29 a second.
 */
#include "fm1.h"
#include <math.h>
#include <string.h>

#define BIT(n) (1u << (n))
#define RGB(r, g, b) (uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3)

#define SW 240
#define SH 240
#define HUD_H 14
#define FRAME_BLOCKS 6    /* audio blocks for one picture */
#define TWO_PI 6.2831853f

/* The note keys of the controls (computer keys A, W, S, D, J, K). */
#define KEY_LEFT 7
#define KEY_UP 8
#define KEY_DOWN 9
#define KEY_RIGHT 11
#define KEY_FIRE 18
#define KEY_FIRE2 19

/* ======================================================================
 * Small tools
 * ====================================================================== */

static uint32_t rng_state = 0x2F6E2B1u;

static uint32_t rnd(void) { /* xorshift */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); } /* 0 to 1 */
static float frange(float a, float b) { return a + (b - a) * frand(); }
static float clampf(float v, float low, float high) { return v < low ? low : v > high ? high : v; }
static int clampi(int v, int low, int high) { return v < low ? low : v > high ? high : v; }
static float absf(float v) { return v < 0 ? -v : v; }

/* ======================================================================
 * Sound
 * ====================================================================== */

/* ---- The tunes ----
 * A melody has one entry for each step (a 16th note): a note, __ to hold the
 * note, or XX for silence. A note is a MIDI number: C5 is 72. */
#define __ 0
#define XX -1
enum {
    Gs4 = 68, A4, As4, B4, C5, Cs5, D5, Ds5, E5, F5, Fs5, G5, Gs5, A5, As5, B5,
    C6, Cs6, D6, Ds6, E6, F6, Fs6, G6, Gs6, A6, As6, B6
};

/* A chord for each half bar (8 steps): the root, 0 (C) to 11 (B), and MINOR. */
#define MINOR 0x80
enum { cC = 0, cD = 2, cE = 4, cF = 5, cG = 7, cA = 9, cB = 11 };

static const int8_t main_lead[16 * 16] = {
    C5, __, E5, G5,  __, E5, G5, __,  C6, __, __, G5,  __, E5, G5, __,
    B5, __, __, G5,  __, D5, G5, __,  B5, __, A5, G5,  __, D5, __, __,
    A5, __, C6, __,  E6, __, C6, A5,  __, C6, __, A5,  E5, __, A5, __,
    F5, __, A5, C6,  __, A5, F5, __,  G5, __, A5, __,  B5, __, __, __,
    C6, __, __, G5,  __, E5, G5, __,  C6, __, D6, __,  E6, __, __, __,
    D6, __, __, B5,  __, G5, B5, __,  D6, __, E6, __,  D6, __, B5, __,
    C6, __, A5, __,  F5, __, A5, __,  C6, __, __, A5,  __, F5, A5, __,
    B5, __, G5, __,  D5, __, G5, __,  B5, __, D6, __,  G6, __, __, XX,

    E5, E5, XX, E5,  XX, E5, A5, __,  __, G5, __, E5,  __, __, XX, XX,
    F5, F5, XX, F5,  XX, F5, A5, __,  __, G5, __, F5,  __, __, XX, XX,
    G5, G5, XX, G5,  XX, G5, B5, __,  __, A5, __, G5,  __, __, D6, __,
    E6, __, __, D6,  __, B5, __, Gs5, __, B5, __, E5,  __, __, __, __,
    A5, __, E5, A5,  __, C6, __, A5,  E6, __, __, C6,  __, A5, __, __,
    A5, __, F5, A5,  __, C6, __, A5,  F6, __, __, C6,  __, A5, __, __,
    D6, __, A5, D6,  __, F6, __, D6,  A5, __, D6, __,  F6, __, E6, __,
    D6, __, __, B5,  __, __, G5, __,  A5, __, B5, __,  D6, __, G6, __,
};
static const uint8_t main_chords[32] = {
    cC, cC, cG, cG, cA | MINOR, cA | MINOR, cF, cG,
    cC, cC, cG, cG, cF, cF, cG, cG,
    cA | MINOR, cA | MINOR, cF, cF, cG, cG, cE, cE,
    cA | MINOR, cA | MINOR, cF, cF, cD | MINOR, cD | MINOR, cG, cG,
};

static const int8_t power_lead[8 * 16] = {
    Fs5, __, A5, __,  D6, __, A5, __,  Fs5, A5, D6, __,  Fs6, __, D6, __,
    E6, __, D6, __,  A5, __, D6, __,  E6, __, Fs6, __,  E6, __, D6, __,
    B5, __, D6, __,  G6, __, D6, __,  B5, D6, G6, __,  B6, __, G6, __,
    A6, __, G6, __,  E6, __, Cs6, __,  A5, __, Cs6, __,  E6, __, A6, __,
    Fs6, __, __, D6,  __, B5, D6, __,  Fs6, __, __, D6,  __, B5, __, __,
    G6, __, __, D6,  __, B5, D6, __,  G6, __, __, D6,  __, B5, __, __,
    E6, __, Cs6, __,  A5, __, Cs6, __,  E6, __, G6, __,  E6, __, Cs6, __,
    D6, __, Fs6, __,  A6, __, Fs6, __,  D6, __, __, __,  D6, D6, __, __,
};
static const uint8_t power_chords[16] = {
    cD, cD, cD, cD, cG, cG, cA, cA,
    cB | MINOR, cB | MINOR, cG, cG, cA, cA, cD, cD,
};

static const int8_t boss_lead[4 * 16] = {
    A5, __, A5, C6,  __, A5, E6, __,  Ds6, __, E6, __,  C6, __, A5, __,
    A5, __, A5, C6,  __, A5, E6, __,  F6, __, E6, __,  C6, __, A5, __,
    F5, __, F5, A5,  __, F5, C6, __,  B5, __, C6, __,  A5, __, F5, __,
    E5, __, Gs5, __,  B5, __, E6, __,  Gs6, __, E6, __,  B5, __, Gs5, __,
};
static const uint8_t boss_chords[8] = {
    cA | MINOR, cA | MINOR, cA | MINOR, cA | MINOR, cF, cF, cE, cE,
};

static const int8_t start_lead[2 * 16] = {
    C5, __, C5, C5,  E5, __, E5, E5,  G5, __, G5, G5,  C6, __, __, __,
    E6, __, D6, __,  C6, __, G5, __,  C6, __, __, __,  __, __, XX, XX,
};
static const uint8_t start_chords[4] = { cC, cC, cG, cC };

static const int8_t clear_lead[2 * 16] = {
    F5, A5, C6, F6,  __, C6, F6, __,  G5, B5, D6, G6,  __, D6, G6, __,
    C6, __, E6, __,  G6, __, E6, __,  C6, __, __, __,  __, __, __, XX,
};
static const uint8_t clear_chords[4] = { cF, cG, cC, cC };

static const int8_t over_lead[2 * 16] = {
    A5, __, __, E5,  __, __, C5, __,  A4, __, __, __,  C5, __, __, __,
    B4, __, __, __,  Gs4, __, __, __,  A4, __, __, __,  __, __, __, XX,
};
static const uint8_t over_chords[4] = { cA | MINOR, cF, cE, cA | MINOR };

enum { SONG_NONE, SONG_START, SONG_MAIN, SONG_POWER, SONG_BOSS, SONG_CLEAR, SONG_OVER, SONGS };
typedef struct {
    const int8_t *lead;
    const uint8_t *chords;
    int steps;     /* its length */
    int tempo;     /* quarter notes in a minute */
    int drums;
    int next;      /* the song after it (itself: it plays again) */
} song_t;
static const song_t songs[SONGS] = {
    { 0, 0, 0, 120, 0, SONG_NONE },
    { start_lead, start_chords, 32, 150, 1, SONG_MAIN },
    { main_lead, main_chords, 256, 150, 1, SONG_MAIN },
    { power_lead, power_chords, 128, 172, 1, SONG_POWER },
    { boss_lead, boss_chords, 64, 164, 1, SONG_BOSS },
    { clear_lead, clear_chords, 32, 150, 1, SONG_NONE },
    { over_lead, over_chords, 32, 96, 0, SONG_NONE },
};

/* ---- From loop() to audio() ---- */

enum {
    SFX_SHOT = 1, SFX_SHOT2, SFX_HIT, SFX_BOOM, SFX_BOOM_GROUND, SFX_POWER, SFX_BONUS, SFX_PEARL,
    SFX_DEATH, SFX_ALARM, SFX_BOSS_HIT, SFX_BOSS_BOOM, SFX_EXTRA, SFX_CAPSULE
};

#define SFX_QUEUE 32u
static volatile uint32_t sfx_queue[SFX_QUEUE];
static volatile uint32_t sfx_written, sfx_taken;
static volatile int music_wanted = -1;  /* the song to start, or -1 */
static volatile int music_paused;
static volatile uint32_t audio_blocks;  /* count of audio() calls: the clock of loop() */

/* pan: 0 is left, 255 is right. */
static void sfx(int id, int pan) {
    if (sfx_written - sfx_taken >= SFX_QUEUE) return;
    sfx_queue[sfx_written % SFX_QUEUE] = (uint32_t)id | (uint32_t)clampi(pan, 0, 255) << 16;
    sfx_written++;
}

static void music(int s) { music_wanted = s; }

/* ---- The voices ---- */

static float note_step[128]; /* cycles in one sample, for each MIDI note */

/* A square wave without the hard edges that would give false tones (PolyBLEP). */
static float soft_edge(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

static float square(float *phase, float dt, float duty) {
    float p = *phase;
    float v = p < duty ? 1.0f : -1.0f;
    float p2 = p - duty;
    if (p2 < 0) p2 += 1.0f;
    v += soft_edge(p, dt) - soft_edge(p2, dt);
    p += dt;
    if (p >= 1.0f) p -= 1.0f;
    *phase = p;
    return v;
}

/* Noise as a sound chip makes it: a shift register that is clocked at a pitch. */
typedef struct { uint32_t bits; float phase, value; } noise_t;

static float noise(noise_t *n, float dt) {
    n->phase += dt > 1.0f ? 1.0f : dt;
    if (n->phase >= 1.0f) {
        n->phase -= 1.0f;
        if (!n->bits) n->bits = 0x4000;
        n->bits = (n->bits >> 1) | (((n->bits ^ (n->bits >> 1)) & 1u) << 14);
        n->value = (n->bits & 1u) ? 1.0f : -1.0f;
    }
    return n->value;
}

/* A voice of the music. */
typedef struct { float phase, dt, level, amp; int held; } channel_t;
static channel_t lead, arp, bass;
static struct { float phase, dt, level; } kick;
static struct { noise_t n; float dt, level, decay; } drum;

static int song, song_step;
static float step_wait; /* samples until the next step */

#define ECHO 5200 /* the echo of the melody: 118 ms */
static float echo_line[ECHO];
static int echo_pos;

static void start_song(int s) {
    song = s;
    song_step = 0;
    step_wait = 0;
    lead.level = arp.level = bass.level = 0;
}

/* One step of the song: sets the notes of the three voices and the drums. */
static void music_step(void) {
    const song_t *s = &songs[song];
    if (!s->steps) return;
    int note = s->lead[song_step];
    if (note > 0) {
        lead.dt = note_step[note];
        lead.level = 1.0f;
        lead.held = 0;
    } else if (note < 0) {
        lead.level = 0;
    } else {
        lead.held++;
    }
    int chord = s->chords[song_step / 8], root = chord & 15;
    const int tones[4] = { 0, (chord & MINOR) ? 3 : 4, 7, 12 };
    static const int8_t arp_order[8] = { 0, 2, 1, 2, 3, 2, 1, 2 };
    arp.dt = note_step[60 + root + tones[arp_order[song_step & 7]]];
    arp.level = 1.0f;
    if (!(song_step & 1)) { /* the bass jumps between octaves, in 8th notes */
        static const int8_t bass_order[8] = { 0, 12, 0, 12, 7, 12, 0, 12 };
        bass.dt = note_step[36 + root + bass_order[(song_step >> 1) & 7]];
        bass.level = 1.0f;
    }
    if (s->drums) {
        int beat = song_step & 7;
        if (beat == 0) { kick.dt = 170.0f / FM1_AUDIO_RATE; kick.level = 1.0f; kick.phase = 0; }
        if (beat == 4) { drum.dt = 5200.0f / FM1_AUDIO_RATE; drum.level = 1.0f; drum.decay = 0.99935f; }
        else if ((song_step & 3) == 2) { drum.dt = 14000.0f / FM1_AUDIO_RATE; drum.level = 0.5f; drum.decay = 0.9975f; }
    }
    if (++song_step >= s->steps) { /* again, or the song after it: on the beat */
        song_step = 0;
        song = s->next;
    }
}

/* A voice of the effects: a square wave or noise that slides in pitch. */
typedef struct {
    int on, is_noise;
    int delay;         /* samples until it starts */
    float t, dt;       /* 0 to 1 through its life */
    float step, slide; /* cycles for each sample, and its change for each sample */
    float volume, duty, phase;
    float left, right;
    noise_t n;
} effect_t;
#define EFFECTS 8
static effect_t effect[EFFECTS];

/* Starts an effect: from f0 to f1 Hz in `seconds`. duty 0: noise. */
static void blip(float delay, float seconds, float f0, float f1, float volume, float duty, int pan) {
    effect_t *e = &effect[0];
    for (int i = 0; i < EFFECTS; i++) { /* a free voice, or else the one nearest to its end */
        if (!effect[i].on) { e = &effect[i]; break; }
        if (effect[i].t > e->t) e = &effect[i];
    }
    float samples = seconds * FM1_AUDIO_RATE;
    e->on = 1;
    e->is_noise = duty == 0;
    e->delay = (int)(delay * FM1_AUDIO_RATE);
    e->t = 0;
    e->dt = 1.0f / samples;
    e->step = f0 / FM1_AUDIO_RATE;
    e->slide = powf(f1 / f0, 1.0f / samples);
    e->volume = volume;
    e->duty = duty;
    e->phase = 0;
    float p = pan / 255.0f;
    e->left = 1.0f - 0.6f * p;
    e->right = 0.4f + 0.6f * p;
}

/* A fast row of notes. */
static void run(const float *notes, int count, float gap, float volume, float duty) {
    for (int i = 0; i < count; i++) blip(i * gap, gap * 1.6f, notes[i], notes[i], volume, duty, 128);
}

static void start_sfx(uint32_t command) {
    int id = command & 255, pan = (command >> 16) & 255;
    static const float power[6] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1318.5f, 1568.0f };
    static const float bonus[2] = { 987.8f, 1318.5f };
    static const float pearl[8] = { 1318.5f, 1568.0f, 1975.5f, 2637.0f, 1568.0f, 1975.5f, 2637.0f, 3136.0f };
    static const float extra[5] = { 784.0f, 1046.5f, 1318.5f, 1568.0f, 2093.0f };
    switch (id) {
    case SFX_SHOT: blip(0, 0.07f, 1500, 420, 0.13f, 0.25f, pan); break;
    case SFX_SHOT2: blip(0, 0.06f, 2300, 700, 0.12f, 0.125f, pan); break;
    case SFX_HIT: blip(0, 0.04f, 950, 600, 0.14f, 0.5f, pan); break;
    case SFX_BOOM: blip(0, 0.28f, 7000, 500, 0.30f, 0, pan); break;
    case SFX_BOOM_GROUND:
        blip(0, 0.40f, 2600, 150, 0.32f, 0, pan);
        blip(0, 0.25f, 140, 45, 0.24f, 0.5f, pan);
        break;
    case SFX_POWER: run(power, 6, 0.05f, 0.20f, 0.5f); break;
    case SFX_BONUS: run(bonus, 2, 0.07f, 0.20f, 0.5f); break;
    case SFX_PEARL: run(pearl, 8, 0.035f, 0.17f, 0.25f); break;
    case SFX_DEATH:
        blip(0, 0.95f, 5000, 90, 0.42f, 0, 128);
        blip(0, 0.70f, 900, 50, 0.24f, 0.5f, 128);
        break;
    case SFX_ALARM:
        blip(0.00f, 0.12f, 880, 880, 0.20f, 0.5f, 128);
        blip(0.13f, 0.12f, 660, 660, 0.20f, 0.5f, 128);
        break;
    case SFX_BOSS_HIT: blip(0, 0.05f, 260, 120, 0.20f, 0.5f, pan); break;
    case SFX_BOSS_BOOM:
        blip(0.0f, 1.40f, 4200, 60, 0.50f, 0, 128);
        blip(0.0f, 1.00f, 220, 30, 0.30f, 0.5f, 128);
        blip(0.3f, 0.80f, 6500, 200, 0.30f, 0, 128);
        break;
    case SFX_EXTRA: run(extra, 5, 0.06f, 0.20f, 0.5f); break;
    case SFX_CAPSULE: blip(0, 0.16f, 600, 1300, 0.18f, 0.5f, pan); break;
    }
}

void audio(int32_t *out, int frames) {
    static float gain;

    while (sfx_taken != sfx_written) {
        start_sfx(sfx_queue[sfx_taken % SFX_QUEUE]);
        sfx_taken++;
    }
    if (music_wanted >= 0) {
        start_song(music_wanted);
        music_wanted = -1;
    }
    float target_gain = fm1_master() / (float)FM1_MASTER_MAX;
    float step_samples = FM1_AUDIO_RATE * 15.0f / songs[song].tempo; /* a 16th note */
    int playing = song != SONG_NONE && !music_paused;
    float waver = 1.0f + 0.006f * sinf(audio_blocks * 0.22f); /* 6 times a second */

    for (int i = 0; i < frames; i++) {
        float left = 0, right = 0;

        if (playing) {
            step_wait -= 1.0f;
            if (step_wait <= 0) {
                step_wait += step_samples;
                music_step();
                step_samples = FM1_AUDIO_RATE * 15.0f / songs[song].tempo;
                playing = song != SONG_NONE;
            }
        } else {
            lead.level = arp.level = bass.level = 0;
        }

        /* The melody: a square wave that wavers a little when a note is long, with an echo. */
        lead.level *= 0.99996f;
        lead.amp += (lead.level - lead.amp) * 0.02f;
        float melody = square(&lead.phase, lead.held >= 2 ? lead.dt * waver : lead.dt, 0.5f) * lead.amp * 0.15f;
        float echo = echo_line[echo_pos];
        echo_line[echo_pos] = melody + echo * 0.3f;
        if (++echo_pos == ECHO) echo_pos = 0;
        left += melody + echo * 0.25f;
        right += melody * 0.8f + echo * 0.45f;

        /* The arpeggio: a thin pulse, plucked on each step. */
        arp.level *= 0.9996f;
        arp.amp += (arp.level - arp.amp) * 0.02f;
        float chord = square(&arp.phase, arp.dt, 0.25f) * arp.amp * 0.075f;
        left += chord * 0.6f;
        right += chord;

        bass.level *= 0.99985f;
        bass.amp += (bass.level - bass.amp) * 0.02f;
        float low = square(&bass.phase, bass.dt, 0.5f) * bass.amp * 0.15f;
        left += low;
        right += low;

        /* The drums: a low tone that falls, and noise. */
        if (kick.level > 0.001f) {
            float thump = sinf(kick.phase * TWO_PI) * kick.level * 0.26f;
            kick.phase += kick.dt;
            kick.dt *= 0.9996f;
            kick.level *= 0.9992f;
            left += thump;
            right += thump;
        }
        if (drum.level > 0.001f) {
            float hiss = noise(&drum.n, drum.dt) * drum.level * 0.085f;
            drum.level *= drum.decay;
            left += hiss;
            right += hiss;
        }

        for (int n = 0; n < EFFECTS; n++) {
            effect_t *e = &effect[n];
            if (!e->on) continue;
            if (e->delay > 0) { e->delay--; continue; }
            float env = 1.0f - e->t;
            float s = e->is_noise ? noise(&e->n, e->step) * env : square(&e->phase, e->step, e->duty);
            s *= env * e->volume;
            left += s * e->left;
            right += s * e->right;
            e->step *= e->slide;
            e->t += e->dt;
            if (e->t >= 1.0f) e->on = 0;
        }

        gain += (target_gain - gain) * 0.002f;
        out[2 * i] = (int32_t)(tanhf(left) * gain * FM1_AUDIO_MAX);
        out[2 * i + 1] = (int32_t)(tanhf(right) * gain * FM1_AUDIO_MAX);
    }
    audio_blocks++;
}

/* ======================================================================
 * Drawing in the frame buffer
 * ====================================================================== */

static uint16_t fb[SW * SH];      /* the picture that is made */
static uint16_t shown[SW * SH];   /* the picture on the screen */
static int shown_valid;

#define WHITE RGB(255, 255, 255)
#define INK RGB(6, 6, 16)

static inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < SW && (unsigned)y < SH) fb[y * SW + x] = c;
}

static void rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    for (int row = 0; row < h; row++) {
        uint16_t *p = &fb[(y + row) * SW + x];
        for (int col = 0; col < w; col++) p[col] = c;
    }
}

/* A filled ellipse. */
static void oval(int cx, int cy, int rx, int ry, uint16_t c) {
    for (int dy = -ry; dy <= ry; dy++) {
        int half = (int)(rx * sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry + 1)) + 0.5f);
        rect(cx - half, cy + dy, 2 * half + 1, 1, c);
    }
}

static void disc(int cx, int cy, int r, uint16_t c) { oval(cx, cy, r, r, c); }

/* A ring: the pixels from r0 to r1 from the centre. */
static void ring(int cx, int cy, int r0, int r1, uint16_t c) {
    for (int dy = -r1; dy <= r1; dy++)
        for (int dx = -r1; dx <= r1; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r1 * r1 && d2 >= r0 * r0) px(cx + dx, cy + dy, c);
        }
}

/* Adds light to a pixel. r, g and b are in the units of RGB565 (31, 63, 31 is full). */
static inline void light(int x, int y, int r, int g, int b) {
    if ((unsigned)x >= SW || (unsigned)y >= SH) return;
    uint16_t c = fb[y * SW + x];
    r += c >> 11;
    g += (c >> 5) & 63;
    b += c & 31;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    fb[y * SW + x] = (uint16_t)(r << 11 | g << 5 | b);
}

/* A round glow in the colour c. strength is 0 to 256. */
static void glow(int cx, int cy, int radius, uint16_t c, int strength) {
    if (cx + radius < 0 || cx - radius >= SW || cy + radius < 0 || cy - radius >= SH) return;
    int r2 = radius * radius, r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int k = (r2 - d2) * 256 / r2; /* 256 in the centre, 0 at the edge */
            k = k * k / 256 * strength / 256;
            light(cx + dx, cy + dy, (r * k) >> 8, (g * k) >> 8, (b * k) >> 8);
        }
}

/* The colour that is t/256 of the way from a to b. */
static uint16_t blend(uint16_t a, uint16_t b, int t) {
    int red = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
    int green = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
    int blue = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
    return (uint16_t)(red << 11 | green << 5 | blue);
}

/* A picture of w x h characters: '.' is open, '1' to '9' are colours of a palette.
 * With `flash`, all of it is white. */
typedef struct { int w, h; const char *rows; } sprite_t;

static void sprite(const sprite_t *s, int cx, int cy, const uint16_t *palette, int flash) {
    int x = cx - s->w / 2, y = cy - s->h / 2;
    for (int row = 0; row < s->h; row++)
        for (int col = 0; col < s->w; col++) {
            char c = s->rows[row * s->w + col];
            if (c != '.') px(x + col, y + row, flash ? WHITE : palette[c - '1']);
        }
}

/* A 3 x 5 pixel font. Each octal digit is one row, the top row first; in a
 * row, 4 is the left pixel and 1 the right one. */
static const char font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.:!/";
static const uint16_t font[] = {
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011152,
    055655, 044447, 057755, 065555, 025552, 065644, 025563, 065655, 034216, 072222,
    055557, 055552, 055775, 055255, 055222, 071247, 075557, 026227, 061247, 061216,
    055711, 074616, 034757, 071222, 075757, 075716, 002720, 000700, 000002, 002020,
    022202, 011244,
};

static void text(int x, int y, const char *s, int scale, uint16_t colour) {
    for (; *s; s++, x += 4 * scale) {
        const char *found = *s == ' ' ? 0 : strchr(font_chars, *s);
        if (!found) continue;
        uint16_t glyph = font[found - font_chars];
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if ((glyph >> (3 * (4 - row) + 2 - col)) & 1u)
                    rect(x + col * scale, y + row * scale, scale, scale, colour);
    }
}

static int text_width(const char *s, int scale) { return ((int)strlen(s) * 4 - 1) * scale; }

/* Text with its centre at x, and a dark shadow. */
static void text_centre(int x, int y, const char *s, int scale, uint16_t colour) {
    x -= text_width(s, scale) / 2;
    text(x + scale, y + scale, s, scale, INK);
    text(x, y, s, scale, colour);
}

/* Writes `value` with `digits` digits (0 at the front), then a 0 character. */
static char *number(char *s, int value, int digits) {
    s[digits] = 0;
    for (int i = digits - 1; i >= 0; i--, value /= 10) s[i] = (char)('0' + value % 10);
    return s;
}

/* Sends the picture to the screen: of each row, only the part that changed. */
static void present(void) {
    for (int y = 0; y < SH; y++) {
        uint16_t *a = &fb[y * SW], *b = &shown[y * SW];
        int first = 0, last = SW - 1;
        if (shown_valid) {
            while (first < SW && a[first] == b[first]) first++;
            if (first == SW) continue;
            while (a[last] == b[last]) last--;
        }
        int w = last - first + 1;
        fm1_lcd_blit((uint32_t)first, (uint32_t)y, (uint32_t)w, 1, a + first, (uint32_t)w);
        memcpy(b + first, a + first, (size_t)w * sizeof *a);
    }
    shown_valid = 1;
}

/* ======================================================================
 * The game
 * ====================================================================== */

#define COLS 15
#define TILE 16
#define ROWS 124              /* the length of a zone in tiles */
#define ZONE_END (ROWS * TILE - 110) /* the guardian comes here */
#define BOSS_HP(z) (40 + 12 * (z))

#define MAX_GROUND 110
#define MAX_AIR 26
#define MAX_SHOTS 10
#define MAX_BULLETS 44
#define MAX_BOOMS 14
#define MAX_PARTICLES 140
#define MAX_POPUPS 5
#define STARS 56
#define GROUPS 8

static uint8_t map[ROWS][COLS]; /* 1: reef. Row 0 is the start of the zone. */

enum { G_NONE, G_POD, G_TURRET, G_SHELL, G_BEACON, G_CRATER };
typedef struct { uint8_t type, hp, flash; int16_t x; int32_t wy; int timer; } ground_t;
static ground_t ground[MAX_GROUND];
static int ground_count;

enum { A_NONE, A_DART, A_MANTA, A_JELLY, A_DIVER, A_URCHIN, A_RING };
typedef struct {
    uint8_t type, hp, flash, group;
    int t;
    float x, y, vx, vy, base;
} air_t;
static air_t air[MAX_AIR];
static struct { int total, killed; } group[GROUPS];

typedef struct { int on; float x, y; } shot_t;
static shot_t shot[MAX_SHOTS];

typedef struct { int on; float x, y, vx, vy; } bullet_t;
static bullet_t bullet[MAX_BULLETS];

typedef struct { int age, span, size, on_ground; float x, y; } boom_t;
static boom_t boom[MAX_BOOMS];

typedef struct { int life, span; float x, y, vx, vy; uint16_t colour; } particle_t;
static particle_t particle[MAX_PARTICLES];

typedef struct { int life; float x, y; char text[8]; uint16_t colour; } popup_t;
static popup_t popup[MAX_POPUPS];

static struct { uint8_t x, speed, bright; float y; } star[STARS];

static struct {
    float x, y;
    int alive;
    int safe;     /* pictures in which nothing can hit it */
    int reload;
    int power;    /* it has the twin gun */
    int wait;     /* pictures until the next ship */
} ship;

static struct { int on; float x, y; int t; } capsule;

enum { B_NONE, B_WARN, B_ENTER, B_FIGHT, B_LEAVE, B_DYING };
static struct { int state, t, hp, flash; float x, y; } boss;

enum { ST_TITLE, ST_PLAY, ST_OVER };
static int state;
static int paused;
static uint32_t frame;
static float scroll;        /* how far the ship is in the zone, in pixels */
static float scroll_speed;
static int zone, score, best = 20000, lives, next_extra;
static int wave_wait, last_kind, next_group;
static int clear_wait;      /* pictures until the next zone */
static int over_wait;
static char banner[24], banner2[24];
static int banner_time;

/* The colours of a zone. */
typedef struct { uint16_t space, base, dark, light, neon, dim; } zone_colours_t;
static const zone_colours_t zone_colours[4] = {
    { RGB(4, 6, 22), RGB(20, 62, 88), RGB(10, 32, 54), RGB(44, 100, 124), RGB(70, 255, 224), RGB(30, 130, 130) },
    { RGB(10, 4, 22), RGB(66, 38, 100), RGB(36, 18, 62), RGB(104, 68, 144), RGB(255, 100, 224), RGB(140, 50, 130) },
    { RGB(14, 6, 8), RGB(88, 58, 28), RGB(52, 30, 14), RGB(130, 94, 48), RGB(255, 206, 70), RGB(150, 100, 30) },
    { RGB(4, 12, 10), RGB(26, 84, 52), RGB(12, 46, 30), RGB(52, 124, 80), RGB(150, 255, 100), RGB(70, 140, 50) },
};
static const zone_colours_t *colours(void) { return &zone_colours[(zone - 1) & 3]; }

static int reef(int r, int c) { return r >= 0 && r < ROWS && c >= 0 && c < COLS && map[r][c]; }

/* The place on the screen of a place in the zone. */
static int ground_y(int wy) { return SH - (wy - (int)scroll); }

/* ---- Making a zone ---- */

static void add_ground(int type, int r, int c) {
    if (ground_count >= MAX_GROUND) return;
    ground_t *g = &ground[ground_count++];
    g->type = (uint8_t)type;
    g->hp = type == G_SHELL ? 4 : type == G_TURRET ? 2 : 1;
    g->flash = 0;
    g->x = (int16_t)(c * TILE + TILE / 2);
    g->wy = r * TILE + TILE / 2;
    g->timer = 30 + (int)(rnd() % 60);
}

/* Reefs of many forms, with things on them. Each zone is the same in each game. */
static void make_zone(int z) {
    rng_state = 0xC0FFEEu + (uint32_t)z * 7919u;
    memset(map, 0, sizeof map);
    memset(ground, 0, sizeof ground);
    ground_count = 0;
    int turret_chance = clampi(18 + 6 * z, 0, 46);
    for (int r = 10, island = 0; r < ROWS - 14; island++) {
        int h = 5 + (int)(rnd() % 8), w = 4 + (int)(rnd() % 7);
        int left = (int)(rnd() % (uint32_t)(COLS - w + 1));
        int most = w >= 6 ? 2 : 1; /* how far the sides can go in */
        int in_left = 1, in_right = 1;
        for (int j = 0; j < h; j++) {
            if (j == 0 || j == h - 1) {
                in_left = in_right = 1;
            } else {
                in_left = clampi(in_left + (int)(rnd() % 3) - 1, 0, most);
                in_right = clampi(in_right + (int)(rnd() % 3) - 1, 0, most);
            }
            for (int c = left + in_left; c <= left + w - 1 - in_right; c++) map[r + j][c] = 1;
        }
        /* Things on the tiles that have reef on all sides. */
        int beacon = island % 3 == 0;
        for (int j = 1; j < h - 1; j++)
            for (int c = left + 1; c < left + w - 1; c++) {
                int rr = r + j;
                if (((rr + c) & 1) || !reef(rr, c) || !reef(rr + 1, c) || !reef(rr - 1, c) || !reef(rr, c + 1) || !reef(rr, c - 1)) continue;
                if (beacon) { add_ground(G_BEACON, rr, c); beacon = 0; continue; }
                int roll = (int)(rnd() % 100);
                if (roll >= 62) continue;
                int kind = (int)(rnd() % 100);
                add_ground(kind < turret_chance ? G_TURRET : kind < turret_chance + 16 ? G_SHELL : G_POD, rr, c);
            }
        r += h + 2 + (int)(rnd() % 4);
    }
}

/* ---- Effects ---- */

static int pan_of(float x) { return clampi((int)(x * 255.0f / SW), 0, 255); }

static void add_particle(float x, float y, float vx, float vy, int life, uint16_t colour) {
    static int next;
    particle_t *p = &particle[next];
    next = (next + 1) % MAX_PARTICLES;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->span = life;
    p->colour = colour;
}

static void burst(float x, float y, int count, float speed, uint16_t colour) {
    for (int i = 0; i < count; i++) {
        float a = frange(0, TWO_PI), s = frange(0.3f, 1.0f) * speed;
        add_particle(x, y, cosf(a) * s, sinf(a) * s, 8 + (int)(rnd() % 14), (rnd() & 3) ? colour : WHITE);
    }
}

static void add_boom(float x, float y, int size, int on_ground) {
    static int next;
    boom_t *b = &boom[next];
    next = (next + 1) % MAX_BOOMS;
    b->x = x; b->y = y;
    b->size = size;
    b->on_ground = on_ground;
    b->age = 0;
    b->span = 9 + size / 4;
}

static void add_popup(float x, float y, int points, uint16_t colour) {
    static int next;
    popup_t *p = &popup[next];
    next = (next + 1) % MAX_POPUPS;
    p->x = clampf(x, 12, SW - 12);
    p->y = y;
    p->life = 34;
    p->colour = colour;
    number(p->text, points, points >= 10000 ? 5 : points >= 1000 ? 4 : 3);
}

static void say(const char *line1, const char *line2, int time) {
    strcpy(banner, line1);
    strcpy(banner2, line2);
    banner_time = time;
}

static void add_score(int points) {
    score += points;
    if (score > best) best = score;
    if (score >= next_extra) {
        next_extra += 40000;
        if (lives < 6) lives++;
        sfx(SFX_EXTRA, 128);
    }
}

/* ---- Game flow ---- */

static void play_song(void) { music(boss.state ? SONG_BOSS : ship.power ? SONG_POWER : SONG_MAIN); }

static void start_zone(int z) {
    zone = z;
    make_zone(z);
    memset(air, 0, sizeof air);
    memset(bullet, 0, sizeof bullet);
    capsule.on = 0;
    boss.state = B_NONE;
    scroll = 0;
    scroll_speed = clampf(1.0f + 0.08f * (z - 1), 1.0f, 1.6f);
    wave_wait = 70;
    clear_wait = 0;
    char line[12] = "ZONE ";
    number(line + 5, z, z > 9 ? 2 : 1);
    say(line, "", 75);
}

static void place_ship(void) {
    ship.x = SW / 2;
    ship.y = 200;
    ship.alive = 1;
    ship.safe = 70;
    ship.reload = 0;
}

static void start_game(void) {
    memset(shot, 0, sizeof shot);
    memset(boom, 0, sizeof boom);
    memset(particle, 0, sizeof particle);
    memset(popup, 0, sizeof popup);
    score = 0;
    lives = 3;
    next_extra = 20000;
    paused = 0;
    music_paused = 0;
    ship.power = 0;
    place_ship();
    state = ST_PLAY;
    start_zone(1);
    music(SONG_START);
}

static void kill_ship(void) {
    ship.alive = 0;
    add_boom(ship.x, ship.y, 26, 0);
    burst(ship.x, ship.y, 40, 3.6f, RGB(255, 190, 80));
    sfx(SFX_DEATH, 128);
    lives--;
    ship.wait = 50;
    if (ship.power) {
        ship.power = 0;
        if (!boss.state) music(SONG_MAIN);
    }
}

/* ---- Bullets of the enemies ---- */

static void fire_at(float x, float y, float angle, float speed) {
    if (y < HUD_H || y > 200) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *b = &bullet[i];
        if (b->on) continue;
        b->on = 1;
        b->x = x; b->y = y;
        b->vx = cosf(angle) * speed;
        b->vy = sinf(angle) * speed;
        return;
    }
}

/* A bullet to the ship. `turn` moves its aim, in radians. */
static void fire_ship(float x, float y, float turn, float speed) {
    if (!ship.alive) return;
    fire_at(x, y, atan2f(ship.y - y, ship.x - x) + turn, speed);
}

static float zone_speed(void) { return clampf(0.1f * (zone - 1), 0, 0.8f); }

/* ---- Enemies in the air ---- */

static air_t *add_air(int type, float x, float y, int g) {
    for (int i = 0; i < MAX_AIR; i++) {
        air_t *e = &air[i];
        if (e->type) continue;
        memset(e, 0, sizeof *e);
        e->type = (uint8_t)type;
        e->hp = type == A_URCHIN ? 3 : type == A_JELLY ? 2 : 1;
        e->group = (uint8_t)g;
        e->x = e->base = x;
        e->y = y;
        group[g].total++;
        return e;
    }
    return 0;
}

/* A new swarm comes in at the top. */
static void spawn_wave(void) {
    int kinds = zone >= 2 ? 6 : 4;
    int kind = 1 + (int)(rnd() % (uint32_t)kinds);
    if (kind == last_kind) kind = kind % kinds + 1;
    last_kind = kind;
    int g = next_group = (next_group + 1) % GROUPS;
    group[g].total = group[g].killed = 0;
    air_t *e;
    switch (kind) {
    case A_DART: {
        float x = 50 + (float)(rnd() % 140);
        for (int i = 0; i < 5; i++) add_air(A_DART, x, -8.0f - i * 15, g);
        break;
    }
    case A_MANTA: {
        float x = 30 + (float)(rnd() % 180);
        for (int i = 0; i < 4; i++)
            if ((e = add_air(A_MANTA, x, -8.0f - i * 24, g))) e->vy = 3.0f;
        break;
    }
    case A_JELLY: {
        int count = zone > 2 ? 3 : 2;
        float x = 30 + (float)(rnd() % 50);
        for (int i = 0; i < count; i++) add_air(A_JELLY, x + i * (150.0f / count), -10.0f - i * 10, g);
        break;
    }
    case A_DIVER:
        for (int i = 0; i < 3; i++) add_air(A_DIVER, 20 + (float)(rnd() % 200), -10.0f - i * 42, g);
        break;
    case A_URCHIN:
        add_air(A_URCHIN, 30 + (float)(rnd() % 80), -10, g);
        add_air(A_URCHIN, 130 + (float)(rnd() % 80), -34, g);
        break;
    case A_RING: {
        float cx = 70 + (float)(rnd() % 100);
        for (int i = 0; i < 6; i++)
            if ((e = add_air(A_RING, cx, -80, g))) { e->base = i * (TWO_PI / 6); e->vx = cx; }
        break;
    }
    }
}

static void kill_air(air_t *e) {
    static const int points[7] = { 0, 100, 150, 200, 100, 150, 150 };
    add_boom(e->x, e->y, 12, 0);
    burst(e->x, e->y, 10, 2.4f, RGB(255, 200, 90));
    sfx(SFX_BOOM, pan_of(e->x));
    add_score(points[e->type]);
    if (e->type == A_URCHIN && zone >= 2) /* its spines fly off */
        for (int i = 0; i < 4; i++) fire_at(e->x, e->y, 0.785f + i * 1.571f, 1.7f);
    int g = e->group;
    if (++group[g].killed == group[g].total && group[g].total > 1) {
        add_score(1000);
        add_popup(e->x, e->y, 1000, RGB(255, 236, 120));
        sfx(SFX_BONUS, pan_of(e->x));
    }
    e->type = A_NONE;
}

static void update_air(void) {
    float zs = zone_speed();
    for (int i = 0; i < MAX_AIR; i++) {
        air_t *e = &air[i];
        if (!e->type) continue;
        e->t++;
        if (e->flash) e->flash--;
        int gone = 0;
        switch (e->type) {
        case A_DART:
            e->y += 2.3f + zs;
            e->x = e->base + 42 * sinf(e->y * 0.04f);
            break;
        case A_MANTA:
            e->y += e->vy;
            if (e->y > 0 || e->vy < 3.0f) {
                e->vy -= 0.042f;
                e->vx = clampf(e->vx + (ship.x > e->x ? 0.08f : -0.08f), -2.3f, 2.3f);
                e->x += e->vx;
            }
            gone = e->vy < 0 && e->y < -14;
            break;
        case A_JELLY: {
            int period = clampi(84 - 8 * zone, 44, 84);
            e->y += 0.8f;
            e->x = e->base + 14 * sinf(e->t * 0.07f);
            if (e->t % period == period / 2) fire_ship(e->x, e->y + 4, 0, 1.7f + zs);
            break;
        }
        case A_DIVER:
            if (e->y < 0) {
                e->y += 3.0f;
                if (e->y >= 0) e->vx = clampf((ship.x - e->x) / 55.0f, -2.6f, 2.6f);
            } else {
                e->y += 3.8f + zs;
                e->x += e->vx;
            }
            break;
        case A_URCHIN:
            e->y += 1.3f;
            e->x += 0.5f * sinf(e->t * 0.05f);
            break;
        case A_RING: {
            float cy = e->t < 70 ? -70 + e->t * 2.3f : e->t < 250 ? 91 : 91 - (e->t - 250) * 2.6f;
            float a = e->base + e->t * 0.055f;
            e->x = e->vx + 56 * cosf(a);
            e->y = cy + 40 * sinf(a);
            gone = e->t > 250 && e->y < -14;
            break;
        }
        }
        /* From zone 2, any of them shoots now and then. */
        if (zone >= 2 && e->y > HUD_H && (int)(rnd() % 500) < zone) fire_ship(e->x, e->y, 0, 1.6f + zs);
        if (gone || e->y > SH + 12 || e->x < -30 || e->x > SW + 30) { e->type = A_NONE; continue; }
        if (ship.alive && !ship.safe && absf(e->x - ship.x) < 9 && absf(e->y - ship.y) < 9) {
            kill_air(e);
            kill_ship();
        }
    }
}

/* ---- Things on the reefs ---- */

static void hit_ground(ground_t *g) {
    int y = ground_y(g->wy);
    if (--g->hp > 0) {
        g->flash = 3;
        add_score(10);
        sfx(SFX_HIT, pan_of(g->x));
        return;
    }
    add_boom(g->x, (float)y, 14, 1);
    burst(g->x, (float)y, 8, 2.0f, RGB(255, 150, 60));
    sfx(SFX_BOOM_GROUND, pan_of(g->x));
    if (g->type == G_POD) add_score(100);
    else if (g->type == G_TURRET) add_score(200);
    else if (g->type == G_SHELL) {
        add_score(1000);
        add_popup(g->x, (float)y, 1000, RGB(255, 200, 255));
        burst(g->x, (float)y, 14, 2.6f, RGB(255, 200, 255));
        sfx(SFX_PEARL, pan_of(g->x));
    } else if (g->type == G_BEACON) {
        add_score(100);
        capsule.on = 1;
        capsule.x = g->x;
        capsule.y = (float)y;
        capsule.t = 0;
        sfx(SFX_CAPSULE, pan_of(g->x));
    }
    g->type = G_CRATER;
}

static void update_ground(void) {
    for (int i = 0; i < ground_count; i++) {
        ground_t *g = &ground[i];
        if (g->flash) g->flash--;
        if (g->type != G_TURRET) continue;
        int y = ground_y(g->wy);
        if (y < HUD_H + 4 || y > 170) continue;
        if (--g->timer <= 0) {
            g->timer = clampi(84 - 7 * zone, 40, 84) + (int)(rnd() % 30);
            fire_ship(g->x, (float)y, 0, 1.6f + zone_speed());
        }
    }
}

/* ---- The guardian ---- */

static void end_zone(void) { clear_wait = 100; }

static void hit_boss(void) {
    boss.flash = 2;
    sfx(SFX_BOSS_HIT, pan_of(boss.x));
    add_score(20);
    if (--boss.hp > 0) return;
    boss.state = B_DYING;
    boss.t = 0;
    memset(bullet, 0, sizeof bullet);
    music(SONG_NONE);
}

static void update_boss(void) {
    if (!boss.state) return;
    boss.t++;
    if (boss.flash) boss.flash--;
    float zs = zone_speed();
    switch (boss.state) {
    case B_WARN:
        if (boss.t % 14 == 1) sfx(SFX_ALARM, 128);
        if (boss.t > 84) {
            boss.state = B_ENTER;
            boss.x = SW / 2;
            boss.y = -30;
        }
        break;
    case B_ENTER:
        boss.y += 1.3f;
        if (boss.y >= 58) { boss.state = B_FIGHT; boss.t = 0; }
        break;
    case B_FIGHT: {
        boss.x = SW / 2 + 74 * sinf(boss.t * 0.022f);
        boss.y = 58 + 8 * sinf(boss.t * 0.05f);
        int spread = clampi(50 - 3 * zone, 30, 50);
        if (boss.t % spread == 0)
            for (int k = -1; k <= 1; k++) fire_ship(boss.x, boss.y + 12, k * 0.27f, 1.9f + zs);
        if (boss.t % 150 == 75)
            for (int k = 0; k < 10; k++) fire_at(boss.x, boss.y + 4, k * (TWO_PI / 10) + boss.t * 0.01f, 1.5f + zs);
        if (boss.t > 950) {
            boss.state = B_LEAVE;
            say("IT GOT AWAY", "", 70);
            music(SONG_NONE);
        }
        break;
    }
    case B_LEAVE:
        boss.y -= 2.2f;
        if (boss.y < -40) { boss.state = B_NONE; end_zone(); }
        break;
    case B_DYING:
        if (boss.t % 4 == 0) {
            add_boom(boss.x + frange(-30, 30), boss.y + frange(-14, 14), 10 + (int)(rnd() % 10), 0);
            sfx(SFX_BOOM, pan_of(boss.x));
        }
        if (boss.t > 50) {
            int bonus = 3000 + 2000 * zone;
            boss.state = B_NONE;
            add_boom(boss.x, boss.y, 44, 0);
            add_boom(boss.x - 24, boss.y + 4, 26, 0);
            add_boom(boss.x + 24, boss.y + 4, 26, 0);
            burst(boss.x, boss.y, 60, 4.5f, colours()->neon);
            sfx(SFX_BOSS_BOOM, 128);
            add_score(bonus);
            add_popup(boss.x, boss.y, bonus, WHITE);
            say("ZONE CLEAR", "", 90);
            music(SONG_CLEAR);
            end_zone();
        }
        break;
    }
}

/* ---- The ship ---- */

static void fire(void) {
    int active = 0, limit = ship.power ? 8 : 3;
    for (int i = 0; i < MAX_SHOTS; i++) active += shot[i].on;
    if (active >= limit) return;
    for (int side = ship.power ? -1 : 0; side <= 1; side += 2) {
        for (int i = 0; i < MAX_SHOTS; i++) {
            if (shot[i].on) continue;
            shot[i].on = 1;
            shot[i].x = ship.x + side * 4;
            shot[i].y = ship.y - 8;
            break;
        }
        if (!ship.power) break;
    }
    ship.reload = ship.power ? 3 : 5;
    sfx(ship.power ? SFX_SHOT2 : SFX_SHOT, pan_of(ship.x));
}

static void update_ship(uint32_t held) {
    if (!ship.alive) {
        if (--ship.wait <= 0) {
            if (lives <= 0) {
                state = ST_OVER;
                over_wait = 60;
                music(SONG_OVER);
            } else {
                place_ship();
            }
        }
        return;
    }
    const float speed = 2.7f;
    if (held & BIT(KEY_LEFT)) ship.x -= speed;
    if (held & BIT(KEY_RIGHT)) ship.x += speed;
    if (held & BIT(KEY_UP)) ship.y -= speed;
    if (held & BIT(KEY_DOWN)) ship.y += speed;
    ship.x = clampf(ship.x, 8, SW - 8);
    ship.y = clampf(ship.y, HUD_H + 10, SH - 10);
    if (ship.safe > 0) ship.safe--;
    if (ship.reload > 0) ship.reload--;
    if ((held & (BIT(KEY_FIRE) | BIT(KEY_FIRE2))) && ship.reload == 0) fire();
}

static void update_shots(void) {
    for (int i = 0; i < MAX_SHOTS; i++) {
        shot_t *s = &shot[i];
        if (!s->on) continue;
        s->y -= 9;
        if (s->y < HUD_H - 4) { s->on = 0; continue; }
        /* The guardian, then the air, then the reefs. A shot goes 9 pixels in a step. */
        if (boss.state == B_FIGHT && absf(s->x - boss.x) < 30 && absf(s->y - boss.y) < 17) {
            s->on = 0;
            hit_boss();
            continue;
        }
        for (int n = 0; n < MAX_AIR && s->on; n++) {
            air_t *e = &air[n];
            if (!e->type || absf(e->x - s->x) > 8 || absf(e->y - s->y) > 9) continue;
            s->on = 0;
            if (--e->hp > 0) {
                e->flash = 3;
                sfx(SFX_HIT, pan_of(e->x));
            } else {
                kill_air(e);
            }
        }
        for (int n = 0; n < ground_count && s->on; n++) {
            ground_t *g = &ground[n];
            if (g->type == G_NONE || g->type == G_CRATER) continue;
            if (absf(g->x - s->x) > 8 || absf(ground_y(g->wy) - s->y) > 9) continue;
            s->on = 0;
            hit_ground(g);
        }
    }
}

static void update_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *b = &bullet[i];
        if (!b->on) continue;
        b->x += b->vx;
        b->y += b->vy;
        if (b->x < -4 || b->x > SW + 4 || b->y < HUD_H - 4 || b->y > SH + 4) { b->on = 0; continue; }
        if (ship.alive && !ship.safe && absf(b->x - ship.x) < 4 && absf(b->y - ship.y) < 4) {
            b->on = 0;
            kill_ship();
        }
    }
}

static void update_capsule(void) {
    if (!capsule.on) return;
    capsule.t++;
    capsule.y += 0.7f;
    capsule.x += 0.8f * sinf(capsule.t * 0.08f);
    if (capsule.y > SH + 10) { capsule.on = 0; return; }
    if (!ship.alive || absf(capsule.x - ship.x) > 11 || absf(capsule.y - ship.y) > 11) return;
    capsule.on = 0;
    sfx(SFX_POWER, 128);
    if (ship.power) {
        add_score(2000);
        add_popup(capsule.x, capsule.y, 2000, RGB(140, 255, 255));
    } else {
        ship.power = 1;
        add_score(500);
        say("TWIN GUN", "", 50);
        if (!boss.state) music(SONG_POWER);
    }
}

static void update_effects(void) {
    for (int i = 0; i < MAX_BOOMS; i++) {
        boom_t *b = &boom[i];
        if (!b->span) continue;
        if (b->on_ground) b->y += scroll_speed;
        if (++b->age >= b->span) b->span = 0;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particle[i];
        if (!p->life) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->vx *= 0.93f;
        p->vy *= 0.93f;
        p->life--;
    }
    for (int i = 0; i < MAX_POPUPS; i++)
        if (popup[i].life) { popup[i].life--; popup[i].y -= 0.5f; }
    for (int i = 0; i < STARS; i++) {
        star[i].y += star[i].speed * 0.3f * scroll_speed;
        if (star[i].y >= SH) { star[i].y -= SH - HUD_H; star[i].x = (uint8_t)(rnd() % SW); }
    }
}

/* One step of the game. */
static void update(uint32_t held) {
    scroll += scroll_speed;
    update_ship(held);
    if (state != ST_PLAY) return;

    if (!boss.state && !clear_wait) {
        if (scroll >= ZONE_END) {
            boss.state = B_WARN;
            boss.t = 0;
            boss.hp = BOSS_HP(zone);
            say("WARNING", "A GUARDIAN COMES", 84);
            music(SONG_BOSS);
        } else if (--wave_wait <= 0) {
            spawn_wave();
            wave_wait = clampi(96 - 7 * zone, 54, 96);
        }
    }
    update_air();
    update_ground();
    update_boss();
    update_shots();
    update_bullets();
    update_capsule();

    if (clear_wait && --clear_wait == 0) {
        start_zone(zone + 1);
        play_song();
    }
    if (banner_time > 0) banner_time--;
}

/* ======================================================================
 * The picture
 * ====================================================================== */

static const sprite_t SHIP = { 13, 12,
    "......1......"
    ".....121....."
    ".....121....."
    "....12321...."
    "....12321...."
    "4...12221...4"
    "4..1222221..4"
    "4112222222114"
    "4122222222214"
    "41.1225221.14"
    "4...15551...4"
    ".....555....." };
static const uint16_t SHIP_PAL[5] = {
    RGB(250, 250, 255), RGB(130, 170, 236), RGB(255, 80, 110), RGB(255, 170, 50), RGB(60, 70, 120),
};

static const sprite_t DART = { 9, 9,
    "1.......1"
    "12.....21"
    "122...221"
    ".1223221."
    "..12321.."
    "..12321.."
    "...121..."
    "...121..."
    "....1...." };
static const uint16_t DART_PAL[3] = { RGB(220, 255, 140), RGB(110, 190, 60), RGB(255, 60, 60) };

static const sprite_t MANTA = { 13, 9,
    "..11.....11.."
    ".1221...1221."
    "1222211122221"
    "1222222222221"
    ".12223332221."
    "..122333221.."
    "...1223221..."
    "....12221...."
    ".....111....." };
static const uint16_t MANTA_PAL[3] = { RGB(150, 210, 255), RGB(50, 110, 220), RGB(255, 240, 120) };

static const sprite_t JELLY = { 11, 11,
    "...11111..."
    "..1222221.."
    ".122222221."
    "12232223221"
    "12222222221"
    "12222222221"
    ".111111111."
    ".1.1.1.1.1."
    ".1.1.1.1.1."
    "1..1...1..1"
    "...1...1..." };
static const uint16_t JELLY_PAL[3] = { RGB(255, 180, 240), RGB(220, 70, 190), RGB(40, 10, 60) };

static const sprite_t DIVER = { 9, 11,
    "1...1...1"
    "1..121..1"
    "12.121.21"
    "122222221"
    ".1222221."
    "..12321.."
    "..12321.."
    "..12321.."
    "...121..."
    "...121..."
    "....1...." };
static const uint16_t DIVER_PAL[3] = { RGB(255, 220, 130), RGB(236, 120, 40), RGB(255, 255, 255) };

static const sprite_t URCHIN = { 11, 11,
    ".....1....."
    ".1...1...1."
    "..1.222.1.."
    "...22322..."
    "..2233322.."
    "11223332211"
    "..2233322.."
    "...22322..."
    "..1.222.1.."
    ".1...1...1."
    ".....1....." };
static const uint16_t URCHIN_PAL[3] = { RGB(200, 200, 220), RGB(110, 90, 150), RGB(255, 90, 60) };

static const sprite_t ORB = { 9, 9,
    "..11111.."
    ".1222221."
    "122333221"
    "123333321"
    "123313321"
    "123333321"
    "122333221"
    ".1222221."
    "..11111.." };
static const uint16_t ORB_PAL[3] = { RGB(255, 255, 255), RGB(60, 200, 190), RGB(20, 70, 110) };

static const sprite_t LIFE = { 7, 7,
    "...1..."
    "...1..."
    "..121.."
    "..121.."
    "4.121.4"
    "4122214"
    "41.2.14" };

static void draw_tile(int r, int c, int x, int y, const zone_colours_t *p) {
    rect(x, y, TILE, TILE, p->base);
    uint32_t h = (uint32_t)(r * 7349 + c * 2971) * 2654435761u;
    int ax = 2 + (int)((h >> 4) % 11), ay = 2 + (int)((h >> 9) % 11);
    int bx = 2 + (int)((h >> 14) % 11), by = 2 + (int)((h >> 19) % 11);
    rect(x + ax, y + ay, 2, 2, p->dark);
    px(x + ax, y + ay, p->light);
    rect(x + bx, y + by, 3, 2, p->dark);
    if (((h >> 24) & 7) == 0) { /* a small coral */
        rect(x + 6, y + 5, 1, 6, p->dim);
        rect(x + 4, y + 7, 5, 1, p->dim);
    }
    /* The edges glow where the reef ends. Up on the screen is the next row. */
    if (!reef(r + 1, c)) { rect(x, y, TILE, 1, p->neon); rect(x, y + 1, TILE, 1, p->light); }
    if (!reef(r, c - 1)) rect(x, y, 1, TILE, p->neon);
    if (!reef(r, c + 1)) rect(x + TILE - 1, y, 1, TILE, p->dim);
    if (!reef(r - 1, c)) { /* the reef is thick: its side shows below */
        rect(x, y + TILE - 1, TILE, 1, p->dim);
        rect(x, y + TILE, TILE, 5, p->dark);
        rect(x, y + TILE + 5, TILE, 2, blend(p->dark, p->space, 150));
    }
}

static void draw_back(void) {
    const zone_colours_t *p = colours();
    rect(0, 0, SW, SH, p->space);
    for (int i = 0; i < STARS; i++) {
        int level = star[i].bright;
        px(star[i].x, (int)star[i].y, RGB(level, level, level + 40));
    }
    int s = (int)scroll;
    for (int r = (s + SH) / TILE; r >= s / TILE - 1; r--) {
        if (r < 0 || r >= ROWS) continue;
        int y = SH - ((r + 1) * TILE - s);
        for (int c = 0; c < COLS; c++)
            if (map[r][c]) draw_tile(r, c, c * TILE, y, p);
    }
}

static void draw_ground(void) {
    const zone_colours_t *p = colours();
    for (int i = 0; i < ground_count; i++) {
        ground_t *g = &ground[i];
        int x = g->x, y = ground_y(g->wy);
        if (g->type == G_NONE || y < -10 || y > SH + 10) continue;
        switch (g->type) {
        case G_CRATER:
            disc(x, y, 6, p->dark);
            disc(x, y, 4, RGB(8, 8, 14));
            break;
        case G_POD:
            disc(x, y, 6, p->dark);
            disc(x, y, 5, g->flash ? WHITE : RGB(236, 120, 56));
            disc(x - 2, y - 2, 1, RGB(255, 226, 170));
            break;
        case G_TURRET: {
            float dx = ship.x - x, dy = ship.y - y, d = sqrtf(dx * dx + dy * dy);
            if (d < 1) d = 1;
            rect(x - 6, y - 6, 12, 12, p->dark);
            rect(x - 5, y - 5, 10, 10, RGB(118, 124, 144));
            rect(x - 5, y - 5, 10, 1, RGB(176, 182, 200));
            for (int k = 2; k <= 6; k++) /* its gun points at the ship */
                rect(x + (int)(dx / d * k) - 1, y + (int)(dy / d * k) - 1, 2, 2, RGB(236, 236, 250));
            disc(x, y, 3, g->flash ? WHITE : RGB(224, 56, 84));
            break;
        }
        case G_SHELL:
            disc(x, y, 7, p->dark);
            disc(x, y, 6, g->flash ? WHITE : blend(RGB(150, 110, 196), RGB(250, 230, 255), (4 - g->hp) * 70));
            ring(x, y, 3, 4, RGB(96, 64, 140));
            px(x, y, RGB(96, 64, 140));
            break;
        case G_BEACON: {
            int on = (frame >> 2) & 1;
            glow(x, y, 12, p->neon, 120 + on * 80);
            for (int dy = -6; dy <= 6; dy++) {
                int half = 6 - (dy < 0 ? -dy : dy);
                rect(x - half, y + dy, 2 * half + 1, 1, on ? WHITE : p->neon);
            }
            text(x - 1, y - 2, "P", 1, INK);
            break;
        }
        }
    }
}

static void draw_boss(void) {
    if (boss.state < B_ENTER) return;
    const zone_colours_t *p = colours();
    int x = (int)boss.x, y = (int)boss.y;
    int shake = boss.state == B_DYING ? (int)(rnd() % 5) - 2 : 0;
    x += shake;
    uint16_t shell = boss.flash ? WHITE : p->light, rim = boss.flash ? WHITE : p->neon;
    oval(x - 30, y + 5, 9, 11, p->dim);
    oval(x + 30, y + 5, 9, 11, p->dim);
    oval(x - 30, y + 5, 6, 8, shell);
    oval(x + 30, y + 5, 6, 8, shell);
    oval(x, y, 30, 16, rim);
    oval(x, y, 28, 14, p->base);
    oval(x, y - 3, 25, 10, shell);
    for (int k = -2; k <= 2; k++) rect(x + k * 9, y - 12, 1, 20, p->base); /* the plates of its shell */
    /* Its heart, which gets red as it loses strength. */
    int hurt = 256 - boss.hp * 256 / BOSS_HP(zone);
    uint16_t heart = blend(RGB(120, 255, 255), RGB(255, 50, 50), clampi(hurt, 0, 256));
    glow(x, y + 5, 14, heart, 200);
    oval(x, y + 5, 8, 6, INK);
    oval(x, y + 5, 6, 4, (frame & 2) ? heart : WHITE);
    rect(x - 15, y - 4, 4, 3, RGB(255, 240, 120));
    rect(x + 12, y - 4, 4, 3, RGB(255, 240, 120));
}

static void draw_air(void) {
    for (int i = 0; i < MAX_AIR; i++) {
        air_t *e = &air[i];
        int x = (int)e->x, y = (int)e->y;
        switch (e->type) {
        case A_DART: sprite(&DART, x, y, DART_PAL, e->flash); break;
        case A_MANTA: sprite(&MANTA, x, y, MANTA_PAL, e->flash); break;
        case A_JELLY: sprite(&JELLY, x, y + ((e->t >> 3) & 1), JELLY_PAL, e->flash); break;
        case A_DIVER: sprite(&DIVER, x, y, DIVER_PAL, e->flash); break;
        case A_URCHIN: sprite(&URCHIN, x, y, URCHIN_PAL, e->flash); break;
        case A_RING: sprite(&ORB, x, y, ORB_PAL, e->flash); break;
        }
    }
}

static void draw_action(void) {
    if (capsule.on) {
        int x = (int)capsule.x, y = (int)capsule.y, on = (frame >> 1) & 1;
        glow(x, y, 13, RGB(120, 255, 255), 170);
        disc(x, y, 6, on ? WHITE : RGB(90, 230, 255));
        disc(x, y, 4, on ? RGB(90, 230, 255) : RGB(20, 80, 150));
        text(x - 1, y - 2, "P", 1, WHITE);
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!shot[i].on) continue;
        int x = (int)shot[i].x, y = (int)shot[i].y;
        rect(x - 1, y, 2, 7, ship.power ? RGB(140, 255, 255) : RGB(255, 240, 150));
        rect(x - 1, y, 2, 2, WHITE);
    }
    for (int i = 0; i < MAX_BOOMS; i++) {
        boom_t *b = &boom[i];
        if (!b->span) continue;
        int x = (int)b->x, y = (int)b->y, r = 2 + b->size * b->age / b->span, third = b->span / 3;
        if (b->age < third) {
            disc(x, y, r, WHITE);
        } else if (b->age < 2 * third) {
            disc(x, y, r, RGB(255, 150, 40));
            disc(x, y, r * 2 / 3, RGB(255, 236, 130));
        } else {
            ring(x, y, r - 2, r, RGB(200, 60, 40));
            ring(x, y, r / 2, r / 2 + 1, RGB(255, 150, 40));
        }
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particle[i];
        if (!p->life) continue;
        int level = p->life * 256 / p->span;
        uint16_t c = p->colour;
        light((int)p->x, (int)p->y, ((c >> 11) * level) >> 8, (((c >> 5) & 63) * level) >> 8, ((c & 31) * level) >> 8);
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullet_t *b = &bullet[i];
        if (!b->on) continue;
        int x = (int)b->x, y = (int)b->y;
        uint16_t c = (frame & 2) ? RGB(255, 90, 170) : RGB(255, 230, 120);
        rect(x - 1, y - 2, 3, 5, c);
        rect(x - 2, y - 1, 5, 3, c);
        rect(x - 1, y - 1, 2, 2, WHITE);
    }
    if (ship.alive && !(ship.safe && (frame & 2))) {
        int x = (int)ship.x, y = (int)ship.y;
        sprite(&SHIP, x, y, SHIP_PAL, 0);
        int flame = 2 + (int)(frame & 3);
        rect(x - 1, y + 6, 3, flame, RGB(255, 200, 80));
        rect(x, y + 6, 1, flame + 1, WHITE);
        if (ship.power) { /* the two guns */
            rect(x - 5, y - 4, 2, 5, RGB(140, 255, 255));
            rect(x + 4, y - 4, 2, 5, RGB(140, 255, 255));
        }
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        popup_t *p = &popup[i];
        if (p->life) text_centre((int)p->x, (int)p->y, p->text, 1, (p->life & 2) ? p->colour : WHITE);
    }
}

static void draw_hud(void) {
    char digits[8];
    rect(0, 0, SW, HUD_H, RGB(6, 6, 16));
    rect(0, HUD_H - 1, SW, 1, colours()->dim);
    text(3, 2, number(digits, score, 6), 2, WHITE);
    text(62, 2, "HI", 2, RGB(255, 90, 110));
    text(82, 2, number(digits, best, 6), 2, RGB(255, 220, 120));
    text(142, 2, "Z", 2, colours()->neon);
    text(151, 2, number(digits, zone, zone > 9 ? 2 : 1), 2, WHITE);
    if (ship.power) text(172, 4, "TWIN", 1, (frame & 4) ? RGB(140, 255, 255) : WHITE);
    for (int i = 0; i < lives - 1 && i < 5; i++) sprite(&LIFE, 232 - i * 9, 6, SHIP_PAL, 0);
}

static void draw_title(void) {
    static const char *const lines[3] = { "A D  LEFT AND RIGHT", "W S  UP AND DOWN", "J    FIRE" };
    char line[16] = "HI ";
    int beat = (frame >> 3) & 1;
    glow(SW / 2, 52, 70, RGB(70, 255, 224), 60);
    glow(SW / 2, 86, 70, RGB(255, 100, 224), 60);
    text(SW / 2 - text_width("NEON", 6) / 2 + 3, 39, "NEON", 6, RGB(10, 60, 70));
    text(SW / 2 - text_width("NEON", 6) / 2, 36, "NEON", 6, beat ? RGB(70, 255, 224) : RGB(190, 255, 244));
    text(SW / 2 - text_width("REEF", 6) / 2 + 3, 75, "REEF", 6, RGB(70, 16, 60));
    text(SW / 2 - text_width("REEF", 6) / 2, 72, "REEF", 6, beat ? RGB(255, 180, 240) : RGB(255, 100, 224));
    for (int i = 0; i < 3; i++) {
        text(45, 123 + i * 13, lines[i], 2, INK);
        text(44, 122 + i * 13, lines[i], 2, RGB(214, 220, 240));
    }
    text_centre(SW / 2, 166, "CATCH THE P FOR A TWIN GUN", 2, RGB(140, 255, 255));
    if (frame & 16) text_centre(SW / 2, 190, "PRESS J OR PLAY", 2, WHITE);
    number(line + 3, best, 6);
    text_centre(SW / 2, 14, line, 2, RGB(255, 220, 120));
}

static void draw(void) {
    draw_back();
    if (state == ST_TITLE) {
        draw_ground();
        draw_title();
        return;
    }
    draw_ground();
    draw_boss();
    draw_air();
    draw_action();
    draw_hud();
    if (state == ST_PLAY) {
        if (banner_time > 0 && !(boss.state == B_WARN && (frame & 4))) {
            text_centre(SW / 2, 60, banner, 3, boss.state == B_WARN ? RGB(255, 80, 90) : WHITE);
            text_centre(SW / 2, 82, banner2, 2, RGB(255, 220, 120));
        }
        if (paused) text_centre(SW / 2, 110, "PAUSED", 3, WHITE);
    } else {
        char line[16] = "SCORE ";
        text_centre(SW / 2, 78, "GAME OVER", 4, RGB(255, 96, 110));
        text_centre(SW / 2, 112, number(line + 6, score, 6) - 6, 2, WHITE);
        if (!over_wait && (frame & 16)) text_centre(SW / 2, 140, "PRESS J OR PLAY", 2, WHITE);
    }
}

/* ======================================================================
 * Program
 * ====================================================================== */

static void set_leds(void) {
    static uint64_t shown_leds;
    const uint32_t keys = BIT(KEY_LEFT) | BIT(KEY_RIGHT) | BIT(KEY_UP) | BIT(KEY_DOWN) | BIT(KEY_FIRE) | BIT(KEY_FIRE2);
    uint64_t leds = (uint64_t)keys << FM1_KEY_NOTE0;
    if (state != ST_PLAY ? (frame & 16) != 0 : paused) leds |= BIT(FM1_BTN_PLAY);
    if (boss.state == B_WARN && (frame & 4)) leds |= BIT(FM1_BTN_REC);
    for (uint32_t id = 0; id < FM1_NKEY; id++)
        if (((leds ^ shown_leds) >> id) & 1u) fm1_led_key(id, (int)((leds >> id) & 1u));
    shown_leds = leds;
}

static void show_title(void) {
    state = ST_TITLE;
    zone = 1;
    make_zone(1);
    scroll = 0;
    scroll_speed = 1.0f;
    boss.state = B_NONE;
}

void setup(void) {
    for (int n = 0; n < 128; n++) note_step[n] = 440.0f * powf(2.0f, (n - 69) / 12.0f) / FM1_AUDIO_RATE;
    for (int i = 0; i < STARS; i++) {
        star[i].x = (uint8_t)(rnd() % SW);
        star[i].y = (float)(HUD_H + rnd() % (SH - HUD_H));
        star[i].speed = (uint8_t)(1 + rnd() % 3);
        star[i].bright = (uint8_t)(50 + star[i].speed * 50);
    }
    show_title();
}

void loop(void) {
    static uint32_t last_block;
    static int started;
    uint32_t now = audio_blocks;
    if (started && now - last_block < FRAME_BLOCKS) return;
    last_block = started && now - last_block < 4 * FRAME_BLOCKS ? last_block + FRAME_BLOCKS : now;
    started = 1;
    frame++;

    uint32_t held = fm1_in.notes;
    uint32_t pressed = fm1_input_note_edges();
    uint32_t buttons = fm1_input_edges(0);
    int go = (pressed & (BIT(KEY_FIRE) | BIT(KEY_FIRE2))) || (buttons & BIT(FM1_BTN_PLAY));

    if (state == ST_TITLE) {
        scroll += scroll_speed;
        if (scroll > ROWS * TILE) scroll = 0;
        update_effects();
        if (go) start_game();
    } else if (state == ST_OVER) {
        update_effects();
        if (over_wait > 0) over_wait--;
        else if (go) show_title();
    } else {
        if (buttons & BIT(FM1_BTN_PLAY)) {
            paused = !paused;
            music_paused = paused;
        }
        if (!paused) {
            update(held);
            update_effects();
        }
    }

    draw();
    present();
    set_leds();
}
