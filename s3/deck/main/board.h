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
// (backlight off), starts I2S at `rate` Hz, 16-bit stereo.
void board_init(Board& b, uint32_t rate);
void board_backlight(int percent);         // 0..100
// Draws rows [y0, y1) from big-endian RGB565 pixels, full width. Waits until the panel has them.
void board_lcd_rows(Board& b, int y0, int y1, const uint16_t* px);
// The same without waiting: px must stay untouched until board_lcd_wait() returns.
void board_lcd_send(Board& b, int y0, int y1, const uint16_t* px);
void board_lcd_wait();
