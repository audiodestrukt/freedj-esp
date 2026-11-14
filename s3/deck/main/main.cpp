// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// The round deck screen on the S3, running on its own: a made-up track (host/fake_track.bin)
// "plays", so the waveform scrolls and the platter ticks turn. No audio or touch yet: this is
// for judging the layout on the real glass and measuring the frame rate.
//
// Drawing: 32-row bands into two DMA buffers; one band is drawn while the other is sent.
#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "round_ui.h"

static const char* TAG = "deck_s3";
static Board s_board;

extern const uint8_t fake_track_start[] asm("_binary_fake_track_bin_start");

extern "C" void app_main(void)
{
    board_init(s_board, 44100);

    // the embedded waveform: "RWAV", bins, bins a second, peaks, colours
    uint32_t bins; float bps;
    memcpy(&bins, fake_track_start + 4, 4);
    memcpy(&bps, fake_track_start + 8, 4);
    const uint8_t* peaks = fake_track_start + 12;
    uint16_t* colors = (uint16_t*)heap_caps_malloc(bins * 2, MALLOC_CAP_SPIRAM);   // aligned copy
    memcpy(colors, peaks + bins, bins * 2);

    round_ui::View v;
    v.title = "Fake Track (Original Mix) - Nobody";
    v.len = bins / bps;
    v.cue = 76.19f;
    v.playing = true;
    v.bpm = 126; v.key = "8A";
    v.peak = peaks; v.color = colors; v.bins = bins; v.binsPerSec = bps;
    v.gridStart = 0; v.gridBpm = 126;
    const round_ui::Layout l = round_ui::layout(LCD_W);

    const int BAND = 32;
    uint16_t* buf[2];
    for (auto& p : buf) p = (uint16_t*)heap_caps_malloc(LCD_W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    rgfx::Band band[2] = { { buf[0], LCD_W, LCD_H, BAND, true }, { buf[1], LCD_W, LCD_H, BAND, true } };

    round_ui::prof.now = esp_timer_get_time;
    const int64_t start = esp_timer_get_time();
    int64_t drawUs = 0, lastLog = start;
    int frames = 0;
    bool lit = false;
    for (;;) {
        const float t = (esp_timer_get_time() - start) / 1e6f;
        v.pos = fmodf(70 + t, v.len);
        v.platterDeg = fmodf(t * 360 * 33.333f / 60, 360);    // 33 1/3 rpm

        bool pending = false;
        int k = 0;
        for (int y0 = 0; y0 < LCD_H; y0 += BAND, k ^= 1) {
            const int64_t t0 = esp_timer_get_time();
            band[k].at(y0);
            round_ui::draw(band[k], l, v);
            drawUs += esp_timer_get_time() - t0;
            if (pending) board_lcd_wait();                      // the other buffer is free again
            board_lcd_send(s_board, y0, band[k].y1(), buf[k]);
            pending = true;
        }
        board_lcd_wait();
        frames++;
        if (!lit) { board_backlight(80); lit = true; }

        const int64_t now = esp_timer_get_time();
        if (now - lastLog > 2000000) {
            ESP_LOGI(TAG, "%.1f fps, drawing %lld us a frame (sending ~10300)", frames * 1e6f / (now - lastLog),
                     drawUs / frames);
            const int64_t* u = round_ui::prof.us;
            ESP_LOGI(TAG, "  clear %lld ring %lld progress %lld top %lld wave %lld overview %lld buttons %lld (us a frame)",
                     u[0] / frames, u[1] / frames, u[2] / frames, u[3] / frames, u[4] / frames, u[5] / frames, u[6] / frames);
            memset(round_ui::prof.us, 0, sizeof round_ui::prof.us);
            frames = 0; drawUs = 0; lastLog = now;
        }
    }
}
