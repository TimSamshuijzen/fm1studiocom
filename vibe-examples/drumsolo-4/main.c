#include "fm1.h"

enum { CRASH, CLOSED, RIDE, HIGH, MID, LOW, SNARE, KICK, OPEN, DRUMS };
enum { VOICES = 16, TILE_W = 70, TILE_H = 50, RATE = FM1_AUDIO_RATE };
enum { BG = 0x0862, PANEL = 0x1125, EDGE = 0x29e9, WHITE = 0xef7d,
       MUTED = 0x94d2, GREEN = 0x7730 };

typedef struct {
    const char *name;
    uint16_t color;
    float hz, sweep, decay, noise, snap, snap_decay, gain, pan;
    uint16_t life_ms;
} Drum;

static const Drum kit[DRUMS] = {
    {"CRASH", 0xfca9,    0,    0, .99990f, .74f, .21f, .9982f, .37f, -.56f, 1150},
    {"C HAT", 0x4ed8,    0,    0, .99875f, .71f, .46f, .9950f, .30f, -.38f,  100},
    {"RIDE",  0xff0a,    0,    0, .99988f, .37f, .52f, .9983f, .31f,  .61f, 1050},
    {"HI TOM",0x8cdf,  171,   93, .99962f, .05f, .10f, .9940f, .54f, -.30f,  310},
    {"MID TOM",0xa53f, 126,   81, .99972f, .06f, .11f, .9940f, .56f,  .03f,  400},
    {"LO TOM",0xc57f,   85,   69, .99976f, .06f, .12f, .9940f, .58f,  .36f,  460},
    {"SNARE", 0xfacf,  178,   70, .99944f, .63f, .22f, .9960f, .68f, -.14f,  240},
    {"KICK",  0x7730,   48,  113, .99966f, .02f, .12f, .9890f, .88f,  0.0f,  400},
    {"O HAT", 0x4ed8,    0,    0, .99975f, .68f, .30f, .9970f, .32f, -.38f,  480}
};
static const float strength[] = { .38f, .68f, 1.0f };

typedef struct {
    const Drum *drum;
    uint32_t phase, overtone;
    float pitch, base, env, snap, decay, left, right;
    int key, remaining, age;
} Voice;

static Voice voices[VOICES];
static float sine[1024];
static uint32_t random_state = 0x91e10da5u;
static uint32_t metal_phase[4];
static float noise_low, volume;

/* Each shared word has one writer. Audio owns the voices and clock. */
static volatile uint32_t strikes[FM1_NOTE_KEYS];
static volatile uint32_t controls = 118u;
static volatile uint32_t master;
static volatile uint32_t sounding, flashing, transport;
static uint32_t handled[FM1_NOTE_KEYS];

static uint16_t pixels[TILE_W * TILE_H];
static uint8_t led_cache[FM1_NLED];
static int bpm = 118, playing;
static unsigned screen_y;
static unsigned displayed_pads, next_pad;
static uint32_t displayed_status = UINT32_MAX;

static void hit(int drum, int level)
{
    const Drum *d = &kit[drum];
    Voice *slot = &voices[0];

    for (int i = 0; i < VOICES; ++i) {
        Voice *v = &voices[i];
        if (drum == CLOSED && v->remaining && v->drum == &kit[OPEN]) {
            v->decay = .976f;
            v->snap *= .12f;
            if (v->remaining > 250) v->remaining = 250;
        }
        if (!v->remaining || (slot->remaining && v->env < slot->env))
            slot = v;
    }

    slot->drum = d;
    slot->key = drum * 3 + level;
    slot->phase = slot->overtone = 0;
    slot->base = d->hz * (4294967296.0f / RATE);
    slot->pitch = (d->hz + d->sweep) * (4294967296.0f / RATE);
    slot->env = strength[level];
    slot->snap = strength[level] * d->snap;
    slot->decay = d->decay;
    slot->left = d->gain * (.76f - .29f * d->pan);
    slot->right = d->gain * (.76f + .29f * d->pan);
    slot->remaining = (int)d->life_ms * RATE / 1000;
    slot->age = 0;
}

static void solo_step(unsigned beat)
{
    static const uint16_t kicks[8] = {
        0x0549, 0x4581, 0x0529, 0x00c5, 0x0949, 0x05a1, 0x4585, 0x0151
    };
    static const uint16_t snares[8] = {
        0x1010, 0x9010, 0x1010, 0x0210, 0x1210, 0x1010, 0x9010, 0x0290
    };
    static const uint16_t cymbals[8] = {
        0x5554, 0x7555, 0x5555, 0x0155, 0x5554, 0x7555, 0x5755, 0x0155
    };
    static const uint16_t open_hats[8] = {
        0x0040, 0x4000, 0x0400, 0, 0, 0, 0, 0
    };
    static const int fills[2][8] = {
        {SNARE, HIGH, -1, MID, MID, LOW, SNARE, LOW},
        {HIGH, -1, MID, SNARE, LOW, MID, LOW, SNARE}
    };
    unsigned bar = beat / 16u;
    unsigned step = beat % 16u;
    unsigned bit = 1u << step;

    if (kicks[bar] & bit) hit(KICK, step % 4u ? 1 : 2);
    if (snares[bar] & bit) hit(SNARE, (step == 4u || step == 12u) ? 2 : 0);
    if (!step && (bar == 0u || bar == 4u)) hit(CRASH, 2);
    if (cymbals[bar] & bit) {
        int cymbal = bar < 4u ? CLOSED : RIDE;
        if (open_hats[bar] & bit) cymbal = OPEN;
        hit(cymbal, step % 4u ? 0 : 1);
    }
    if ((bar == 3u || bar == 7u) && step >= 8u) {
        int drum = fills[bar == 7u][step - 8u];
        if (drum >= 0) hit(drum, step % 2u ? 1 : 2);
    }
}

static int32_t sample24(float x)
{
    if (x > .73f) x = 1.0f - .27f / (1.0f + (x - .73f) * 3.70f);
    if (x < -.73f) x = -1.0f + .27f / (1.0f + (-x - .73f) * 3.70f);
    return (int32_t)(x * (.96f * FM1_AUDIO_MAX));
}

void audio(int32_t *out, int frames)
{
    static int running, left_q8, pulse;
    static unsigned step, current;
    uint32_t command = controls;
    int run = (int)(command >> 8);
    int interval = RATE * 60 * 64 / (int)(command & 255u);
    float target = (float)master * (1.0f / FM1_MASTER_MAX);
    target *= target;

    if (run != running) {
        running = run;
        step = current = 0;
        left_q8 = pulse = 0;
    }
    for (int key = 0; key < (int)FM1_NOTE_KEYS; ++key) {
        uint32_t count = strikes[key];
        if (count != handled[key]) {
            handled[key] = count;
            hit(key / 3, key % 3);
        }
    }

    for (int i = 0; i < frames; ++i) {
        if (running) {
            if (left_q8 <= 0) {
                current = step;
                solo_step(step);
                int swing = interval / 10;
                left_q8 += interval + ((step & 1u) ? -swing : swing);
                if (step % 4u == 0u) pulse = RATE / 10;
                step = (step + 1u) & 127u;
            }
            left_q8 -= 256;
            if (pulse) --pulse;
        }

        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        float noise = (float)(random_state >> 16) * (1.0f / 32768.0f) - 1.0f;
        noise_low += .18f * (noise - noise_low);
        float bright = noise - noise_low;
        static const uint32_t metal_inc[] = {30775700u, 43297100u, 55721100u, 78317500u};
        unsigned metal_bits = 0;
        for (int m = 0; m < 4; ++m) {
            metal_phase[m] += metal_inc[m];
            metal_bits += metal_phase[m] >> 31;
        }
        float metal = ((float)metal_bits - 2.0f) * .32f;

        float l = 0.0f, r = 0.0f;
        for (int j = 0; j < VOICES; ++j) {
            Voice *v = &voices[j];
            if (!v->remaining) continue;
            const Drum *d = v->drum;
            float body;
            if (d->hz == 0.0f) {
                body = bright * d->noise + metal * (1.0f - d->noise);
            } else {
                uint32_t inc = (uint32_t)v->pitch;
                v->pitch += .0040f * (v->base - v->pitch);
                v->phase += inc;
                v->overtone += inc + (inc >> 1);
                float tone = .86f * sine[v->phase >> 22]
                           + .14f * sine[v->overtone >> 22];
                body = tone * (1.0f - d->noise) + (.65f * bright + noise_low) * d->noise;
            }
            float sample = v->env * body + v->snap * bright;
            if (v->age < 32) sample *= (float)v->age * (1.0f / 32.0f);
            if (v->remaining < 64) sample *= (float)v->remaining * (1.0f / 64.0f);
            l += sample * v->left;
            r += sample * v->right;
            v->env *= v->decay;
            v->snap *= d->snap_decay;
            --v->remaining;
            ++v->age;
        }
        volume += .006f * (target - volume);
        if (target == 0.0f && volume < .00001f) volume = 0.0f;
        out[2 * i] = sample24(l * volume);
        out[2 * i + 1] = sample24(r * volume);
    }

    uint32_t notes = 0, pads = 0;
    for (int j = 0; j < VOICES; ++j) {
        const Voice *v = &voices[j];
        if (!v->remaining) continue;
        notes |= 1u << v->key;
        if (v->age < RATE / 12) pads |= 1u << (v->key / 3);
    }
    sounding = notes;
    flashing = pads;
    transport = (running ? 128u : 0u) | (pulse ? 256u : 0u) | current;
}

/* Five columns per glyph, with the top row in the low bit. */
static const uint8_t letters[36][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
};

static void rect(int stride, int x, int y, int w, int h, uint16_t color)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            pixels[j * stride + i] = color;
}

static void label(int stride, int x, int y, const char *s, int scale, uint16_t color)
{
    for (; *s; ++s, x += 6 * scale) {
        if (*s == '-') {
            rect(stride, x + scale, y + 3 * scale, 3 * scale, scale, color);
            continue;
        }
        int g = *s >= 'A' && *s <= 'Z' ? *s - 'A' : *s - '0' + 26;
        if (*s == ' ' || g < 0 || g >= 36) continue;
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 7; ++j)
                if (letters[g][i] & (1u << j))
                    rect(stride, x + i * scale, y + j * scale, scale, scale, color);
    }
}

static void draw_pad(int drum, int active)
{
    const Drum *d = &kit[drum];
    int cymbal = d->hz == 0.0f;
    int rx = cymbal ? 22 : (drum == KICK ? 17 : 15);
    int ry = cymbal ? 5 : (drum == KICK ? 12 : 8);
    int border = rx * rx * ry * ry;
    int first = drum * 3 + 1, last = first + 2;
    char range[] = {'0' + first / 10, '0' + first % 10, '-',
                    '0' + last / 10, '0' + last % 10, 0};
    uint16_t rim = active ? WHITE : d->color;
    uint16_t skin = active ? d->color : EDGE;

    rect(TILE_W, 0, 0, TILE_W, TILE_H, active ? EDGE : PANEL);
    rect(TILE_W, 0, 0, TILE_W, 2, active ? d->color : EDGE);
    if (cymbal) rect(TILE_W, 34, 11, 2, 15, MUTED);
    else if (drum != KICK) rect(TILE_W, 21, 17, 28, 8, EDGE);
    for (int y = -ry; y <= ry; ++y) {
        for (int x = -rx; x <= rx; ++x) {
            int distance = x * x * ry * ry + y * y * rx * rx;
            if (distance <= border)
                pixels[(16 + y) * TILE_W + 35 + x] = distance * 10 > border * 7 ? rim : skin;
        }
    }
    if (cymbal) rect(TILE_W, 32, 14, 7, 3, rim);
    int width = 0;
    while (d->name[width]) ++width;
    label(TILE_W, (TILE_W - (width * 6 - 1)) / 2, 30, d->name, 1, WHITE);
    label(TILE_W, 20, 40, range, 1, active ? WHITE : MUTED);
    fm1_lcd_blit(9u + (uint32_t)(drum % 3) * 76u, 52u + (uint32_t)(drum / 3) * 56u,
                 TILE_W, TILE_H, pixels, TILE_W * TILE_H);
}

static void draw_status(uint32_t state)
{
    char tempo[] = {'0' + bpm / 100, '0' + (bpm / 10) % 10, '0' + bpm % 10,
                    ' ', 'B', 'P', 'M', 0};
    char bar[] = "BAR 1";
    rect(222, 0, 0, 222, 14, PANEL);
    label(222, 6, 4, tempo + (bpm < 100), 1, WHITE);
    if (state & 128u) {
        bar[4] = '1' + ((state & 127u) / 16u);
        label(222, 79, 4, bar, 1, GREEN);
        for (unsigned i = 0; i < 4; ++i)
            rect(222, 150 + (int)i * 15, 5, 9, 4,
                 i == ((state & 15u) / 4u) ? GREEN : EDGE);
    } else {
        label(222, 79, 4, "READY", 1, MUTED);
    }
    fm1_lcd_blit(9, 30, 222, 14, pixels, 222 * 14);
}

static void led(unsigned id, int level)
{
    if (led_cache[id] == level) return;
    led_cache[id] = (uint8_t)level;
    fm1_led_level(id, level);
}

void setup(void)
{
    /* Rotate a unit vector by one table step. */
    float s = 0.0f, c = 1.0f;
    for (int i = 0; i < 1024; ++i) {
        sine[i] = s;
        float next = s * .9999811753f + c * .0061358846f;
        c = c * .9999811753f - s * .0061358846f;
        s = next;
    }
    for (unsigned i = 0; i < FM1_NLED; ++i) led_cache[i] = 255u;
}

void loop(void)
{
    uint32_t button = fm1_input_edges(0);
    uint32_t notes = fm1_input_note_edges();
    for (unsigned i = 0; i < FM1_NOTE_KEYS; ++i)
        if (notes & (1u << i)) ++strikes[i];
    if (button & (1u << FM1_BTN_PLAY)) playing = !playing;
    int32_t delta = fm1_enc_take(FM1_ENC_SELECT);
    if (delta < 60 - bpm) delta = 60 - bpm;
    if (delta > 190 - bpm) delta = 190 - bpm;
    bpm += (int)delta;
    controls = ((uint32_t)playing << 8) | (uint32_t)bpm;
    master = FM1_MASTER_MAX; /* MASTER is the FM-1's own volume (on the speaker): the full level here */

    uint32_t state = transport;
    uint32_t note_lights = sounding;
    for (unsigned i = 0; i < FM1_NLED; ++i) {
        int level = FM1_LED_OFF;
        if (i == FM1_BTN_PLAY)
            level = (!playing || (state & 256u)) ? FM1_LED_ON : FM1_LED_GLOW;
        if (i >= FM1_KEY_NOTE0 && i < FM1_KEY_NOTE0 + FM1_NOTE_KEYS)
            level = note_lights & (1u << (i - FM1_KEY_NOTE0)) ? FM1_LED_ON : FM1_LED_OFF;
        led(i, level);
    }

    if (screen_y < FM1_LCD_HEIGHT) {
        fm1_lcd_fill_rect(0, screen_y, FM1_LCD_WIDTH, 24, BG);
        screen_y += 24;
        return;
    }
    if (screen_y == FM1_LCD_HEIGHT) {
        rect(108, 0, 0, 108, 16, BG);
        label(108, 0, 1, "DRUM ROOM", 2, WHITE);
        fm1_lcd_blit(9, 8, 108, 16, pixels, 108 * 16);
        rect(222, 0, 0, 222, 11, BG);
        label(222, 0, 2, "PLAY SOLO   SELECT BPM", 1, MUTED);
        fm1_lcd_blit(9, 224, 222, 11, pixels, 222 * 11);
        ++screen_y;
        return;
    }

    uint32_t status = (state & 252u) | ((uint32_t)bpm << 8);
    if (status != displayed_status) {
        draw_status(state);
        displayed_status = status;
    }
    unsigned active = flashing;
    for (int i = 0; i < DRUMS; ++i) {
        unsigned drum = next_pad;
        unsigned bit = 1u << drum;
        next_pad = (next_pad + 1u) % DRUMS;
        if (!(displayed_pads & (bit << DRUMS)) || ((displayed_pads ^ active) & bit)) {
            draw_pad((int)drum, (active & bit) != 0u);
            displayed_pads = (displayed_pads & ~bit) | (active & bit) | (bit << DRUMS);
            break;
        }
    }
}
