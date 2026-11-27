/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* fm1_names.h - put in front of each file of the program (fm1.toml o2_include).
 *
 * felucca-bsp has its own functions with the names of fm1.h, so on the FM-1
 * the program's names get the prefix fm1x_. adapter/app.c has those functions. */
#pragma once

#define setup fm1x_setup
#define audio fm1x_audio
#define loop fm1x_loop

#define fm1_in fm1x_in
#define fm1_lcd_fill_rect fm1x_lcd_fill_rect
#define fm1_lcd_blit fm1x_lcd_blit
#define fm1_input_edges fm1x_input_edges
#define fm1_input_note_edges fm1x_input_note_edges
#define fm1_led_key fm1x_led_key
#define fm1_led_level fm1x_led_level
#define fm1_led_dim_low fm1x_led_dim_low
#define fm1_enc_take fm1x_enc_take
#define fm1_master fm1x_master
#define fm1_battery fm1x_battery
#define fm1_midi_take fm1x_midi_take
#define fm1_midi_send fm1x_midi_send
#define fm1_store_load fm1x_store_load
#define fm1_store_save fm1x_store_save
