/* main.c - GLOAMING - a twilight shooter for the FM-1.
 *
 * A valley at dusk goes around in a ring. Eight lanterns stand in it. Moths
 * come out of the dark, sit on a lantern and drink its light. Fly your glider
 * along the valley and shoot them before the lanterns go out. A lantern that
 * lost light can get it back: hover just above it and your own lamp fills it.
 * The game ends when you have no gliders left, or when all lanterns are dark.
 *
 * Computer key   FM-1 key   What
 *   A   D        C4   E4    fly left, fly right (the glider turns)
 *   W   S        C#4  D4    up, down
 *   J            B4         fire (hold it)
 *   K            C5         pulse: a ring that clears all near you (you have few)
 *                PLAY       start, pause
 *                MASTER     volume
 *
 * The LEDs of these note keys are on. REC blinks while a moth drinks.
 *
 * The enemies: MOTHS go for the lanterns. WISPS (from wave 2) hunt you.
 * STINGERS (from wave 3) cross the valley fast and shoot.
 * The strip at the top is a map of the whole valley.
 *
 * How it is made: the game draws each picture in its own frame buffer, then
 * sends to the screen only the parts of each row that changed. All sounds are
 * made in audio() from two-operator FM voices and noise. loop() counts the
 * audio blocks as its clock: one picture each 6 blocks, about 29 a second.
 */
#include "fm1.h"
#include <math.h>
#include <string.h>

#define BIT(n) (1u << (n))
#define RGB(r, g, b) (uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3)

#define SW 240
#define SH 240
#define WORLD 2048        /* width of the valley in pixels; it wraps. A power of two. */
#define HUD_H 24          /* the top rows have the score and the map */
#define FRAME_BLOCKS 6    /* audio blocks for one picture */

/* The note keys of the controls (computer keys A, W, S, D, J, K). */
#define KEY_LEFT 7
#define KEY_UP 8
#define KEY_DOWN 9
#define KEY_RIGHT 11
#define KEY_FIRE 18
#define KEY_PULSE 19

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
static float absf(float v) { return v < 0 ? -v : v; }

/* A position in the valley, 0 to WORLD. */
static float wrapx(float x) {
    while (x < 0) x += WORLD;
    while (x >= WORLD) x -= WORLD;
    return x;
}

/* The short way from b to a around the valley: -WORLD/2 to WORLD/2. */
static float delta(float a, float b) {
    float d = a - b;
    if (d > WORLD / 2) d -= WORLD;
    else if (d < -WORLD / 2) d += WORLD;
    return d;
}

/* ======================================================================
 * Sound: voices of two sine operators and noise
 * ====================================================================== */

enum {
    SFX_FIRE = 1, SFX_BOOM, SFX_SPARK, SFX_DEATH, SFX_PULSE, SFX_TICK, SFX_CHIME,
    SFX_DARK, SFX_WAVE, SFX_CLEAR, SFX_OVER, SFX_EXTRA
};

/* loop() writes sound commands here, audio() reads them. */
#define SFX_QUEUE 32u
static volatile uint32_t sfx_queue[SFX_QUEUE];
static volatile uint32_t sfx_written, sfx_taken;
static volatile int snd_thrust;  /* 1 while the engine burns */
static volatile int snd_alarm;   /* 1 while a moth drinks */
static volatile uint32_t audio_blocks; /* count of audio() calls: the clock of loop() */

/* pan: 0 is left, 255 is right. */
static void sfx(int id, int param, int pan) {
    if (sfx_written - sfx_taken >= SFX_QUEUE) return;
    sfx_queue[sfx_written % SFX_QUEUE] = (uint32_t)id | (uint32_t)(param & 255) << 8 | (uint32_t)(pan & 255) << 16;
    sfx_written++;
}

typedef struct {
    int on;
    int delay;          /* samples until it starts */
    int age;            /* samples since it started */
    float t, dt;        /* 0 to 1 through its life */
    float freq, fmul;   /* cycles for each sample, and its change for each sample */
    float ratio;        /* modulator pitch as a multiple of the carrier */
    float index, dindex;/* modulation depth and its change for each sample */
    float amp;
    float noise;        /* 0: tone, 1: noise */
    float lpk, lp;      /* low pass on the noise */
    float carrier, modulator;
    float left, right;
} voice_t;

#define VOICES 12
static voice_t voice[VOICES];

#define SINE_SIZE 1024
static float sine_table[SINE_SIZE];
static float sine(float phase) { /* in cycles */
    return sine_table[(int)(phase * SINE_SIZE) & (SINE_SIZE - 1)];
}

static uint32_t noise_state = 0x9E3779B9u;
static float white(void) {
    noise_state = noise_state * 1664525u + 1013904223u;
    return (float)(int32_t)noise_state * (1.0f / 2147483648.0f);
}

/* Starts a voice: from f0 to f1 Hz in `seconds`, modulation from i0 to i1. */
static void tone(float delay, float seconds, float f0, float f1, float ratio,
                 float i0, float i1, float amp, float noise, float lpk, int pan) {
    voice_t *v = &voice[0];
    for (int i = 0; i < VOICES; i++) { /* a free voice, or else the one nearest to its end */
        if (!voice[i].on) { v = &voice[i]; break; }
        if (voice[i].t > v->t) v = &voice[i];
    }
    float samples = seconds * FM1_AUDIO_RATE;
    v->on = 1;
    v->delay = (int)(delay * FM1_AUDIO_RATE);
    v->age = 0;
    v->t = 0;
    v->dt = 1.0f / samples;
    v->freq = f0 / FM1_AUDIO_RATE;
    v->fmul = powf(f1 / f0, 1.0f / samples);
    v->ratio = ratio;
    v->index = i0;
    v->dindex = (i1 - i0) / samples;
    v->amp = amp;
    v->noise = noise;
    v->lpk = lpk;
    v->lp = 0;
    v->carrier = v->modulator = 0;
    float p = pan / 255.0f;
    v->left = 1.0f - 0.7f * p;
    v->right = 0.3f + 0.7f * p;
}

/* A short row of notes. */
static void melody(const float *notes, int count, float gap, float seconds, float ratio, float index, float amp) {
    for (int i = 0; i < count; i++)
        tone(i * gap, seconds, notes[i], notes[i], ratio, index, 0.2f, amp, 0, 0, 128);
}

static void start_sfx(uint32_t command) {
    int id = command & 255, param = (command >> 8) & 255, pan = (command >> 16) & 255;
    static const float chime[4] = { 659.3f, 784.0f, 987.8f, 1318.5f };
    static const float wave[3] = { 392.0f, 523.3f, 659.3f };
    static const float clear[4] = { 523.3f, 659.3f, 784.0f, 1046.5f };
    static const float over[4] = { 392.0f, 329.6f, 261.6f, 196.0f };
    static const float extra[5] = { 784.0f, 987.8f, 1174.7f, 1568.0f, 1975.5f };
    switch (id) {
    case SFX_FIRE:
        tone(0, 0.10f, 1900, 240, 2.0f, 2.6f, 0.2f, 0.34f, 0, 0, pan);
        break;
    case SFX_BOOM:
        tone(0, 0.30f, 170, 42, 0.5f, 4.0f, 0.0f, 0.55f, 0, 0, pan);
        tone(0, 0.26f, 100, 100, 1, 0, 0, 0.50f, 1, 0.22f, pan);
        break;
    case SFX_SPARK:
        tone(0, 0.09f, 760, 330, 3.0f, 1.6f, 0.0f, 0.16f, 0, 0, pan);
        break;
    case SFX_DEATH:
        tone(0, 1.10f, 520, 28, 1.41f, 2.0f, 9.0f, 0.60f, 0, 0, 128);
        tone(0, 0.90f, 100, 100, 1, 0, 0, 0.55f, 1, 0.10f, 128);
        break;
    case SFX_PULSE:
        tone(0, 0.55f, 80, 1500, 0.5f, 6.0f, 0.5f, 0.50f, 0, 0, 128);
        tone(0, 0.50f, 100, 100, 1, 0, 0, 0.30f, 1, 0.35f, 128);
        break;
    case SFX_TICK: {
        float f = 440.0f + 6.0f * param;
        tone(0, 0.06f, f, f * 1.02f, 2.0f, 1.0f, 0.0f, 0.16f, 0, 0, pan);
        break;
    }
    case SFX_CHIME: melody(chime, 4, 0.07f, 0.40f, 3.5f, 1.6f, 0.26f); break;
    case SFX_DARK:
        tone(0.00f, 0.40f, 311, 277, 1.0f, 3.0f, 0.5f, 0.40f, 0, 0, pan);
        tone(0.28f, 0.70f, 207, 155, 1.0f, 3.5f, 0.5f, 0.42f, 0, 0, pan);
        break;
    case SFX_WAVE: melody(wave, 3, 0.11f, 0.28f, 2.0f, 2.0f, 0.26f); break;
    case SFX_CLEAR: melody(clear, 4, 0.08f, 0.30f, 2.0f, 1.5f, 0.26f); break;
    case SFX_OVER: melody(over, 4, 0.24f, 0.60f, 1.0f, 3.0f, 0.34f); break;
    case SFX_EXTRA: melody(extra, 5, 0.06f, 0.22f, 4.0f, 1.2f, 0.24f); break;
    }
}

void audio(int32_t *out, int frames) {
    static float gain, thrust_level, thrust_lp;
    static int alarm_wait, alarm_step;

    while (sfx_taken != sfx_written) {
        start_sfx(sfx_queue[sfx_taken % SFX_QUEUE]);
        sfx_taken++;
    }

    /* The alarm: two notes in turn while a moth drinks. */
    if (snd_alarm) {
        alarm_wait -= frames;
        if (alarm_wait <= 0) {
            alarm_wait = FM1_AUDIO_RATE * 17 / 100;
            float f = (alarm_step++ & 1) ? 830.6f : 1108.7f;
            tone(0, 0.11f, f, f, 2.0f, 1.3f, 0.2f, 0.10f, 0, 0, 128);
        }
    } else {
        alarm_wait = 0;
    }

    float master = fm1_master() / (float)FM1_MASTER_MAX;
    float target_gain = master * master * master * 0.25f; /* audio taper, max 12 dB quieter */
    float target_thrust = snd_thrust ? 0.11f : 0.0f;

    for (int i = 0; i < frames; i++) {
        float left = 0, right = 0;
        for (int n = 0; n < VOICES; n++) {
            voice_t *v = &voice[n];
            if (!v->on) continue;
            if (v->delay > 0) { v->delay--; continue; }
            float env = (1.0f - v->t) * (1.0f - v->t);
            if (v->age < 88) env *= v->age * (1.0f / 88.0f); /* 2 ms attack: no click */
            float s;
            if (v->noise > 0) {
                v->lp += (white() - v->lp) * v->lpk;
                s = v->lp * 2.5f;
                v->lpk *= 0.99993f; /* the noise gets darker */
            } else {
                s = sine(v->carrier + v->index * sine(v->modulator) * 0.159f);
                v->carrier += v->freq;
                v->modulator += v->freq * v->ratio;
                if (v->carrier >= 1.0f) v->carrier -= 1.0f;
                if (v->modulator >= 1.0f) v->modulator -= 1.0f;
                v->freq *= v->fmul;
                v->index += v->dindex;
            }
            s *= env * v->amp;
            left += s * v->left;
            right += s * v->right;
            v->age++;
            v->t += v->dt;
            if (v->t >= 1.0f) v->on = 0;
        }

        /* The engine: soft low noise. */
        thrust_level += (target_thrust - thrust_level) * 0.002f;
        thrust_lp += (white() - thrust_lp) * 0.06f;
        float engine = thrust_lp * thrust_level * 3.0f;
        left += engine;
        right += engine;

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
static uint16_t sky[SW * SH];     /* the sky and the moon: made one time */
static int shown_valid;

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

/* A round glow. r, g and b are 0 to 255, strength is 0 to 256. */
static void glow(int cx, int cy, int radius, int r, int g, int b, int strength) {
    if (radius < 1 || strength < 1) return;
    if (cx + radius < 0 || cx - radius >= SW || cy + radius < 0 || cy - radius >= SH) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int k = (r2 - d2) * 256 / r2; /* 256 in the centre, 0 at the edge */
            k = k * k / 256 * strength / 256;
            light(cx + dx, cy + dy, (r * k) >> 11, (g * k) >> 10, (b * k) >> 11);
        }
    }
}

/* The colour that is t/256 of the way from a to b. */
static uint16_t blend(uint16_t a, uint16_t b, int t) {
    int red = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
    int green = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
    int blue = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
    return (uint16_t)(red << 11 | green << 5 | blue);
}

/* A picture of w x h characters: '.' is open, '1' to '9' are colours of a palette. */
typedef struct { int w, h; const char *rows; } sprite_t;

static void sprite(const sprite_t *s, int x, int y, int flip, const uint16_t *palette) {
    for (int row = 0; row < s->h; row++)
        for (int col = 0; col < s->w; col++) {
            char c = s->rows[row * s->w + (flip ? s->w - 1 - col : col)];
            if (c != '.') px(x + col, y + row, palette[c - '1']);
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
    text(x + scale, y + scale, s, scale, RGB(10, 6, 24));
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
 * The valley
 * ====================================================================== */

#define LANTERNS 8
#define MAX_ENEMIES 28
#define MAX_BOLTS 8
#define MAX_SPARKS 20
#define MAX_PARTICLES 180
#define STARS 70
#define TWO_PI 6.2831853f

static uint8_t ground[WORLD];          /* height of the ground at each place */
static uint8_t far_hill[WORLD / 2];    /* the hills behind: they move at half the speed */
static struct { uint16_t x; uint8_t y, phase; } star[STARS]; /* they move at a quarter of the speed */

typedef struct {
    float x;
    float charge;   /* light: 0 (dark) to 100 */
    int drained;    /* a moth drinks from it in this picture */
    int low;        /* it was below half: filling it gives points */
} lantern_t;
static lantern_t lantern[LANTERNS];

static int ground_y(float x) { return SH - ground[(int)x & (WORLD - 1)]; }
static int lamp_y(const lantern_t *l) { return ground_y(l->x) - 15; } /* the centre of the glass */

enum { E_NONE, E_MOTH, E_WISP, E_STINGER };
enum { M_SEEK, M_DRINK };
typedef struct {
    int type, state, target, timer;
    float x, y, vx, vy, alt, phase;
} enemy_t;
static enemy_t enemy[MAX_ENEMIES];

typedef struct { int life; float x, y, vx; } bolt_t;
static bolt_t bolt[MAX_BOLTS];

typedef struct { int life; float x, y, vx, vy; } spark_t;
static spark_t spark[MAX_SPARKS];

typedef struct { int life, span; float x, y, vx, vy; uint16_t colour; } particle_t;
static particle_t particle[MAX_PARTICLES];

static struct {
    float x, y, vx, vy;
    int face;       /* 1: right, -1: left */
    int alive;
    int safe;       /* pictures in which nothing can hit it */
    int reload;
    int burning;
    int filling;    /* the lantern that it fills, or -1 */
} ship;

enum { ST_TITLE, ST_PLAY, ST_OVER };
static int state;
static int paused;
static uint32_t frame;      /* count of pictures */
static float cam;           /* the place in the valley at the left side of the screen */
static float cam_lead;      /* the screen looks ahead of the glider */
static int score, best, lives, pulses, wave, next_extra;
static int to_spawn[4];     /* enemies of each type that this wave still sends */
static int spawn_wait;
static int dead_wait;       /* pictures until the next glider */
static int clear_wait;      /* pictures until the next wave */
static int over_wait;
static float pulse_radius;  /* 0: no pulse */
static float pulse_x, pulse_y;
static char banner[32], banner2[32];
static int banner_time;
static const char *over_reason;

static const uint16_t COL_AMBER = RGB(255, 196, 84);
static const uint16_t COL_WHITE = RGB(255, 255, 255);
static const uint16_t COL_MOTH = RGB(204, 170, 236);
static const uint16_t COL_WISP = RGB(110, 255, 200);
static const uint16_t COL_STING = RGB(255, 96, 150);

static void make_valley(void) {
    /* Hills from a few sine waves that fit the ring. */
    float p1 = frange(0, TWO_PI), p2 = frange(0, TWO_PI), p3 = frange(0, TWO_PI), p4 = frange(0, TWO_PI);
    for (int x = 0; x < WORLD; x++) {
        float a = x * (TWO_PI / WORLD);
        float h = 30 + 11 * sinf(3 * a + p1) + 7 * sinf(7 * a + p2) + 4 * sinf(17 * a + p3) + 2 * sinf(41 * a + p4);
        ground[x] = (uint8_t)clampf(h, 12, 54);
    }
    for (int x = 0; x < WORLD / 2; x++) {
        float a = x * (TWO_PI / (WORLD / 2));
        float h = 26 + 14 * sinf(2 * a + p3) + 9 * sinf(5 * a + p1) + 5 * sinf(11 * a + p2);
        far_hill[x] = (uint8_t)clampf(h, 4, 60);
    }
    for (int i = 0; i < STARS; i++) {
        star[i].x = (uint16_t)(rnd() % (WORLD / 4));
        star[i].y = (uint8_t)(HUD_H + 2 + rnd() % 130);
        star[i].phase = (uint8_t)rnd();
    }
    /* The lanterns stand on flat ground. */
    for (int i = 0; i < LANTERNS; i++) {
        int x = i * (WORLD / LANTERNS) + 90 + (int)(rnd() % 80);
        lantern[i].x = (float)x;
        for (int d = -7; d <= 7; d++) ground[(x + d) & (WORLD - 1)] = ground[x];
    }
}

static void make_sky(void) {
    /* Dusk: dark blue at the top, to purple, to the last orange at the hills. */
    static const struct { int y, r, g, b; } stops[] = {
        { 0, 6, 8, 30 }, { 110, 34, 22, 70 }, { 175, 104, 48, 84 }, { 239, 200, 104, 70 },
    };
    static const int bayer[4] = { 0, 2, 3, 1 }; /* 2 x 2 dither: softer steps between the colours */
    for (int y = 0; y < SH; y++) {
        int s = 0;
        while (y > stops[s + 1].y) s++;
        int span = stops[s + 1].y - stops[s].y, t = y - stops[s].y;
        int r = stops[s].r + (stops[s + 1].r - stops[s].r) * t / span;
        int g = stops[s].g + (stops[s + 1].g - stops[s].g) * t / span;
        int b = stops[s].b + (stops[s + 1].b - stops[s].b) * t / span;
        for (int x = 0; x < SW; x++) {
            int d = bayer[(y & 1) * 2 + (x & 1)];
            int rr = r + d * 2, gg = g + d, bb = b + d * 2;
            sky[y * SW + x] = RGB(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb);
        }
    }
    /* A thin moon with a halo. */
    const int mx = 212, my = 98;
    memcpy(fb, sky, sizeof fb);
    glow(mx, my, 34, 150, 150, 190, 90);
    for (int dy = -12; dy <= 12; dy++)
        for (int dx = -12; dx <= 12; dx++) {
            int in_moon = dx * dx + dy * dy <= 144;
            int in_shade = (dx - 6) * (dx - 6) + (dy + 4) * (dy + 4) <= 132;
            if (in_moon && !in_shade) px(mx + dx, my + dy, RGB(244, 236, 204));
        }
    memcpy(sky, fb, sizeof sky);
}

/* ---- Particles ---- */

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
        add_particle(x, y, cosf(a) * s, sinf(a) * s, 10 + (int)(rnd() % 18), (rnd() & 3) ? colour : COL_WHITE);
    }
}

/* The pan of a sound at place x: where it is on the screen. */
static int pan_of(float x) {
    float sx = delta(x, cam) * (255.0f / SW);
    return (int)clampf(sx, 0, 255);
}

/* ---- Game flow ---- */

static void say(const char *line1, const char *line2, int time) {
    strcpy(banner, line1);
    strcpy(banner2, line2);
    banner_time = time;
}

static void add_score(int points) {
    score += points;
    if (score > best) best = score;
    if (score >= next_extra) {
        next_extra += 10000;
        if (lives < 5) lives++;
        sfx(SFX_EXTRA, 0, 128);
    }
}

static void start_wave(int n) {
    wave = n;
    to_spawn[E_MOTH] = n + 4 > 12 ? 12 : n + 4;
    to_spawn[E_WISP] = n - 1 > 6 ? 6 : n - 1;
    to_spawn[E_STINGER] = (n - 1) / 2 > 4 ? 4 : (n - 1) / 2;
    spawn_wait = 40;
    char line[16] = "WAVE ";
    number(line + 5, n, n > 9 ? 2 : 1);
    say(line, "", 70);
    sfx(SFX_WAVE, 0, 128);
}

static void place_ship(void) {
    ship.y = 110;
    ship.vx = ship.vy = 0;
    ship.alive = 1;
    ship.safe = 75;
    ship.reload = 0;
    ship.filling = -1;
}

static void start_game(void) {
    memset(enemy, 0, sizeof enemy);
    memset(bolt, 0, sizeof bolt);
    memset(spark, 0, sizeof spark);
    memset(particle, 0, sizeof particle);
    for (int i = 0; i < LANTERNS; i++) {
        lantern[i].charge = 100;
        lantern[i].low = 0;
    }
    score = 0;
    lives = 3;
    pulses = 3;
    next_extra = 10000;
    pulse_radius = 0;
    dead_wait = clear_wait = over_wait = 0;
    paused = 0;
    ship.x = lantern[0].x + 40;
    ship.face = 1;
    cam_lead = 45;
    place_ship();
    state = ST_PLAY;
    start_wave(1);
}

static void end_game(const char *reason) {
    state = ST_OVER;
    over_reason = reason;
    over_wait = 45;
    ship.alive = 0;
    ship.filling = -1;
    snd_thrust = 0;
    snd_alarm = 0;
    sfx(SFX_OVER, 0, 128);
}

static int lit_lanterns(void) {
    int count = 0;
    for (int i = 0; i < LANTERNS; i++) count += lantern[i].charge > 0;
    return count;
}

/* ---- Enemies ---- */

/* A lantern with light for a moth at x: mostly any of them, sometimes the nearest. -1: all are dark. */
static int pick_lantern(float x) {
    int nearest = -1, any = -1, count = 0;
    float distance = WORLD;
    for (int i = 0; i < LANTERNS; i++) {
        if (lantern[i].charge <= 0) continue;
        float d = absf(delta(lantern[i].x, x));
        if (d < distance) { distance = d; nearest = i; }
        if (rnd() % (uint32_t)++count == 0) any = i;
    }
    return (rnd() & 3) ? any : nearest;
}

static void spawn(int type) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        if (e->type) continue;
        memset(e, 0, sizeof *e);
        e->type = type;
        e->x = wrapx(ship.x + WORLD / 2 + frange(-620, 620)); /* far from the glider */
        e->y = frange(40, 80);
        e->alt = frange(50, 120);
        e->phase = frange(0, TWO_PI);
        e->timer = 60 + (int)(rnd() % 60);
        if (type == E_MOTH) e->target = pick_lantern(e->x);
        if (type == E_STINGER) e->vx = (rnd() & 1) ? 1.0f : -1.0f;
        burst(e->x, e->y, 8, 1.5f, COL_WHITE);
        return;
    }
}

static void shoot_at_ship(const enemy_t *e, float speed) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        spark_t *s = &spark[i];
        if (s->life) continue;
        float dx = delta(ship.x, e->x), dy = ship.y - e->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 1) d = 1;
        s->x = e->x; s->y = e->y;
        s->vx = dx / d * speed + e->vx * 0.3f;
        s->vy = dy / d * speed;
        s->life = 95;
        sfx(SFX_SPARK, 0, pan_of(e->x));
        return;
    }
}

static void kill_enemy(enemy_t *e) {
    static const int points[4] = { 0, 150, 200, 250 };
    static const uint16_t *const colours[4] = { 0, &COL_MOTH, &COL_WISP, &COL_STING };
    burst(e->x, e->y, 16, 2.6f, *colours[e->type]);
    sfx(SFX_BOOM, 0, pan_of(e->x));
    add_score(points[e->type]);
    e->type = E_NONE;
}

static void kill_ship(void) {
    ship.alive = 0;
    ship.filling = -1;
    burst(ship.x, ship.y, 40, 3.4f, COL_AMBER);
    burst(ship.x, ship.y, 20, 1.6f, RGB(120, 200, 255));
    sfx(SFX_DEATH, 0, 128);
    lives--;
    dead_wait = 55;
}

static void update_moth(enemy_t *e) {
    float speed = 0.85f + 0.07f * wave;
    if (speed > 1.7f) speed = 1.7f;
    if (e->target < 0 || lantern[e->target].charge <= 0) {
        e->target = pick_lantern(e->x);
        e->state = M_SEEK;
        if (e->target < 0) return;
    }
    lantern_t *l = &lantern[e->target];
    float perch = (float)(lamp_y(l) - 7);
    e->phase += 0.35f;
    if (e->state == M_DRINK) {
        e->x = l->x;
        e->y = perch;
        l->drained = 1;
        l->charge -= 0.30f + 0.035f * wave;
        if (l->charge < 50) l->low = 1;
        if (l->charge <= 0) {
            l->charge = 0;
            sfx(SFX_DARK, 0, pan_of(l->x));
            if (lit_lanterns() > 0) say("A LANTERN IS DARK", "", 60);
            e->state = M_SEEK;
            e->target = -1;
        }
        return;
    }
    float dx = delta(l->x, e->x);
    float want_y = absf(dx) > 34 ? e->alt + 7 * sinf(e->phase * 0.2f) : perch;
    e->vx = clampf(dx, -speed, speed);
    e->x = wrapx(e->x + e->vx);
    e->y += clampf(want_y - e->y, -speed, speed);
    if (absf(dx) < 1.5f && absf(perch - e->y) < 1.5f) e->state = M_DRINK;
    /* From wave 3, a moth near the glider shoots now and then. */
    if (wave >= 3 && ship.alive && --e->timer <= 0) {
        e->timer = 70 + (int)(rnd() % 80);
        if (absf(delta(ship.x, e->x)) < 110) shoot_at_ship(e, 1.7f);
    }
}

static void update_wisp(enemy_t *e) {
    float dx = delta(ship.x, e->x), dy = ship.y - e->y;
    float push = 0.07f + 0.006f * wave, top = 2.3f + 0.08f * wave;
    if (top > 3.3f) top = 3.3f;
    if (!ship.alive) { dx = -dx; dy = 80 - e->y; } /* it goes away while no glider is there */
    e->vx = clampf(e->vx * 0.985f + (dx > 0 ? push : -push), -top, top);
    e->vy = clampf(e->vy * 0.96f + (dy > 0 ? push : -push), -1.6f, 1.6f);
    e->x = wrapx(e->x + e->vx);
    e->y = clampf(e->y + e->vy, HUD_H + 6, (float)(ground_y(e->x) - 6));
    e->phase += 0.4f;
    if ((frame & 3) == 0) add_particle(e->x, e->y, 0, 0, 9, RGB(40, 150, 120));
}

static void update_stinger(enemy_t *e) {
    float speed = 2.0f + 0.08f * wave;
    if (speed > 3.0f) speed = 3.0f;
    float dir = e->vx > 0 ? 1.0f : -1.0f;
    e->vx = dir * speed;
    e->x = wrapx(e->x + e->vx);
    float floor_y = (float)(ground_y(e->x) - 14);
    float want_y = ship.alive ? ship.y : 90;
    e->y = clampf(e->y + clampf(want_y - e->y, -0.6f, 0.6f), HUD_H + 8, floor_y);
    float ahead = delta(ship.x, e->x) * dir; /* the glider is in front of it */
    if (e->timer > 0) e->timer--;
    if (ship.alive && e->timer == 0 && ahead > 24 && ahead < 135) {
        shoot_at_ship(e, 2.5f);
        e->timer = 36;
    }
}

/* ---- The glider ---- */

static void fire(void) {
    for (int i = 0; i < MAX_BOLTS; i++) {
        bolt_t *b = &bolt[i];
        if (b->life) continue;
        b->x = wrapx(ship.x + ship.face * 8);
        b->y = ship.y + 1;
        b->vx = ship.face * 10.0f + ship.vx;
        b->life = 22;
        ship.reload = 4;
        sfx(SFX_FIRE, 0, pan_of(ship.x));
        return;
    }
}

static void update_ship(uint32_t held, uint32_t pressed) {
    if (!ship.alive) {
        snd_thrust = 0;
        if (--dead_wait <= 0) {
            if (lives <= 0) end_game("NO GLIDERS LEFT");
            else place_ship();
        }
        return;
    }
    int left = (held & BIT(KEY_LEFT)) != 0, right = (held & BIT(KEY_RIGHT)) != 0;
    ship.burning = left != right;
    if (ship.burning) {
        ship.face = right ? 1 : -1;
        ship.vx += ship.face * 0.38f;
    }
    ship.vx = clampf(ship.vx * 0.965f, -5.2f, 5.2f);
    float want_vy = 0;
    if (held & BIT(KEY_UP)) want_vy -= 2.6f;
    if (held & BIT(KEY_DOWN)) want_vy += 2.6f;
    ship.vy += (want_vy - ship.vy) * 0.4f;
    ship.x = wrapx(ship.x + ship.vx);
    ship.y = clampf(ship.y + ship.vy, HUD_H + 6, (float)(ground_y(ship.x) - 6));
    snd_thrust = ship.burning;
    if (ship.burning && (frame & 1))
        add_particle(ship.x - ship.face * 8, ship.y + 1, -ship.face * frange(1, 2.5f), frange(-0.4f, 0.4f), 7, COL_AMBER);

    if (ship.safe > 0) ship.safe--;
    if (ship.reload > 0) ship.reload--;
    if (((held | pressed) & BIT(KEY_FIRE)) && ship.reload == 0) fire();

    if ((pressed & BIT(KEY_PULSE)) && pulses > 0 && pulse_radius == 0) {
        pulses--;
        pulse_radius = 1;
        pulse_x = ship.x;
        pulse_y = ship.y;
        sfx(SFX_PULSE, 0, 128);
    }

    /* Fill the lantern just below with light. */
    ship.filling = -1;
    for (int i = 0; i < LANTERNS; i++) {
        lantern_t *l = &lantern[i];
        float dy = lamp_y(l) - ship.y;
        if (absf(delta(l->x, ship.x)) > 9 || dy < -2 || dy > 34 || l->charge >= 100) continue;
        ship.filling = i;
        l->charge += 1.5f;
        if ((frame % 5) == 0) sfx(SFX_TICK, (int)l->charge, pan_of(l->x));
        if (l->charge >= 100) {
            l->charge = 100;
            if (l->low) {
                l->low = 0;
                add_score(250);
                sfx(SFX_CHIME, 0, 128);
                burst(l->x, (float)lamp_y(l), 14, 1.8f, COL_AMBER);
            }
        }
    }
}

/* One step of the game. */
static void update(uint32_t held, uint32_t pressed) {
    for (int i = 0; i < LANTERNS; i++) lantern[i].drained = 0;
    update_ship(held, pressed);
    if (state != ST_PLAY) return;

    /* New enemies, one at a time. */
    int alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) alive += enemy[i].type != E_NONE;
    int waiting = to_spawn[E_MOTH] + to_spawn[E_WISP] + to_spawn[E_STINGER];
    if (waiting && --spawn_wait <= 0 && alive < 6 + wave && alive < MAX_ENEMIES) {
        int type = to_spawn[E_MOTH] ? E_MOTH : to_spawn[E_WISP] ? E_WISP : E_STINGER;
        if (to_spawn[E_WISP] && (rnd() & 3) == 0) type = E_WISP;
        if (to_spawn[E_STINGER] && (rnd() & 7) == 0) type = E_STINGER;
        to_spawn[type]--;
        spawn(type);
        spawn_wait = 48 - 3 * wave < 20 ? 20 : 48 - 3 * wave;
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        if (e->type == E_MOTH) update_moth(e);
        else if (e->type == E_WISP) update_wisp(e);
        else if (e->type == E_STINGER) update_stinger(e);
        else continue;
        if (ship.alive && !ship.safe && absf(delta(e->x, ship.x)) < 9 && absf(e->y - ship.y) < 6) {
            kill_enemy(e);
            kill_ship();
        }
    }

    /* Bolts: each one hits the first enemy on its path. */
    for (int i = 0; i < MAX_BOLTS; i++) {
        bolt_t *b = &bolt[i];
        if (!b->life) continue;
        for (int n = 0; n < MAX_ENEMIES; n++) {
            enemy_t *e = &enemy[n];
            if (!e->type || absf(e->y - b->y) > 6) continue;
            float d = delta(e->x, b->x);
            if (b->vx > 0 ? (d >= -6 && d <= b->vx + 6) : (d <= 6 && d >= b->vx - 6)) {
                kill_enemy(e);
                b->life = 1; /* it ends below */
                break;
            }
        }
        b->x = wrapx(b->x + b->vx);
        b->life--;
    }

    for (int i = 0; i < MAX_SPARKS; i++) {
        spark_t *s = &spark[i];
        if (!s->life) continue;
        s->x = wrapx(s->x + s->vx);
        s->y += s->vy;
        s->life--;
        if (s->y < HUD_H || s->y >= ground_y(s->x)) s->life = 0;
        if (s->life && ship.alive && !ship.safe && absf(delta(s->x, ship.x)) < 6 && absf(s->y - ship.y) < 4) {
            s->life = 0;
            kill_ship();
        }
    }

    /* The pulse: a ring that grows and clears all that it passes. */
    if (pulse_radius > 0) {
        pulse_radius += 6.5f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            enemy_t *e = &enemy[i];
            if (!e->type) continue;
            float dx = delta(e->x, pulse_x), dy = e->y - pulse_y;
            if (dx * dx + dy * dy < pulse_radius * pulse_radius) kill_enemy(e);
        }
        for (int i = 0; i < MAX_SPARKS; i++) {
            float dx = delta(spark[i].x, pulse_x), dy = spark[i].y - pulse_y;
            if (dx * dx + dy * dy < pulse_radius * pulse_radius) spark[i].life = 0;
        }
        if (pulse_radius > 135) pulse_radius = 0;
    }

    int drinking = 0;
    for (int i = 0; i < LANTERNS; i++) drinking |= lantern[i].drained;
    snd_alarm = drinking;

    if (lit_lanterns() == 0) {
        end_game("THE VALLEY IS DARK");
        return;
    }

    /* The wave is clear: points for the light that is left, then the next wave. */
    if (!waiting && !alive && ship.alive && !clear_wait) {
        int lit = lit_lanterns(), bonus = lit * 100 * wave;
        add_score(bonus);
        char line[24] = "BONUS ";
        number(line + 6, bonus, bonus > 9999 ? 5 : 4);
        say("WAVE CLEAR", line, 80);
        sfx(SFX_CLEAR, 0, 128);
        clear_wait = 85;
    }
    if (clear_wait && --clear_wait == 0) {
        if (pulses < 5) pulses++;
        start_wave(wave + 1);
    }
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particle[i];
        if (!p->life) continue;
        p->x = wrapx(p->x + p->vx);
        p->y += p->vy;
        p->vx *= 0.94f;
        p->vy = p->vy * 0.94f + 0.03f;
        p->life--;
    }
}

/* ======================================================================
 * The picture
 * ====================================================================== */

static const sprite_t SHIP = { 14, 7,
    ".55..........."
    ".522.........."
    ".52222211....."
    "4522222331111."
    "45222222222221"
    ".552222221...."
    "...5555......." };
static const uint16_t SHIP_PAL[5] = {
    RGB(236, 240, 248), RGB(150, 172, 206), RGB(96, 226, 255), RGB(255, 190, 70), RGB(70, 84, 124),
};

static const sprite_t MOTH[2] = {
    { 11, 7,
      "11.......11"
      "121.....121"
      "1221.3.1221"
      ".122333221."
      "..1134311.."
      "....333...."
      ".....3....." },
    { 11, 7,
      "..........."
      ".....3....."
      ".111333111."
      "12223432221"
      "1221.3.1221"
      "121.....121"
      "11.......11" },
};
static const uint16_t MOTH_PAL[4] = { RGB(214, 186, 240), RGB(132, 96, 190), RGB(62, 44, 84), RGB(255, 90, 90) };

static const sprite_t STINGER = { 12, 5,
    "22.........."
    ".2211......."
    "..2111111133"
    ".2211......."
    "22.........." };
static const uint16_t STING_PAL[3] = { RGB(255, 110, 160), RGB(176, 40, 96), RGB(255, 236, 130) };

static const sprite_t LIFE = { 7, 4,
    "22....."
    "222211."
    "2222221"
    ".2221.." };

/* The place on the screen of a place in the valley. */
static int screen_x(float x) { return (int)floorf(delta(x, cam)); }

static void draw_back(void) {
    memcpy(fb, sky, sizeof fb);
    int star_shift = (int)(cam * 0.25f);
    for (int i = 0; i < STARS; i++) {
        int x = (star[i].x - star_shift) & (WORLD / 4 - 1);
        if (x >= SW) continue;
        int twinkle = ((frame + star[i].phase) >> 3) & 7;
        if (twinkle == 0) continue;
        light(x, star[i].y, twinkle > 4 ? 22 : 12, twinkle > 4 ? 44 : 24, twinkle > 4 ? 24 : 16);
    }
    int far_shift = (int)(cam * 0.5f), near_shift = (int)cam;
    const uint16_t far_top = RGB(58, 34, 92), far_body = RGB(34, 20, 62);
    const uint16_t edge = RGB(52, 128, 116), soil1 = RGB(16, 48, 54), soil2 = RGB(10, 28, 38), soil3 = RGB(6, 16, 26);
    for (int x = 0; x < SW; x++) {
        int wx = (near_shift + x) & (WORLD - 1);
        int top = SH - ground[wx];
        int far_y = SH - 34 - far_hill[(far_shift + x) & (WORLD / 2 - 1)];
        if (far_y < top) {
            fb[far_y * SW + x] = far_top;
            for (int y = far_y + 1; y < top; y++) fb[y * SW + x] = far_body;
        }
        if ((wx * 7) % 13 == 0) fb[(top - 1) * SW + x] = soil1; /* a blade of grass */
        fb[top * SW + x] = edge;
        for (int y = top + 1; y < SH; y++) {
            int depth = y - top + ((wx ^ y) & 3);
            fb[y * SW + x] = depth < 7 ? soil1 : depth < 20 ? soil2 : soil3;
        }
    }
}

static void draw_lanterns(void) {
    for (int i = 0; i < LANTERNS; i++) {
        lantern_t *l = &lantern[i];
        int x = screen_x(l->x);
        if (x < -30 || x > SW + 30) continue;
        int base = ground_y(l->x), ly = base - 15;
        int level = (int)(l->charge * 2.56f); /* 0 to 256 */
        if (l->drained && (frame & 2)) level = level * 5 / 8; /* it flickers */
        glow(x, ly, 9 + level / 14, 255, 170, 60, level * 3 / 4);
        const uint16_t iron = RGB(58, 56, 80);
        rect(x - 1, base - 12, 2, 12, iron);
        rect(x - 2, base - 1, 4, 1, iron);
        rect(x - 3, ly - 3, 6, 7, iron);
        rect(x - 2, ly - 2, 4, 5, blend(RGB(34, 34, 50), RGB(255, 214, 110), level));
        rect(x - 2, ly - 4, 4, 1, iron);
        rect(x - 1, ly - 5, 2, 1, iron);
        if (level > 200) rect(x - 1, ly - 1, 2, 3, RGB(255, 246, 200));
    }
    /* The light that the glider gives to a lantern. */
    if (ship.filling >= 0) {
        lantern_t *l = &lantern[ship.filling];
        int x = screen_x(l->x), top = (int)ship.y + 4, bottom = lamp_y(l) - 5;
        for (int y = top; y < bottom; y++)
            if (((y + (int)frame) & 3) < 2) light(x - 1 + ((y >> 1) & 1), y, 20, 30, 6);
    }
}

static void draw_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        if (!e->type) continue;
        int x = screen_x(e->x), y = (int)e->y;
        if (x < -20 || x > SW + 20) continue;
        if (e->type == E_MOTH) {
            int flap = e->state == M_DRINK ? (frame >> 2) & 1 : (frame >> 1) & 1;
            sprite(&MOTH[flap], x - 5, y - 3, 0, MOTH_PAL);
        } else if (e->type == E_WISP) {
            int flicker = (int)(sinf(e->phase) * 40);
            glow(x, y, 10, 60, 255, 170, 150 + flicker);
            rect(x - 1, y - 2, 3, 5, COL_WISP);
            rect(x - 2, y - 1, 5, 3, COL_WISP);
            rect(x - 1, y - 1, 3, 3, COL_WHITE);
        } else {
            glow(x, y, 8, 255, 60, 120, 110);
            sprite(&STINGER, x - 6, y - 2, e->vx < 0, STING_PAL);
        }
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        spark_t *s = &spark[i];
        if (!s->life) continue;
        int x = screen_x(s->x), y = (int)s->y;
        glow(x, y, 5, 255, 120, 40, 200);
        rect(x - 1, y - 1, 2, 2, (frame & 1) ? COL_WHITE : RGB(255, 170, 70));
    }
}

static void draw_action(void) {
    for (int i = 0; i < MAX_BOLTS; i++) {
        bolt_t *b = &bolt[i];
        if (!b->life) continue;
        int x = screen_x(b->x), y = (int)b->y, dir = b->vx > 0 ? 1 : -1;
        for (int k = 0; k < 16; k++) {
            int level = 31 - k * 2;
            light(x - dir * k, y, level, level * 2, level / 2 + 14);
        }
        px(x, y, COL_WHITE);
        px(x - dir, y, COL_WHITE);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particle[i];
        if (!p->life) continue;
        int level = p->life * 256 / p->span;
        uint16_t c = p->colour;
        light(screen_x(p->x), (int)p->y, ((c >> 11) * level) >> 8, (((c >> 5) & 63) * level) >> 8, ((c & 31) * level) >> 8);
    }
    if (pulse_radius > 0) {
        int cx = screen_x(pulse_x), cy = (int)pulse_y;
        int level = 31 - (int)(pulse_radius * 0.2f);
        int steps = (int)(pulse_radius * 7);
        for (int k = 0; k < steps; k++) {
            float a = k * TWO_PI / steps;
            float c = cosf(a), s = sinf(a);
            int x = cx + (int)(c * pulse_radius), y = cy + (int)(s * pulse_radius);
            if (y < HUD_H) continue;
            light(x, y, level / 2, level * 2, level);
            light(cx + (int)(c * (pulse_radius - 2)), cy + (int)(s * (pulse_radius - 2)), level / 4, level, level / 2);
        }
    }
    if (ship.alive && !(ship.safe && (frame & 2))) {
        int x = screen_x(ship.x), y = (int)ship.y;
        glow(x, y, 13, 120, 160, 255, 70);
        sprite(&SHIP, x - 7, y - 3, ship.face < 0, SHIP_PAL);
        if (ship.burning) {
            int length = 2 + (int)(rnd() % 4);
            for (int k = 0; k < length; k++)
                px(x - ship.face * (8 + k), y + 1, k < 2 ? COL_WHITE : COL_AMBER);
        }
    }
}

/* An arrow at the side of the screen: a moth drinks that way. */
static void draw_arrows(void) {
    if (!(frame & 4)) return;
    float nearest = WORLD;
    for (int i = 0; i < LANTERNS; i++) {
        if (!lantern[i].drained) continue;
        float d = delta(lantern[i].x, ship.x);
        if (absf(d) < absf(nearest)) nearest = d;
    }
    if (nearest == WORLD) return;
    int sx = screen_x(wrapx(ship.x + nearest));
    if (sx >= 0 && sx < SW) return; /* the lantern is on the screen */
    int left = nearest < 0;
    for (int k = 0; k < 6; k++)
        rect(left ? 3 + k : SW - 4 - k, 130 - k, 1, 2 * k + 1, RGB(255, 80, 70));
}

static void draw_hud(void) {
    char digits[8];
    rect(0, 0, SW, HUD_H, RGB(8, 6, 22));
    rect(0, HUD_H - 1, SW, 1, RGB(52, 40, 96));
    text(3, 2, number(digits, score, 6), 2, COL_AMBER);
    for (int i = 0; i < lives && i < 5; i++) sprite(&LIFE, 58 + i * 9, 5, 0, SHIP_PAL);
    for (int i = 0; i < pulses; i++) {
        int x = 112 + i * 8;
        rect(x + 1, 3, 3, 1, COL_WISP); rect(x + 1, 9, 3, 1, COL_WISP);
        rect(x, 4, 1, 5, COL_WISP); rect(x + 4, 4, 1, 5, COL_WISP);
    }
    text(196, 2, "W", 2, RGB(140, 130, 190));
    text(206, 2, number(digits, wave, 2), 2, COL_WHITE);

    /* The map: all of the valley in the width of the screen. */
    const int map_y = 14, map_h = 8;
    rect(0, map_y - 1, SW, map_h + 1, RGB(18, 14, 44));
    int view = (int)(cam * SW / WORLD), view_w = SW * SW / WORLD;
    rect(view, map_y - 1, view_w, map_h + 1, RGB(44, 36, 92));
    if (view + view_w > SW) rect(0, map_y - 1, view + view_w - SW, map_h + 1, RGB(44, 36, 92));
    for (int i = 0; i < LANTERNS; i++) {
        lantern_t *l = &lantern[i];
        uint16_t c = l->charge <= 0 ? RGB(70, 70, 90) : blend(RGB(120, 70, 30), COL_AMBER, (int)(l->charge * 2.56f));
        if (l->drained && (frame & 2)) c = RGB(255, 60, 60);
        rect((int)(l->x * SW / WORLD), map_y + map_h - 3, 1, 3, c);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t *e = &enemy[i];
        if (!e->type) continue;
        uint16_t c = e->type == E_MOTH ? COL_MOTH : e->type == E_WISP ? COL_WISP : COL_STING;
        rect((int)(e->x * SW / WORLD), map_y + (int)((e->y - HUD_H) * (map_h - 2) / (SH - HUD_H)), 2, 2, c);
    }
    if (ship.alive && (frame & 2))
        rect((int)(ship.x * SW / WORLD), map_y + (int)((ship.y - HUD_H) * (map_h - 2) / (SH - HUD_H)), 2, 2, COL_WHITE);
}

static void draw_title(void) {
    static const char *const lines[4] = { "A D  LEFT AND RIGHT", "W S  UP AND DOWN", "J    FIRE", "K    PULSE" };
    const char *name = "GLOAMING";
    int x = SW / 2 - text_width(name, 5) / 2;
    text(x + 3, 31, name, 5, RGB(30, 12, 50));
    text(x, 28, name, 5, COL_AMBER);
    for (int row = 0; row < 8; row++) /* the top of each letter is lighter */
        for (int col = x; col < x + text_width(name, 5); col++)
            if (fb[(28 + row) * SW + col] == COL_AMBER) fb[(28 + row) * SW + col] = RGB(255, 236, 170);
    text_centre(SW / 2, 60, "KEEP THE LANTERNS LIT", 2, RGB(214, 186, 240));
    for (int i = 0; i < 4; i++) {
        text(45, 83 + i * 13, lines[i], 2, RGB(10, 6, 24));
        text(44, 82 + i * 13, lines[i], 2, RGB(200, 200, 220));
    }
    text_centre(SW / 2, 138, "HOVER OVER A LANTERN", 2, COL_AMBER);
    text_centre(SW / 2, 151, "TO GIVE IT LIGHT", 2, COL_AMBER);
    if (frame & 16) text_centre(SW / 2, 172, "PRESS J OR PLAY", 2, COL_WHITE);
    if (best) {
        char line[16] = "BEST ";
        number(line + 5, best, 6);
        text_centre(SW / 2, 10, line, 2, RGB(160, 150, 200));
    }
}

static void draw(void) {
    draw_back();
    draw_lanterns();
    if (state != ST_TITLE) {
        draw_enemies();
        draw_action();
    }
    if (state == ST_PLAY) {
        draw_arrows();
        if (banner_time > 0) {
            text_centre(SW / 2, 40, banner, 2, COL_WHITE);
            text_centre(SW / 2, 56, banner2, 2, COL_AMBER);
        }
        if (paused) text_centre(SW / 2, 100, "PAUSED", 3, COL_WHITE);
    }
    if (state != ST_TITLE) draw_hud();
    if (state == ST_TITLE) draw_title();
    if (state == ST_OVER) {
        char line[16] = "SCORE ";
        text_centre(SW / 2, 70, "GAME OVER", 4, RGB(255, 96, 110));
        text_centre(SW / 2, 100, over_reason, 2, COL_AMBER);
        text_centre(SW / 2, 122, number(line + 6, score, 6) - 6, 2, COL_WHITE);
        if (!over_wait && (frame & 16)) text_centre(SW / 2, 150, "PRESS J OR PLAY", 2, COL_WHITE);
    }
}

/* ======================================================================
 * Program
 * ====================================================================== */

static void set_leds(void) {
    static uint64_t shown_leds;
    uint64_t leds = 0;
    const uint32_t keys = BIT(KEY_LEFT) | BIT(KEY_RIGHT) | BIT(KEY_UP) | BIT(KEY_DOWN) | BIT(KEY_FIRE);
    leds |= (uint64_t)keys << FM1_KEY_NOTE0;
    if (state != ST_PLAY || pulses > 0) leds |= (uint64_t)BIT(KEY_PULSE) << FM1_KEY_NOTE0;
    if (state != ST_PLAY ? (frame & 16) != 0 : paused) leds |= BIT(FM1_BTN_PLAY);
    if (state == ST_PLAY && snd_alarm && (frame & 4)) leds |= BIT(FM1_BTN_REC);
    for (uint32_t id = 0; id < FM1_NKEY; id++)
        if (((leds ^ shown_leds) >> id) & 1u) fm1_led_key(id, (int)((leds >> id) & 1u));
    shown_leds = leds;
}

void setup(void) {
    for (int i = 0; i < SINE_SIZE; i++) sine_table[i] = sinf(i * TWO_PI / SINE_SIZE);
    make_valley();
    make_sky();
    for (int i = 0; i < LANTERNS; i++) lantern[i].charge = 100;
    ship.filling = -1;
    state = ST_TITLE;
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
    int go = (pressed & (BIT(KEY_FIRE) | BIT(KEY_PULSE))) || (buttons & BIT(FM1_BTN_PLAY));
    rnd(); /* the time of each key press changes the game */

    if (state == ST_TITLE) {
        cam = wrapx(cam + 0.7f);
        if (go) start_game();
    } else if (state == ST_OVER) {
        update_particles();
        if (over_wait > 0) over_wait--;
        else if (go) state = ST_TITLE;
    } else {
        if (buttons & BIT(FM1_BTN_PLAY)) {
            paused = !paused;
            if (paused) snd_thrust = snd_alarm = 0;
        }
        if (!paused) {
            update(held, pressed);
            update_particles();
            if (banner_time > 0) banner_time--;
            /* The screen looks ahead of the glider. */
            cam_lead += (ship.face * 45 - cam_lead) * 0.09f;
            cam = wrapx(ship.x - SW / 2 + cam_lead);
        }
    }

    draw();
    present();
    set_leds();
}
