// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The S3 board's parts, started in order by board_init(). One board header for now
// (board_ws146.h); more S3 boards add theirs and pick one at build time.
#include <stdint.h>
#include <stddef.h>
#include "board_ws146.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2s_std.h"

struct Board {
    esp_lcd_panel_handle_t lcd = nullptr;
    i2s_chan_handle_t      i2s = nullptr;
};

// Latches battery power, starts I2C and the expander, resets and starts the LCD
// (backlight off), starts I2S at `rate` Hz, 16-bit stereo in 32-bit slots.
void board_init(Board& b, uint32_t rate);
void board_audio_rate(Board& b, uint32_t rate);   // change the I2S rate (a track's own)

// Vsync: the panel's tearing-effect line (TE, GPIO18) pulses once per refresh. Starting a
// frame's send right after it keeps the write ahead of the panel's scan, so a frame never
// shows half old, half new. Waits for the next pulse; false if none came within `ms`.
bool  board_vsync(int ms);
float board_vsync_hz();                          // measured at start-up (0: no TE)
void board_backlight(int percent);         // 0..100
// Draws rows [y0, y1) from big-endian RGB565 pixels, full width. Waits until the panel has them.
void board_lcd_rows(Board& b, int y0, int y1, const uint16_t* px);
// The same without waiting: px must stay untouched until board_lcd_wait() returns.
void board_lcd_send(Board& b, int y0, int y1, const uint16_t* px);
void board_lcd_wait();

// Touch: the SPD2010's touch side, read by its own task when the panel signals (INT) or every
// 20 ms. board_touch() returns the latest: down, and where (screen pixels), plus a count of
// reports so a caller can tell a new reading from the same one.
struct Touch { bool down = false; uint16_t x = 0, y = 0; uint32_t seq = 0; };
// fn (optional) is called from the touch task on every report: that task can be the
// deck's one control task.
void  board_touch_start(void (*fn)(const Touch&) = nullptr);
Touch board_touch();
