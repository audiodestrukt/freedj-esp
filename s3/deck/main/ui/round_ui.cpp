// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "round_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fonts.h"

namespace round_ui {

using rgfx::rgb;

static const uint16_t kBg       = rgb(8, 9, 12);
static const uint16_t kRing     = rgb(22, 25, 31);
static const uint16_t kRingTch  = rgb(18, 44, 70);
static const uint16_t kTick     = rgb(70, 76, 88);
static const uint16_t kTickTch  = rgb(120, 180, 240);
static const uint16_t kMarker   = rgb(240, 240, 240);
static const uint16_t kTrack    = rgb(34, 38, 46);
static const uint16_t kText     = rgb(235, 235, 235);
static const uint16_t kDim      = rgb(130, 136, 148);
static const uint16_t kRed      = rgb(235, 50, 40);
static const uint16_t kOrange   = rgb(255, 150, 20);
static const uint16_t kGreen    = rgb(40, 200, 90);
static const uint16_t kBlue     = rgb(70, 150, 255);

static const float kPi = 3.14159265f;

Layout layout(int d)
{
    Layout l{};
    l.d = (float)d;
    l.cx = l.cy = d / 2.0f;
    const float R = d / 2.0f;
    l.ringOut = R;
    l.ringIn  = R - 0.085f * d;
    l.progOut = l.ringIn - 0.006f * d;
    l.progIn  = l.progOut - 0.011f * d;
    l.inner   = l.progIn - 0.012f * d;
    l.titleY  = 0.232f * d;
    l.timeY   = 0.338f * d;
    l.infoY   = 0.394f * d;
    l.waveY   = 0.512f * d;
    l.waveH   = 0.094f * d;
    l.ovTop   = 0.627f * d;
    l.ovH     = 0.034f * d;
    l.btnTop  = 0.692f * d;
    l.secAcross = 8.0f;
    using namespace fonts;
    const int nS = sizeof kSansAll / sizeof kSansAll[0], nB = sizeof kBoldAll / sizeof kBoldAll[0];
    l.title = &rgfx::nearest(kSansAll, nS, fmaxf(11, 0.045f * d));
    l.time  = &rgfx::nearest(kBoldAll, nB, fmaxf(20, 0.105f * d));
    l.info  = &rgfx::nearest(kBoldAll, nB, fmaxf(12, 0.046f * d));
    l.small = &rgfx::nearest(kSansAll, nS, fmaxf(10, 0.032f * d));
    l.btn   = &rgfx::nearest(kBoldAll, nB, fmaxf(12, 0.050f * d));
    return l;
}

static float halfChord(const Layout& l, float y)          // half the width of the content circle at row y
{
    const float dy = y - l.cy;
    const float h2 = l.inner * l.inner - dy * dy;
    return h2 > 0 ? sqrtf(h2) : 0;
}

Zone hit(const Layout& l, float x, float y)
{
    const float r = hypotf(x - l.cx, y - l.cy);
    if (r > l.ringOut + 4) return Zone::None;
    if (r >= l.ringIn - 0.01f * l.d) return Zone::Jog;     // a little forgiving on the inside
    if (y >= l.btnTop) return x < l.cx ? Zone::Cue : Zone::Play;
    if (y >= l.ovTop - 2 && y <= l.ovTop + l.ovH + 2) return Zone::Overview;
    if (fabsf(y - l.waveY) <= l.waveH) return Zone::Wave;
    return Zone::None;
}

float jogAngleDeg(const Layout& l, float x, float y)
{
    float a = atan2f(x - l.cx, -(y - l.cy)) * 180 / kPi;
    return a < 0 ? a + 360 : a;
}

// "-2:31.4"
static void fmtTime(char* s, float t, bool minus)
{
    if (t < 0) t = 0;
    const int ds = (int)(t * 10);
    sprintf(s, "%s%d:%02d.%d", minus ? "-" : "", ds / 600, ds / 10 % 60, ds % 10);
}

static void drawRing(rgfx::Band& b, const Layout& l, const View& v)
{
    if (!b.rows(0, b.h())) return;
    b.ring(l.cx, l.cy, l.ringIn, l.ringOut + 1, v.jogTouched ? kRingTch : kRing);
    // 48 dots that turn with the platter, like a turntable's strobe dots; bigger every 90
    // degrees, and one line for the platter's marker
    const float rd = (l.ringIn + l.ringOut) / 2;
    const float dot = fmaxf(1.2f, 0.0065f * l.d);
    const uint16_t dc = v.jogTouched ? kTickTch : kTick;
    for (int i = 1; i < 48; i++) {
        const float a = (v.platterDeg + i * 7.5f) * kPi / 180;
        const float x = l.cx + sinf(a) * rd, y = l.cy - cosf(a) * rd;
        if (!b.rows((int)(y - 2 * dot - 1), (int)(4 * dot + 3))) continue;
        b.disc(x, y, i % 12 == 0 ? dot * 1.6f : dot, dc);
    }
    const float a = v.platterDeg * kPi / 180, s = sinf(a), c = cosf(a);
    const float r0 = l.ringIn + 0.014f * l.d, r1 = l.ringOut - 0.014f * l.d;
    b.line(l.cx + s * r0, l.cy - c * r0, l.cx + s * r1, l.cy - c * r1, fmaxf(2, 0.008f * l.d), kMarker);
}

static void drawProgress(rgfx::Band& b, const Layout& l, const View& v)
{
    b.ring(l.cx, l.cy, l.progIn, l.progOut, kTrack);
    if (v.len <= 0) return;
    const float f = fminf(1, fmaxf(0, v.pos / v.len));
    const bool endWarn = v.len - v.pos < 30;
    if (f > 0) b.arc(l.cx, l.cy, l.progIn, l.progOut, 0, f * 2 * kPi, endWarn ? kRed : kText);
}

static void drawTop(rgfx::Band& b, const Layout& l, const View& v)
{
    if (!b.rows((int)(l.titleY - 2 * l.title->cap), (int)(l.infoY - l.titleY + 3 * l.title->cap))) return;
    // the title cut to fit: worked out once per title (it's an O(n^2) search)
    static const char* cTitle = nullptr;
    static char fitted[96];
    if (cTitle != v.title) {
        cTitle = v.title;
        const float maxw = 2 * halfChord(l, l.titleY - l.title->cap) - 0.04f * l.d;
        snprintf(fitted, sizeof fitted, "%s", v.title);
        size_t n = strlen(fitted);
        while (n > 0 && rgfx::Band::width(fitted, *l.title) > maxw) {
            fitted[--n] = 0;
            if (n >= 3) { char t[100]; snprintf(t, sizeof t, "%.*s...", (int)(n - 3), fitted); if (rgfx::Band::width(t, *l.title) <= maxw) { strcpy(fitted, t); break; } }
        }
    }
    b.text((int)(l.cx - rgfx::Band::width(fitted, *l.title) / 2.0f), (int)l.titleY, fitted, *l.title, kText);

    char t[24];
    fmtTime(t, v.len - v.pos, true);
    const bool endWarn = v.len > 0 && v.len - v.pos < 30;
    b.text((int)(l.cx - rgfx::Band::width(t, *l.time) / 2.0f), (int)l.timeY, t, *l.time, endWarn ? kRed : kText);

    // key | BPM | pitch
    char bpm[16], pitch[16];
    snprintf(bpm, sizeof bpm, "%.1f", v.bpm);
    snprintf(pitch, sizeof pitch, "%+.1f%%", v.pitchPct);
    // spaced by their widths, so it fits at 240 px as well as 412
    const rgfx::Font& f = *l.info;
    const float sp = fmaxf(6, 0.03f * l.d);
    const int wb = rgfx::Band::width(bpm, f);
    const float bx = l.cx - wb / 2.0f;
    b.text((int)bx, (int)l.infoY, bpm, f, kText);
    b.text((int)(bx - sp - rgfx::Band::width(v.key, f)), (int)l.infoY, v.key, f, v.keylock ? kOrange : kDim);
    b.text((int)(bx + wb + sp), (int)l.infoY, pitch, f, fabsf(v.pitchPct) < 0.05f ? kDim : kGreen);
}

static void drawWave(rgfx::Band& b, const Layout& l, const View& v)
{
    const int top = (int)(l.waveY - l.waveH), bot = (int)(l.waveY + l.waveH);
    if (!b.rows(top - 2, bot - top + 5)) return;       // the playhead reaches 2 px past the wave
    const float pxPerSec = l.d / l.secAcross;
    const int x0 = (int)(l.cx - l.inner), x1 = (int)(l.cx + l.inner);
    // beat grid behind the wave: a faint line a beat, brighter on the bar
    if (v.gridBpm > 0) {
        const float spb = 60 / v.gridBpm;
        const float tLeft = v.pos - (l.cx - x0) / pxPerSec;
        long k = (long)ceilf((tLeft - v.gridStart) / spb);
        for (;; k++) {
            const float x = l.cx + (v.gridStart + k * spb - v.pos) * pxPerSec;
            if (x > x1) break;
            const float lim = fminf(l.waveH, sqrtf(fmaxf(0, l.inner * l.inner - (x - l.cx) * (x - l.cx))));
            if (lim <= 2) continue;
            const bool bar = (k % 4 + 4) % 4 == 0;
            b.fill((int)x, (int)(l.waveY - lim), 1, (int)(2 * lim), bar ? rgb(60, 66, 78) : rgb(30, 33, 40));
        }
    }
    // the wave: mirrored about the centre line, the part already played dimmed
    for (int x = x0; x < x1; x++) {
        const float dx = x + 0.5f - l.cx;
        const float lim2 = l.inner * l.inner - dx * dx;
        if (lim2 <= 4) continue;
        const float lim = fminf(l.waveH, sqrtf(lim2));
        const float t = v.pos + dx / pxPerSec;
        if (t < 0 || !v.peak || v.binsPerSec <= 0) continue;
        const size_t bin = (size_t)(t * v.binsPerSec);
        if (bin >= v.bins) continue;
        // the column covers several bins at this zoom: take the loudest
        const size_t span = (size_t)fmaxf(1, v.binsPerSec / pxPerSec);
        uint8_t pk = 0; uint16_t col = v.color ? v.color[bin] : kBlue;
        for (size_t i = bin; i < bin + span && i < v.bins; i++)
            if (v.peak[i] > pk) { pk = v.peak[i]; if (v.color) col = v.color[i]; }
        const float h = pk / 255.0f * lim;
        if (h < 0.5f) continue;
        if (dx < 0) col = rgfx::mix(col, kBg, 150);
        b.fill(x, (int)(l.waveY - h), 1, (int)(2 * h) + 1, col);
    }
    // cue: an orange line and a flag at the top
    const float cx = l.cx + (v.cue - v.pos) * pxPerSec;
    if (cx >= x0 && cx < x1) {
        b.fill((int)cx, top, 1, bot - top, kOrange);
        const float s = 0.018f * l.d;
        for (int i = 0; i < (int)s; i++) b.fill((int)(cx - (s - i)), top + i, (int)(2 * (s - i)), 1, kOrange);
    }
    // playhead
    const float pw = fmaxf(2, 0.006f * l.d);
    b.fill((int)(l.cx - pw / 2), top - 2, (int)pw, bot - top + 4, kText);
}

static void drawOverview(rgfx::Band& b, const Layout& l, const View& v)
{
    const int top = (int)l.ovTop, h = (int)l.ovH;
    if (!b.rows(top - 2, h + 5)) return;               // the playhead reaches 2 px past the strip
    const float half = halfChord(l, l.ovTop + l.ovH) - 0.035f * l.d;
    const int x0 = (int)(l.cx - half), w = (int)(2 * half);
    if (w <= 0 || v.len <= 0 || !v.peak) return;
    const float played = v.pos / v.len;
    // column heights, worked out once per track and size (it's every bin of the track)
    static const uint8_t* cPeak = nullptr;
    static size_t cBins = 0;
    static int cW = 0, cH = 0;
    static uint8_t col[512];
    if (cPeak != v.peak || cBins != v.bins || cW != w || cH != h) {
        for (int i = 0; i < w && i < 512; i++) {
            const size_t a = (size_t)((double)i / w * v.bins), e = (size_t)((double)(i + 1) / w * v.bins);
            uint8_t pk = 0;
            for (size_t k = a; k < e && k < v.bins; k++) if (v.peak[k] > pk) pk = v.peak[k];
            col[i] = (uint8_t)(pk * h / 255);
        }
        cPeak = v.peak; cBins = v.bins; cW = w; cH = h;
    }
    const int split = (int)(played * w);
    for (int i = 0; i < w && i < 512; i++)
        b.fill(x0 + i, top + h - col[i], 1, col[i], i < split ? rgb(70, 76, 90) : rgb(170, 178, 192));
    b.fill((int)(x0 + v.cue / v.len * w), top - 1, 1, h + 2, kOrange);
    b.fill((int)(x0 + played * w), top - 2, 2, h + 4, kText);
}

static void drawButtons(rgfx::Band& b, const Layout& l, const View& v)
{
    const int top = (int)l.btnTop;
    if (!b.rows(top, (int)(l.cy + l.inner) - top + 1)) return;
    const float gap = fmaxf(2, 0.008f * l.d);
    // fills: the content circle below btnTop, split at the centre
    const uint16_t cueC  = v.atCue ? rgb(90, 52, 6) : rgb(40, 30, 18);
    const uint16_t playC = v.playing ? rgb(16, 74, 36) : rgb(20, 36, 26);
    for (int y = top; y < (int)(l.cy + l.inner) + 1; y++) {
        if (!b.rows(y, 1)) continue;
        const float hc = halfChord(l, y + 0.5f);
        if (hc <= gap) continue;
        // anti-aliased ends: the circle's edge
        const float xl = l.cx - hc, xr = l.cx + hc;
        b.hspan((int)ceilf(xl), (int)(l.cx - gap / 2), y, cueC);
        b.hspan((int)(l.cx + gap / 2), (int)floorf(xr), y, playC);
        b.blend((int)floorf(xl), y, cueC, (int)((ceilf(xl) - xl) * 255));
        b.blend((int)floorf(xr), y, playC, (int)((xr - floorf(xr)) * 255));
    }
    // labels
    const float ly = top + (l.cy + l.inner - top) * 0.52f + l.btn->cap / 2.0f;
    const float off = 0.135f * l.d;
    const char* cue = "CUE";
    b.text((int)(l.cx - off - rgfx::Band::width(cue, *l.btn) / 2.0f), (int)ly, cue, *l.btn, v.atCue ? kOrange : rgb(200, 140, 60));
    // play / pause: a triangle, or two bars
    const float s = l.btn->cap * 1.5f, px = l.cx + off, py = ly - l.btn->cap / 2.0f;
    const uint16_t pc = v.playing ? kGreen : rgb(120, 200, 140);
    if (v.playing) {
        b.fill((int)(px - s * 0.45f), (int)(py - s / 2), (int)(s * 0.3f), (int)s, pc);
        b.fill((int)(px + s * 0.15f), (int)(py - s / 2), (int)(s * 0.3f), (int)s, pc);
    } else {
        for (int i = 0; i < (int)s; i++) {
            const float t = (float)i / s, w = (1 - fabsf(t - 0.5f) * 2) * s * 0.85f;
            b.fill((int)(px - s * 0.35f), (int)(py - s / 2 + i), (int)w, 1, pc);
        }
    }
}

Prof prof;

void draw(rgfx::Band& b, const Layout& l, const View& v)
{
    int64_t t = prof.now ? prof.now() : 0;
    auto lap = [&](int i) { if (prof.now) { const int64_t n = prof.now(); prof.us[i] += n - t; t = n; } };
    b.clear(kBg);           lap(0);
    drawRing(b, l, v);      lap(1);
    drawProgress(b, l, v);  lap(2);
    drawTop(b, l, v);       lap(3);
    drawWave(b, l, v);      lap(4);
    drawOverview(b, l, v);  lap(5);
    drawButtons(b, l, v);   lap(6);
}

} // namespace round_ui
