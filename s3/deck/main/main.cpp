// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// The S3 deck, first playable version: freedj-nano's deck engine (the C6's: transport, CDJ
// cue, jog nudge / scratch, keylock) playing tracks from the `tracks` flash partition through
// the speaker, with the round UI and touch.
//
//   jog ring       touch holds the platter; turning scratches (1.8 s of track a turn: 33 1/3)
//   waveform       drag it: the track follows the finger (scratch)
//   title          tap: next track
//   CUE            CDJ cue: playing -> back to the cue; paused -> set it; held at the cue -> preview
//   PLAY           play / pause
//
// Tasks: audio (core 1, top priority) renders the deck into I2S. Touch (core 1) is the deck's
// one control task: it turns touches into deck events. The screen (core 0) waits for vsync,
// then draws the frame in 32-row bands, each band split between the two cores, sending one
// band while the next is drawn.
#include <math.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nano/deck.h"
#include "nano/library.h"
#include "round_ui.h"

static const char* TAG = "deck_s3";

// ---- feel: one block, so they're easy to tune on the hardware
static const float kJogSecPerRev = 1.8f;       // 33 1/3 rpm: a turn of the ring is 1.8 s of track
static const float kTickSec      = 0.004f;     // the engine's scratch step (deck.cpp kScratchMsPerTick)

static Board s_board;
static nano::Library s_lib;
static nano::Deck* s_deck;
static round_ui::Layout s_layout;
static int s_track = -1;                       // what the control task last loaded

static deck::Event ev(deck::Type t, bool pressed = true, int16_t delta = 0, float pos = 0)
{
    deck::Event e;
    e.type = t; e.pressed = pressed; e.delta = delta; e.position = pos;
    return e;
}

// ---- audio

static void audio_task(void*)
{
    static int16_t buf[nano::Deck::kBlock * 2];
    for (;;) {
        board_audio_rate(s_board, s_deck->rate());
        s_deck->render(buf, nano::Deck::kBlock);
        size_t n;
        i2s_channel_write(s_board.i2s, buf, sizeof buf, &n, portMAX_DELAY);
    }
}

// ---- touch -> deck events (runs in the touch task: the deck's one control task)

static void on_touch(const Touch& t)
{
    static bool down = false;
    static round_ui::Zone zone = round_ui::Zone::None;
    static float lastA = 0, lastX = 0, acc = 0;
    const round_ui::Layout& l = s_layout;

    if (t.down && !down) {
        zone = round_ui::hit(l, t.x, t.y);
        acc = 0;
        switch (zone) {
        case round_ui::Zone::Jog:
            lastA = round_ui::jogAngleDeg(l, t.x, t.y);
            s_deck->post(ev(deck::Type::JogTouch, true));
            break;
        case round_ui::Zone::Wave:
            lastX = t.x;
            s_deck->post(ev(deck::Type::JogTouch, true));
            break;
        case round_ui::Zone::Cue:  s_deck->post(ev(deck::Type::Cue, true)); break;
        case round_ui::Zone::Play: s_deck->post(ev(deck::Type::PlayPause, true)); break;
        case round_ui::Zone::Title:
            if (s_lib.count()) {
                s_track = (s_track + 1) % s_lib.count();
                s_deck->post(ev(deck::Type::Load, true, (int16_t)s_track));
            }
            break;
        default: break;
        }
        down = true;
    } else if (t.down && down) {
        float sec = 0;
        if (zone == round_ui::Zone::Jog) {
            const float a = round_ui::jogAngleDeg(l, t.x, t.y);
            float d = a - lastA;
            if (d > 180) d -= 360;
            if (d < -180) d += 360;
            lastA = a;
            sec = d / 360 * kJogSecPerRev;
        } else if (zone == round_ui::Zone::Wave) {
            // the waveform follows the finger: dragging right goes back in the track
            sec = -(t.x - lastX) * l.secAcross / l.d;
            lastX = t.x;
        }
        if (sec != 0) {
            acc += sec / kTickSec;
            const int ticks = (int)acc;
            if (ticks) { s_deck->post(ev(deck::Type::Jog, false, (int16_t)ticks)); acc -= ticks; }
        }
    } else if (!t.down && down) {
        switch (zone) {
        case round_ui::Zone::Jog: case round_ui::Zone::Wave: s_deck->post(ev(deck::Type::JogTouch, false)); break;
        case round_ui::Zone::Cue:  s_deck->post(ev(deck::Type::Cue, false)); break;
        case round_ui::Zone::Play: s_deck->post(ev(deck::Type::PlayPause, false)); break;
        default: break;
        }
        down = false;
        zone = round_ui::Zone::None;
    }
}

// ---- screen: two cores per band

struct Half { rgfx::Band* band; const round_ui::View* v; };
static Half s_half;
static SemaphoreHandle_t s_go, s_done;

static void draw_helper(void*)
{
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        round_ui::draw(*s_half.band, s_layout, *s_half.v);
        xSemaphoreGive(s_done);
    }
}

static bool open_tracks()
{
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "tracks");
    if (!p) { ESP_LOGE(TAG, "no tracks partition"); return false; }
    const void* base;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &base, &h) != ESP_OK) { ESP_LOGE(TAG, "mmap failed"); return false; }
    if (!s_lib.open(base, p->size)) {
        ESP_LOGW(TAG, "no track pack at 0x%x: tools/flash_tracks.sh music...", (unsigned)p->address);
        return false;
    }
    ESP_LOGI(TAG, "%d tracks", s_lib.count());
    return true;
}

extern "C" void app_main(void)
{
    board_init(s_board, 44100);
    s_layout = round_ui::layout(LCD_W);
    open_tracks();
    s_deck = new nano::Deck(s_lib);
    if (s_lib.count()) { s_track = 0; s_deck->post(ev(deck::Type::Load, true, 0)); }
    xTaskCreatePinnedToCore(audio_task, "audio", 6144, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
    board_touch_start(on_touch);

    s_go = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(draw_helper, "draw2", 4096, nullptr, 4, nullptr, 1);

    const int BAND = 32, HALF = BAND / 2;
    uint16_t* buf[2];
    for (auto& p : buf) p = (uint16_t*)heap_caps_malloc(LCD_W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    // each buffer as two 16-row halves: core 0 draws the top, core 1 the bottom
    rgfx::Band top[2] = { { buf[0], LCD_W, LCD_H, HALF, true }, { buf[1], LCD_W, LCD_H, HALF, true } };
    rgfx::Band bot[2] = { { buf[0] + LCD_W * HALF, LCD_W, LCD_H, HALF, true }, { buf[1] + LCD_W * HALF, LCD_W, LCD_H, HALF, true } };

    round_ui::Prof prof;
    prof.now = esp_timer_get_time;
    round_ui::View v;
    nano::Track tr;
    int trIndex = -2;
    int64_t lastLog = esp_timer_get_time(), drawUs = 0, worstUs = 0;
    int frames = 0, missed = 0;
    bool lit = false;
    for (;;) {
        if (!board_vsync(40)) missed++;
        const int64_t t0 = esp_timer_get_time();

        // ---- the view, from the deck
        const nano::DeckStatus st = s_deck->status();
        if (st.track != trIndex) {
            trIndex = st.track;
            v = round_ui::View();
            if (trIndex >= 0 && s_lib.get(trIndex, tr)) {
                v.title = tr.name;
                v.bands = tr.bands ? tr.peaks : nullptr;
                v.peak = tr.bands ? nullptr : tr.peaks;
                v.bins = tr.peaksLen;
                v.binsPerSec = (float)tr.pcm.rate / tr.peakBucket;
                v.gridBpm = tr.bpmX100 / 100.0f;
                v.gridStart = tr.pcm.rate ? (float)tr.firstBeat / tr.pcm.rate : 0;
            } else {
                v.title = "no tracks: tools/flash_tracks.sh";
            }
        }
        const float rate = st.rate ? (float)st.rate : 44100;
        v.len = st.frames / rate;
        v.pos = (float)(st.posQ16 / 65536.0 / rate);
        v.cue = st.cue / rate;
        v.playing = st.playing;
        v.atCue = !st.playing && fabsf(v.pos - v.cue) < 0.02f;
        v.pitchPct = st.pitchPermille / 10.0f;
        v.bpm = v.gridBpm * (1 + v.pitchPct / 100);
        v.keylock = st.keylock;
        v.platterDeg = fmodf(v.pos / kJogSecPerRev * 360, 360);
        v.jogTouched = st.scratching;
        round_ui::prepare(s_layout, v);

        // ---- draw and send
        s_half.v = &v;
        bool pending = false;
        int k = 0;
        for (int y0 = 0; y0 < LCD_H; y0 += BAND, k ^= 1) {
            top[k].at(y0);
            bot[k].at(y0 + HALF);
            const bool two = y0 + HALF < LCD_H;
            if (two) { s_half.band = &bot[k]; xSemaphoreGive(s_go); }
            round_ui::draw(top[k], s_layout, v, &prof);
            if (two) xSemaphoreTake(s_done, portMAX_DELAY);
            if (pending) board_lcd_wait();
            const int y1 = y0 + BAND < LCD_H ? y0 + BAND : LCD_H;
            board_lcd_send(s_board, y0, y1, buf[k]);
            pending = true;
        }
        board_lcd_wait();
        const int64_t dt = esp_timer_get_time() - t0;
        drawUs += dt;
        if (dt > worstUs) worstUs = dt;
        frames++;
        if (!lit) { board_backlight(80); lit = true; }

        const int64_t now = esp_timer_get_time();
        if (now - lastLog > 3000000) {
            ESP_LOGI(TAG, "%.1f fps (vsync %.0f Hz, %d missed), frame %lld us avg, %lld worst", frames * 1e6f / (now - lastLog),
                     board_vsync_hz(), missed, drawUs / frames, worstUs);
            const int64_t* u = prof.us;
            ESP_LOGI(TAG, "  core 0 half: clear %lld ring %lld progress %lld top %lld wave %lld overview %lld buttons %lld",
                     u[0] / frames, u[1] / frames, u[2] / frames, u[3] / frames, u[4] / frames, u[5] / frames, u[6] / frames);
            memset(prof.us, 0, sizeof prof.us);
            frames = 0; drawUs = 0; worstUs = 0; missed = 0; lastLog = now;
        }
    }
}
