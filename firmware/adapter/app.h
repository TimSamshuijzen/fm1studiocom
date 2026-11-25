/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* app.h - the adapter that runs an fm1.h program on a real M-VAVE FM-1.
 *
 * The program (setup, audio, loop) is an "app" of fm1-base, the open platform
 * for FM-1 firmware (github.com/charlesvestal/fm1-base, on felucca-bsp). This
 * header is the app's half of the contract with that platform; app.c is the
 * other side of fm1.h: what fm1_runtime.c is in the browser. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "fm1_app_name.h"                /* APP_NAME: made by firmware/make_app.py from the name of the project */

#define APP_BLOCK 256u                   /* frames in each audio() call, as FM1_AUDIO_BLOCK in fm1.h */
#define APP_TICK                         /* 1 kHz: the keys are read, so a short press is not lost while loop() draws */
#define APP_RESTART                      /* plat_restart: a clean start after an update that was cancelled */
#define APP_CANVAS_PX (240u * 48u)       /* the platform's own screens draw in strips: the program gets the RAM */
#define APP_SPLASH                       /* app_splash: no start screen; black until setup() draws */

/* The store: the panel calibration, which is the platform's, and the slots of
 * fm1.h (fm1_store_load, fm1_store_save), each with two 4 KiB sectors in the
 * data area, from 0x97000 to 0xB6FFF. OBJ_SETTINGS (not used) and OBJ_PANEL
 * have the numbers and flash places of fm1-base's template, so a calibration
 * that another fm1-base firmware saved there stays good. */
#define AD_STORE_SLOTS 16u               /* FM1_STORE_SLOTS */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_STORE0, OBJ_NOBJ = OBJ_STORE0 + AD_STORE_SLOTS };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, \
    0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, 0xA5000u, \
    0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, 0xB3000u, 0xB5000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_tick(void);
void app_splash(void);
