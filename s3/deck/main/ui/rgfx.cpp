// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rgfx.h"

#include <math.h>
#include <string.h>

namespace rgfx {

// floorf/ceilf are library calls on the S3 (~60 cycles); these are a few
static inline int ifloor(float x) { const int i = (int)x; return i - (x < i); }
static inline int iceil(float x) { const int i = (int)x; return i + (x > i); }

static inline uint16_t sw(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }

uint16_t mix(uint16_t a, uint16_t b, int al)
{
    if (al >= 255) return b;
    if (al <= 0) return a;
    const int ar = a >> 11, ag = a >> 5 & 63, ab = a & 31;
    const int br = b >> 11, bg = b >> 5 & 63, bb = b & 31;
    const int r = ar + ((br - ar) * al + 128) / 256;
    const int g = ag + ((bg - ag) * al + 128) / 256;
    const int bl = ab + ((bb - ab) * al + 128) / 256;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

uint16_t Band::get(int x, int y) const
{
    const uint16_t v = px_[(y - y0_) * w_ + x];
    return swap_ ? sw(v) : v;
}

void Band::put(int x, int y, uint16_t c) { px_[(y - y0_) * w_ + x] = swap_ ? sw(c) : c; }

void Band::clear(uint16_t c)
{
    const uint16_t v = swap_ ? sw(c) : c;
    const int n = (y1() - y0_) * w_;
    for (int i = 0; i < n; i++) px_[i] = v;
}

void Band::fill(int x, int y, int w, int h, uint16_t c)
{
    int ya = y < y0_ ? y0_ : y, yb = y + h > y1() ? y1() : y + h;
    for (int yy = ya; yy < yb; yy++) hspan(x, x + w, yy, c);
}

void Band::hspan(int x0, int x1, int y, uint16_t c)
{
    if (y < y0_ || y >= y1()) return;
    if (x0 < 0) x0 = 0;
    if (x1 > w_) x1 = w_;
    const uint16_t v = swap_ ? sw(c) : c;
    uint16_t* p = px_ + (y - y0_) * w_;
    for (int x = x0; x < x1; x++) p[x] = v;
}

void Band::blend(int x, int y, uint16_t c, int alpha)
{
    if (x < 0 || x >= w_ || y < y0_ || y >= y1() || alpha <= 0) return;
    put(x, y, alpha >= 255 ? c : mix(get(x, y), c, alpha));
}

// Coverage of a pixel whose centre is d px inside an edge (negative: outside), 0..255.
static inline int cover(float d)
{
    if (d >= 0.5f) return 255;
    if (d <= -0.5f) return 0;
    return (int)((d + 0.5f) * 255.0f);
}

void Band::ring(float cx, float cy, float r0, float r1, uint16_t c) { arc(cx, cy, r0, r1, 0, 7, c); }

void Band::disc(float cx, float cy, float r, uint16_t c)
{
    const int ya = ifloor(cy - r - 1), yb = iceil(cy + r + 1);
    for (int y = ya < y0_ ? y0_ : ya; y < (yb > y1() ? y1() : yb); y++) {
        const float dy = y + 0.5f - cy;
        const float h2 = (r + 0.5f) * (r + 0.5f) - dy * dy;
        if (h2 <= 0) continue;
        const float ho = sqrtf(h2);
        const float hs2 = (r - 0.5f) * (r - 0.5f) - dy * dy;
        const float hs = hs2 > 0 ? sqrtf(hs2) : 0;
        const int s0 = iceil(cx - hs), s1 = ifloor(cx + hs);
        if (s1 > s0) hspan(s0, s1, y, c);
        for (int x = ifloor(cx - ho); x < iceil(cx + ho); x++) {
            if (x >= s0 && x < s1) { x = s1 - 1; continue; }
            const float dx = x + 0.5f - cx;
            blend(x, y, c, cover(r - sqrtf(dx * dx + dy * dy)));
        }
    }
}

void Band::arc(float cx, float cy, float r0, float r1, float a0, float a1, uint16_t c)
{
    // Per row, the span splits into: outer edge (AA) | solid | inner edge (AA) | hole |
    // inner edge | solid | outer edge. The square roots happen once per row; solid pixels are
    // a plain fill and only edge pixels work out a distance. (The S3's float ops are slow:
    // ~11 cycles for a dependent multiply-add, 80 for sqrtf.)
    const bool full = a1 - a0 >= 6.2831f, wide = a1 - a0 > 3.14159265f;
    const float d0x = sinf(a0), d0y = -cosf(a0), d1x = sinf(a1), d1y = -cosf(a1);
    const float hole = r0 > 0.5f ? r0 : 0;
    const int ya = (int)(cy - r1 - 1), yb = (int)(cy + r1 + 2);
    const uint16_t v = swap_ ? sw(c) : c;
    for (int y = ya < y0_ ? y0_ : ya; y < (yb > y1() ? y1() : yb); y++) {
        const float dy = y + 0.5f - cy, dy2 = dy * dy;
        auto half = [dy2](float r) { const float h = r * r - dy2; return h > 0 ? sqrtf(h) : 0.0f; };
        const float oa = half(r1 + 0.5f);
        if (oa <= 0) continue;
        const float os = half(r1 - 0.5f), is = hole ? half(hole + 0.5f) : 0, ia = hole ? half(hole - 0.5f) : 0;
        // boundaries, pixel x covers centre x + 0.5; kept in order so no pixel is visited twice
        int e[8] = {
            iceil(cx - oa - 0.5f), iceil(cx - os - 0.5f), ifloor(cx - is - 0.5f) + 1, ifloor(cx - ia - 0.5f) + 1,
            iceil(cx + ia - 0.5f), iceil(cx + is - 0.5f), ifloor(cx + os - 0.5f) + 1, ifloor(cx + oa - 0.5f) + 1,
        };
        if (!hole) { e[2] = e[3] = e[4] = e[5] = iceil(cx - 0.5f); }
        for (int k = 1; k < 8; k++) if (e[k] < e[k - 1]) e[k] = e[k - 1];
        uint16_t* row = px_ + (y - y0_) * w_;
        for (int seg = 0; seg < 7; seg++) {
            if (seg == 3) continue;                                  // the hole
            int xa = e[seg], xb = e[seg + 1];
            xa = xa < 0 ? 0 : xa; xb = xb > w_ ? w_ : xb;
            if (xa >= xb) continue;
            const bool solid = seg == 1 || seg == 5;
            if (solid && full) { for (int x = xa; x < xb; x++) row[x] = v; continue; }
            for (int x = xa; x < xb; x++) {
                const float dx = x + 0.5f - cx;
                int al = 255;
                if (!solid) {
                    const float r = sqrtf(dx * dx + dy2);
                    const int ao = cover(r1 - r), ai = hole ? cover(r - r0) : 255;
                    al = ao < ai ? ao : ai;
                }
                if (!full) {
                    // signed distances to the end rays: clockwise of a0 and anticlockwise of a1
                    const int c0 = cover(d0x * dy - d0y * dx), c1 = cover(dx * d1y - dy * d1x);
                    const int ca = wide ? (c0 > c1 ? c0 : c1) : (c0 < c1 ? c0 : c1);
                    al = al < ca ? al : ca;
                }
                if (al >= 255) row[x] = v; else if (al > 0) blend(x, y, c, al);
            }
        }
    }
}

void Band::line(float x0, float y0, float x1, float ya, float w, uint16_t c)
{
    const float hw = w / 2, m = hw + 1;
    const float miny = fminf(y0, ya) - m, maxy = fmaxf(y0, ya) + m;
    if (!rows((int)miny, (int)(maxy - miny) + 2)) return;
    const float vx = x1 - x0, vy = ya - y0, l2 = vx * vx + vy * vy;
    const float il2 = l2 > 0 ? 1 / l2 : 0;
    const float ivy = fabsf(vy) > 1e-3f ? 1 / vy : 0;
    const int yl = ifloor(miny) < y0_ ? y0_ : ifloor(miny);
    const int yh = iceil(maxy) > y1() ? y1() : iceil(maxy);
    for (int y = yl; y < yh; y++) {
        const float py = y + 0.5f - y0;
        // the part of the segment within m rows of this one, widened by m: the only x to visit
        float ta = 0, tb = 1;
        if (ivy != 0) {
            ta = (py - m) * ivy; tb = (py + m) * ivy;
            if (ta > tb) { const float t = ta; ta = tb; tb = t; }
            ta = ta < 0 ? 0 : ta; tb = tb > 1 ? 1 : tb;
            if (ta > tb) continue;
        }
        const float xa = x0 + fminf(ta * vx, tb * vx) - m, xb = x0 + fmaxf(ta * vx, tb * vx) + m;
        for (int x = ifloor(xa); x <= iceil(xb); x++) {
            const float px = x + 0.5f - x0;
            float t = (px * vx + py * vy) * il2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            const float ex = px - t * vx, ey = py - t * vy, e2 = ex * ex + ey * ey;
            if (e2 >= m * m) continue;
            blend(x, y, c, e2 <= (hw - 0.5f) * (hw - 0.5f) && hw > 0.5f ? 255 : cover(hw - sqrtf(e2)));
        }
    }
}

int Band::text(int x, int y, const char* s, const Font& f, uint16_t c)
{
    const bool vis = rows(y - f.line, f.line * 2);
    for (; *s; s++) {
        const int ch = *s < 32 || *s > 126 ? '?' : *s;
        const Glyph& g = f.g[ch - 32];
        if (vis && g.w) {
            const int gx = x + g.dx, gy = y + g.dy, stride = (g.w + 1) / 2;
            const int ya = gy < y0_ ? y0_ : gy, yb = gy + g.h > y1() ? y1() : gy + g.h;
            for (int yy = ya; yy < yb; yy++) {
                const uint8_t* row = f.bits + g.off + (yy - gy) * stride;
                for (int i = 0; i < g.w; i++) {
                    const int v = i & 1 ? row[i >> 1] & 15 : row[i >> 1] >> 4;
                    if (v) blend(gx + i, yy, c, v * 17);
                }
            }
        }
        x += g.adv;
    }
    return x;
}

int Band::width(const char* s, const Font& f)
{
    int n = 0;
    for (; *s; s++) n += f.g[(*s < 32 || *s > 126 ? '?' : *s) - 32].adv;
    return n;
}

const Font& nearest(const Font* const* all, int n, float px)
{
    const Font* best = all[0];
    for (int i = 1; i < n; i++)
        if (fabsf(all[i]->size - px) < fabsf(best->size - px)) best = all[i];
    return *best;
}

} // namespace rgfx
