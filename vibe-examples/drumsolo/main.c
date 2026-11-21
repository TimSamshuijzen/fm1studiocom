#include "fm1.h"

enum { KICK, SNARE, HIGH, FLOOR, CLOSED, OPEN, RIDE, CRASH, DRUMS };

/* Build one-shots at half rate. The interrupt only interpolates and mixes. */
static const int length[DRUMS] = {9000, 7500, 10000, 13500, 2200, 8500, 18000, 26000};
static int16_t bank[94700];
static int start[DRUMS];
static uint32_t seed = 173u;

static float noise(void) {
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 16) / 32768.0f - 1.0f;
}

static float wave(float *phase, float hz) {
    *phase += hz / 22050.0f;
    if (*phase >= 1.0f) *phase -= 1.0f;
    float x = *phase * 2.0f - 1.0f;
    float a = x < 0 ? -x : x;
    return 4.0f * x * (1.0f - a);
}

static void build_kit(void) {
    const float pitch[] = {49, 185, 155, 92};
    const float decay[] = {0.99942f, 0.99913f, 0.99955f, 0.99967f,
                           0.9970f, 0.99945f, 0.99974f, 0.99984f};
    const float metal[] = {617, 913, 1447, 2081, 2939, 3881};
    int offset = 0;
    for (int d = 0; d < DRUMS; ++d) {
        float p[7] = {0}, env = 1, bend = 1, snap = 1, prev = 0, wash = 0;
        start[d] = offset;
        for (int i = 0; i < length[d]; ++i) {
            float n = noise(), hiss = (n - prev) * 0.5f;
            float s;
            prev = n;
            if (d <= FLOOR) {
                float hz = pitch[d] * (1.0f + (d == KICK ? 2.8f : 0.28f) * bend);
                float head = wave(&p[0], hz);
                float shell = wave(&p[1], hz * 1.59f);
                s = 0.74f * head + 0.21f * shell * (0.3f + 0.7f * bend);
                if (d == SNARE) s = 0.35f * s + 0.76f * hiss + 0.16f * n;
                s += 0.16f * snap * hiss;
            } else {
                float buzz = 0;
                for (int k = 0; k < 6; ++k) {
                    float tone = wave(&p[k], metal[k] * (d == RIDE ? 0.79f : 1.0f));
                    buzz += tone > 0 ? 0.166f : -0.166f;
                }
                wash += 0.18f * (buzz - wash);
                s = 0.48f * (buzz - wash) + 0.62f * hiss;
                if (d == RIDE) s = 0.55f * s + 0.38f * wave(&p[6], 1153) * bend;
            }
            float fade = length[d] - i < 160 ? (length[d] - i) / 160.0f : 1.0f;
            bank[offset++] = (int16_t)(s * env * fade * 24500.0f);
            env *= decay[d];
            bend *= d == RIDE ? 0.99965f : 0.9968f;
            snap *= 0.983f;
        }
    }
}

typedef struct { int drum, pos, speed; float gain; } Voice;
static Voice voices[16];
static const float pan[DRUMS] = {0.50f,0.44f,0.38f,0.67f,0.27f,0.27f,0.74f,0.33f};
static volatile uint32_t taps[DRUMS], lit, flash, pulse;
static volatile int run, bpm = 112;
static volatile float master;
static uint32_t consumed[DRUMS], strikes;
static int glow[DRUMS];

static void hit(int d, float strength) {
    int slot = 0;
    for (int i = 0; i < 16; ++i) {
        Voice *v = &voices[i];
        if (d == CLOSED && v->drum == OPEN) v->gain = 0;
        if (v->gain < voices[slot].gain) slot = i;
    }
    Voice *v = &voices[slot];
    v->drum = d;
    v->pos = 0;
    v->speed = 125 + (int)((++strikes * 13u) % 7u);
    v->gain = strength;
    glow[d] = 2900;
}

/* Each mask is one bar, least significant bit first, one bit per sixteenth. */
static const uint16_t kick_pattern[] = {0x0441,0x0941,0x0521,0x0041};
static const uint16_t ghost_pattern[] = {0x4008,0x0480,0x8048,0x0088};

static void solo(int step) {
    int bar = step / 16, t = step % 16;
    uint32_t mask = 1u << t;
    if (!step) hit(CRASH, 0.84f);
    if (kick_pattern[bar] & mask) hit(KICK, 0.96f);
    if (bar == 3 && t >= 8) {
        const int fill[] = {SNARE,SNARE,HIGH,HIGH,SNARE,FLOOR,HIGH,FLOOR};
        hit(fill[t-8], 0.55f + 0.05f * (t-8));
        if (t == 12) hit(KICK, 0.8f);
        return;
    }
    if (t == 4 || t == 12) hit(SNARE, t == 12 ? 0.96f : 0.86f);
    if (ghost_pattern[bar] & mask) hit(SNARE, 0.28f);
    if (bar == 1 && t >= 13) hit(t == 15 ? FLOOR : HIGH, 0.55f + 0.09f*(t-13));
    if (!(t & 1) && step) {
        int cym = bar == 2 ? RIDE : (t == 14 ? OPEN : CLOSED);
        hit(cym, (t & 2) ? 0.43f : 0.63f);
    }
}

static float limit(float x) {
    if (x > 0.90f) return 0.90f;
    if (x < -0.90f) return -0.90f;
    return x;
}

void audio(int32_t *out, int frames) {
    static int playing, step, beat_left;
    static float left, volume;
    int enabled = run, tempo = bpm;
    float target = master;
    if (enabled != playing) {
        step = 0;
        left = 0;
        beat_left = 0;
        playing = enabled;
    }
    for (int d = 0; d < DRUMS; ++d) {
        uint32_t count = taps[d];
        if (count != consumed[d]) hit(d, 0.91f);
        consumed[d] = count;
    }
    for (int i = 0; i < frames; ++i) {
        if (playing && left <= 0) {
            solo(step);
            if (!(step & 3)) beat_left = 3900;
            left += (FM1_AUDIO_RATE * 15.0f / tempo) * ((step & 1) ? 0.97f : 1.03f);
            step = (step + 1) % 64;
        }
        if (playing) left -= 1.0f;
        if (beat_left) --beat_left;
        float l = 0, r = 0;
        for (int j = 0; j < 16; ++j) {
            Voice *v = &voices[j];
            if (v->gain == 0) continue;
            int d = v->drum, n = v->pos >> 8;
            const int16_t *p = bank + start[d] + n;
            float sample = p[0] + (p[1] - p[0]) * ((v->pos & 255) / 256.0f);
            sample *= v->gain / 32768.0f;
            l += sample * (1.0f - pan[d]);
            r += sample * pan[d];
            v->pos += v->speed;
            if ((v->pos >> 8) >= length[d]-1) v->gain = 0;
        }
        volume += 0.002f * (target - volume);
        out[2*i] = (int32_t)(limit(l) * volume * FM1_AUDIO_MAX);
        out[2*i+1] = (int32_t)(limit(r) * volume * FM1_AUDIO_MAX);
    }
    uint32_t active = 0, bright = 0;
    for (int j = 0; j < 16; ++j)
        if (voices[j].gain) active |= 1u << voices[j].drum;
    for (int d = 0; d < DRUMS; ++d) {
        if (glow[d] > 0) {
            bright |= 1u << d;
            glow[d] -= frames;
        }
    }
    lit = active;
    flash = bright;
    pulse = beat_left != 0;
}

enum { BG=0x10e4, RIM=0x9513, HEAD=0xd6db, SHELL=0x22cd, GOLD=0xe4e8, LIGHT=0xffba };
/* Closed and open hats share a stand. */
static const int x_pos[] = {119,65,115,181,33,33,205,63};
static const int y_pos[] = {150,114,86,125,75,75,69,45};

static void oval(int x, int y, int w, int h, uint16_t color) {
    for (int dy = -h; dy <= h; ++dy) {
        int dx = w;
        while (dx*dx*h*h + dy*dy*w*w > w*w*h*h) --dx;
        fm1_lcd_fill_rect(x-dx,y+dy,2*dx+1,1,color);
    }
}

static void kit_picture(void) {
    fm1_lcd_fill_rect(0,0,240,240,BG);
    for (int d = 0; d < DRUMS; ++d) {
        if (d == OPEN) continue;
        int x = x_pos[d], y = y_pos[d], metal = d >= CLOSED;
        int w = d == KICK ? 32 : (d == CLOSED ? 20 : 24);
        int h = d == KICK ? 27 : (metal ? 7 : 12);
        fm1_lcd_fill_rect(x-1,y,3,h+25,RIM);
        fm1_lcd_fill_rect(x-7,y+h+24,15,2,RIM);
        oval(x,y+(metal ? 3 : 6),w,h,metal ? RIM : SHELL);
        oval(x,y,w,h,metal ? GOLD : RIM);
        oval(x,y,w-3,h-2,metal ? LIGHT : HEAD);
        if (metal) oval(x,y-2,5,2,GOLD);
    }
}

/* Tiny 3-by-5 digits for the tempo display. */
static const uint16_t digits[] = {
    0x7b6f,0x2c97,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf
};
static void tempo_display(int value) {
    uint16_t pixels[22*10];
    for (int y=0; y<10; ++y) {
        for (int x=0; x<22; ++x) {
            int place = x/8, col = (x%8)/2;
            int digit = (value / (place == 0 ? 100 : place == 1 ? 10 : 1)) % 10;
            int on = col < 3 && (place || value >= 100) &&
                     (digits[digit] & (1u << (14-3*(y/2)-col)));
            pixels[y*22+x] = on ? LIGHT : BG;
        }
    }
    fm1_lcd_blit(109,211,22,10,pixels,220);
}

void setup(void) {
    build_kit();
    kit_picture();
    tempo_display(bpm);
    master = fm1_master() / (float)FM1_MASTER_MAX;
    fm1_led_level(FM1_BTN_PLAY,FM1_LED_ON);
}

void loop(void) {
    static uint32_t old_lit, old_flash;
    static int old_play = FM1_LED_ON;
    if (fm1_input_edges(0) & (1u << FM1_BTN_PLAY)) run = !run;
    uint32_t keys = fm1_input_note_edges();
    for (int d=0; d<DRUMS; ++d)
        if (keys & (1u << d)) ++taps[d];
    int delta = fm1_enc_take(FM1_ENC_SELECT);
    if (delta) {
        int value = delta > 190-bpm ? 190 : delta < 55-bpm ? 55 : bpm+delta;
        if (value != bpm) {
            bpm = value;
            tempo_display(value);
        }
    }
    master = fm1_master() / (float)FM1_MASTER_MAX;
    uint32_t keys_on = lit, shown = flash;
    if (shown & (1u << OPEN)) shown |= 1u << CLOSED;
    for (int d=0; d<DRUMS; ++d) {
        uint32_t bit = 1u << d;
        if ((old_lit ^ keys_on) & bit)
            fm1_led_key(FM1_KEY_NOTE0+d,(keys_on & bit) != 0);
        if (d != OPEN && ((old_flash ^ shown) & bit)) {
            uint16_t idle = d >= CLOSED ? GOLD : HEAD;
            fm1_lcd_fill_rect(x_pos[d]-2,y_pos[d]-2,5,3,
                              (shown & bit) ? 0xfa01 : idle);
        }
    }
    old_lit = keys_on;
    old_flash = shown;
    int level = (!run || pulse) ? FM1_LED_ON : FM1_LED_GLOW;
    if (level != old_play) {
        fm1_led_level(FM1_BTN_PLAY,level);
        old_play = level;
    }
}
