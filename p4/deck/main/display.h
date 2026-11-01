// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// MSP4021 stand-in screen: ST7796S 480x320 (landscape) over SPI, FT6336U touch,
// driven by LVGL through esp_lvgl_port (its task runs on core 0).
#include "lvgl.h"

namespace display {
constexpr int W = 480, H = 320;
// Panel + LVGL, first thing at boot, so a splash can show while the rest
// starts. The backlight stays off until backlight(true). Returns the LVGL
// display, or nullptr on failure.
lv_display_t* begin();
// Touch: the FT6336U shares the codec's I2C bus, so call this once the audio
// side has created the bus. Optional: without it the screen still works.
void addTouch();
void backlight(bool on);
} // namespace display
