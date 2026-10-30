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
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

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

// Keylock: feed WSOLA from the stream at 1×, let it stretch to `speed`.
static bool renderKeylock(Deck& d, float speed, int16_t* out)
{
    static int16_t feed[512 * 2];
    s_wsola.setSpeed(speed);
    int got = 0;
    while (got < kBlock) {
        got += s_wsola.pullFrames(out + 2 * got, kBlock - got);
        if (got >= kBlock) break;
        const size_t at = (size_t)d.pos;
        if (at >= s_stream.frames()) { memset(out + 2 * got, 0, (kBlock - got) * 4); d.playing = false; return true; }
        size_t n = s_wsola.inputCapacity();
        if (n > 512) n = 512;
        if (n > s_stream.frames() - at) n = s_stream.frames() - at;
        if (!s_stream.get(at, feed, n)) { memset(out + 2 * got, 0, (kBlock - got) * 4); return false; }
        d.pos += s_wsola.pushFrames(feed, n);
    }
    return true;
}

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
        strlcpy(u.name, s_stream.name(), sizeof u.name);
    }
    u.tempoPct = (d.tempo - 1.0f) * 100.0f;
    ui::publish(u);
}

static void audioTask(void*)
{
    static int16_t buf[kBlock * 2];
    Deck d;
    s_wsola.begin(2);
    logSelection(d);
    uint32_t lastReport = 0;
    uint32_t underruns = 0;
    uint32_t lastXruns = audio_out::xruns();
    int64_t tWrote = esp_timer_get_time(), worstUs = 0, tXrunLog = 0;

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
                if (s_stream.sampleRate() != kRate)
                    ESP_LOGW(TAG, "%u Hz track: varispeed resamples it; keylock will play at the wrong pitch",
                             (unsigned)s_stream.sampleRate());
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
        } else {
            memset(buf, 0, sizeof buf);
        }
        // Watch for the DAC running dry: report new xruns and the slowest
        // block of work (everything but the blocking write) since last time.
        const int64_t workUs = esp_timer_get_time() - tWrote;
        if (workUs > worstUs) worstUs = workUs;
        if (audio_out::xruns() != lastXruns && esp_timer_get_time() - tXrunLog > 1000000) {
            ESP_LOGW(TAG, "DAC ran dry %u time(s); slowest block of work %.1f ms",
                     (unsigned)(audio_out::xruns() - lastXruns), worstUs / 1000.0);
            lastXruns = audio_out::xruns();
            worstUs = 0;
            tXrunLog = esp_timer_get_time();
        }
        audio_out::write(buf, kBlock);
        tWrote = esp_timer_get_time();

        static int blocks;
        if (++blocks % 4 == 0) publishState(d);  // ~43 Hz; the screen redraws at 20

        const uint32_t now = xTaskGetTickCount();
        if (d.playing && now - lastReport > pdMS_TO_TICKS(2000)) {
            lastReport = now;
            const float t = d.pos / s_stream.sampleRate();
            ESP_LOGI(TAG, "%s  %d:%04.1f / %d:%04.1f  %+.2f %%%s  buffered %.0f s  underruns %u",
                     s_stream.name(), (int)t / 60, fmodf(t, 60),
                     (int)s_stream.seconds() / 60, fmodf(s_stream.seconds(), 60), (speed - 1) * 100,
                     d.keylock ? "  keylock" : "", s_stream.buffered() / 44100.0, (unsigned)underruns);
        }
    }
}

extern "C" void app_main(void)
{
    s_events   = xQueueCreate(64, sizeof(deck::Event));
    s_loadReq  = xQueueCreate(1, sizeof(int));
    s_loadDone = xQueueCreate(1, sizeof(bool));
    s_select   = xQueueCreate(8, sizeof(int));
    if (!audio_out::begin(kRate, 70)) ESP_LOGE(TAG, "audio init failed");
    if (!sd::begin()) ESP_LOGE(TAG, "no SD card — nothing to play");
    s_stream.begin(12 * 1024 * 1024);
    library::scan(sd::kMount);
    xTaskCreatePinnedToCore(loaderTask, "loader", 32768, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 10, nullptr, 1);
    usb_midi::begin(onMidiPacket);
    ui::begin(library::tracks(), sd::kMount, onUiEvent, onUiSelect);
    ESP_LOGI(TAG, "browse with the knob, LOAD A to load, PLAY to play");
}
