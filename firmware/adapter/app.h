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

/* The store has only the panel calibration, which is the platform's. The
 * numbers and flash places are those of fm1-base's template, so a calibration
 * that another fm1-base firmware saved there stays good. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_tick(void);
