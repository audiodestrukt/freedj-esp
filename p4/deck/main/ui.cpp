#include "ui.h"

#include <math.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "library.h"
#include "rekordbox.h"
#include "stream.h"

extern "C" volatile bool lvgl_mem_psram;   // lvgl_mem.c

namespace ui {

static const char* TAG = "ui";

// Layout (480 x 320):
//   y   0.. 34  track name ................... SYNC  KEY  state chip
//   y  36.. 90  elapsed (big)  -remaining               tempo %
//                                          master BPM | our BPM
//   y  94..116  track overview (rekordbox preview), touch to jump there
//   y 120..264  track list; while playing, the phase meter (120..152) and
//               the scrolling waveform (156..236) instead
//   y 270..316  [ CUE ] [ PLAY ] [ LOAD ]
constexpr int kMargin = 10;
// Track overview: the whole track in one strip, from rekordbox's preview
// waveform (PWAV). Touch or drag it to jump (needle search).
constexpr int kOvX = kMargin, kOvY = 94, kOvW = display::W - 2 * kMargin, kOvH = 22;

static const lv_color_t kBg     = lv_color_hex(0x0B0D10);
static const lv_color_t kPanel  = lv_color_hex(0x1A1E24);
static const lv_color_t kText   = lv_color_hex(0xE8EAED);
static const lv_color_t kDim    = lv_color_hex(0x8A9099);
static const lv_color_t kGreen  = lv_color_hex(0x2FBF71);
static const lv_color_t kOrange = lv_color_hex(0xF29D38);
static const lv_color_t kBlue   = lv_color_hex(0x3D7EFF);
static const lv_color_t kDimBlue = lv_color_hex(0x243A5C);
static const lv_color_t kGold   = lv_color_hex(0xE8C547);

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static State s_pub;           // written by publish(), read by refresh()
static State s_shown;         // what's on screen now
static bool  s_first = true;

static EventFn  s_onEvent;
static SelectFn s_onSelect;
static SeekFn   s_onSeek;

static lv_obj_t *s_name, *s_state, *s_time, *s_remain, *s_tempo, *s_key, *s_sync, *s_bpm;
static lv_obj_t *s_ov, *s_list, *s_cueBtn, *s_playBtn;
static uint16_t* s_ovBuf;            // kOvW x kOvH RGB565, PSRAM; redrawn when the playhead moves a pixel
static std::vector<lv_obj_t*> s_rows;

// Scrolling waveform: playhead fixed in the middle, track moves right to
// left. Shown while playing, unless the list was used in the last few
// seconds. Drawn into a canvas straight from the stream's peak bins.
constexpr int kWaveY = 156, kWaveH = 80;
constexpr int kBinsPerPx = 4;                  // 512 frames/px: ~5.6 s across at 44.1 kHz
constexpr uint32_t kListLingerMs = 3000;
static const TrackStream* s_stream;
static std::atomic<const rekordbox::Grid*> s_grid{nullptr};
static lv_obj_t* s_wave;
static uint16_t* s_waveBuf;                    // RGB565, W x kWaveH, PSRAM
static uint32_t s_lastListUse;                 // lv_tick of last select/scroll
static bool s_waveShown;
// fps measurement: s_screenFrames counts frames LVGL actually rendered and
// sent to the panel; s_waveFrames counts waveform redraws, which can outrun
// the panel when LVGL merges several into one frame.
static uint32_t s_waveFrames, s_waveT0;
static std::atomic<uint32_t> s_screenFrames;
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
static void onSyncTap(lv_event_t*) { send(deck::Type::Sync, true); }
static void onKeyTap(lv_event_t*) { send(deck::Type::KeyLock, true); }
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

// A tappable status chip in the header (SYNC, KEY).
static lv_obj_t* chip(lv_obj_t* parent, const char* text, lv_event_cb_t cb)
{
    lv_obj_t* c = label(parent, &lv_font_montserrat_16, kDim);
    lv_label_set_text(c, text);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, kPanel, 0);
    lv_obj_set_style_radius(c, 4, 0);
    lv_obj_set_style_pad_hor(c, 8, 0);
    lv_obj_set_style_pad_ver(c, 3, 0);
    lv_obj_set_ext_click_area(c, 8);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, cb, LV_EVENT_PRESSED, nullptr);
    return c;
}

// Needle search: press or drag on the overview. Only sends when the touch
// moves to a new pixel, so a held finger doesn't keep re-seeking.
static void onOverviewTouch(lv_event_t* e)
{
    static int lastX = -1;
    if (!s_shown.loaded) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int x = p.x - kOvX;
    x = x < 0 ? 0 : x >= kOvW ? kOvW - 1 : x;
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) lastX = -1;   // a new touch always seeks
    if (x == lastX) return;
    lastX = x;
    s_onSeek((float)x / (kOvW - 1));
}

static const rekordbox::Grid* overviewGrid();

// The overview strip. rekordbox's preview columns are stretched to the
// width; the played part is dimmed, as on the XDJ. Without a preview (SD
// tracks) it's a plain bar. White line = playhead, orange = cue.
static void drawOverview(const State& s, int playX, int cueX)
{
    const uint16_t bg = lv_color_to_u16(kBg), base = lv_color_to_u16(kPanel);
    for (int i = 0; i < kOvW * kOvH; i++) s_ovBuf[i] = bg;
    const rekordbox::Grid* g = overviewGrid();
    const bool wave = s.loaded && g && !g->preview.empty();
    for (int x = 0; x < kOvW; x++) {
        int h = 2;
        uint16_t c = base;
        if (wave) {
            const uint8_t v = g->preview[(size_t)x * g->preview.size() / kOvW];
            h = (v & 31) * kOvH / 31;
            if (h < 1) h = 1;
            // Whiteness 0..7 blends blue toward white, like the XDJ preview.
            const int w = v >> 5;
            const lv_color_t col = lv_color_mix(lv_color_white(), kBlue, (lv_opa_t)(w * 255 / 7 / 2));
            c = lv_color_to_u16(x < playX ? lv_color_mix(col, kBg, 90) : col);
        } else if (s.loaded && x < playX) {
            c = lv_color_to_u16(kDimBlue);
        }
        for (int y = kOvH - h; y < kOvH; y++) s_ovBuf[y * kOvW + x] = c;
    }
    if (s.loaded) {
        if (cueX >= 0 && cueX < kOvW) for (int y = 0; y < kOvH; y++) s_ovBuf[y * kOvW + cueX] = lv_color_to_u16(kOrange);
        if (playX >= 0 && playX < kOvW) for (int y = 0; y < kOvH; y++) {
            s_ovBuf[y * kOvW + playX] = 0xFFFF;
            if (playX + 1 < kOvW) s_ovBuf[y * kOvW + playX + 1] = 0xFFFF;
        }
    }
    lv_obj_invalidate(s_ov);
}

static void buildMeter(lv_obj_t* scr);   // phase meter, below

static void build()
{
    const auto& tracks = library::tracks();
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_scrollable(scr, false);

    s_name = label(scr, &lv_font_montserrat_20, kText);
    lv_obj_set_pos(s_name, kMargin, 8);
    // DOTS only truncates a label with a fixed height; without one a long
    // name wraps onto the time counter.
    lv_obj_set_size(s_name, 236, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_DOTS);

    s_state = label(scr, &lv_font_montserrat_16, kBg);
    lv_obj_set_style_bg_opa(s_state, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_state, 4, 0);
    lv_obj_set_style_pad_hor(s_state, 8, 0);
    lv_obj_set_style_pad_ver(s_state, 3, 0);
    lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -kMargin, 8);
    lv_label_set_text(s_state, "PLAYING");      // widest text: chips sit left of it
    lv_obj_update_layout(s_state);
    s_key = chip(scr, "KEY", onKeyTap);
    lv_obj_align_to(s_key, s_state, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    s_sync = chip(scr, "SYNC", onSyncTap);
    lv_obj_align_to(s_sync, s_key, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    s_time = label(scr, &lv_font_montserrat_40, kText);
    lv_obj_set_pos(s_time, kMargin, 42);

    s_remain = label(scr, &lv_font_montserrat_24, kDim);
    lv_obj_set_pos(s_remain, 160, 56);

    s_tempo = label(scr, &lv_font_montserrat_24, kText);
    lv_obj_align(s_tempo, LV_ALIGN_TOP_RIGHT, -kMargin, 44);

    s_bpm = label(scr, &lv_font_montserrat_16, kDim);
    lv_obj_align(s_bpm, LV_ALIGN_TOP_RIGHT, -kMargin, 76);

    s_ovBuf = (uint16_t*)heap_caps_malloc(kOvW * kOvH * 2, MALLOC_CAP_SPIRAM);
    s_ov = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_ov, s_ovBuf, kOvW, kOvH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_ov, kOvX, kOvY);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_ov, 6);
    lv_obj_add_event_cb(s_ov, onOverviewTouch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_ov, onOverviewTouch, LV_EVENT_PRESSING, nullptr);

    s_list = lv_obj_create(scr);     // flex column of rows (lv_list is deprecated)
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_pos(s_list, 0, 120);
    lv_obj_set_size(s_list, display::W, 144);
    lv_obj_set_style_bg_color(s_list, kBg, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    // One shared style for every row: per-object style setters allocate a
    // local style each, which adds up over hundreds of tracks.
    lvgl_mem_psram = true;          // the rows live for the whole session: PSRAM
    static lv_style_t rowStyle, rowChecked;
    lv_style_init(&rowStyle);
    lv_style_set_radius(&rowStyle, 0);
    lv_style_set_shadow_width(&rowStyle, 0);
    lv_style_set_pad_left(&rowStyle, kMargin);
    lv_style_set_bg_color(&rowStyle, kBg);
    lv_style_set_text_color(&rowStyle, kText);
    lv_style_set_text_font(&rowStyle, &lv_font_montserrat_16);
    lv_style_set_border_width(&rowStyle, 0);
    lv_style_init(&rowChecked);
    lv_style_set_bg_color(&rowChecked, kBlue);
    for (size_t i = 0; i < tracks.size(); i++) {
        lv_obj_t* row = lv_button_create(s_list);
        lv_obj_set_size(row, LV_PCT(100), 36);
        lv_obj_add_style(row, &rowStyle, 0);
        lv_obj_add_style(row, &rowChecked, LV_STATE_CHECKED);
        lv_obj_t* text = lv_label_create(row);
        lv_label_set_text(text, library::displayName(i).c_str());
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(text, LV_PCT(100), lv_font_get_line_height(&lv_font_montserrat_16));
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(row, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_rows.push_back(row);
    }
    lvgl_mem_psram = false;
    if (tracks.empty()) {
        lv_obj_t* l = label(s_list, &lv_font_montserrat_16, kDim);
        lv_label_set_text(l, "  no tracks on the USB stick or SD card");
    }

    s_waveBuf = (uint16_t*)heap_caps_malloc(display::W * kWaveH * 2, MALLOC_CAP_SPIRAM);
    s_wave = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_wave, s_waveBuf, display::W, kWaveH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_wave, 0, kWaveY);
    lv_obj_set_clickable(s_wave, true);
    lv_obj_add_event_cb(s_wave, onWaveTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(s_wave, true);

    buildMeter(scr);

    s_cueBtn  = button(scr, "CUE",  kMargin,       onCue,  LV_EVENT_ALL);
    s_playBtn = button(scr, "PLAY", kMargin + 157, onPlay, LV_EVENT_PRESSED);
    button(scr, "LOAD", kMargin + 314, onLoad, LV_EVENT_CLICKED);
}

// ── Phase meter, XDJ style ───────────────────────────────────────────────────
// Two views, switched by tapping the meter, as on the unit:
//  beat display:   two rows of four boxes. Top: bar within the 4-bar phrase.
//                  Bottom: beat within the bar (orange if we were master).
//  alignment view: "MASTER n", then beat ticks scrolling under a fixed
//                  playhead. Top row the master, bottom row us; the tall tick
//                  is each deck's own downbeat. Phase offset reads as
//                  horizontal displacement.
// The "Bars" readouts on the right count down to the next memory / hot cue
// on an XDJ. There are no cues yet, so they show dashes, like the unit does
// for a track without cues.
constexpr int kMeterY = 120, kMeterH = 32, kMeterW = 330, kTagW = 92;
constexpr int kTickW = kMeterW - kTagW, kTickH = kMeterH;
static lv_obj_t *s_meter, *s_boxes[8], *s_ticks, *s_tag, *s_tagNum;
static uint16_t* s_tickBuf;           // kTickW x kTickH RGB565, PSRAM (internal RAM is the scarce one)
static bool s_tickView;
static int s_litTop = -1, s_litBot = -1;

static void onMeterTap(lv_event_t*)
{
    s_tickView = !s_tickView;
    for (auto* b : s_boxes) lv_obj_set_hidden(b, s_tickView);
    lv_obj_set_hidden(s_ticks, !s_tickView);
    lv_obj_set_hidden(s_tag, !s_tickView);
    lv_obj_set_hidden(s_tagNum, !s_tickView);
    s_litTop = s_litBot = -1;
}

static void buildMeter(lv_obj_t* scr)
{
    s_meter = lv_obj_create(scr);
    lv_obj_remove_style_all(s_meter);
    lv_obj_set_pos(s_meter, 0, kMeterY);
    lv_obj_set_size(s_meter, display::W, kMeterH);
    lv_obj_add_flag(s_meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_meter, onMeterTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(s_meter, true);

    const int gap = 4, cw = (kMeterW - 3 * gap) / 4, rh = (kMeterH - gap) / 2;
    for (int i = 0; i < 8; i++) {
        lv_obj_t* b = lv_obj_create(s_meter);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, kMargin + (i % 4) * (cw + gap), i < 4 ? 0 : rh + gap);
        lv_obj_set_size(b, cw, rh);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, i < 4 ? kDimBlue : kBlue, 0);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, kBg, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        s_boxes[i] = b;
    }

    s_tag = label(s_meter, &lv_font_montserrat_16, kText);
    lv_label_set_text(s_tag, "MASTER");
    lv_obj_set_pos(s_tag, kMargin, 7);
    s_tagNum = label(s_meter, &lv_font_montserrat_16, kBg);
    lv_obj_set_style_bg_opa(s_tagNum, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_tagNum, kGold, 0);
    lv_obj_set_style_pad_hor(s_tagNum, 5, 0);
    lv_obj_set_pos(s_tagNum, kMargin + 64, 6);
    s_tickBuf = (uint16_t*)heap_caps_malloc(kTickW * kTickH * 2, MALLOC_CAP_SPIRAM);
    s_ticks = lv_canvas_create(s_meter);
    lv_canvas_set_buffer(s_ticks, s_tickBuf, kTickW, kTickH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_ticks, kTagW, 0);
    lv_obj_remove_flag(s_ticks, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_hidden(s_ticks, true);
    lv_obj_set_hidden(s_tag, true);
    lv_obj_set_hidden(s_tagNum, true);

    const int bx = kMeterW + 2 * kMargin;
    for (int row = 0; row < 2; row++) {
        const lv_color_t c = row ? kBlue : kOrange;
        lv_obj_t* v = label(s_meter, &lv_font_montserrat_16, c);
        lv_label_set_text(v, "--.-");
        lv_obj_set_pos(v, bx, row * (rh + gap) - 2);
        lv_obj_t* u = label(s_meter, &lv_font_montserrat_16, c);
        lv_label_set_text(u, "Bars");
        lv_obj_set_pos(u, bx + 48, row * (rh + gap) - 2);
    }
}

static void drawTicks(const State& s)
{
    const uint16_t bg = lv_color_to_u16(kBg), base = lv_color_to_u16(kPanel), dim = lv_color_to_u16(kDim),
                   white = 0xFFFF;
    for (int i = 0; i < kTickW * kTickH; i++) s_tickBuf[i] = bg;
    const float beatPx = kTickW / 8.0f;          // 8 beats across
    const int cx = kTickW / 2;
    struct Row { int y; float phase; int inBar; uint16_t bar; };
    const Row rows[2] = {
        {kTickH * 30 / 100, s.masterPhase, s.masterInBar, lv_color_to_u16(kOrange)},   // master
        {kTickH * 80 / 100, s.beatPhase,   s.beatInBar,   lv_color_to_u16(kBlue)},     // us
    };
    for (const Row& r : rows) {
        for (int x = 0; x < kTickW; x++) s_tickBuf[r.y * kTickW + x] = base;
        if (r.phase < 0 || r.inBar < 1) continue;
        for (int i = -5; i <= 5; i++) {
            const int x = (int)lroundf(cx + (i - r.phase) * beatPx);
            if (x < 0 || x + 1 >= kTickW) continue;
            const bool down = ((i + r.inBar - 1) % 4 + 4) % 4 == 0;
            const int len = down ? 10 : 5;
            const uint16_t c = down ? r.bar : dim;
            for (int y = r.y - len; y <= r.y; y++) {
                if (y < 0) continue;
                s_tickBuf[y * kTickW + x] = c;
                s_tickBuf[y * kTickW + x + 1] = c;
            }
        }
    }
    for (int y = 0; y < kTickH; y++) s_tickBuf[y * kTickW + cx] = white;
    lv_obj_invalidate(s_ticks);
}

static void updateMeter(const State& s)
{
    if (s_tickView) {
        static uint8_t shownMaster = 0xFF;
        if (s.masterPlayer != shownMaster) {
            shownMaster = s.masterPlayer;
            char b[4];
            snprintf(b, sizeof b, "%s", "-");
            if (shownMaster) snprintf(b, sizeof b, "%u", shownMaster);
            lv_label_set_text(s_tagNum, b);
        }
        drawTicks(s);
        return;
    }
    // Beat display: only touch the boxes when the lit cell changes.
    const int top = s.beatPhase >= 0 ? s.barInPhrase - 1 : -1;
    const int bot = s.beatPhase >= 0 ? s.beatInBar - 1 : -1;
    if (top != s_litTop) {
        if (s_litTop >= 0) lv_obj_set_style_bg_color(s_boxes[s_litTop], kBg, 0);
        if (top >= 0) lv_obj_set_style_bg_color(s_boxes[top], kBlue, 0);
        s_litTop = top;
    }
    if (bot != s_litBot) {
        if (s_litBot >= 0) lv_obj_set_style_bg_color(s_boxes[4 + s_litBot], kBg, 0);
        if (bot >= 0) lv_obj_set_style_bg_color(s_boxes[4 + bot], kBlue, 0);   // orange once we can be master
        s_litBot = bot;
    }
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
    // Beat grid, as on the XDJ: a short tick at the top and bottom edge for
    // every beat, longer and red for each bar's first beat.
    const rekordbox::Grid* g = s_grid.load();
    if (s.loaded && s.hasGrid && g && !g->empty() && s_stream->sampleRate()) {
        const double msPerPx = framesPerPx * 1000.0 / s_stream->sampleRate();
        const double leftMs = (centre - W / 2) * msPerPx, rightMs = (centre + W / 2) * msPerPx;
        const uint16_t beat = lv_color_to_u16(lv_color_hex(0xC8CCD2)), bar = lv_color_to_u16(lv_color_hex(0xE0453A));
        long i = (long)floor(g->beatAt(leftMs > 0 ? leftMs : 0));
        if (i < 0) i = 0;
        for (; i < (long)g->beats.size(); i++) {
            const auto& b = g->beats[i];
            if (b.ms > rightMs) break;
            const long x = (long)(b.ms / msPerPx) - centre + W / 2;
            if (x < 0 || x >= W) continue;
            const bool down = b.inBar == 1;
            const int len = down ? 14 : 7;
            const uint16_t c = down ? bar : beat;
            for (int y = 0; y < len; y++) {
                s_waveBuf[y * W + x] = c;
                s_waveBuf[(kWaveH - 1 - y) * W + x] = c;
            }
        }
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
}

static char s_stats[64] = "list";

const char* waveStats()
{
    if (!s_waveShown) return "list";
    const uint32_t now = lv_tick_get(), dt = now - s_waveT0;
    if (dt >= 1000) {
        snprintf(s_stats, sizeof s_stats, "screen %.1f fps, waveform drawn %.0f/s",
                 s_screenFrames.exchange(0) * 1000.0f / dt, s_waveFrames * 1000.0f / dt);
        s_waveFrames = 0;
        s_waveT0 = now;
    }
    return s_stats;
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
        lv_obj_set_hidden(s_meter, !want);
        lv_obj_set_hidden(s_list, want);
        s_waveFrames = 0;
        s_screenFrames = 0;
        s_waveT0 = lv_tick_get();
    }
    if (want && s_waveTest != 2) { drawWave(s); updateMeter(s); }
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
    }
    // Overview: redraw only when something on it moves by a pixel.
    {
        static int shownPlay = -2, shownCue = -2;
        static const void* shownGrid = (const void*)1;
        static bool shownLoaded = false;
        const int playX = s.loaded && s.len > 0 ? (int)((kOvW - 1) * s.pos / s.len) : -1;
        const int cueX = s.loaded && s.len > 0 ? (int)((kOvW - 1) * s.cue / s.len) : -1;
        const void* g = overviewGrid();
        if (s_first || playX != shownPlay || cueX != shownCue || g != shownGrid || s.loaded != shownLoaded) {
            drawOverview(s, playX, cueX);
            shownPlay = playX; shownCue = cueX; shownGrid = g; shownLoaded = s.loaded;
        }
    }

    if (s_first || fabsf(s.tempoPct - o.tempoPct) >= 0.005f) {
        snprintf(buf, sizeof buf, "%+.2f%%", s.tempoPct);
        lv_label_set_text(s_tempo, buf);
    }
    if (s_first || s.keylock != o.keylock) {
        lv_obj_set_style_bg_color(s_key, s.keylock ? kOrange : kPanel, 0);
        lv_obj_set_style_text_color(s_key, s.keylock ? kBg : kDim, 0);
    }
    if (s_first || s.sync != o.sync || s.masterPlayer != o.masterPlayer) {
        // Blue: synced to a master. Outline only: on, but no master to follow.
        const bool following = s.sync && s.masterPlayer;
        lv_obj_set_style_bg_color(s_sync, following ? kBlue : kPanel, 0);
        lv_obj_set_style_text_color(s_sync, following ? kText : s.sync ? kBlue : kDim, 0);
    }
    if (s_first || fabsf(s.bpm - o.bpm) >= 0.005f || fabsf(s.masterBpm - o.masterBpm) >= 0.005f ||
        s.masterPlayer != o.masterPlayer) {
        char ours[16] = "--- BPM";
        if (s.bpm > 0) snprintf(ours, sizeof ours, "%.2f BPM", s.bpm);
        if (s.masterPlayer) snprintf(buf, sizeof buf, "M%u %.2f | %s", s.masterPlayer, s.masterBpm, ours);
        else strlcpy(buf, ours, sizeof buf);
        lv_label_set_text(s_bpm, buf);
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

void setGrid(const rekordbox::Grid* grid) { s_grid.store(grid); }
static const rekordbox::Grid* overviewGrid() { return s_grid.load(); }

void togglePhaseView()
{
    lvgl_port_lock(0);
    onMeterTap(nullptr);
    lvgl_port_unlock();
}

bool begin(const TrackStream& stream, EventFn onEvent, SelectFn onSelect, SeekFn onSeek)
{
    s_stream = &stream;
    s_onEvent = onEvent;
    s_onSelect = onSelect;
    s_onSeek = onSeek;
    lv_display_t* disp = display::begin();
    if (!disp) { ESP_LOGE(TAG, "no screen"); return false; }
    lvgl_port_lock(0);
    build();
    lv_display_add_event_cb(disp, [](lv_event_t*) { s_screenFrames++; }, LV_EVENT_RENDER_READY, nullptr);
    lv_timer_create(refresh, 50, nullptr);
    lv_timer_create(waveTick, 5, nullptr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "deck screen up, %u track(s)", (unsigned)library::tracks().size());
    return true;
}

} // namespace ui
