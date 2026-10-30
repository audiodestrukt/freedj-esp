// P4 deck, milestone 1: DJ2Go → deck events → audio on the board speaker.
// Plays a test signal (two tones + a 120 BPM click) instead of a track, so
// the control path, codec and amp can be checked before file playback.
//
// Deck A: PLAY toggles, pitch fader = ±8 % varispeed (tones and click rate
// both follow), jog = temporary nudge, CUE = back to the bar start,
// channel volume = level. Every event is logged.

#include <math.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio_out.h"
#include "deck_event.h"
#include "dj2go.h"
#include "sd.h"
#include "usb_midi.h"

static const char* TAG = "deck";

constexpr uint32_t kRate       = 44100;
constexpr int      kBlock      = 256;     // frames per write (~5.8 ms)
constexpr float    kTempoRange = 0.08f;   // ±8 %
constexpr float    kBpm        = 120.0f;

static QueueHandle_t s_events;
static dj2go::Decoder s_decoder;

static void logEvent(const deck::Event& e)
{
    const char* dk = e.deck == deck::DeckA ? "A" : e.deck == deck::DeckB ? "B" : "-";
    switch (e.type) {
    case deck::Type::Jog: case deck::Type::Browse:
        ESP_LOGI(TAG, "%s %-11s %+d", dk, deck::typeName(e.type), e.delta); break;
    case deck::Type::TempoFader: case deck::Type::Volume: case deck::Type::Crossfader:
    case deck::Type::MasterVolume: case deck::Type::PhonesVolume:
        ESP_LOGI(TAG, "%s %-11s %.3f", dk, deck::typeName(e.type), e.position); break;
    default:
        ESP_LOGI(TAG, "%s %-11s %s", dk, deck::typeName(e.type), e.pressed ? "down" : "up");
    }
}

// USB task context: decode and hand off; never block the USB client.
static void onMidiPacket(const uint8_t p[4])
{
    deck::Event ev[2];
    int n = s_decoder.decodeUsbPacket(p, ev, 2);
    for (int i = 0; i < n; i++) xQueueSend(s_events, &ev[i], 0);
}

struct DeckState {
    bool   playing   = false;
    float  tempo     = 1.0f;   // from the pitch fader
    float  nudge     = 0.0f;   // from the jog, decays back to 0
    float  gain      = 0.5f;   // channel volume until the fader reports
    double phaseLo   = 0, phaseHi = 0;  // tone oscillators, cycles
    double beatPos   = 0;      // beats since cue
};

static void apply(DeckState& d, const deck::Event& e)
{
    logEvent(e);
    if (e.deck != deck::DeckA) return;
    switch (e.type) {
    case deck::Type::PlayPause:  if (e.pressed) d.playing = !d.playing; break;
    case deck::Type::Cue:        if (e.pressed) d.beatPos = 0; break;
    case deck::Type::TempoFader: d.tempo = 1.0f + (e.position - 0.5f) * 2.0f * kTempoRange; break;
    case deck::Type::Jog:        d.nudge += e.delta * 0.004f; break;
    case deck::Type::Volume:     d.gain = e.position; break;
    default: break;
    }
}

static void audioTask(void*)
{
    static int16_t buf[kBlock * 2];
    DeckState d;
    uint32_t lastReport = 0;
    for (;;) {
        deck::Event e;
        while (xQueueReceive(s_events, &e, 0) == pdTRUE) apply(d, e);

        if (d.nudge > 0.3f) d.nudge = 0.3f;
        if (d.nudge < -0.3f) d.nudge = -0.3f;
        const float speed = d.tempo + d.nudge;
        d.nudge *= 0.97f;  // ~0.2 s decay at this block rate

        for (int i = 0; i < kBlock; i++) {
            float s = 0;
            if (d.playing) {
                const double step = speed / (double)kRate;
                d.phaseLo += 220.0 * step;       // A3
                d.phaseHi += 329.63 * step;      // E4
                d.beatPos += kBpm / 60.0 * step;
                const double inBeat = d.beatPos - floor(d.beatPos);
                const float click = inBeat < 0.03 ? (float)(1.0 - inBeat / 0.03) : 0.0f;
                const bool downbeat = ((long)d.beatPos % 4) == 0;
                s = 0.25f * sinf(2 * M_PI * (d.phaseLo - floor(d.phaseLo)))
                  + 0.15f * sinf(2 * M_PI * (d.phaseHi - floor(d.phaseHi)))
                  + click * (downbeat ? 0.6f : 0.35f) * sinf(2 * M_PI * 1800.0 * inBeat / (kBpm / 60.0));
            }
            const int16_t v = (int16_t)(s * d.gain * 32767.0f * 0.8f);
            buf[2 * i] = v;
            buf[2 * i + 1] = v;
        }
        audio_out::write(buf, kBlock);

        const uint32_t now = xTaskGetTickCount();
        if (d.playing && now - lastReport > pdMS_TO_TICKS(2000)) {
            lastReport = now;
            ESP_LOGI(TAG, "playing  speed %.4f  (%+.2f %%)  beat %.1f", speed, (speed - 1) * 100, d.beatPos);
        }
    }
}

extern "C" void app_main(void)
{
    s_events = xQueueCreate(64, sizeof(deck::Event));
    if (!audio_out::begin(kRate, 70)) ESP_LOGE(TAG, "audio init failed — continuing for MIDI");
    xTaskCreate(audioTask, "audio", 8192, nullptr, 10, nullptr);
    usb_midi::begin(onMidiPacket);
    ESP_LOGI(TAG, "ready: deck A PLAY to start the test signal");
    if (sd::begin()) sd::listAudio();
}
