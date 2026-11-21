/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* app.c - the hardware side of fm1.h, for a real M-VAVE FM-1.
 *
 * In the browser, fm1_runtime.c gives a program its screen, keys and sound.
 * Here the same functions are made from fm1-base's platform (plat.h, lcd.c).
 * This file is a part of fm1-base's unity build (device/unity.c on the FM-1,
 * host/plat_host.c in the host simulator), so lcd_fill, lcd_blit, lcd_sync and
 * swap16 are in scope.
 *
 * The program is compiled apart from this file. felucca-bsp has functions with
 * the names of fm1.h (fm1_lcd_blit, fm1_in, ...) and they are in this unit, so
 * the program's names get the prefix fm1x_ (adapter/fm1_names.h does that for
 * each file of the program).
 *
 * What is the same as in the simulator:
 *   - setup() runs one time, before the first audio() call.
 *   - loop() runs one time for each audio block. When a loop() call takes
 *     longer than a block (a large drawing), the next call comes right after it.
 *   - audio() gets 256 frames, and its samples are signed 24-bit.
 * What is different:
 *   - audio() runs in an interrupt, loop() does not. Draw only from loop().
 *   - The stack of loop() is 24 KB, that of audio() about 7 KB.
 *   - After an update that was cancelled, the FM-1 starts again. */

/* The program. */
void fm1x_setup(void);
void fm1x_audio(int32_t *out, int frames);
void fm1x_loop(void);

/* ---- Keys and buttons ---- */

typedef struct {
    uint32_t notes;    /* bit n is 1 while note key n is down */
    uint32_t buttons;  /* bit i is 1 while button i is down, with the numbers of fm1.h */
} fm1x_in_t;
volatile fm1x_in_t fm1x_in;

static volatile uint32_t ad_pressed, ad_released, ad_notes_pressed;

/* The buttons that are down, with the numbers of fm1.h: OCT- and OCT+ are 0
 * and 1 there, and the last two (12, 13) in plat.h. */
static uint32_t ad_buttons(void)
{
    uint32_t b = plat_buttons();
    return (b & 0xFFFu) << 2 | ((b >> B_OCTDN) & 1u) | ((b >> B_OCTUP) & 1u) << 1;
}

/* Reads the keys and keeps what changed. From the 1 kHz tick, and from the main loop with the tick locked out. */
static void ad_sample(void)
{
    uint32_t buttons = ad_buttons(), notes = plat_keys();
    uint32_t old_buttons = fm1x_in.buttons, old_notes = fm1x_in.notes;
    ad_pressed |= buttons & ~old_buttons;
    ad_released |= ~buttons & old_buttons;
    ad_notes_pressed |= notes & ~old_notes;
    fm1x_in.buttons = buttons;
    fm1x_in.notes = notes;
}

void app_tick(void) { ad_sample(); }

uint32_t fm1x_input_edges(uint32_t *released)
{
    uint32_t lock = plat_lock(), pressed = ad_pressed;
    if (released)
        *released = ad_released;
    ad_pressed = ad_released = 0;
    plat_unlock(lock);
    return pressed;
}

uint32_t fm1x_input_note_edges(void)
{
    uint32_t lock = plat_lock(), pressed = ad_notes_pressed;
    ad_notes_pressed = 0;
    plat_unlock(lock);
    return pressed;
}

/* ---- LEDs ----
 *
 * The level of each LED, with the ids of fm1.h (41: PLAY's red LED). After a loop() that changed one, ad_leds
 * gives them all to the platform: fully lit and the glow with plat_lights, the mid level (over the glow) with
 * plat_lights_mid, and the breath with felucca-bsp's fm1_led_breath, for which fm1-base has no function.
 * fm1_led_dim_low gives plat_lights the low glow (1), which also makes the mid level and the breath darker. */

enum { AD_OFF, AD_GLOW, AD_MID, AD_ON, AD_BREATH };   /* FM1_LED_OFF ... FM1_LED_BREATH */
#define AD_NLED 42u                                     /* FM1_NLED */
#define AD_PLAY_RED 41u                                 /* FM1_LED_PLAY_RED */

static uint8_t ad_level[AD_NLED];
static uint8_t ad_low, ad_led_changed;

void fm1x_led_level(uint32_t id, int level)
{
    if (id >= AD_NLED || level < AD_OFF || level > AD_BREATH || ad_level[id] == (uint8_t)level)
        return;
    ad_level[id] = (uint8_t)level;
    ad_led_changed = 1;
}

void fm1x_led_key(uint32_t id, int on) { fm1x_led_level(id, on ? AD_ON : AD_OFF); }

void fm1x_led_dim_low(int low)
{
    if (ad_low == (low != 0))
        return;
    ad_low = low != 0;
    ad_led_changed = 1;
}

/* The bit of a button's LED in plat_lights' buttons: OCT- and OCT+ are 0 and 1 in fm1.h and B_OCTDN and B_OCTUP
 * in plat.h, the others follow from FX (B_FX); PLAY's green LED is LED_PLAY_GREEN, its red one PLAY's own bit. */
static uint32_t ad_button_bit(uint32_t id)
{
    return id == 0u ? 1u << B_OCTDN : id == 1u ? 1u << B_OCTUP : id == 12u ? LED_PLAY_GREEN :
           id == AD_PLAY_RED ? 1u << B_PLAY : 1u << (id - 2u);
}

#ifndef PLAT_HOST
/* felucca-bsp's breath (boards/fm1/fm1_input.h): fm1_led_breath has the LEDs that breathe, by their place in the
 * matrix, found as plat_lights finds them (plat_fm1.c: led_pos, panel; PLAY's green LED at column 8, row PA5). */
static void ad_breath(uint32_t buttons, uint32_t keys)
{
    uint8_t br[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint8_t q = led_pos[i < NB ? panel.btn[i] : 14u + (i - NB)];
        if (q != 0xFF && (i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u))
            br[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    if (buttons & LED_PLAY_GREEN)
        br[8] |= 1u << 1;
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led_breath[c] = br[c];
}
#endif

static void ad_leds(void)
{
    uint32_t lit[2] = {0}, glow[2] = {0}, mid[2] = {0}, breath[2] = {0};   /* [0]: buttons, [1]: keys */
    uint32_t id;
    for (id = 0; id < AD_NLED; id++) {
        uint32_t key = id >= 14u && id < 41u, bit = key ? 1u << (id - 14u) : ad_button_bit(id);
        switch (ad_level[id]) {
        case AD_ON: lit[key] |= bit; break;
        case AD_MID: mid[key] |= bit; glow[key] |= bit; break;
        case AD_GLOW: glow[key] |= bit; break;
        case AD_BREATH: breath[key] |= bit; break;
        }
    }
#ifdef PLAT_HOST
    glow[0] |= breath[0];                               /* the host simulator has no breath: the glow */
    glow[1] |= breath[1];
#endif
    plat_lights(lit[0], lit[1], glow[0], glow[1], ad_low ? 1 : 2);
    plat_lights_mid(mid[0], mid[1]);
#ifndef PLAT_HOST
    if ((mid[0] & LED_PLAY_GREEN) && !(fm1_led[8] & (1u << 1)))   /* plat_lights_mid has no PLAY's green */
        fm1_led_mid[8] |= 1u << 1;
    ad_breath(breath[0], breath[1]);
#endif
}

/* ---- Encoders, MASTER, MIDI ---- */

int32_t fm1x_enc_take(uint32_t e)
{
    /* fm1.h: SELECT, ALGORITHM, KNOB1 to KNOB4, PRESETS */
    static const uint8_t ROLE[7] = {EN_SELECT, EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4, EN_PRESET};
    return e < 7u ? plat_enc(ROLE[e]) : 0;
}

uint32_t fm1x_master(void)
{
    uint32_t position = plat_master() * 1023u / 4096u;   /* plat.h: 0 to 4096; fm1.h: 0 to 1023 */
    return position > 1023u ? 1023u : position;
}

int fm1x_midi_take(uint32_t *packet)
{
    int port;
    return plat_midi_in(packet, &port);   /* the same 32-bit USB-MIDI packet as in fm1.h */
}

/* ---- Screen ---- */

/* The panel takes each pixel high byte first, and it reads the pixels while
 * the program runs on. So a picture goes to the panel in strips: each strip is
 * copied here with its bytes swapped, and stays as it is until it is sent. */
#define AD_STRIP_PIXELS 2400u
static uint16_t ad_strip[AD_STRIP_PIXELS];

static int ad_on_screen(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    return x < 240u && y < 240u && w <= 240u - x && h <= 240u - y;
}

int fm1x_lcd_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb)
{
    if (!w || !h)
        return 0;
    if (!ad_on_screen(x, y, w, h))
        return -2;
    lcd_fill(x, y, w, h, rgb);
    return 0;
}

int fm1x_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *pixels, uint32_t pixel_count)
{
    uint32_t rows_in_strip, row, i;
    if (!w || !h)
        return 0;
    if (!ad_on_screen(x, y, w, h) || !pixels || pixel_count < w * h)
        return -2;
    rows_in_strip = AD_STRIP_PIXELS / w;
    for (row = 0; row < h; row += rows_in_strip) {
        uint32_t rows = h - row < rows_in_strip ? h - row : rows_in_strip;
        const uint16_t *from = pixels + row * w;
        lcd_sync();                       /* the strip before this one is sent now */
        for (i = 0; i < rows * w; i++)
            ad_strip[i] = swap16(from[i]);
        lcd_blit(x, y + row, w, rows, ad_strip);
    }
    return 0;
}

/* ---- Program ---- */

static volatile uint8_t ad_running;     /* setup() is done: audio() and loop() run */
static volatile uint8_t ad_silent;      /* an update is near: no sound */
static volatile uint32_t ad_blocks;     /* count of audio blocks */

void app_init(void) {}                  /* setup() waits for the screen: app_ui_init */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    (void)master_q12;                   /* the program reads MASTER itself: fm1_master() */
    if (ad_running && !ad_silent) {
        fm1x_audio(lr, (int)n);
    } else {
        uint32_t i;
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
    }
    ad_blocks++;
}

void app_panic(void) { ad_silent = 1; }

void app_say(const char *a, const char *b)
{
    (void)a;
    (void)b;
}

/* The first screen. The platform calls this again after an update that was
 * cancelled: its screen then covers the program's. A program that draws only
 * what changes cannot repair that, so the FM-1 starts again. */
void app_ui_init(void)
{
    static uint8_t started;
    uint32_t lock;
    if (started) {
        plat_restart();
        return;
    }
    started = 1;
    lcd_fill(0, 0, 240, 240, 0);
    lock = plat_lock();
    ad_sample();
    ad_pressed = ad_released = ad_notes_pressed = 0;   /* a key held at power-on is not a press */
    plat_unlock(lock);
    fm1x_setup();
    ad_running = 1;
}

/* One pass of the platform's main loop. */
static void ad_pass(void)
{
    static uint32_t seen_blocks;
    uint32_t lock, blocks = ad_blocks;
    if (!ad_running || blocks == seen_blocks)
        return;
    seen_blocks = blocks;
    lock = plat_lock();
    ad_sample();
    plat_unlock(lock);
    fm1x_loop();
    if (ad_led_changed) {
        ad_led_changed = 0;
        ad_leds();
    }
}

void app_frame(void) { ad_pass(); }
void app_input_only(void) { ad_pass(); }
