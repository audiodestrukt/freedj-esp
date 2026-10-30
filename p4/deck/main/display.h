#pragma once
// MSP4021 stand-in screen: ST7796S 480x320 (landscape) over SPI, FT6336U touch.
#include <stdint.h>

namespace display {
constexpr int W = 480, H = 320;
bool begin();                                         // panel + backlight + touch
// Blit a w*h block of RGB565 pixels (host byte order) at x,y.
void blit(int x, int y, int w, int h, const uint16_t* px);
void fill(int x, int y, int w, int h, uint16_t rgb565);
// Latest touch point; false if not touched (or no touch chip).
bool touch(int* x, int* y);
} // namespace display
