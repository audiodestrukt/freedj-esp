// P4 deck: browse a rekordbox USB stick and the SD card with the DJ2Go or the
// touch screen, stream a WAV/MP3 through a PSRAM window, and play it
// CDJ-style on the board speaker. The screen (ui.cpp) shows the list, time,
// tempo and state. On Pro DJ Link (prolink.cpp) it follows the tempo master
// when SYNC is on, using the track's rekordbox beat grid.
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
//   SYNC             beat sync to the Link tempo master on/off (needs a
//                    rekordbox track); keylock is on the screen's KEY chip
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
#include "esp_heap_caps.h"
#include "driver/uart.h"

#include "audio_out.h"
#include "deck_event.h"
#include "dj2go.h"
#include "library.h"
#include "net.h"
#include "prolink.h"
#include "rekordbox.h"
#include "sd.h"
#include "stream.h"
#include "ui.h"
#include "usb_midi.h"
#include "usb_stick.h"
#include "wsola.h"

static const char* TAG = "deck";

constexpr uint32_t kRate       = 44100;
constexpr int      kBlock      = 256;    // frames per write (~5.8 ms)
constexpr float    kTempoRange = 0.08f;  // ±8 %
constexpr int      kScrubFrames = 441;   // paused jog: 10 ms per tick
constexpr uint8_t  kLinkPlayer = 2;      // our Pro DJ Link player number
// Audio queued between the audio task and the speaker: the I2S driver's
// default 6 DMA buffers x 240 frames, plus about half a block in flight.
constexpr int      kOutLatencyFrames = 6 * 240 + kBlock / 2;
// Beat sync: fraction of the phase error corrected per second-ish, and the
// most the tempo may bend to do it.
constexpr float    kSyncGain = 0.3f, kSyncMaxBend = 0.03f;

static QueueHandle_t s_events;      // deck::Event, USB task → audio task
static QueueHandle_t s_loadReq;     // int track index, audio task → loader
static QueueHandle_t s_loadDone;    // bool ok, loader → audio task
static QueueHandle_t s_select;      // int track index, screen → audio task
static QueueHandle_t s_seekReq;     // float 0..1, overview needle search → audio task (latest wins)
static TrackStream s_stream;        // deck A
static dj2go::Decoder s_decoder;
static wsola::Wsola s_wsola;        // ~40 KB, keep out of task stacks
// Beat grid of the loaded track, double-buffered because the screen draws
// from it too: the loader (or the test hook) fills the spare one and the audio
// task swaps it in, so nobody ever reads a grid that is being rewritten.
static rekordbox::Grid s_gridBuf[2];
static rekordbox::Grid* s_gridLive = &s_gridBuf[0];
static rekordbox::Grid& grid() { return *s_gridLive; }
static rekordbox::Grid& spareGrid() { return s_gridLive == &s_gridBuf[0] ? s_gridBuf[1] : s_gridBuf[0]; }
static void swapGrid() { s_gridLive = &spareGrid(); ui::setGrid(s_gridLive); }
static volatile int s_syncOffsetMs; // serial [ / ]: trim our phase against the master
static volatile int s_fakeGridBpm;  // serial g: test grid for a track without rekordbox analysis

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
            spareGrid().beats.clear();
            spareGrid().preview.clear();
            if (ok && !library::anlz(index).empty())
                rekordbox::readAnlz(library::anlz(index).c_str(), nullptr, &spareGrid().beats, &spareGrid().preview);
            // Prefill ~1 s before handing over so PLAY starts clean.
            while (ok && s_stream.buffered() < 44100 && s_stream.service()) {}
            xQueueSend(s_loadDone, &ok, portMAX_DELAY);
        }
        if (!s_stream.service()) vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
        // Full speed until ~8 s are decoded ahead (after a load or a needle
        // jump), then pace the rest of the window. Unpaced, refilling 70 s
        // of MP3 from the stick held core 0 at 100 % and the screen at 9 fps
        // for several seconds.
        else if (s_stream.ahead() > 8 * s_stream.sampleRate()) vTaskDelay(pdMS_TO_TICKS(15));
    }
}

struct Deck {
    bool   loaded    = false;
    bool   loading   = false;
    bool   playing   = false;
    bool   previewing = false;  // CUE held from pause
    bool   keylock   = false;
    bool   sync      = false;   // follow the Link tempo master
    bool   alignNext = false;   // snap our beat phase to the master before the next block
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
             library::displayName(d.selected).c_str());
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
        if (e.pressed && d.loaded) {
            d.playing = !d.playing; d.previewing = false;
            if (d.playing && d.sync) d.alignNext = true;   // start on the master's beat
        }
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
        if (e.pressed) {
            d.sync = !d.sync;
            // Off: the deck keeps the synced tempo until the fader moves.
            if (d.sync) d.alignNext = d.playing;
        }
        break;
    case Type::KeyLock:
        if (e.pressed) { d.keylock = !d.keylock; s_wsola.reset(); }
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

// ── Beat sync ────────────────────────────────────────────────────────────────
// Track time (ms) of what is coming out of the speaker now: the playhead
// minus the audio still queued in the DMA (and in WSOLA, in keylock).
static double s_wsolaLag;   // smoothed: the raw estimate is a sawtooth (input arrives in 512-frame chunks)

// Once per block. Approximate: input WSOLA holds (its 4096-frame buffer
// minus free space) beyond the middle of the frame it is windowing, plus
// output it has made but not handed over. Trim the rest with [ ].
static void stepWsolaLag(float speed)
{
    const int held = 4096 - s_wsola.inputCapacity() - wsola::Wsola::FRAME_N / 2;
    const double raw = (held > 0 ? held : 0) + s_wsola.outputAvailable() * speed;
    s_wsolaLag += (raw - s_wsolaLag) * 0.02;    // ~50 blocks, ~0.3 s
}

static double heardMs(const Deck& d, float speed)
{
    double lagOut = kOutLatencyFrames * speed;               // output-rate frames, in track time
    if (d.keylock) lagOut += s_wsolaLag;
    const double fileRate = s_stream.sampleRate();
    const double frames = d.pos - lagOut * fileRate / kRate;
    return frames * 1000.0 / fileRate + s_syncOffsetMs;
}

// Error between our beat phase and the master's at time `atUs`, in beats,
// -0.5..0.5. Positive: we are ahead.
static double phaseError(double ourBeat, const prolink::Master& m, int64_t atUs)
{
    const double masterBeat = (atUs - m.beatUs) * (double)m.bpm / 60e6;   // beats since its last beat
    double e = ourBeat - masterBeat;
    e -= floor(e + 0.5);
    return e;
}

// The master's tempo as a speed for our track, taking half or double time
// when that's closer (a 70 BPM track against a 140 BPM master plays at 1.0).
static float syncSpeed(float masterBpm, float trackBpm)
{
    float s = masterBpm / trackBpm;
    if (s > 1.5f) s *= 0.5f;
    else if (s < 0.75f) s *= 2.0f;
    return s < 0.5f ? 0.5f : s > 2.0f ? 2.0f : s;
}

struct SyncStats { double errSumMs = 0, errMaxMs = 0; int n = 0; };
static SyncStats s_syncStats;

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
static void onUiSeek(float fraction) { xQueueOverwrite(s_seekReq, &fraction); }

static prolink::Master s_master;     // audio task's copy, refreshed every block

static void publishState(const Deck& d, float speed)
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
    u.tempoPct = (speed - 1.0f) * 100.0f;
    u.sync = d.sync;
    u.hasGrid = d.loaded && !grid().empty();
    if (u.hasGrid) u.bpm = grid().bpmAt(d.pos * 1000.0 / s_stream.sampleRate()) * speed;
    u.masterPlayer = s_master.present ? s_master.player : 0;
    u.masterBpm = s_master.bpm;
    // Phase meter: where we are in our beat/bar/phrase as heard, and where the
    // master is in its beat.
    if (u.hasGrid) {
        const double b = grid().beatAt(heardMs(d, speed));
        const long k = (long)floor(b);
        u.beatPhase = (float)(b - k);
        u.beatInBar = (uint8_t)grid().inBarAt(k);
        // Bars counted from the grid's first downbeat.
        const long firstDown = (5 - grid().inBarAt(0)) % 4;     // index of the first beat 1
        const long bar = (long)floor((k - firstDown) / 4.0);
        u.barInPhrase = (uint8_t)(((bar % 4) + 4) % 4 + 1);
    }
    if (s_master.present && s_master.beating) {
        const double since = (esp_timer_get_time() - s_master.beatUs) * (double)s_master.bpm / 60e6;
        const long k = (long)floor(since);
        u.masterPhase = (float)(since - k);
        u.masterInBar = (uint8_t)((s_master.beatInBar - 1 + k) % 4 + 1);
    }
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
    bool     synced = false;           // sync on and following a playing master
    double   syncErrAvgMs = 0, syncErrMaxMs = 0;
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
        ESP_LOGI(TAG, "  mem: internal %u KB free (lowest %u KB, largest block %u KB), PSRAM %u KB free",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        ESP_LOGI(TAG, "  top: %s", top);
        static char link[160];
        prolink::summary(link, sizeof link);
        ESP_LOGI(TAG, "  link: %s", link);
        if (r.synced)
            ESP_LOGI(TAG, "  sync: phase error avg %.1f / max %.1f ms (offset %+d ms)",
                     r.syncErrAvgMs, r.syncErrMaxMs, s_syncOffsetMs);
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
            swapGrid();
            if (ok) ESP_LOGI(TAG, "beat grid: %s", grid().empty() ? "none (SYNC needs a rekordbox track)" : "yes");
            if (!ok) {
                ESP_LOGE(TAG, "load failed");
            } else {
                ESP_LOGI(TAG, "ready: %s (%d:%02d) — PLAY", s_stream.name(),
                         (int)s_stream.seconds() / 60, (int)s_stream.seconds() % 60);
            }
        }

        float needle;
        if (xQueueReceive(s_seekReq, &needle, 0) == pdTRUE && d.loaded && !d.loading) {
            // Needle search. Synced and playing: land back on the master's beat.
            seek(d, needle * s_stream.frames());
            if (d.sync && d.playing) d.alignNext = true;
        }
        if (s_fakeGridBpm && d.loaded) {
            // Test hook: a constant grid from the track start. The loader
            // only touches the spare grid while loading, and we're loaded.
            const double period = 60000.0 / s_fakeGridBpm;
            auto& g = spareGrid().beats;
            g.clear();
            for (uint32_t i = 0; i * period < s_stream.seconds() * 1000 + period; i++)
                g.push_back({(uint32_t)(i * period + 0.5), (uint16_t)(s_fakeGridBpm * 100), (uint8_t)(i % 4 + 1)});
            swapGrid();
            s_fakeGridBpm = 0;
        }
        if (d.nudge > 0.3f) d.nudge = 0.3f;
        if (d.nudge < -0.3f) d.nudge = -0.3f;
        prolink::master(s_master);
        float speed = d.tempo + d.nudge;
        if (d.keylock) stepWsolaLag(speed);
        bool synced = false;
        if (d.sync && d.loaded && !grid().empty() && s_master.present && s_master.bpm > 0) {
            const float trackBpm = grid().bpmAt(d.pos * 1000.0 / s_stream.sampleRate());
            const float base = syncSpeed(s_master.bpm, trackBpm);
            d.tempo = base;                           // stays here if sync is switched off
            speed = base + d.nudge;
            if (s_master.beating && d.playing && !d.previewing) {
                const int64_t now = esp_timer_get_time();
                int16_t probe[2];
                if (d.alignNext && !s_stream.get((size_t)d.pos, probe, 1)) {
                    // Nothing decoded here yet (just jumped): snap once the
                    // audio is there, or the master moves on while we wait.
                } else if (d.alignNext) {
                    // Jump so our next block lands on the master's phase. At
                    // play start nothing is audible yet, so this is silent.
                    const double audibleUs = now + kOutLatencyFrames * 1e6 / kRate;
                    const double here = d.pos * 1000.0 / s_stream.sampleRate() + s_syncOffsetMs;
                    const double e = phaseError(grid().beatAt(here), s_master, (int64_t)audibleUs);
                    double target = grid().beatAt(here) - e;
                    if (grid().msAt(target) - s_syncOffsetMs < 0) target += 1;   // can't go before 0:00: next beat instead
                    seek(d, (grid().msAt(target) - s_syncOffsetMs) * s_stream.sampleRate() / 1000.0);
                    d.alignNext = false;
                } else {
                    // Then keep it there by bending the tempo a little.
                    const double e = phaseError(grid().beatAt(heardMs(d, speed)), s_master, now);
                    float bend = (float)(-kSyncGain * e);
                    if (bend > kSyncMaxBend) bend = kSyncMaxBend;
                    if (bend < -kSyncMaxBend) bend = -kSyncMaxBend;
                    speed = base * (1 + bend) + d.nudge;
                    const double errMs = fabs(e) * 60000.0 / s_master.bpm;
                    s_syncStats.errSumMs += errMs;
                    if (errMs > s_syncStats.errMaxMs) s_syncStats.errMaxMs = errMs;
                    s_syncStats.n++;
                    synced = true;
                }
            }
        }
        d.nudge *= 0.97f;

        if (d.playing && d.loaded) {
            const bool fed = d.keylock ? renderKeylock(d, speed, buf) : renderPitched(d, speed, buf);
            if (!fed) {
                underruns++;
                if (d.sync) d.alignNext = true;   // we stalled; the master didn't: re-snap
            }
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

        publishState(d, speed);  // every block (~170 Hz) so the waveform scrolls smoothly
        if ((r.blocks & 7) == 0) {   // what Link peers see, ~20 Hz
            prolink::Ours o;
            o.loaded = d.loaded;
            o.playing = d.playing;
            o.sync = d.sync;
            o.pitch = speed;
            if (d.loaded && !grid().empty()) {
                const double ms = heardMs(d, speed);
                const double b = grid().beatAt(ms);
                o.trackBpm = grid().bpmAt(ms);
                if (b >= 0) { o.beat = (uint32_t)b + 1; o.beatInBar = (uint8_t)grid().inBarAt((long)b); }
            }
            prolink::publish(o);
        }

        if (tWrote - r.t0 >= 2000000) {
            r.t1 = tWrote;
            r.playing = d.playing && d.loaded;
            r.pos = d.loaded ? d.pos / s_stream.sampleRate() : 0;
            r.speedPct = (speed - 1) * 100;
            r.keylock = d.keylock;
            r.synced = synced && s_syncStats.n > 0;
            if (r.synced) { r.syncErrAvgMs = s_syncStats.errSumMs / s_syncStats.n; r.syncErrMaxMs = s_syncStats.errMaxMs; }
            s_syncStats = SyncStats{};
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
//   k keylock          s sync             [ ] phase trim -/+ 2 ms
//   g give the loaded track a 120 BPM test grid (for SYNC without rekordbox)
//   v switch the phase meter view (beat boxes / alignment ticks)
//   n needle search to the middle of the track
//   w cycle waveform test mode (normal / no flush / off)
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
        case 'k': e.type = deck::Type::KeyLock; break;
        case 's': e.type = deck::Type::Sync; break;
        case 'v': ui::togglePhaseView(); continue;
        case 'n': onUiSeek(0.5f); continue;   // needle search to the middle (test)
        case 'g': s_fakeGridBpm = 120; ESP_LOGI(TAG, "test grid: 120 BPM from the start"); continue;
        case '[': s_syncOffsetMs -= 2; ESP_LOGI(TAG, "sync offset %+d ms", s_syncOffsetMs); continue;
        case ']': s_syncOffsetMs += 2; ESP_LOGI(TAG, "sync offset %+d ms", s_syncOffsetMs); continue;
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
    s_seekReq  = xQueueCreate(1, sizeof(float));
    s_report   = xQueueCreate(1, sizeof(Report));
    const bool sdOk = sd::begin();
    if (!sdOk) ESP_LOGW(TAG, "no SD card");
    s_stream.begin(12 * 1024 * 1024);
    // USB first: the track list needs to know whether a rekordbox stick is in.
    usb_midi::begin(onMidiPacket);
    usb_stick::begin();
    const bool stick = usb_stick::waitMounted(3000);
    library::scan(sdOk ? sd::kMount : nullptr, stick ? usb_stick::kMount : nullptr);
    ESP_LOGI(TAG, "after scan: internal %u KB free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    // Network from core 0, so the EMAC interrupt stays off the audio core.
    net::begin();
    prolink::begin(kLinkPlayer, "P4-DECK");
    xTaskCreatePinnedToCore(loaderTask, "loader", 32768, nullptr, 4, nullptr, 0);
    s_audioUp = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 10, nullptr, 1);
    xSemaphoreTake(s_audioUp, portMAX_DELAY);  // the screen's touch shares the codec's I2C bus
    ui::begin(s_stream, onUiEvent, onUiSelect, onUiSeek);
    xTaskCreatePinnedToCore(reporterTask, "report", 6144, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(serialKeysTask, "keys", 3072, nullptr, 2, nullptr, 0);
    ESP_LOGI(TAG, "memory free: internal %u KB (largest %u KB), PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "browse with the knob, LOAD A to load, PLAY to play");
}
