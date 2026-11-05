// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The deck screen, picked at build time (menuconfig "freedj deck" > Screen):
//   4"   MSP4021: ST7796S 480x320 (landscape) over SPI, FT6336U touch
//   2.8" MSP2834: ILI9341V 320x240 (landscape) over SPI, FT6336G touch
// Same 14-pin header and wiring. Driven by LVGL through esp_lvgl_port (its
// task runs on core 0).
#include "sdkconfig.h"
#include "lvgl.h"

namespace display {
#if CONFIG_FREEDJ_SCREEN_28
constexpr int W = 320, H = 240;       // what the UI draws on
#else
constexpr int W = 480, H = 320;
#endif
// The panel itself; bigger than W x H only in the 2.8" preview on the 4".
#if CONFIG_FREEDJ_SCREEN_28 && !CONFIG_FREEDJ_SCREEN_PREVIEW
constexpr int PANEL_W = 320, PANEL_H = 240;
#else
constexpr int PANEL_W = 480, PANEL_H = 320;
#endif
// Panel + LVGL, first thing at boot, so a splash can show while the rest
// starts. The backlight stays off until backlight(true). Returns the LVGL
// display, or nullptr on failure.
lv_display_t* begin();
// Touch: the FT6336U shares the codec's I2C bus, so call this once the audio
// side has created the bus. Optional: without it the screen still works.
void addTouch();
void backlight(bool on);
} // namespace display
