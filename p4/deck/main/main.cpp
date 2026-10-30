// P4 deck: browse the SD card with the DJ2Go or the touch screen, stream a
// WAV/MP3 through a PSRAM window, and play it CDJ-style on the board speaker.
// The screen (ui.cpp) shows the list, time, tempo and state.
//
// Deck A controls:
//   browse knob      move through the track list (also touch: tap a row,
//                    tap it again to load)
//   LOAD A / ENTER   load the selected track (stops the deck)
//   PLAY             play / pause
//   CUE              playing: jump to cue and pause
//                    paused:  set cue here, play while held, return on release
//   pitch fader      ±8 % tempo
//   jog              playing: nudge; paused: scrub
//   SYNC             keylock on/off (WSOLA) — stand-in until a real button
//   channel volume   level

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"

#include "audio_out.h"
#include "deck_event.h"
#include "dj2go.h"
#include "library.h"
#include "sd.h"
#include "stream.h"
#include "ui.h"
#include "usb_midi.h"
#include "wsola.h"

static const char* TAG = "deck";

constexpr uint32_t kRate       = 44100;
constexpr int      kBlock      = 256;    // frames per write (~5.8 ms)
constexpr float    kTempoRange = 0.08f;  // ±8 %
constexpr int      kScrubFrames = 441;   // paused jog: 10 ms per tick

static QueueHandle_t s_events;      // deck::Event, USB task → audio task
static QueueHandle_t s_loadReq;     // int track index, audio task → loader
static QueueHandle_t s_loadDone;    // bool ok, loader → audio task
static QueueHandle_t s_select;      // int track index, screen → audio task
static TrackStream s_stream;        // deck A
static dj2go::Decoder s_decoder;
static wsola::Wsola s_wsola;        // ~40 KB, keep out of task stacks

static void onMidiPacket(const uint8_t p[4])
{
    deck::Event ev[2];
    int n = s_decoder.decodeUsbPacket(p, ev, 2);
    for (int i = 0; i < n; i++) xQueueSend(s_events, &ev[i], 0);
}

// Owns the SD card: opens tracks on request and keeps the stream window
// filled. Runs on core 0, away from the audio task.
static void loaderTask(void*)
{
    for (;;) {
        int index;
        if (xQueueReceive(s_loadReq, &index, 0) == pdTRUE) {
            const auto& list = library::tracks();
            bool ok = index >= 0 && index < (int)list.size() && s_stream.open(list[index].c_str());
            // Prefill ~1 s before handing over so PLAY starts clean.
            while (ok && s_stream.buffered() < 44100 && s_stream.service()) {}
            xQueueSend(s_loadDone, &ok, portMAX_DELAY);
        }
        if (!s_stream.service()) vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
    }
}

struct Deck {
    bool   loaded    = false;
    bool   loading   = false;
    bool   playing   = false;
    bool   previewing = false;  // CUE held from pause
    bool   keylock   = false;
    double pos       = 0;       // frames into track (fractional for varispeed)
    double cue       = 0;
    float  tempo     = 1.0f;
    float  nudge     = 0.0f;
    float  gain      = 0.7f;
    int    selected  = 0;
};

static void logSelection(const Deck& d)
{
    const auto& list = library::tracks();
    if (list.empty()) { ESP_LOGW(TAG, "no tracks"); return; }
    ESP_LOGI(TAG, "> [%d/%u] %s", d.selected + 1, (unsigned)list.size(),
             list[d.selected].c_str() + strlen(sd::kMount));
}

static void seek(Deck& d, double frame)
{
    if (frame < 0) frame = 0;
    if (frame > s_stream.frames()) frame = s_stream.frames();
    d.pos = frame;
    s_stream.setPlayhead((size_t)frame);
    s_wsola.reset();
}

static void apply(Deck& d, const deck::Event& e)
{
    using deck::Type;
    if (e.type == Type::Browse) {
        const int n = library::tracks().size();
        if (n) { d.selected = ((d.selected + e.delta) % n + n) % n; logSelection(d); }
        return;
    }
    if (e.type == Type::Load && e.pressed && (e.deck == deck::DeckA || e.deck == deck::DeckAny)) {
        if (library::tracks().empty() || d.loading) return;
        d.playing = d.previewing = false;
        d.loaded = false;
        d.loading = true;
        ESP_LOGI(TAG, "loading…");
        xQueueSend(s_loadReq, &d.selected, 0);
        return;
    }
    if (e.deck != deck::DeckA) return;
    switch (e.type) {
    case Type::PlayPause:
        if (e.pressed && d.loaded) { d.playing = !d.playing; d.previewing = false; }
        break;
    case Type::Cue:
        if (!d.loaded) break;
        if (e.pressed) {
            if (d.playing) { seek(d, d.cue); d.playing = false; }
            else { d.cue = d.pos; d.previewing = true; d.playing = true; }
        } else if (d.previewing) {
            d.previewing = false; d.playing = false; seek(d, d.cue);
        }
        break;
    case Type::TempoFader:
        d.tempo = 1.0f + (e.position - 0.5f) * 2.0f * kTempoRange;
        break;
    case Type::Jog:
        if (d.playing) d.nudge += e.delta * 0.004f;
        else if (d.loaded) seek(d, d.pos + e.delta * kScrubFrames);
        break;
    case Type::Sync:
        if (e.pressed) { d.keylock = !d.keylock; s_wsola.reset(); ESP_LOGI(TAG, "keylock %s", d.keylock ? "ON" : "off"); }
        break;
    case Type::Volume:
        d.gain = e.position;
        break;
    default: break;
    }
}

// Varispeed: read at a fractional rate with linear interpolation. Pitch
// follows speed, like a turntable. Also absorbs file/output rate mismatch.
// Returns false on a stream underrun (window not filled yet).
static bool renderPitched(Deck& d, float speed, int16_t* out)
{
    const double step = speed * (double)s_stream.sampleRate() / kRate;
    for (int i = 0; i < kBlock; i++) {
        const size_t i0 = (size_t)d.pos;
        if (i0 + 1 >= s_stream.frames()) { memset(out + 2 * i, 0, (kBlock - i) * 4); d.playing = false; return true; }
        int16_t two[4];
        if (!s_stream.get(i0, two, 2)) { memset(out + 2 * i, 0, (kBlock - i) * 4); return false; }
        const float fr = (float)(d.pos - i0);
        out[2 * i]     = (int16_t)(two[0] + fr * (two[2] - two[0]));
        out[2 * i + 1] = (int16_t)(two[1] + fr * (two[3] - two[1]));
        d.pos += step;
    }
    return true;
}

// Keylock: feed WSOLA at the output rate (resampling the file to 44.1 kHz
// first, so a 48 kHz track isn't heard 8.8 % slow and flat), let it
// stretch to `speed`.
static bool renderKeylock(Deck& d, float speed, int16_t* out)
{
    constexpr int kFeed = 512;
    static int16_t feed[kFeed * 2];
    static int16_t raw[(kFeed * 3 + 4) * 2];          // file frames for one feed (rates up to 3x 44.1k)
    const double step = (double)s_stream.sampleRate() / kRate;
    s_wsola.setSpeed(speed);
    int got = 0;
    while (got < kBlock) {
        got += s_wsola.pullFrames(out + 2 * got, kBlock - got);
        if (got >= kBlock) break;
        const size_t base = (size_t)d.pos;
        const size_t total = s_stream.frames();
        if (base + 1 >= total) { memset(out + 2 * got, 0, (kBlock - got) * 4); d.playing = false; return true; }
        // How many output-rate frames fit before the end of the track.
        int n = s_wsola.inputCapacity();
        if (n > kFeed) n = kFeed;
        const double frac = d.pos - base;
        const int fit = (int)((total - 1 - base - frac) / step);
        if (n > fit) n = fit;
        if (n <= 0) { memset(out + 2 * got, 0, (kBlock - got) * 4); d.playing = false; return true; }
        // Linear-interpolate n frames at `step` out of a contiguous run of file frames.
        const size_t span = (size_t)(frac + (n - 1) * step) + 2;
        if (!s_stream.get(base, raw, span)) { memset(out + 2 * got, 0, (kBlock - got) * 4); return false; }
        for (int i = 0; i < n; i++) {
            const double x = frac + i * step;
            const size_t j = (size_t)x;
            const float f = (float)(x - j);
            const int16_t* a0 = raw + 2 * j;
            feed[2 * i]     = (int16_t)(a0[0] + f * (a0[2] - a0[0]));
            feed[2 * i + 1] = (int16_t)(a0[1] + f * (a0[3] - a0[1]));
        }
        d.pos += s_wsola.pushFrames(feed, n) * step;
    }
    return true;
}

// Click detector on what actually goes to the DAC. A clean signal changes
// slope smoothly; a dropout, a missed block or a bad splice makes the second
// difference spike far above its running average. Exact on a test tone
// (sine.wav); on music, sharp transients can count too, so treat it as a
// hint there. Only measures while playing continuously.
struct ClickProbe {
    int16_t p1 = 0, p2 = 0;
    int     primed = 0;      // samples of history so far
    float   avg = 0;         // running mean |second difference|
    uint32_t clicks = 0;
    int     worst = 0;       // largest |second difference| seen in a click

    void reset() { primed = 0; }
    void feed(const int16_t* lr, int frames)
    {
        for (int i = 0; i < frames; i++) {
            const int x = lr[2 * i];
            if (primed >= 2) {
                const int d2 = abs(x - 2 * p1 + p2);
                if (primed > 2000 && d2 > 1500 && d2 > 8 * avg) {
                    clicks++;
                    if (d2 > worst) worst = d2;
                }
                avg += (d2 - avg) * 0.001f;
            }
            if (primed <= 2000) primed++;
            p2 = p1;
            p1 = x;
        }
    }
};
static ClickProbe s_clicks;

static void onUiEvent(const deck::Event& e) { xQueueSend(s_events, &e, 0); }
static void onUiSelect(int index) { xQueueSend(s_select, &index, 0); }

static void publishState(const Deck& d)
{
    ui::State u;
    u.selected = d.selected;
    u.loaded = d.loaded;
    u.loading = d.loading;
    u.playing = d.playing;
    u.previewing = d.previewing;
    u.keylock = d.keylock;
    if (d.loaded) {
        const float rate = s_stream.sampleRate();
        u.pos = d.pos / rate;
        u.len = s_stream.seconds();
        u.cue = d.cue / rate;
        u.frame = (size_t)d.pos;
        u.cueFrame = (size_t)d.cue;
        strlcpy(u.name, s_stream.name(), sizeof u.name);
    }
    u.tempoPct = (d.tempo - 1.0f) * 100.0f;
    ui::publish(u);
}

static SemaphoreHandle_t s_audioUp;

// Audio health over one ~2 s period, measured in the audio task and printed
// by reporterTask. realtime = frames sent to the DAC / frames it must have
// played in that wall time; under 100 % means gaps of silence.
struct Report {
    int64_t  t0 = 0, t1 = 0;
    int64_t  blocks = 0, workSumUs = 0, workMaxUs = 0, minSlackUs = INT64_MAX;
    bool     playing = false, keylock = false;
    float    pos = 0, speedPct = 0;
    uint32_t underruns = 0, clicks = 0;
    int      clickWorst = 0;
};
static QueueHandle_t s_report;   // length 1, overwritten

// Top CPU users since the last call, as "name 12% name 8% ...".
static void topTasks(char* out, size_t n, double wallUs)
{
    struct Prev { TaskHandle_t h; uint32_t t; };
    static Prev prev[32];
    static int nPrev;
    static TaskStatus_t st[32];   // static: too big for the stack
    uint32_t total;
    const int count = uxTaskGetSystemState(st, 32, &total);
    struct Use { const char* name; uint32_t dt; BaseType_t core; };
    static Use use[32];
    for (int i = 0; i < count; i++) {
        uint32_t before = 0;
        for (int j = 0; j < nPrev; j++) if (prev[j].h == st[i].xHandle) { before = prev[j].t; break; }
        use[i] = {st[i].pcTaskName, st[i].ulRunTimeCounter - before, st[i].xCoreID};
    }
    for (int i = 0; i < count; i++) prev[i] = {st[i].xHandle, st[i].ulRunTimeCounter};
    nPrev = count;
    for (int i = 0; i < count; i++)          // sort by usage, descending
        for (int j = i + 1; j < count; j++)
            if (use[j].dt > use[i].dt) { Use t = use[i]; use[i] = use[j]; use[j] = t; }
    int len = 0;
    out[0] = 0;
    for (int i = 0; i < count && i < 7 && len < (int)n; i++) {
        const char* core = use[i].core == 0 ? "/0" : use[i].core == 1 ? "/1" : "";
        len += snprintf(out + len, n - len, "%s%s %.0f%%  ", use[i].name, core, 100.0 * use[i].dt / wallUs);
    }
}

static void reporterTask(void*)
{
    Report r;
    uint32_t idle0 = ulTaskGetIdleRunTimeCounterForCore(0), idle1 = ulTaskGetIdleRunTimeCounterForCore(1);
    int64_t tIdle = esp_timer_get_time();
    for (;;) {
        xQueueReceive(s_report, &r, portMAX_DELAY);
        // CPU load per core = 1 - idle time / wall time since the last report.
        // Core 0 is the shared one (UI, SD, USB, and later the network).
        const uint32_t i0 = ulTaskGetIdleRunTimeCounterForCore(0), i1 = ulTaskGetIdleRunTimeCounterForCore(1);
        const int64_t now = esp_timer_get_time();
        const double wall = (double)(now - tIdle);
        const double load0 = 100.0 * (1.0 - (uint32_t)(i0 - idle0) / wall);
        const double load1 = 100.0 * (1.0 - (uint32_t)(i1 - idle1) / wall);
        idle0 = i0; idle1 = i1; tIdle = now;
        if (!r.playing) continue;
        const double realtime = 100.0 * r.blocks * kBlock / ((r.t1 - r.t0) * (kRate / 1e6));
        ESP_LOGI(TAG, "%s  %d:%04.1f / %d:%04.1f  %+.2f %%%s  buffered %.0f s  underruns %u",
                 s_stream.name(), (int)r.pos / 60, fmodf(r.pos, 60),
                 (int)s_stream.seconds() / 60, fmodf(s_stream.seconds(), 60), r.speedPct,
                 r.keylock ? "  keylock" : "", s_stream.buffered() / 44100.0, (unsigned)r.underruns);
        ESP_LOGI(TAG, "  audio: realtime %.1f %%  block work avg %.2f / max %.2f ms (budget %.2f)  "
                 "min slack %.1f ms  clicks %u (worst %d)",
                 realtime, r.blocks ? r.workSumUs / 1000.0 / r.blocks : 0.0, r.workMaxUs / 1000.0,
                 kBlock * 1000.0 / kRate, r.minSlackUs / 1000.0, (unsigned)r.clicks, r.clickWorst);
        static char top[256];
        topTasks(top, sizeof top, wall);
        ESP_LOGI(TAG, "  cpu: core0 %.0f %%  core1 %.0f %%  (%s)", load0, load1, ui::waveStats());
        ESP_LOGI(TAG, "  top: %s", top);
    }
}

static void audioTask(void*)
{
    // Bring up I2S + codec from this task so the I2S interrupt lands on
    // core 1 with the audio, not on core 0 behind the screen's SPI traffic.
    if (!audio_out::begin(kRate, 70)) ESP_LOGE(TAG, "audio init failed");
    xSemaphoreGive(s_audioUp);
    static int16_t buf[kBlock * 2];
    Deck d;
    s_wsola.begin(2);
    logSelection(d);
    uint32_t underruns = 0, clicksAtStart = 0;
    int64_t tWrote = esp_timer_get_time();
    Report r;
    r.t0 = tWrote;

    for (;;) {
        deck::Event e;
        while (xQueueReceive(s_events, &e, 0) == pdTRUE) apply(d, e);
        int pick;
        while (xQueueReceive(s_select, &pick, 0) == pdTRUE)
            if (pick >= 0 && pick < (int)library::tracks().size() && pick != d.selected) {
                d.selected = pick; logSelection(d);
            }
        bool ok;
        if (xQueueReceive(s_loadDone, &ok, 0) == pdTRUE) {
            d.loading = false;
            d.loaded = ok;
            d.cue = 0;
            d.pos = 0;
            s_wsola.reset();
            if (!ok) {
                ESP_LOGE(TAG, "load failed");
            } else {
                ESP_LOGI(TAG, "ready: %s (%d:%02d) — PLAY", s_stream.name(),
                         (int)s_stream.seconds() / 60, (int)s_stream.seconds() % 60);
            }
        }

        if (d.nudge > 0.3f) d.nudge = 0.3f;
        if (d.nudge < -0.3f) d.nudge = -0.3f;
        const float speed = d.tempo + d.nudge;
        d.nudge *= 0.97f;

        if (d.playing && d.loaded) {
            const bool fed = d.keylock ? renderKeylock(d, speed, buf) : renderPitched(d, speed, buf);
            if (!fed) underruns++;
            s_stream.setPlayhead((size_t)d.pos);
            const float g = d.gain;
            for (int i = 0; i < kBlock * 2; i++) buf[i] = (int16_t)(buf[i] * g);
            if (!d.playing) ESP_LOGI(TAG, "end of track");
            if (fed && d.playing) s_clicks.feed(buf, kBlock); else s_clicks.reset();
        } else {
            memset(buf, 0, sizeof buf);
            s_clicks.reset();
        }
        // Measure, but never print from here: a log line over the UART takes
        // longer than an audio block. The reporter task prints instead.
        const int64_t workUs = esp_timer_get_time() - tWrote;
        if (workUs > r.workMaxUs) r.workMaxUs = workUs;
        r.workSumUs += workUs;
        r.blocks++;
        const int64_t tWrite = esp_timer_get_time();
        audio_out::write(buf, kBlock);
        tWrote = esp_timer_get_time();
        // Time spent waiting for DMA room = slack before the DAC would starve.
        if (tWrote - tWrite < r.minSlackUs) r.minSlackUs = tWrote - tWrite;

        publishState(d);  // every block (~170 Hz) so the waveform scrolls smoothly

        if (tWrote - r.t0 >= 2000000) {
            r.t1 = tWrote;
            r.playing = d.playing && d.loaded;
            r.pos = d.loaded ? d.pos / s_stream.sampleRate() : 0;
            r.speedPct = (speed - 1) * 100;
            r.keylock = d.keylock;
            r.underruns = underruns;
            r.clicks = s_clicks.clicks - clicksAtStart;
            r.clickWorst = s_clicks.worst;
            xQueueOverwrite(s_report, &r);
            clicksAtStart = s_clicks.clicks;
            s_clicks.worst = 0;
            r = Report{};
            r.t0 = tWrote;
        }
    }
}

// Serial console keys, for testing without touching the hardware:
//   1-9 select track   l load   p play/pause   c cue (press+release)
//   k keylock          w cycle waveform test mode (normal / no flush / off)
static void serialKeysTask(void*)
{
    uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0);
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) continue;
        deck::Event e;
        e.deck = deck::DeckA;
        e.pressed = true;
        switch (ch) {
        case '1' ... '9': onUiSelect(ch - '1'); continue;
        case 'l': e.type = deck::Type::Load; break;
        case 'p': e.type = deck::Type::PlayPause; break;
        case 'k': e.type = deck::Type::Sync; break;
        case 'c':
            e.type = deck::Type::Cue; onUiEvent(e);
            e.pressed = false; break;
        case 'w': ESP_LOGI(TAG, "waveform test mode %d", ui::cycleWaveTestMode()); continue;
        default: continue;
        }
        onUiEvent(e);
    }
}

extern "C" void app_main(void)
{
    s_events   = xQueueCreate(64, sizeof(deck::Event));
    s_loadReq  = xQueueCreate(1, sizeof(int));
    s_loadDone = xQueueCreate(1, sizeof(bool));
    s_select   = xQueueCreate(8, sizeof(int));
    s_report   = xQueueCreate(1, sizeof(Report));
    if (!sd::begin()) ESP_LOGE(TAG, "no SD card — nothing to play");
    s_stream.begin(12 * 1024 * 1024);
    library::scan(sd::kMount);
    xTaskCreatePinnedToCore(loaderTask, "loader", 32768, nullptr, 4, nullptr, 0);
    s_audioUp = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 10, nullptr, 1);
    xSemaphoreTake(s_audioUp, portMAX_DELAY);  // the screen's touch shares the codec's I2C bus
    usb_midi::begin(onMidiPacket);
    ui::begin(library::tracks(), sd::kMount, s_stream, onUiEvent, onUiSelect);
    xTaskCreatePinnedToCore(reporterTask, "report", 6144, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(serialKeysTask, "keys", 3072, nullptr, 2, nullptr, 0);
    ESP_LOGI(TAG, "browse with the knob, LOAD A to load, PLAY to play");
}
