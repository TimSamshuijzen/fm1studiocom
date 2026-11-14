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

/* ---- LEDs ---- */

static uint32_t ad_led_buttons, ad_led_keys;   /* as plat_lights takes them */
static uint8_t ad_led_changed;

void fm1x_led_key(uint32_t id, int on)
{
    uint32_t *leds, bit;
    if (id >= 41u)
        return;
    if (id >= 14u) {
        leds = &ad_led_keys;
        bit = 1u << (id - 14u);
    } else {
        leds = &ad_led_buttons;
        bit = id == 0u ? 1u << B_OCTDN : id == 1u ? 1u << B_OCTUP :
              id == 12u ? LED_PLAY_GREEN : 1u << (id - 2u);   /* PLAY is green, as in the simulator */
    }
    if (((*leds & bit) != 0u) == (on != 0))
        return;
    *leds ^= bit;
    ad_led_changed = 1;
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
        plat_lights(ad_led_buttons, ad_led_keys, 0, 0, 0);
    }
}

void app_frame(void) { ad_pass(); }
void app_input_only(void) { ad_pass(); }
