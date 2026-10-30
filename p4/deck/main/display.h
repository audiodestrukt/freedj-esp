#pragma once
// MSP4021 stand-in screen: ST7796S 480x320 (landscape) over SPI, FT6336U touch,
// driven by LVGL through esp_lvgl_port (its task runs on core 0).
#include "lvgl.h"

namespace display {
constexpr int W = 480, H = 320;
// Panel + touch + LVGL. Returns the LVGL display, or nullptr on failure.
// Touch is optional: if the FT6336U doesn't answer, the screen still works.
lv_display_t* begin();
} // namespace display
