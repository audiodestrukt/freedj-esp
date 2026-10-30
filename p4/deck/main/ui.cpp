#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "stream.h"

namespace ui {

static const char* TAG = "ui";

// Layout (480 x 320):
//   y   0.. 34  track name ............................. state chip
//   y  36.. 90  elapsed (big)      -remaining           tempo % / KEY
//   y  98..112  progress bar with cue marker
//   y 120..264  track list, or the scrolling waveform while playing
//   y 270..316  [ CUE ] [ PLAY ] [ LOAD ]
constexpr int kMargin = 10;
constexpr int kBarY = 100, kBarH = 10, kBarW = display::W - 2 * kMargin;

static const lv_color_t kBg     = lv_color_hex(0x0B0D10);
static const lv_color_t kPanel  = lv_color_hex(0x1A1E24);
static const lv_color_t kText   = lv_color_hex(0xE8EAED);
static const lv_color_t kDim    = lv_color_hex(0x8A9099);
static const lv_color_t kGreen  = lv_color_hex(0x2FBF71);
static const lv_color_t kOrange = lv_color_hex(0xF29D38);
static const lv_color_t kBlue   = lv_color_hex(0x3D7EFF);

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static State s_pub;           // written by publish(), read by refresh()
static State s_shown;         // what's on screen now
static bool  s_first = true;

static EventFn  s_onEvent;
static SelectFn s_onSelect;

static lv_obj_t *s_name, *s_state, *s_time, *s_remain, *s_tempo, *s_key;
static lv_obj_t *s_bar, *s_cueMark, *s_list, *s_cueBtn, *s_playBtn;
static std::vector<lv_obj_t*> s_rows;

// Scrolling waveform: playhead fixed in the middle, track moves right to
// left. Shown while playing, unless the list was used in the last few
// seconds. Drawn into a canvas straight from the stream's peak bins.
constexpr int kWaveY = 132, kWaveH = 100;
constexpr int kBinsPerPx = 4;                  // 512 frames/px: ~5.6 s across at 44.1 kHz
constexpr uint32_t kListLingerMs = 3000;
static const TrackStream* s_stream;
static lv_obj_t* s_wave;
static uint16_t* s_waveBuf;                    // RGB565, W x kWaveH, PSRAM
static uint32_t s_lastListUse;                 // lv_tick of last select/scroll
static bool s_waveShown;
static uint32_t s_waveFrames, s_waveT0;         // fps measurement
static volatile int s_waveTest;                 // see cycleWaveTestMode()

int cycleWaveTestMode() { s_waveTest = (s_waveTest + 1) % 3; return s_waveTest; }

void publish(const State& s)
{
    taskENTER_CRITICAL(&s_mux);
    s_pub = s;
    taskEXIT_CRITICAL(&s_mux);
}

static void send(deck::Type t, bool pressed)
{
    deck::Event e;
    e.type = t;
    e.deck = deck::DeckA;
    e.pressed = pressed;
    s_onEvent(e);
}

// Tap a row to select it; tap the selected row again to load it.
static void onRow(lv_event_t* e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == s_shown.selected) send(deck::Type::Load, true);
    else s_onSelect(i);
}

static void onCue(lv_event_t* e)
{
    const lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) send(deck::Type::Cue, true);
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) send(deck::Type::Cue, false);
}

static void onWaveTap(lv_event_t*) { s_lastListUse = lv_tick_get(); }

static void onPlay(lv_event_t*) { send(deck::Type::PlayPause, true); }
static void onLoad(lv_event_t*) { send(deck::Type::Load, true); }

static void fmtTime(char* out, size_t n, float sec, bool tenths)
{
    if (sec < 0) sec = 0;
    const int m = (int)sec / 60;
    if (tenths) snprintf(out, n, "%d:%04.1f", m, fmodf(sec, 60));
    else        snprintf(out, n, "%d:%02d", m, (int)sec % 60);
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t* button(lv_obj_t* parent, const char* text, int x, lv_event_cb_t cb, lv_event_code_t code)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, 270);
    lv_obj_set_size(b, 146, 44);
    lv_obj_set_style_bg_color(b, kPanel, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_t* l = label(b, &lv_font_montserrat_20, kText);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, code, nullptr);
    return b;
}

static void build(const std::vector<std::string>& tracks, const char* prefix)
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_scrollable(scr, false);

    s_name = label(scr, &lv_font_montserrat_20, kText);
    lv_obj_set_pos(s_name, kMargin, 8);
    lv_obj_set_width(s_name, 340);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_DOTS);

    s_state = label(scr, &lv_font_montserrat_16, kBg);
    lv_obj_set_style_bg_opa(s_state, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_state, 4, 0);
    lv_obj_set_style_pad_hor(s_state, 8, 0);
    lv_obj_set_style_pad_ver(s_state, 3, 0);
    lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -kMargin, 8);

    s_time = label(scr, &lv_font_montserrat_40, kText);
    lv_obj_set_pos(s_time, kMargin, 42);

    s_remain = label(scr, &lv_font_montserrat_24, kDim);
    lv_obj_set_pos(s_remain, 200, 56);

    s_tempo = label(scr, &lv_font_montserrat_24, kText);
    lv_obj_align(s_tempo, LV_ALIGN_TOP_RIGHT, -kMargin, 44);

    s_key = label(scr, &lv_font_montserrat_16, kOrange);
    lv_label_set_text(s_key, "KEY LOCK");
    lv_obj_align(s_key, LV_ALIGN_TOP_RIGHT, -kMargin, 74);

    s_bar = lv_bar_create(scr);
    lv_obj_set_pos(s_bar, kMargin, kBarY);
    lv_obj_set_size(s_bar, kBarW, kBarH);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_bar, kPanel, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, kBlue, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_bar, 0, 0);

    s_cueMark = lv_obj_create(scr);
    lv_obj_set_size(s_cueMark, 3, kBarH + 8);
    lv_obj_set_style_bg_color(s_cueMark, kOrange, 0);
    lv_obj_set_style_border_width(s_cueMark, 0, 0);
    lv_obj_set_style_radius(s_cueMark, 0, 0);
    lv_obj_set_clickable(s_cueMark, false);

    s_list = lv_obj_create(scr);     // flex column of rows (lv_list is deprecated)
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_pos(s_list, 0, 120);
    lv_obj_set_size(s_list, display::W, 144);
    lv_obj_set_style_bg_color(s_list, kBg, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    const size_t skip = prefix ? strlen(prefix) : 0;
    for (size_t i = 0; i < tracks.size(); i++) {
        const char* path = tracks[i].c_str();
        if (strncmp(path, prefix, skip) == 0) path += skip;
        if (*path == '/') path++;
        lv_obj_t* row = lv_button_create(s_list);
        lv_obj_set_size(row, LV_PCT(100), 36);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_left(row, kMargin, 0);
        lv_obj_t* text = lv_label_create(row);
        lv_label_set_text(text, path);
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(text, LV_PCT(100));
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(row, kBg, 0);
        lv_obj_set_style_bg_color(row, kBlue, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(row, kText, 0);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_16, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_add_event_cb(row, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_rows.push_back(row);
    }
    if (tracks.empty()) {
        lv_obj_t* l = label(s_list, &lv_font_montserrat_16, kDim);
        lv_label_set_text(l, "  no tracks on the SD card");
    }

    s_waveBuf = (uint16_t*)heap_caps_malloc(display::W * kWaveH * 2, MALLOC_CAP_SPIRAM);
    s_wave = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_wave, s_waveBuf, display::W, kWaveH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_wave, 0, kWaveY);
    lv_obj_set_clickable(s_wave, true);
    lv_obj_add_event_cb(s_wave, onWaveTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(s_wave, true);

    s_cueBtn  = button(scr, "CUE",  kMargin,       onCue,  LV_EVENT_ALL);
    s_playBtn = button(scr, "PLAY", kMargin + 157, onPlay, LV_EVENT_PRESSED);
    button(scr, "LOAD", kMargin + 314, onLoad, LV_EVENT_CLICKED);
}

static void drawWave(const State& s)
{
    const int W = display::W, mid = kWaveH / 2;
    const uint16_t bg = lv_color_to_u16(kBg), played = lv_color_to_u16(lv_color_hex(0x27466F)),
                   ahead = lv_color_to_u16(lv_color_hex(0x4FA3FF)), axis = lv_color_to_u16(kPanel);
    const size_t framesPerPx = TrackStream::kPeakFrames * kBinsPerPx;
    const long centre = (long)(s.frame / framesPerPx);
    const long bins = (long)s_stream->peakBins();
    static int16_t h[display::W];
    static uint16_t col[display::W];
    for (int x = 0; x < W; x++) {
        const long px = centre + (x - W / 2);
        int p = 0;
        if (px >= 0) for (long b = px * kBinsPerPx, e = b + kBinsPerPx; b < e && b < bins; b++) {
            const int v = s_stream->peak(b);
            if (v > p) p = v;
        }
        h[x] = (int16_t)(p * (mid - 2) / 255);
        col[x] = px < centre ? played : ahead;
    }
    for (int y = 0; y < kWaveH; y++) {
        uint16_t* row = s_waveBuf + y * W;
        const int dy = abs(y - mid);
        for (int x = 0; x < W; x++) row[x] = dy <= h[x] ? col[x] : (dy == 0 ? axis : bg);
    }
    // Cue marker, then the playhead on top.
    if (s.loaded) {
        const long cx = (long)(s.cueFrame / framesPerPx) - centre + W / 2;
        if (cx >= 0 && cx < W) for (int y = 0; y < kWaveH; y++) s_waveBuf[y * W + cx] = lv_color_to_u16(kOrange);
    }
    for (int y = 0; y < kWaveH; y++) {
        s_waveBuf[y * W + W / 2 - 1] = 0xFFFF;
        s_waveBuf[y * W + W / 2] = 0xFFFF;
    }
    if (s_waveTest == 0) lv_obj_invalidate(s_wave);

    s_waveFrames++;
    const uint32_t now = lv_tick_get();
    if (now - s_waveT0 >= 5000) {
        ESP_LOGI(TAG, "waveform %.1f fps", s_waveFrames * 1000.0f / (now - s_waveT0));
        s_waveFrames = 0;
        s_waveT0 = now;
    }
}

// Runs as fast as LVGL can flush; only redraws while the waveform is up.
static void waveTick(lv_timer_t*)
{
    State s;
    taskENTER_CRITICAL(&s_mux);
    s = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    const bool want = s.loaded && s.playing && lv_tick_elaps(s_lastListUse) > kListLingerMs;
    if (want != s_waveShown) {
        s_waveShown = want;
        lv_obj_set_hidden(s_wave, !want);
        lv_obj_set_hidden(s_list, want);
        s_waveFrames = 0;
        s_waveT0 = lv_tick_get();
    }
    if (want && s_waveTest != 2) drawWave(s);
}

static void refresh(lv_timer_t*)
{
    State s;
    taskENTER_CRITICAL(&s_mux);
    s = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    const State& o = s_shown;
    char buf[32];

    if (s_first || strcmp(s.name, o.name) || s.loading != o.loading || s.loaded != o.loaded)
        lv_label_set_text(s_name, s.loading ? "loading..." : s.loaded ? s.name : "no track loaded");

    const char* st; lv_color_t sc;
    if (s.loading)          { st = "LOADING"; sc = kDim; }
    else if (!s.loaded)     { st = "EMPTY";   sc = kDim; }
    else if (s.previewing)  { st = "CUE";     sc = kOrange; }
    else if (s.playing)     { st = "PLAYING"; sc = kGreen; }
    else                    { st = "PAUSED";  sc = kOrange; }
    if (s_first || strcmp(lv_label_get_text(s_state), st)) {
        lv_label_set_text(s_state, st);
        lv_obj_set_style_bg_color(s_state, sc, 0);
    }

    // Time at 0.1 s resolution; only touch the labels when the text changes.
    if (s_first || (int)(s.pos * 10) != (int)(o.pos * 10) || s.len != o.len || s.loaded != o.loaded) {
        fmtTime(buf, sizeof buf, s.loaded ? s.pos : 0, true);
        lv_label_set_text(s_time, buf);
        buf[0] = '-';
        fmtTime(buf + 1, sizeof buf - 1, s.loaded ? s.len - s.pos : 0, false);
        lv_label_set_text(s_remain, buf);
        lv_bar_set_value(s_bar, s.len > 0 ? (int)(1000 * s.pos / s.len) : 0, LV_ANIM_OFF);
    }
    if (s_first || s.cue != o.cue || s.len != o.len || s.loaded != o.loaded) {
        if (s.loaded && s.len > 0) {
            lv_obj_set_hidden(s_cueMark, false);
            lv_obj_set_pos(s_cueMark, kMargin + (int)(kBarW * s.cue / s.len) - 1, kBarY - 4);
        } else {
            lv_obj_set_hidden(s_cueMark, true);
        }
    }

    if (s_first || fabsf(s.tempoPct - o.tempoPct) >= 0.005f) {
        snprintf(buf, sizeof buf, "%+.2f%%", s.tempoPct);
        lv_label_set_text(s_tempo, buf);
    }
    if (s_first || s.keylock != o.keylock) {
        lv_obj_set_hidden(s_key, !s.keylock);
    }

    if (s_first || s.playing != o.playing || s.loaded != o.loaded || s.previewing != o.previewing) {
        lv_obj_set_style_bg_color(s_playBtn, s.playing && !s.previewing ? kGreen : kPanel, 0);
        lv_obj_set_style_bg_color(s_cueBtn, s.loaded && (!s.playing || s.previewing) ? kOrange : kPanel, 0);
    }

    if (!s_first && s.selected != o.selected) s_lastListUse = lv_tick_get();
    if ((s_first || s.selected != o.selected) && !s_rows.empty()) {
        if (o.selected >= 0 && o.selected < (int)s_rows.size()) lv_obj_set_checked(s_rows[o.selected], false);
        if (s.selected >= 0 && s.selected < (int)s_rows.size()) {
            lv_obj_set_checked(s_rows[s.selected], true);
            lv_obj_scroll_to_view(s_rows[s.selected], s_first ? LV_ANIM_OFF : LV_ANIM_ON);
        }
    }

    s_shown = s;
    s_first = false;
}

bool begin(const std::vector<std::string>& tracks, const char* prefix, const TrackStream& stream,
           EventFn onEvent, SelectFn onSelect)
{
    s_stream = &stream;
    s_onEvent = onEvent;
    s_onSelect = onSelect;
    if (!display::begin()) { ESP_LOGE(TAG, "no screen"); return false; }
    lvgl_port_lock(0);
    build(tracks, prefix);
    lv_timer_create(refresh, 50, nullptr);
    lv_timer_create(waveTick, 5, nullptr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "deck screen up, %u track(s)", (unsigned)tracks.size());
    return true;
}

} // namespace ui
