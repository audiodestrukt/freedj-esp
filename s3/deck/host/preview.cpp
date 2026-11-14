// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// Renders the round UI to PNGs on the PC, the same code the S3 runs, drawn in the same 32-row
// bands (and checked against one whole-screen draw). The glass is shown as a circle on a
// bezel, so what's outside the circle is visible as cut off.
//
//   make -C host          ->  host/out/*.png
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <math.h>
#include "round_ui.h"

static std::vector<uint8_t> s_peak;
static std::vector<uint16_t> s_color;
static float s_bps;

static void load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    char magic[4]; uint32_t n;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "RWAV", 4) || fread(&n, 4, 1, f) != 1 || fread(&s_bps, 4, 1, f) != 1) exit(1);
    s_peak.resize(n); s_color.resize(n);
    if (fread(s_peak.data(), 1, n, f) != n || fread(s_color.data(), 2, n, f) != n) exit(1);
    fclose(f);
}

static void render(const char* name, int d, round_ui::View v)
{
    const round_ui::Layout l = round_ui::layout(d);
    std::vector<uint16_t> whole(d * d), banded(d * d);
    rgfx::Band all(whole.data(), d, d, d, false);
    round_ui::draw(all, l, v);
    const int B = 32;
    std::vector<uint16_t> band(d * B);
    rgfx::Band b(band.data(), d, d, B, false);
    for (int y0 = 0; y0 < d; y0 += B) {
        b.at(y0);
        round_ui::draw(b, l, v);
        memcpy(&banded[y0 * d], band.data(), (size_t)(b.y1() - y0) * d * 2);
    }
    long diff = 0;
    for (int i = 0; i < d * d; i++)
        if (whole[i] != banded[i]) { if (diff < 3) printf("  differs at %d,%d\n", i % d, i / d); diff++; }
    // PPM on a bezel: 12 px margin, the glass a circle
    const int m = 12, W = d + 2 * m;
    std::string ppm = std::string("out/") + name + ".ppm";
    FILE* f = fopen(ppm.c_str(), "wb");
    fprintf(f, "P6 %d %d 255\n", W, W);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = x - m + 0.5f - d / 2.0f, dy = y - m + 0.5f - d / 2.0f;
            const float r = sqrtf(dx * dx + dy * dy);
            uint8_t px[3] = { 48, 48, 52 };
            if (r < d / 2.0f) {
                const uint16_t c = banded[(y - m) * d + (x - m)];
                px[0] = (uint8_t)((c >> 11) * 255 / 31); px[1] = (uint8_t)((c >> 5 & 63) * 255 / 63); px[2] = (uint8_t)((c & 31) * 255 / 31);
            }
            fwrite(px, 1, 3, f);
        }
    fclose(f);
    printf("%-22s %d px  banded vs whole: %ld pixels differ\n", name, d, diff);
}

int main(int argc, char** argv)
{
    load(argc > 1 ? argv[1] : "fake_track.bin");
    round_ui::View v;
    v.title = "Fake Track (Original Mix) - Nobody";
    v.len = s_peak.size() / s_bps;
    v.pos = 100.3f; v.cue = 76.19f;
    v.playing = true;
    v.bpm = 129.0f; v.pitchPct = 2.4f; v.key = "8A";
    v.peak = s_peak.data(); v.color = s_color.data(); v.bins = s_peak.size(); v.binsPerSec = s_bps;
    v.gridStart = 0; v.gridBpm = 126;
    v.platterDeg = 37;

    render("412_playing", 412, v);
    render("240_playing", 240, v);

    round_ui::View c = v;                 // paused on the cue, before the drop
    c.playing = false; c.atCue = true; c.pos = c.cue = 76.19f; c.pitchPct = 0; c.bpm = 126; c.platterDeg = 0;
    render("412_cued", 412, c);
    render("240_cued", 240, c);

    round_ui::View j = v;                 // finger on the jog ring
    j.jogTouched = true; j.platterDeg = 205; j.pos = 95.2f;
    render("412_jog", 412, j);

    round_ui::View e = v;                 // the last 30 s
    e.pos = v.len - 21.7f;
    render("412_end", 412, e);
    return 0;
}
