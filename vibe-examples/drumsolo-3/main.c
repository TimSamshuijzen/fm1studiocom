#include "fm1.h"
#include <math.h>

enum { KICK, SNARE, CLAP, LOW_TOM, HIGH_TOM, CLOSED_HAT, OPEN_HAT, RIDE, CRASH, DRUMS };
enum { PCM_RATE = FM1_AUDIO_RATE / 2, SINE_SIZE = 512, POLYPHONY = 14 };
enum {
    KICK_LEN = 7600, SNARE_LEN = 6300, CLAP_LEN = 5800,
    LOW_LEN = 8800, HIGH_LEN = 7400, CLOSED_LEN = 1800,
    OPEN_LEN = 8300, RIDE_LEN = 14600, CRASH_LEN = 18200,
    PCM_SIZE = KICK_LEN + SNARE_LEN + CLAP_LEN + LOW_LEN + HIGH_LEN +
               CLOSED_LEN + OPEN_LEN + RIDE_LEN + CRASH_LEN
};

#define BIT(n) (1u << (n))
#define RGB(r,g,b) ((uint16_t)((((r) & 248u) << 8) | (((g) & 252u) << 3) | ((b) >> 3)))

typedef struct {
    int length;
    float pan, level;
    float decay[3];
    const char *label;
    uint16_t color;
} Drum;

static const Drum kit[DRUMS] = {
    {KICK_LEN,    0.00f, 1.05f, {.075f,.024f,.005f}, "KICK",     RGB(255,154,99)},
    {SNARE_LEN,  -0.22f, 1.06f, {.050f,.034f,.084f}, "SNARE",    RGB(108,235,216)},
    {CLAP_LEN,    0.24f, 0.86f, {.005f,.070f,.020f}, "CLAP",     RGB(255,136,172)},
    {LOW_LEN,     0.34f, 0.95f, {.104f,.057f,.012f}, "LOW TOM",  RGB(115,183,255)},
    {HIGH_LEN,   -0.08f, 0.91f, {.082f,.043f,.010f}, "HIGH TOM", RGB(156,174,255)},
    {CLOSED_LEN, -0.48f, 0.67f, {.029f,.017f,.008f}, "CLOSED",   RGB(251,218,122)},
    {OPEN_LEN,   -0.48f, 0.60f, {.110f,.072f,.016f}, "OPEN",     RGB(251,218,122)},
    {RIDE_LEN,    0.52f, 0.74f, {.210f,.135f,.015f}, "RIDE",     RGB(255,193,111)},
    {CRASH_LEN,   0.19f, 0.79f, {.205f,.171f,.035f}, "CRASH",    RGB(255,206,125)}
};

static uint8_t pcm[PCM_SIZE];
static const uint8_t *sample_data[DRUMS];
static float sine_table[SINE_SIZE];
static float decode[256], pan_l[DRUMS], pan_r[DRUMS];
static uint32_t rng = 0x4ea7213du;

static float noise(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

static float osc(float *phase, float hz)
{
    float p = *phase + hz * (1.0f / PCM_RATE);
    if (p >= 1.0f) p -= 1.0f;
    *phase = p;
    return sine_table[(unsigned)(p * SINE_SIZE) & (SINE_SIZE - 1)];
}

/* Build companded, half-rate samples at boot. The audio callback interpolates them. */
static void make_sample(int d, uint8_t *dst)
{
    static const float metal[3][6] = {
        {2231.f, 3187.f, 4237.f, 5641.f, 7349.f, 8893.f},
        {1657.f, 2371.f, 3163.f, 4597.f, 5839.f, 7919.f},
        {1873.f, 2731.f, 3911.f, 5303.f, 7069.f, 8521.f}
    };
    float p[6] = {0}, env[3] = {1.f, 1.f, 1.f}, mul[3];
    float drop = 1.f, slow = 0.f, fast = 0.f;
    float pitch_mul = expf(-1.f / (0.018f * PCM_RATE));
    int len = kit[d].length;
    for (int j = 0; j < 3; ++j) mul[j] = expf(-1.f / (kit[d].decay[j] * PCM_RATE));
    if (d == CLAP) env[1] = 0.f;
    if (d >= CLOSED_HAT)
        for (int j = 0; j < 6; ++j) p[j] = .5f + .49f * noise();

    for (int i = 0; i < len; ++i) {
        float white = noise();
        slow += .11f * (white - slow);
        fast += .61f * (white - fast);
        float band = fast - slow, air = white - fast, s;
        switch (d) {
        case KICK:
            s = .91f * env[0] * osc(&p[0], 49.f + 137.f * drop)
              + .23f * env[1] * osc(&p[1], 97.f + 98.f * drop)
              + .27f * env[2] * (.6f * band + air + .22f * osc(&p[2], 2160.f));
            break;
        case SNARE:
            s = env[0] * (.40f * osc(&p[0], 173.f + 38.f * drop)
                        + .22f * osc(&p[1], 329.f + 65.f * drop)
                        + .09f * osc(&p[2], 497.f))
              + (1.24f * env[1] + .29f * env[2]) * (.72f * band + 1.04f * air);
            break;
        case CLAP:
            if (i == 176 || i == 399) env[0] = 1.f;
            if (i == 399) env[1] = 1.f;
            s = (1.26f * env[0] + .68f * env[1]) * (band + .57f * air)
              + .07f * env[2] * osc(&p[0], 1347.f);
            break;
        case LOW_TOM:
        case HIGH_TOM: {
            float hz = d == LOW_TOM ? 96.f + 51.f * drop : 148.f + 63.f * drop;
            s = .84f * env[0] * osc(&p[0], hz)
              + env[1] * (.23f * osc(&p[1], hz * 1.61f) + .13f * osc(&p[2], hz * 2.19f))
              + .25f * env[2] * (band + .7f * air);
            break;
        }
        default: {
            const float *hz = metal[d == CRASH ? 2 : d == RIDE ? 1 : 0];
            float bell = .54f * osc(&p[0], hz[0]) + .29f * osc(&p[1], hz[1])
                       + .17f * osc(&p[2], hz[2]);
            float shimmer = .41f * osc(&p[3], hz[3]) + .34f * osc(&p[4], hz[4])
                          + .25f * osc(&p[5], hz[5]);
            if (d == RIDE)
                s = .30f * env[0] * bell + env[1] * (.26f * shimmer + .43f * air)
                  + env[2] * (.48f * air + .29f * band);
            else if (d == CRASH)
                s = env[0] * (.19f * bell + .46f * air)
                  + env[1] * (.28f * shimmer + .37f * band)
                  + env[2] * (.46f * air + .21f * band);
            else
                s = env[0] * (.22f * shimmer + .66f * air)
                  + env[1] * (.23f * bell + .36f * band) + .51f * env[2] * air;
            break;
        }
        }
        for (int j = 0; j < 3; ++j) env[j] *= mul[j];
        drop *= pitch_mul;
        if (i < 9) s *= (float)i * (1.f / 9.f);
        if (len - 1 - i < 96) s *= (float)(len - 1 - i) * (1.f / 96.f);
        float mag = fabsf(s) * .79f;
        if (mag > 1.f) mag = 1.f;
        int code = (int)(sqrtf(mag) * 127.f + .5f);
        dst[i] = (uint8_t)(128 + (s < 0.f ? -code : code));
    }
}

/* One row per bar, in keyboard order. Digits are velocities, f = flam, r = roll. */
static const char score[][DRUMS][17] = {
    {
        "9-----6-8--5--6-", "--1-8------29---", "----3-------4---",
        "", "", "7-4-6-3-7-4-6---",
        "--------------5-", "", "7---------------"
    },
    {
        "8--4---68-5----5", "----8--2----9-2-", "----4-----------",
        "---------------4", "", "6-3-7---6-4-7-3-",
        "------5---------", "", ""
    },
    {
        "9-----5--6--8---", "---29-----2-f---", "------------5---",
        "----------------", "---------------5", "7-4-6-3-7-4---3-",
        "------------5---", "", ""
    },
    {
        "8-----6-8-----7-", "----9---1-------", "----4-----------",
        "----------5--7r-", "---------5-76---", "6-4-7-----------",
        "------6---------", "", ""
    },
    {
        "9--5--6-8------6", "----7------28---", "",
        "-----6--7-5----7", "-6-7--r--5---6--", "--4-------------",
        "----------------", "6---6---7---6---", "9---------------"
    },
    {
        "8---7------5--6-", "----f-----2-8---", "",
        "--5----78------8", "---5--6---6--6--", "--------------4-",
        "----------------", "6-3-6-3-6-3-7---", ""
    },
    {
        "9----5--8-----7-", "--2-----f------2", "--------5-------",
        "----8-6----7----", "-7-6--------r-7-", "----------4-----",
        "--------------5-", "6-----4-7---6---", ""
    },
    {
        "8--5--6-8-------", "----8--2-----4f-", "",
        "----------6-7--8", "--------r----5--", "6-4-6-3---------",
        "----------------", "", ""
    },
    {
        "9-----6-8--5--7-", "----9----2--f---", "----6-------5---",
        "--------------6-", "----------5-----", "--4-7---7-3-6---",
        "------5---------", "", "9-------5-------"
    },
    {
        "8-5--6--9-----5-", "---28-----2-9---", "----4-------5---",
        "----------------", "---------------6", "7-4-6-3---------",
        "--------6-------", "----------4-6-4-", ""
    },
    {
        "9-----6-8------6", "----f-------8---", "",
        "-------6-8r---8-", "-----7--6----7--", "7-4-6-----------",
        "----------------", "", "5---------------"
    },
    {
        "9-------8-----7-", "----r-6-2-----f-", "--------------7-",
        "---------7r-8---", "------6-7----6--", "7---------------",
        "----------------", "", "--------------8-"
    },
};
enum { BARS = sizeof score / sizeof score[0], STEPS = 16, SOLO_LIGHT = 1, BEAT_LIGHT = 2 };

/* Each shared word has one writer. Audio owns playback and publishes the LED state. */
static volatile uint32_t want_play, want_bpm = 116, master;
static volatile uint32_t key_ticket[FM1_NOTE_KEYS];
static volatile uint32_t lit_notes, lit_pads, transport;
static uint32_t seen_ticket[FM1_NOTE_KEYS];

typedef struct {
    int drum, pos;
    uint32_t key;
    float left, right, fade, release;
} Voice;

static Voice voices[POLYPHONY];
static int voice_count, playing, bar, step, shown_bar, shown_beat;
static uint32_t now, last_beat, until_step, step_length[2];
static uint32_t last_hit[DRUMS], flashing, flam_at, roll_at, flams, rolls;
static float volume;

static void hit(int drum, int key, float velocity)
{
    int oldest = 0;
    for (int i = 0; i < voice_count; ++i) {
        Voice *v = &voices[i];
        if (v->pos > voices[oldest].pos) oldest = i;
        if (drum == CLOSED_HAT && v->drum == OPEN_HAT) v->release = 1.f / 176.f;
        if (drum == v->drum && drum < RIDE && v->release == 0.f)
            v->release = 1.f / 370.f;
    }
    int slot = voice_count < POLYPHONY ? voice_count++ : oldest;
    voices[slot] = (Voice){drum, 0, BIT(key), pan_l[drum] * velocity,
                         pan_r[drum] * velocity, 1.f, 0.f};
    last_hit[drum] = now;
    flashing |= BIT(drum);
}

static void solo_step(void)
{
    shown_bar = bar;
    shown_beat = step / 4;
    if (step % 4 == 0) last_beat = now;

    until_step = step_length[step & 1];
    flam_at = now + 530u;
    roll_at = now + until_step / 2u;
    for (int d = 0; d < DRUMS; ++d) {
        char mark = score[bar][d][step];
        if (mark >= '1' && mark <= '9')
            hit(d, DRUMS + d, .075f + .102f * (float)(mark - '0'));
        else if (mark == 'f') {
            hit(d, DRUMS + d, .27f);
            flams |= BIT(d);
        } else if (mark == 'r') {
            hit(d, DRUMS + d, .69f);
            rolls |= BIT(d);
        }
    }
    if (++step == STEPS) {
        step = 0;
        if (++bar == BARS) bar = 0;
    }
}

static void ornaments(uint32_t *mask, float velocity)
{
    for (int d = 0; d < DRUMS; ++d)
        if (*mask & BIT(d)) hit(d, DRUMS + d, velocity);
    *mask = 0;
}

static int32_t output_sample(float x)
{
    float mag = fabsf(x);
    if (mag > .70f) {
        float over = mag - .70f;
        mag = .70f + .24f * over / (.24f + over);
    }
    return (int32_t)((x < 0.f ? -mag : mag) * (float)FM1_AUDIO_MAX);
}

void audio(int32_t *out, int frames)
{
    if (frames <= 0) return;
    if ((int)want_play != playing) {
        playing = !playing;
        bar = step = 0;
        until_step = flams = rolls = 0;
    }

    /* Two swing steps have the duration of one straight eighth note. */
    unsigned pair = (FM1_AUDIO_RATE * 30u) / want_bpm;
    step_length[0] = (54u * pair + 50u) / 100u;
    step_length[1] = pair - step_length[0];

    static const float touch[3] = {.53f, .77f, 1.f};
    for (int k = 0; k < (int)FM1_NOTE_KEYS; ++k) {
        uint32_t ticket = key_ticket[k];
        if (ticket != seen_ticket[k]) {
            seen_ticket[k] = ticket;
            hit(k % DRUMS, k, touch[k / DRUMS]);
        }
    }

    float gain = volume;
    float target = (float)master * (1.f / FM1_MASTER_MAX);
    target *= target;
    float ramp = (target - gain) / (float)frames;
    for (int f = 0; f < frames; ++f, ++now) {
        if (playing) {
            if (flams && now == flam_at) ornaments(&flams, .94f);
            if (rolls && now == roll_at) ornaments(&rolls, .88f);
            if (until_step == 0) solo_step();
            --until_step;
        }
        float left = 0.f, right = 0.f;
        for (int i = 0; i < voice_count;) {
            Voice *v = &voices[i];
            const uint8_t *p = sample_data[v->drum] + (v->pos >> 1);
            float s = decode[p[0]];
            if (v->pos & 1) s = .5f * (s + decode[p[1]]);
            s *= v->fade;
            left += s * v->left;
            right += s * v->right;
            v->fade -= v->release;
            if (++v->pos >= 2 * (kit[v->drum].length - 1) || v->fade <= 0.f)
                *v = voices[--voice_count];
            else
                ++i;
        }
        gain += ramp;
        *out++ = output_sample(left * gain);
        *out++ = output_sample(right * gain);
    }
    volume = target;

    uint32_t notes = 0;
    for (int i = 0; i < voice_count; ++i) notes |= voices[i].key;
    for (int d = 0; d < DRUMS; ++d)
        if ((flashing & BIT(d)) && now - last_hit[d] >= FM1_AUDIO_RATE / 10u)
            flashing &= ~BIT(d);
    lit_notes = notes;
    lit_pads = flashing;
    transport = playing ? SOLO_LIGHT | ((uint32_t)shown_beat << 2) |
                ((uint32_t)shown_bar << 4) |
                (now - last_beat < FM1_AUDIO_RATE / 11u ? BEAT_LIGHT : 0u) : 0u;
}

enum { PAD_W = 70, PAD_H = 47, STRIP_W = 222, STRIP_H = 19 };
static uint16_t pixels[STRIP_W * STRIP_H];
static int canvas_w;
static const uint16_t bg = RGB(11,18,29), ink = RGB(235,244,249);
static const uint16_t dim = RGB(149,173,190), line = RGB(40,57,73);

/* Five-by-seven bitmap font, A–Z followed by 0–9. */
static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},  {31,4,4,4,4,4,4},     {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},    {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},    {2,6,10,18,31,2,2},   {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static void canvas(int w, int h, uint16_t color)
{
    canvas_w = w;
    for (int i = 0; i < w * h; ++i) pixels[i] = color;
}

static void box(int x, int y, int w, int h, uint16_t color)
{
    for (int j = 0; j < h; ++j) {
        uint16_t *row = pixels + (y + j) * canvas_w + x;
        for (int i = 0; i < w; ++i) row[i] = color;
    }
}

static void text_at(int x, int y, const char *word, int scale, uint16_t color)
{
    for (; *word; ++word, x += 6 * scale) {
        char ch = *word;
        if (ch == ' ') continue;
        int index = ch >= 'A' ? ch - 'A' : ch - '0' + 26;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (font[index][row] & (16u >> col))
                    box(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static void ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    int x = rx, xx = rx * rx, yy = ry * ry;
    for (int y = 0; y <= ry; ++y) {
        while (x * x * yy + y * y * xx > xx * yy) --x;
        box(cx - x, cy + y, 2 * x + 1, 1, color);
        if (y != 0) box(cx - x, cy - y, 2 * x + 1, 1, color);
    }
}

static void draw_pad(int d, int hot)
{
    static const uint8_t place[DRUMS] = {7, 6, 8, 5, 4, 0, 3, 2, 1};
    const Drum *k = &kit[d];
    uint16_t edge = hot ? k->color : line;
    uint16_t face = hot ? k->color : RGB(51,76,93);
    uint16_t shadow = hot ? RGB(74,76,91) : RGB(28,46,61);
    uint16_t paper = hot ? ink : dim;
    canvas(PAD_W, PAD_H, hot ? RGB(30,46,60) : RGB(17,30,43));
    box(1, 0, PAD_W - 2, 1, edge);
    box(0, 1, 1, PAD_H - 2, edge);
    box(PAD_W - 1, 1, 1, PAD_H - 2, edge);
    box(1, PAD_H - 1, PAD_W - 2, 1, edge);
    box(5, 5, 3, 3, k->color);

    if (d >= CLOSED_HAT) {
        int rx = d == CRASH ? 24 : d == RIDE ? 22 : 17;
        box(34, 15, 2, 14, dim);
        box(28, 28, 14, 1, dim);
        if (d == OPEN_HAT || d == CLOSED_HAT)
            ellipse(35, d == OPEN_HAT ? 22 : 19, rx, 4, k->color);
        ellipse(35, 15, rx, 6, face);
        ellipse(35, 14, rx - 5, 3, hot ? ink : k->color);
        ellipse(35, 14, 3, 2, shadow);
    } else if (d == CLAP) {
        for (int i = 0; i < 3; ++i) {
            int x = 25 + i * 10, h = i == 1 ? 19 : 11;
            box(x, 19 - h / 2, 3, h, face);
            box(x, 18, 3, 2, k->color);
        }
    } else {
        int rx = d == KICK ? 21 : d == HIGH_TOM ? 13 : 17;
        int ry = d == KICK ? 12 : d == SNARE ? 8 : 9;
        ellipse(35, 21, rx, ry, shadow);
        ellipse(35, 17, rx, ry, k->color);
        ellipse(35, 17, rx - 2, ry - 2, face);
        ellipse(35, 16, 5, 2, hot ? ink : RGB(82,110,126));
        box(34 - rx, 21, 3, 4, dim);
        box(34 + rx, 21, 3, 4, dim);
    }

    int length = 0;
    while (k->label[length]) ++length;
    text_at((PAD_W - 6 * length + 1) / 2, 35, k->label, 1, paper);
    fm1_lcd_blit(9u + (place[d] % 3u) * 76u, 56u + (place[d] / 3u) * 52u,
                 PAD_W, PAD_H, pixels, PAD_W * PAD_H);
}

static void draw_status(unsigned state, unsigned bpm)
{
    unsigned number = (state >> 4) + 1u;
    unsigned beat = (state >> 2) & 3u;
    int solo = (state & SOLO_LIGHT) != 0;
    const char *section = number <= 4 ? "GROOVE" : number <= 8 ? "TOM RUN" : "FINALE";
    char bar_text[] = {'B','A','R',' ', (char)('0' + number / 10u), (char)('0' + number % 10u), 0};
    char tempo[] = {(char)('0' + bpm / 100u), (char)('0' + (bpm / 10u) % 10u),
                    (char)('0' + bpm % 10u),
                    ' ','B','P','M',0};
    if (bpm < 100) tempo[0] = ' ';
    canvas(STRIP_W, STRIP_H, bg);
    text_at(0, 1, solo ? section : "HAND PLAY", 1, ink);
    if (solo) text_at(96, 1, bar_text, 1, dim);
    text_at(180, 1, tempo, 1, ink);
    for (unsigned i = 0; i < 4; ++i)
        box((int)i * 56, 13, 54, 3, solo && i == beat ? kit[CLOSED_HAT].color : line);
    fm1_lcd_blit(9, 31, STRIP_W, STRIP_H, pixels, STRIP_W * STRIP_H);
}

static uint8_t led_cache[FM1_NLED];

static void update_leds(uint32_t notes, uint32_t state)
{
    for (unsigned id = 0; id < FM1_NLED; ++id) {
        int on = 0;
        if (id == FM1_BTN_PLAY)
            on = !(state & SOLO_LIGHT) || (state & BEAT_LIGHT);
        else if (id >= FM1_KEY_NOTE0 && id < FM1_KEY_NOTE0 + FM1_NOTE_KEYS)
            on = (notes & BIT(id - FM1_KEY_NOTE0)) != 0;
        if (led_cache[id] != on) {
            fm1_led_key(id, on);
            led_cache[id] = (uint8_t)on;
        }
    }
}

void setup(void)
{
    for (int i = 0; i < SINE_SIZE; ++i)
        sine_table[i] = sinf((float)i * (6.28318530718f / SINE_SIZE));
    for (int i = 0; i < 256; ++i) {
        float x = (float)(i - 128) * (1.f / 127.f);
        decode[i] = x < 0.f ? -x * x : x * x;
    }

    int offset = 0;
    for (int d = 0; d < DRUMS; ++d) {
        sample_data[d] = pcm + offset;
        make_sample(d, pcm + offset);
        offset += kit[d].length;
        pan_l[d] = kit[d].level * sqrtf(.5f * (1.f - kit[d].pan));
        pan_r[d] = kit[d].level * sqrtf(.5f * (1.f + kit[d].pan));
    }
    master = fm1_master();

    fm1_lcd_fill_rect(0, 0, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, bg);
    canvas(STRIP_W, STRIP_H, bg);
    text_at(0, 0, "DRUM KIT", 2, ink);
    fm1_lcd_blit(9, 9, STRIP_W, STRIP_H, pixels, STRIP_W * STRIP_H);
    for (int d = 0; d < DRUMS; ++d) draw_pad(d, 0);
    canvas(STRIP_W, STRIP_H, bg);
    box(0, 0, STRIP_W, 1, line);
    text_at(0, 9, "PLAY", 1, kit[CLOSED_HAT].color);
    text_at(30, 9, "SOLO", 1, dim);
    text_at(162, 9, "SELECT", 1, kit[CLOSED_HAT].color);
    text_at(204, 9, "BPM", 1, dim);
    fm1_lcd_blit(9, 215, STRIP_W, STRIP_H, pixels, STRIP_W * STRIP_H);
    for (unsigned id = 0; id < FM1_NLED; ++id) led_cache[id] = 255u;
    update_leds(0, 0);
}

void loop(void)
{
    static unsigned old_status = ~0u, old_bpm = 0, drawn_pads = 0;
    static int next_pad = 0;

    if (fm1_input_edges(0) & BIT(FM1_BTN_PLAY)) want_play ^= 1u;
    uint32_t pressed = fm1_input_note_edges();
    for (unsigned k = 0; k < FM1_NOTE_KEYS; ++k)
        if (pressed & BIT(k)) ++key_ticket[k];

    int bpm = (int)want_bpm;
    int32_t delta = fm1_enc_take(FM1_ENC_SELECT);
    if (delta < 72 - bpm) bpm = 72;
    else if (delta > 172 - bpm) bpm = 172;
    else bpm += (int)delta;
    want_bpm = (uint32_t)bpm;
    master = fm1_master();

    uint32_t state = transport;
    update_leds(lit_notes, state);
    unsigned status = state & ~((unsigned)BEAT_LIGHT);
    if (status != old_status || (unsigned)bpm != old_bpm) {
        draw_status(status, (unsigned)bpm);
        old_status = status;
        old_bpm = (unsigned)bpm;
    }

    /* Cap the amount of LCD traffic per pass and rotate through changed pads. */
    uint32_t pads = lit_pads;
    uint32_t changed = pads ^ drawn_pads;
    for (int n = 0, drawn = 0; changed && n < DRUMS && drawn < 2; ++n) {
        int d = next_pad;
        if (++next_pad == DRUMS) next_pad = 0;
        if (changed & BIT(d)) {
            draw_pad(d, (pads & BIT(d)) != 0);
            changed ^= BIT(d);
            drawn_pads ^= BIT(d);
            ++drawn;
        }
    }
}
