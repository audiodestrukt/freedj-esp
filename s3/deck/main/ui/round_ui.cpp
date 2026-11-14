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
// 3-band waveform colours (low, mid, high), and dimmed for the part already played
static const uint16_t kBand[3]     = { rgb(20, 90, 255), rgb(255, 150, 20), rgb(245, 245, 245) };
static const uint16_t kBandPast[3] = { rgb(12, 40, 100), rgb(100, 62, 14), rgb(96, 96, 100) };

// ---- what prepare() works out for the frame

static const char* s_title = nullptr;
static char s_fitted[96];
static const void* s_ovSrc = nullptr;
static size_t s_ovBins = 0;
static int s_ovW = 0, s_ovH = 0;
static uint8_t s_ovCol[512];
// The ring's scale dots: each one's anti-aliased mask, worked out once per layout
// (sin, cos and sqrt are slow on the S3; a mask is a few blends)
static const int kDotBox = 10;
static float s_dotD = 0;
static int16_t s_dotX[48], s_dotY[48];
static uint8_t s_dotMask[48][kDotBox * kDotBox];
// the waveform's height limit per column (the content circle), once per layout
static float s_colLim[512];

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
    if (y < l.timeY) return Zone::Title;
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
    // 48 fixed dots, bigger every 90 degrees: a scale to see the marker move against.
    // The marker turns with the platter.
    const uint16_t dc = v.jogTouched ? kTickTch : kTick;
    for (int i = 0; i < 48; i++)
        if (b.rows(s_dotY[i], kDotBox)) b.sprite(s_dotX[i], s_dotY[i], s_dotMask[i], kDotBox, kDotBox, dc);
    const float a = v.platterDeg * kPi / 180, s = sinf(a), c = cosf(a);
    const float r0 = l.ringIn + 0.010f * l.d, r1 = l.ringOut - 0.010f * l.d;
    b.line(l.cx + s * r0, l.cy - c * r0, l.cx + s * r1, l.cy - c * r1, fmaxf(2.5f, 0.010f * l.d), kMarker);
}

static void drawProgress(rgfx::Band& b, const Layout& l, const View& v)
{
    b.ring(l.cx, l.cy, l.progIn, l.progOut, kTrack);
    if (v.len <= 0) return;
    const float f = fminf(1, fmaxf(0, v.pos / v.len));
    const bool endWarn = v.len - v.pos < 30;
    if (f > 0) b.arc(l.cx, l.cy, l.progIn, l.progOut, 0, f * 2 * kPi, endWarn ? kRed : kText);
}

static uint8_t binPeak(const View& v, size_t i)
{
    if (v.bands) { const uint8_t* p = v.bands + 3 * i; return p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]); }
    return v.peak ? v.peak[i] : 0;
}

static void ovGeom(const Layout& l, int* x0, int* w)
{
    const float half = halfChord(l, l.ovTop + l.ovH) - 0.035f * l.d;
    *x0 = (int)(l.cx - half); *w = (int)(2 * half);
}

void prepare(const Layout& l, const View& v)
{
    if (s_dotD != l.d) {
        const float rd = (l.ringIn + l.ringOut) / 2;
        const float dot = fmaxf(1.2f, 0.0065f * l.d);
        for (int i = 0; i < 48; i++) {
            const float a = i * 7.5f * kPi / 180;
            const float x = l.cx + sinf(a) * rd, y = l.cy - cosf(a) * rd;
            const float r = i % 12 == 0 ? fminf(dot * 1.6f, kDotBox / 2 - 1) : dot;
            s_dotX[i] = (int16_t)floorf(x) - kDotBox / 2; s_dotY[i] = (int16_t)floorf(y) - kDotBox / 2;
            for (int py = 0; py < kDotBox; py++)
                for (int px = 0; px < kDotBox; px++) {
                    const float dx = s_dotX[i] + px + 0.5f - x, dy = s_dotY[i] + py + 0.5f - y;
                    const float d = r - sqrtf(dx * dx + dy * dy);
                    s_dotMask[i][py * kDotBox + px] = (uint8_t)(d >= 0.5f ? 255 : d <= -0.5f ? 0 : (d + 0.5f) * 255);
                }
        }
        for (int x = 0; x < 512; x++) {
            const float dx = x + 0.5f - l.cx, lim2 = l.inner * l.inner - dx * dx;
            s_colLim[x] = lim2 > 4 ? fminf(l.waveH, sqrtf(lim2)) : 0;
        }
        s_dotD = l.d;
    }
    if (s_title != v.title) {                    // the title cut to fit: an O(n^2) search, once per title
        s_title = v.title;
        const float maxw = 2 * halfChord(l, l.titleY - l.title->cap) - 0.04f * l.d;
        snprintf(s_fitted, sizeof s_fitted, "%s", v.title);
        size_t n = strlen(s_fitted);
        while (n > 0 && rgfx::Band::width(s_fitted, *l.title) > maxw) {
            s_fitted[--n] = 0;
            if (n >= 3) { char t[100]; snprintf(t, sizeof t, "%.*s...", (int)(n - 3), s_fitted); if (rgfx::Band::width(t, *l.title) <= maxw) { strcpy(s_fitted, t); break; } }
        }
    }
    int x0, w;
    ovGeom(l, &x0, &w);
    const int h = (int)l.ovH;
    const void* src = v.bands ? (const void*)v.bands : (const void*)v.peak;
    if (src && (s_ovSrc != src || s_ovBins != v.bins || s_ovW != w || s_ovH != h)) {   // every bin of the track: once
        for (int i = 0; i < w && i < 512; i++) {
            const size_t a = (size_t)((double)i / w * v.bins), e = (size_t)((double)(i + 1) / w * v.bins);
            uint8_t pk = 0;
            for (size_t k = a; k < e && k < v.bins; k++) { const uint8_t p = binPeak(v, k); if (p > pk) pk = p; }
            s_ovCol[i] = (uint8_t)(pk * h / 255);
        }
        s_ovSrc = src; s_ovBins = v.bins; s_ovW = w; s_ovH = h;
    }
}

static void drawTop(rgfx::Band& b, const Layout& l, const View& v)
{
    if (!b.rows((int)(l.titleY - 2 * l.title->cap), (int)(l.infoY - l.titleY + 3 * l.title->cap))) return;
    b.text((int)(l.cx - rgfx::Band::width(s_fitted, *l.title) / 2.0f), (int)l.titleY, s_fitted, *l.title, kText);

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
    const float secPerPx = 1 / pxPerSec;
    const size_t span = (size_t)fmaxf(1, v.binsPerSec * secPerPx);   // bins a column covers
    for (int x = x0; x < x1; x++) {
        const float dx = x + 0.5f - l.cx;
        const float lim = x >= 0 && x < 512 ? s_colLim[x] : 0;
        if (lim <= 0) continue;
        const float t = v.pos + dx * secPerPx;
        if (t < 0 || v.binsPerSec <= 0 || !(v.peak || v.bands)) continue;
        const size_t bin = (size_t)(t * v.binsPerSec);
        if (bin >= v.bins) continue;
        const bool past = dx < 0;
        if (v.bands) {
            // CDJ-3000 3-band: low at the back, mid, then high in front; the loudest of the
            // bins this column covers
            uint8_t lo = 0, mi = 0, hi = 0;
            for (size_t i = bin; i < bin + span && i < v.bins; i++) {
                const uint8_t* p = v.bands + 3 * i;
                lo = p[0] > lo ? p[0] : lo; mi = p[1] > mi ? p[1] : mi; hi = p[2] > hi ? p[2] : hi;
            }
            const uint8_t lv[3] = { lo, mi, hi };
            for (int k = 0; k < 3; k++) {
                const float h = lv[k] * (1 / 255.0f) * lim;
                if (h < 0.5f) continue;
                const uint16_t col = past ? kBandPast[k] : kBand[k];
                b.fill(x, (int)(l.waveY - h), 1, (int)(2 * h) + 1, col);
            }
            continue;
        }
        uint8_t pk = 0; uint16_t col = v.color ? v.color[bin] : kBlue;
        for (size_t i = bin; i < bin + span && i < v.bins; i++)
            if (v.peak[i] > pk) { pk = v.peak[i]; if (v.color) col = v.color[i]; }
        const float h = pk * (1 / 255.0f) * lim;
        if (h < 0.5f) continue;
        if (past) col = rgfx::mix(col, kBg, 150);
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
    int x0, w;
    ovGeom(l, &x0, &w);
    if (w <= 0 || v.len <= 0 || s_ovW != w) return;
    const float played = v.pos / v.len;
    const int split = (int)(played * w);
    for (int i = 0; i < w && i < 512; i++)
        b.fill(x0 + i, top + h - s_ovCol[i], 1, s_ovCol[i], i < split ? rgb(70, 76, 90) : rgb(170, 178, 192));
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

void draw(rgfx::Band& b, const Layout& l, const View& v, Prof* prof)
{
    int64_t t = prof && prof->now ? prof->now() : 0;
    auto lap = [&](int i) { if (prof && prof->now) { const int64_t n = prof->now(); prof->us[i] += n - t; t = n; } };
    b.clear(kBg);           lap(0);
    drawRing(b, l, v);      lap(1);
    drawProgress(b, l, v);  lap(2);
    drawTop(b, l, v);       lap(3);
    drawWave(b, l, v);      lap(4);
    drawOverview(b, l, v);  lap(5);
    drawButtons(b, l, v);   lap(6);
}

} // namespace round_ui
