// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Drawing for the round screens, one band of rows at a time: the device draws a band into a
// DMA buffer and sends it, the host draws the whole screen as one band and writes a PNG.
// Every primitive clips to the band first, so work outside it costs almost nothing.
// Colours are RGB565; edges of circles, arcs, lines and text are anti-aliased.
// No ESP-IDF in here: the same code builds on the PC.
#include <stdint.h>

namespace rgfx {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3); }
uint16_t mix(uint16_t a, uint16_t b, int alpha);          // alpha 0..255 of b over a

struct Glyph { uint32_t off; uint8_t w, h; int8_t dx, dy; uint8_t adv; };   // dy: top, from the baseline
struct Font {
    const uint8_t* bits;          // 4 bits a pixel, rows padded to whole bytes
    const Glyph*   g;             // ' '..'~'
    uint8_t size;                 // px per em
    uint8_t cap;                  // height of 'H'
    uint8_t descent;
    uint8_t line;
};

class Band {
public:
    // px holds rows [y0, y0 + rows) of a w x h screen. swap: store big-endian (SPI panels).
    Band(uint16_t* px, int w, int h, int rows, bool swap) : px_(px), w_(w), h_(h), rows_(rows), swap_(swap) {}
    void at(int y0) { y0_ = y0; }
    int  y0() const { return y0_; }
    int  y1() const { return y0_ + rows_ < h_ ? y0_ + rows_ : h_; }
    int  w() const { return w_; }
    int  h() const { return h_; }
    bool rows(int y, int n) const { return y < y1() && y + n > y0_; }

    void clear(uint16_t c);
    void fill(int x, int y, int w, int h, uint16_t c);
    void blend(int x, int y, uint16_t c, int alpha);        // one pixel, clipped
    void hspan(int x0, int x1, int y, uint16_t c);           // [x0, x1), clipped

    // Circles: everything inside radius r (ring: between r0 and r1), anti-aliased.
    void disc(float cx, float cy, float r, uint16_t c);
    void ring(float cx, float cy, float r0, float r1, uint16_t c);
    // A ring between angles a0..a1 (radians, 0 = 12 o'clock, clockwise), a1 - a0 <= 2 pi.
    void arc(float cx, float cy, float r0, float r1, float a0, float a1, uint16_t c);
    // A line w px wide with round ends.
    void line(float x0, float y0, float x1, float y1, float w, uint16_t c);

    // Text: x is the left of the first glyph, y the baseline. Returns the x after it.
    int text(int x, int y, const char* s, const Font& f, uint16_t c);
    static int width(const char* s, const Font& f);

private:
    uint16_t get(int x, int y) const;
    void put(int x, int y, uint16_t c);
    uint16_t* px_;
    int w_, h_, rows_, y0_ = 0;
    bool swap_;
};

// The font of `all` (smallest first, n of them) nearest to `px` per em.
const Font& nearest(const Font* const* all, int n, float px);

} // namespace rgfx
