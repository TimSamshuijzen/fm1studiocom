
#include "fm1.h"
#include <math.h>

#define NV 8
#define SIN_N 1024u
#define SW 208
#define SH 72
#define TILE 26

#define BG    0x0841u
#define PANEL 0x18E5u
#define WHITE 0xFFFFu
#define MUTED 0x9D35u
#define CYAN  0x07FFu
#define GRID  0x2947u
#define ZERO  0x4A69u

/* =========================================================
   PARAMETERS
   ========================================================= */

static const uint16_t ratio100[] = {
    25, 50, 75, 100, 150, 200, 250,
    300, 350, 400, 500, 600, 700, 800
};

static const uint16_t time_ms[] = {
    2, 5, 10, 20, 30, 50, 75, 100, 150,
    200, 300, 500, 750, 1000, 1500,
    2000, 3000, 4000
};

static int ratio_i = 3;
static int depth_i = 8;
static int attack_i = 2;
static int release_i = 10;
static int octave = 0;

/* All parameter indices in one 32-bit control word. */
static volatile uint32_t controls;

/* Loop writes the key mailboxes.
   Audio owns all voice state.

   Key word:
   bits 31:8 = generation
   bits  7:1 = MIDI note
   bit     0 = held
*/

static volatile uint32_t key_word[FM1_NOTE_KEYS];
static uint32_t seen[FM1_NOTE_KEYS];
static uint32_t old_keys, old_leds;

static volatile uint32_t playing_keys;
static volatile uint32_t audio_blocks;

/* =========================================================
   SYNTH ENGINE
   ========================================================= */

static int16_t sine_table[SIN_N + 1u];
static uint32_t note_step[128];

static float volume;
static float mix[FM1_AUDIO_BLOCK];

typedef struct {
    int key, releasing;
    uint32_t age, step, car, mod;
    float env, release_level;
} Voice;

static Voice voices[NV];
static uint32_t age_counter;

/* =========================================================
   OSCILLOSCOPE STATE
   ========================================================= */

static volatile int16_t scope_data[2][SW];
static volatile uint32_t scope_seq;

static unsigned scope_pos;
static unsigned scope_wait;
static unsigned scope_div;
static int16_t scope_prev;

static int16_t scope_copy[SW];
static uint8_t old_y[SW], new_y[SW];

static uint32_t displayed_seq;
static uint32_t last_scope_block;
static int scope_tile = SW / TILE;

/* Small reusable LCD buffer. */
static uint16_t pixels[3200];
static unsigned dirty;

/* =========================================================
   OSCILLATORS
   ========================================================= */

/* Sine lookup with linear interpolation.
   A uint32_t phase represents one complete cycle.
*/

static inline int32_t wave(uint32_t phase)
{
    unsigned i = phase >> 22;
    unsigned f = (phase >> 12) & 1023u;

    int32_t a = sine_table[i];
    int32_t b = sine_table[i + 1u];

    return a + ((b - a) * (int32_t)f) / 1024;
}

static void publish_controls(void)
{
    uint32_t m = fm1_master();

    if (m > FM1_MASTER_MAX)
        m = FM1_MASTER_MAX;

    controls = (uint32_t)ratio_i
             | ((uint32_t)depth_i << 4)
             | ((uint32_t)attack_i << 10)
             | ((uint32_t)release_i << 15)
             | (m << 20);
}

/* =========================================================
   VOICE ALLOCATION
   ========================================================= */

static void start_note(int key, unsigned midi)
{
    int i, slot = -1;

    /* Retrigger the same key if it still has a voice. */
    for (i = 0; i < NV; ++i) {
        if (voices[i].key == key) {
            slot = i;
            break;
        }
    }

    /* Find an unused voice. */
    if (slot < 0) {
        for (i = 0; i < NV; ++i) {
            if (voices[i].key < 0) {
                slot = i;
                break;
            }
        }
    }

    /* Prefer stealing the quietest release tail. */
    if (slot < 0) {
        for (i = 0; i < NV; ++i) {
            if (voices[i].releasing &&
                (slot < 0 ||
                 voices[i].env < voices[slot].env)) {
                slot = i;
            }
        }
    }

    /* Otherwise steal the oldest held voice. */
    if (slot < 0) {
        slot = 0;

        for (i = 1; i < NV; ++i) {
            if (age_counter - voices[i].age >
                age_counter - voices[slot].age) {
                slot = i;
            }
        }
    }

    voices[slot] = (Voice){0};
    voices[slot].key = key;
    voices[slot].age = ++age_counter;
    voices[slot].step = note_step[midi];
}

static void sync_keys(void)
{
    uint32_t snapshot[FM1_NOTE_KEYS];
    unsigned k;
    int i;

    for (k = 0; k < FM1_NOTE_KEYS; ++k)
        snapshot[k] = key_word[k];

    /* Release voices whose keys are no longer held. */
    for (i = 0; i < NV; ++i) {
        Voice *v = &voices[i];

        if (v->key >= 0 &&
            !v->releasing &&
            !(snapshot[v->key] & 1u)) {

            v->releasing = 1;
            v->release_level = v->env;
        }
    }

    /* Start newly pressed keys. */
    for (k = 0; k < FM1_NOTE_KEYS; ++k) {
        uint32_t gen = snapshot[k] & 0xFFFFFF00u;

        if (gen != seen[k]) {
            seen[k] = gen;

            start_note(
                (int)k,
                (snapshot[k] >> 1) & 127u
            );
        }
    }
}

/* =========================================================
   OSCILLOSCOPE CAPTURE
   ========================================================= */

static void capture_scope(float sample)
{
    int16_t s = (int16_t)(sample * 32767.0f);

    /* Trigger on a rising zero crossing.
       A timeout allows the scope to update during silence.
    */
    if (scope_pos == 0u) {
        ++scope_wait;

        if (!(scope_prev <= 0 && s > 0) &&
            scope_wait < 256u) {

            scope_prev = s;
            return;
        }
    }

    scope_data[(scope_seq + 1u) & 1u][scope_pos++] = s;
    scope_prev = s;

    if (scope_pos == SW) {
        ++scope_seq;
        scope_pos = 0;
        scope_wait = 0;
    }
}

/* =========================================================
   AUDIO CALLBACK
   ========================================================= */

void audio(int32_t *out, int frames)
{
    uint32_t c = controls;

    float ratio =
        (float)ratio100[c & 15u] * 0.01f;

    /* Phase modulation index in quarter-radian steps. */
    uint32_t pm =
        (uint32_t)((float)((c >> 4) & 63u)
                   * 5215.189f + 0.5f);

    float attack =
        1000.0f /
        ((float)FM1_AUDIO_RATE *
         time_ms[(c >> 10) & 31u]);

    float release =
        1000.0f /
        ((float)FM1_AUDIO_RATE *
         time_ms[(c >> 15) & 31u]);

    float target =
        (float)((c >> 20) & 1023u) /
        (float)FM1_MASTER_MAX;

    const float gain =
        0.90f / (32767.0f * (float)NV);

    int offset = 0, i, j;
    uint32_t active = 0;

    sync_keys();

    /* Normally frames is 256.
       Chunking also supports a different frame count.
    */

    while (offset < frames) {

        int n = frames - offset;

        if (n > FM1_AUDIO_BLOCK)
            n = FM1_AUDIO_BLOCK;

        for (j = 0; j < n; ++j)
            mix[j] = 0.0f;

        /* Render each active voice. */
        for (i = 0; i < NV; ++i) {

            Voice *v = &voices[i];

            uint32_t cp, mp, ms;
            float e, de;

            if (v->key < 0)
                continue;

            cp = v->car;
            mp = v->mod;

            ms = (uint32_t)((float)v->step * ratio);

            e = v->env;

            de = v->releasing
                ? -v->release_level * release
                : attack;

            for (j = 0; j < n; ++j) {

                int32_t m, s;

                /* Amplitude envelope. */
                e += de;

                if (v->releasing && e <= 0.0f) {
                    e = 0.0f;
                    v->key = -1;
                    break;
                }

                if (e > 1.0f)
                    e = 1.0f;

                /* Operator 2: sine modulator. */
                m = wave(mp);

                /* Operator 1: phase-modulated carrier.

                   Unsigned addition and multiplication
                   wrap the phase automatically.
                */
                s = wave(cp + (uint32_t)m * pm);

                mix[j] += (float)s * e;

                cp += v->step;
                mp += ms;
            }

            v->car = cp;
            v->mod = mp;
            v->env = e;
        }

        /* Master volume and stereo output. */
        for (j = 0; j < n; ++j) {

            float s;
            int32_t q;

            /* Smooth master volume changes. */
            volume += (target - volume) * 0.004f;

            s = mix[j] * gain * volume;

            /* Additional safety limit. */
            if (s > 0.95f)
                s = 0.95f;

            if (s < -0.95f)
                s = -0.95f;

            q = (int32_t)(s * (float)FM1_AUDIO_MAX);

            out[2 * (offset + j)] = q;
            out[2 * (offset + j) + 1] = q;

            /* Scope captures every fourth sample. */
            if ((scope_div++ & 3u) == 0u)
                capture_scope(s);
        }

        offset += n;
    }

    /* Publish the keys that are actually sounding. */
    for (i = 0; i < NV; ++i) {
        if (voices[i].key >= 0)
            active |= 1u << voices[i].key;
    }

    playing_keys = active;
    ++audio_blocks;
}

/* =========================================================
   5 x 7 BITMAP FONT
   ========================================================= */

/* Digits 0-9, followed by capital letters A-Z. */

static const uint8_t font[36][7] = {
    {14,17,19,21,25,17,14},
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},

    {14,17,17,31,17,17,17},
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12},
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
    {17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31}
};

static unsigned glyph(char c, int r)
{
    if (c >= '0' && c <= '9')
        return font[c - '0'][r];

    if (c >= 'A' && c <= 'Z')
        return font[10 + c - 'A'][r];

    if (c == '.')
        return r >= 5 ? 6u : 0u;

    if (c == '-')
        return r == 3 ? 14u : 0u;

    if (c == '+')
        return r == 3 ? 14u :
               (r == 2 || r == 4 ? 4u : 0u);

    return 0;
}

/* Draw centered text using one LCD blit. */

static void text_box(
    int x, int y, int w, int h,
    const char *s, int z,
    uint16_t fg, uint16_t bg)
{
    int i, len = 0;
    int ox, oy = (h - 7 * z) / 2;

    while (s[len])
        ++len;

    ox = (w - (len * 6 - 1) * z) / 2;

    for (i = 0; i < w * h; ++i)
        pixels[i] = bg;

    for (i = 0; i < len; ++i) {

        int r, c, dy, dx;

        for (r = 0; r < 7; ++r) {

            unsigned bits = glyph(s[i], r);

            for (c = 0; c < 5; ++c) {

                if (!(bits & (16u >> c)))
                    continue;

                for (dy = 0; dy < z; ++dy) {
                    for (dx = 0; dx < z; ++dx) {

                        int px = ox + (i * 6 + c) * z + dx;
                        int py = oy + r * z + dy;

                        if (px >= 0 && px < w &&
                            py >= 0 && py < h) {

                            pixels[py * w + px] = fg;
                        }
                    }
                }
            }
        }
    }

    fm1_lcd_blit(
        x, y, w, h,
        pixels, (uint32_t)(w * h)
    );
}

/* =========================================================
   NUMBER FORMATTING WITHOUT PRINTF
   ========================================================= */

static char *put_uint(char *p, unsigned n)
{
    char tmp[10];
    int i = 0;

    do {
        tmp[i++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n);

    while (i)
        *p++ = tmp[--i];

    *p = '\0';

    return p;
}

static void fixed100(char *p, unsigned n)
{
    p = put_uint(p, n / 100u);

    *p++ = '.';
    *p++ = (char)('0' + (n / 10u) % 10u);
    *p++ = (char)('0' + n % 10u);
    *p = '\0';
}

/* =========================================================
   USER INTERFACE
   ========================================================= */

static void draw_octave(void)
{
    char s[] = "OCT +0";

    s[4] = octave < 0 ? '-' : '+';
    s[5] = (char)('0' +
           (octave < 0 ? -octave : octave));

    text_box(144, 4, 88, 24, s, 2, WHITE, BG);
}

static void draw_value(int i)
{
    char s[16], *p;

    int x = 8 + (i & 1) * 114;
    int y = 125 + (i / 2) * 56;

    if (i == 0) {
        fixed100(s, ratio100[ratio_i]);
    }
    else if (i == 1) {
        fixed100(s, (unsigned)depth_i * 25u);
    }
    else {
        p = put_uint(
            s,
            time_ms[i == 2 ? attack_i : release_i]
        );

        *p++ = ' ';
        *p++ = 'M';
        *p++ = 'S';
        *p = '\0';
    }

    text_box(
        x + 5, y + 21,
        100, 26,
        s, i < 2 ? 3 : 2,
        CYAN, PANEL
    );
}

/* =========================================================
   OSCILLOSCOPE DRAWING
   ========================================================= */

/* The scope is divided into eight small tiles.
   Only changed tiles are transferred to the LCD.
*/

static void draw_scope_tile(
    int tile, const uint8_t *trace)
{
    int x, y;
    int start = tile * TILE;

    for (x = 0; x < TILE; ++x) {

        int gx = start + x;

        int a = trace[gx ? gx - 1 : 0];
        int b = trace[gx];

        int lo = a < b ? a : b;
        int hi = a > b ? a : b;

        for (y = 0; y < SH; ++y) {

            uint16_t c = BG;

            /* Scope grid. */
            if (gx % TILE == 0 || y % 18 == 0)
                c = GRID;

            if (y == SH / 2)
                c = ZERO;

            /* Waveform. */
            if (y >= lo && y <= hi)
                c = CYAN;

            pixels[y * TILE + x] = c;
        }
    }

    fm1_lcd_blit(
        16 + start, 40,
        TILE, SH,
        pixels, TILE * SH
    );
}

static void update_scope(void)
{
    int i;

    if (scope_tile >= SW / TILE) {

        uint32_t seq = scope_seq;
        uint32_t now = audio_blocks;

        int peak = 2048;

        /* Limit scope refresh to about 21 FPS. */
        if (seq == displayed_seq ||
            now - last_scope_block < 8u)
            return;

        for (i = 0; i < SW; ++i)
            scope_copy[i] = scope_data[seq & 1u][i];

        /* If audio published another frame while copying,
           try again on the next loop call.
        */
        if (scope_seq != seq)
            return;

        /* Automatic vertical scaling. */
        for (i = 0; i < SW; ++i) {

            int a = scope_copy[i];

            if (a < 0)
                a = -a;

            if (a > peak)
                peak = a;
        }

        for (i = 0; i < SW; ++i) {
            new_y[i] = (uint8_t)(
                SH / 2 - (scope_copy[i] * 32) / peak
            );
        }

        displayed_seq = seq;
        last_scope_block = now;
        scope_tile = 0;
    }

    /* Redraw at most one small changed tile per loop. */

    i = scope_tile * TILE;

    if (i > 0)
        --i;

    for (; i < (scope_tile + 1) * TILE; ++i) {

        if (old_y[i] != new_y[i]) {
            draw_scope_tile(scope_tile, new_y);
            break;
        }
    }

    ++scope_tile;

    if (scope_tile == SW / TILE) {
        for (i = 0; i < SW; ++i)
            old_y[i] = new_y[i];
    }
}

/* =========================================================
   ENCODER HELPERS
   ========================================================= */

static int adjust(
    int value, int32_t delta, int min, int max)
{
    if (delta > max - value)
        return max;

    if (delta < min - value)
        return min;

    return value + (int)delta;
}

static void knob(
    int *value, unsigned encoder,
    int max, unsigned bit)
{
    int next = adjust(
        *value,
        fm1_enc_take(encoder),
        0, max
    );

    if (next != *value) {
        *value = next;
        dirty |= bit;
    }
}

/* =========================================================
   SETUP
   ========================================================= */

void setup(void)
{
    unsigned i;

    static const char *const labels[] = {
        "1 RATIO",
        "2 DEPTH",
        "3 ATTACK",
        "4 RELEASE"
    };

    /* Generate the sine table once. */
    for (i = 0; i < SIN_N; ++i) {

        sine_table[i] = (int16_t)(
            32767.0f *
            sinf(6.283185307f *
                 (float)i / (float)SIN_N)
        );
    }

    sine_table[SIN_N] = sine_table[0];

    /* Precalculate all MIDI note phase increments. */
    for (i = 0; i < 128u; ++i) {

        float hz = 440.0f *
            powf(2.0f, ((float)i - 69.0f) / 12.0f);

        note_step[i] = (uint32_t)(
            hz * (4294967296.0f / (float)FM1_AUDIO_RATE)
        );
    }

    /* All voices initially unused. */
    for (i = 0; i < NV; ++i)
        voices[i].key = -1;

    /* Turn off all LEDs. */
    for (i = 0; i < FM1_NKEY; ++i)
        fm1_led_key(i, 0);

    publish_controls();

    /* Draw the static user interface once. */

    fm1_lcd_fill_rect(0, 0, 240, 240, BG);

    text_box(
        8, 4, 128, 24,
        "FM POLY", 3,
        CYAN, BG
    );

    draw_octave();

    /* Oscilloscope border. */
    fm1_lcd_fill_rect(
        15, 39, SW + 2, SH + 2, MUTED
    );

    /* Initial flat waveform. */
    for (i = 0; i < SW; ++i)
        old_y[i] = SH / 2;

    for (i = 0; i < SW / TILE; ++i)
        draw_scope_tile((int)i, old_y);

    /* Four parameter panels. */
    for (i = 0; i < 4; ++i) {

        int x = 8 + (int)(i & 1u) * 114;
        int y = 125 + (int)(i / 2u) * 56;

        fm1_lcd_fill_rect(
            x, y, 110, 50, PANEL
        );

        text_box(
            x + 1, y + 3,
            108, 16,
            labels[i], 2,
            MUTED, PANEL
        );

        draw_value((int)i);
    }
}

/* =========================================================
   MAIN LOOP
   ========================================================= */

void loop(void)
{
    uint32_t buttons = fm1_input_edges(0);

    uint32_t presses, held, led, changed;
    unsigned k;

    int next = octave;

    /* Octave buttons. */
    if (buttons & (1u << FM1_BTN_OCT_DOWN))
        --next;

    if (buttons & (1u << FM1_BTN_OCT_UP))
        ++next;

    next = adjust(next, 0, -2, 2);

    if (next != octave) {
        octave = next;
        dirty |= 16u;
    }

    /* Four sound parameters. */
    knob(&ratio_i,   FM1_ENC_KNOB1, 13, 1u);
    knob(&depth_i,   FM1_ENC_KNOB2, 48, 2u);
    knob(&attack_i,  FM1_ENC_KNOB3, 15, 4u);
    knob(&release_i, FM1_ENC_KNOB4, 17, 8u);

    publish_controls();

    /* Read keyboard input.
       Press edges also capture very short key presses.
    */

    presses = fm1_input_note_edges();
    held = fm1_in.notes;

    presses |= held & ~old_keys;

    for (k = 0; k < FM1_NOTE_KEYS; ++k) {

        uint32_t bit = 1u << k;

        if (presses & bit) {

            /* Note key zero is F3, MIDI note 53. */
            unsigned midi =
                (unsigned)(53 + (int)k + 12 * octave);

            uint32_t gen =
                (key_word[k] + 0x100u) & 0xFFFFFF00u;

            key_word[k] =
                gen |
                ((uint32_t)midi << 1) |
                ((held & bit) ? 1u : 0u);
        }
        else if ((old_keys & bit) && !(held & bit)) {

            key_word[k] &= ~1u;
        }
    }

    old_keys = held;

    /* Illuminate keys whose voices are sounding.
       This includes release tails.
    */

    led = playing_keys;
    changed = led ^ old_leds;

    for (k = 0; k < FM1_NOTE_KEYS; ++k) {

        if (changed & (1u << k)) {
            fm1_led_key(
                FM1_KEY_NOTE0 + k,
                (led >> k) & 1u
            );
        }
    }

    old_leds = led;

    /* At most one LCD blit per loop call.
       Changed control values have priority.
    */

    if (dirty & 16u) {

        draw_octave();
        dirty &= ~16u;
        return;
    }

    for (k = 0; k < 4; ++k) {

        if (dirty & (1u << k)) {

            draw_value((int)k);
            dirty &= ~(1u << k);
            return;
        }
    }

    update_scope();
}
