/* The deck's screen (see deck_draw.h).  Moved here from gui/gui.c, where it
 * was the CDJ-2000's alone; the CDJ-2000's layout keeps every number it had,
 * so that screen is pixel for pixel what it was.  Plain C, no libc. */
#include "deck_draw.h"
#include "font.h"

/* ── colours ── */
#ifdef DD_RGB565
#define RGB(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)))
#else
#define RGB(r, g, b) ((uint16_t)(((r) >> 3) << 10 | ((g) >> 3) << 5 | ((b) >> 3)))
#endif
#define C_BG     RGB(0x0d, 0x0d, 0x0d)
#define C_PANEL  RGB(0x1a, 0x1a, 0x1e)
#define C_TEXT   RGB(0xf0, 0xf0, 0xf0)
#define C_DIM    RGB(0x8a, 0x8a, 0x8a)
#define C_LIME   RGB(0xc8, 0xff, 0x00)
#define C_WAVE   RGB(0x2a, 0x6e, 0xff)
#define C_WAVEHI RGB(0x9c, 0xc8, 0xff)
#define C_PLAYED RGB(0x55, 0x55, 0x60)
#define C_HEAD   RGB(0xff, 0x30, 0x30)
#define C_CUE    RGB(0xff, 0x96, 0x00)
#define C_BEAT   RGB(0x70, 0x70, 0x78)
#define C_BAR    RGB(0xe0, 0xe0, 0xe0)
#define C_LOOP   RGB(0x30, 0x60, 0x20)

/* Layout (the CDJ-2000's, 480 x 234 visible):
 *   0-22    the title; PAUSE, REV, LOOP
 *   26-126  the waveform round the playhead (x = 200), the grid, cue, hot cues, loop
 *   132-156 the overview: the whole track, where we are, the cue
 *   164-234 REMAIN, the beat in the bar, TEMPO, BPM
 * The XDJ-1000MK2's 800 x 480 has the same rows, about twice as tall, with
 * its fonts baked at 1.7 times the size. */
const dd_layout dd_cdj2000 = {
    .w = 480, .h = 255, .top_h = 22, .title_x = 8, .title_y = 3, .flag_y = 5,
    .head_x = 200, .wave_y0 = 26, .wave_y1 = 126, .ms_per_px = 9, .tick_bar = 9, .tick_beat = 5, .cue_tri = 6,
    .hot_h = 12, .hot_w = 2, .hot_dx = 3, .hot_dy = -13,
    .ov_y0 = 132, .ov_y1 = 156, .ov_mark = 2,
    .info_y = 164, .remain_x = 8, .big_dy = 14,
    .beat_x = 184, .beat_step = 18, .beat_box = 14, .beat_dy = 24,
    .tempo_x = 272, .tempo_dy = 16, .mt_dy = 36, .reset_dy = 50, .margin = 8,
};
const dd_layout dd_xdj1000mk2 = {
    .w = 800, .h = 480, .top_h = 40, .title_x = 14, .title_y = 6, .flag_y = 10,
    .head_x = 330, .wave_y0 = 48, .wave_y1 = 268, .ms_per_px = 6, .tick_bar = 16, .tick_beat = 9, .cue_tri = 10,
    .hot_h = 22, .hot_w = 3, .hot_dx = 5, .hot_dy = -24,
    .ov_y0 = 282, .ov_y1 = 330, .ov_mark = 3,
    .info_y = 346, .remain_x = 14, .big_dy = 24,
    .beat_x = 300, .beat_step = 32, .beat_box = 26, .beat_dy = 42,
    .tempo_x = 450, .tempo_dy = 28, .mt_dy = 64, .reset_dy = 88, .margin = 14,
};

static uint16_t *dd_fb;
static const dd_layout *dd_L;
static const sl_state *dd_st;
static const sl_track *dd_tr;
static const uint8_t *dd_peaks;
static uint32_t dd_peaks_max;

static void fill(int x0, int y0, int x1, int y1, uint16_t c)      /* [x0, x1) x [y0, y1) */
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dd_L->w) x1 = dd_L->w;
    if (y1 > dd_L->h) y1 = dd_L->h;
    for (int y = y0; y < y1; y++) {
        uint16_t *p = dd_fb + y * dd_L->w;
        for (int x = x0; x < x1; x++) p[x] = c;
    }
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a)          /* a 0..255 */
{
#ifdef DD_RGB565
    const unsigned r = ((fg >> 11 & 31) * a + (bg >> 11 & 31) * (255 - a)) / 255;
    const unsigned g = ((fg >> 5 & 63) * a + (bg >> 5 & 63) * (255 - a)) / 255;
    const unsigned b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
    return (uint16_t)(r << 11 | g << 5 | b);
#else
    const unsigned r = ((fg >> 10 & 31) * a + (bg >> 10 & 31) * (255 - a)) / 255;
    const unsigned g = ((fg >> 5 & 31) * a + (bg >> 5 & 31) * (255 - a)) / 255;
    const unsigned b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
    return (uint16_t)(r << 10 | g << 5 | b);
#endif
}

static int text_width(const font *f, const char *s)
{
    int w = 0;
    for (; *s; s++) if (*s >= 32 && *s < 127) w += f->w[*s - 32];
    return w;
}

/* Text at (x, y), its cell's top; on a plain background `bg`.  Returns the x after it. */
static int text(const font *f, int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        const int c = *s - 32, w = f->w[c];
        const uint8_t *g = f->data + f->off[c];
        for (int r = 0; r < f->h; r++) {
            if (y + r < 0 || y + r >= dd_L->h) continue;
            uint16_t *p = dd_fb + (y + r) * dd_L->w;
            for (int i = 0; i < w; i++) {
                const unsigned a = g[r * w + i];
                if (a && x + i >= 0 && x + i < dd_L->w) p[x + i] = a == 255 ? fg : blend(fg, bg, a);
            }
        }
        x += w;
    }
    return x;
}

static void text_right(const font *f, int xr, int y, const char *s, uint16_t fg, uint16_t bg)
{
    text(f, xr - text_width(f, s), y, s, fg, bg);
}

/* ── numbers into text, without a libc ── */
static char *put_u(char *p, uint32_t v, int digits)       /* zero-padded to `digits` */
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v || n < digits);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void fmt_time(char *p, uint32_t ms, int minus)      /* "-3:21.4" */
{
    if (minus) *p++ = '-';
    p = put_u(p, ms / 60000, 1);
    *p++ = ':';
    p = put_u(p, ms / 1000 % 60, 2);
    *p++ = '.';
    put_u(p, ms / 100 % 10, 1);
}

static void fmt_signed_c(char *p, int v, const char *unit)  /* hundredths: "+1.25%" */
{
    *p++ = v < 0 ? '-' : '+';
    if (v < 0) v = -v;
    p = put_u(p, (uint32_t)v / 100, 1);
    *p++ = '.';
    p = put_u(p, (uint32_t)v % 100, 2);
    while (*unit) *p++ = *unit++;
    *p = 0;
}

static void fmt_c(char *p, uint32_t v)                      /* hundredths to one decimal: "126.0" */
{
    p = put_u(p, v / 100, 1);
    *p++ = '.';
    put_u(p, v % 100 / 10, 1);
}

static uint32_t peak_of_ms(uint32_t ms)
{
    return dd_tr->rate ? (uint32_t)((uint64_t)ms * dd_tr->rate / 1000u / (dd_tr->peak_frames ? dd_tr->peak_frames : 128)) : 0;
}

static int x_of(uint32_t ms, uint32_t pos)
{
    return dd_L->head_x + ((int32_t)(ms - pos)) / dd_L->ms_per_px;
}

static void draw_wave(uint32_t pos)
{
    const int W = dd_L->w, Y0 = dd_L->wave_y0, Y1 = dd_L->wave_y1, HX = dd_L->head_x, MS = dd_L->ms_per_px;
    const int mid = (Y0 + Y1) / 2, half = (Y1 - Y0) / 2 - 2;
    fill(0, Y0, W, Y1, C_BG);
    if ((dd_st->flags & SL_LOOPING) && dd_st->loop_out_ms > dd_st->loop_in_ms)
        fill(x_of(dd_st->loop_in_ms, pos), Y0, x_of(dd_st->loop_out_ms, pos), Y1, C_LOOP);
    for (int x = 0; x < W; x++) {
        const int32_t t = (int32_t)pos + (x - HX) * MS;
        if (t < 0 || (uint32_t)t >= dd_tr->length_ms) continue;
        const uint32_t a = peak_of_ms((uint32_t)t), b = peak_of_ms((uint32_t)t + MS);
        unsigned v = 0;
        for (uint32_t i = a; i <= b && i < dd_peaks_max; i++) if (dd_peaks[i] > v) v = dd_peaks[i];
        const int h = (int)(v * half / 255);
        if (!h) continue;
        const uint16_t c = x < HX ? C_PLAYED : C_WAVE;
        uint16_t *p = dd_fb + (mid - h) * W + x;
        for (int y = -h; y <= h; y++, p += W) *p = (y > -h / 3 && y < h / 3 && x >= HX) ? C_WAVEHI : c;
    }
    /* The grid: a tick at each beat, the bar's first beat brighter and longer. */
    if (dd_tr->beat_us) {
        const int64_t first = dd_tr->first_beat_ms;
        const int64_t view0 = (int64_t)pos - HX * MS, view1 = view0 + W * MS;
        int64_t k = (view0 - first) * 1000 / (int64_t)dd_tr->beat_us;
        if (k < 0) k = 0;
        for (;; k++) {
            const int64_t ms = first + k * (int64_t)dd_tr->beat_us / 1000;
            if (ms > view1) break;
            const int x = x_of((uint32_t)ms, pos);
            const int bar = (k % 4) == 0, len = bar ? dd_L->tick_bar : dd_L->tick_beat;
            fill(x, Y0, x + 1, Y0 + len, bar ? C_BAR : C_BEAT);
            fill(x, Y1 - len, x + 1, Y1, bar ? C_BAR : C_BEAT);
        }
    }
    const int cx = x_of(dd_st->cue_ms, pos), n = dd_L->cue_tri;     /* the cue: a triangle on the top edge */
    for (int i = 0; i < n; i++) fill(cx - (n - 1) + i, Y0 + i, cx + n - i, Y0 + i + 1, C_CUE);
    for (int k = 0; k < 3; k++)
        if (dd_st->hot_ms[k] != 0xFFFFFFFFu) {
            const int hx = x_of(dd_st->hot_ms[k], pos);
            fill(hx, Y1 - dd_L->hot_h, hx + dd_L->hot_w, Y1, C_LIME);
            char l[2] = {(char)('A' + k), 0};
            text(&font_label, hx + dd_L->hot_dx, Y1 + dd_L->hot_dy, l, C_LIME, C_BG);
        }
    fill(HX - 1, Y0, HX + 1, Y1, C_HEAD);                  /* the playhead */
}

static void draw_overview(uint32_t pos)
{
    const int W = dd_L->w, Y0 = dd_L->ov_y0, Y1 = dd_L->ov_y1;
    const int mid = (Y0 + Y1) / 2, half = (Y1 - Y0) / 2 - 1;
    fill(0, Y0, W, Y1, C_PANEL);
    if (!dd_tr->length_ms || !dd_tr->peaks) return;
    const uint32_t total = dd_tr->peaks < dd_peaks_max ? dd_tr->peaks : dd_peaks_max;
    const int here = (int)((uint64_t)pos * W / dd_tr->length_ms);
    for (int x = 0; x < W; x++) {
        const uint32_t a = (uint32_t)((uint64_t)x * total / W), b = (uint32_t)((uint64_t)(x + 1) * total / W);
        unsigned v = 0;
        for (uint32_t i = a; i < b; i += 4) if (dd_peaks[i] > v) v = dd_peaks[i];      /* every 4th: fast enough, close enough */
        const int h = (int)(v * half / 255);
        fill(x, mid - h, x + 1, mid + h + 1, x < here ? C_PLAYED : C_WAVE);
    }
    const int cx = (int)((uint64_t)dd_st->cue_ms * W / dd_tr->length_ms);
    fill(cx, Y0, cx + 1, Y1, C_CUE);
    fill(here - 1, Y0 - dd_L->ov_mark, here + 1, Y1 + dd_L->ov_mark, C_TEXT);
}

static void draw_info(uint32_t pos)
{
    const int W = dd_L->w, Y = dd_L->info_y;
    char s[24];
    fill(0, Y, W, dd_L->h, C_BG);
    text(&font_label, dd_L->remain_x, Y, "REMAIN", C_DIM, C_BG);
    fmt_time(s, dd_tr->length_ms > pos ? dd_tr->length_ms - pos : 0, 1);
    text(&font_big, dd_L->remain_x - 2, Y + dd_L->big_dy, s, C_TEXT, C_BG);
    /* The beat in the bar: four boxes, the current one lit (the first red). */
    for (int i = 0; i < 4; i++) {
        const int x = dd_L->beat_x + i * dd_L->beat_step;
        const int on = dd_st->beat_in_bar == i + 1;
        fill(x, Y + dd_L->beat_dy, x + dd_L->beat_box, Y + dd_L->beat_dy + dd_L->beat_box, on ? (i == 0 ? C_HEAD : C_TEXT) : C_PANEL);
    }
    text(&font_label, dd_L->tempo_x, Y, "TEMPO", C_DIM, C_BG);
    if (dd_st->range) {
        char r[8];
        if (dd_st->range >= 100) { r[0] = 'W'; r[1] = 'I'; r[2] = 'D'; r[3] = 'E'; r[4] = 0; }
        else { char *p = put_u(r, dd_st->range, 1); *p++ = '%'; *p = 0; }
        text(&font_label, dd_L->tempo_x + text_width(&font_label, "TEMPO ") + 2, Y, r, C_DIM, C_BG);
    }
    fmt_signed_c(s, (dd_st->flags & SL_RESET) ? 0 : dd_st->tempo_c, "%");
    text(&font_small, dd_L->tempo_x, Y + dd_L->tempo_dy, s, C_TEXT, C_BG);
    if (dd_st->flags & SL_KEYLOCK) text(&font_label, dd_L->tempo_x, Y + dd_L->mt_dy, "MASTER TEMPO", C_LIME, C_BG);
    if (dd_st->flags & SL_RESET) text(&font_label, dd_L->tempo_x, Y + dd_L->reset_dy, "TEMPO RESET", C_CUE, C_BG);
    text_right(&font_label, W - dd_L->margin, Y, "BPM", C_DIM, C_BG);
    if (dd_st->bpm_c) { fmt_c(s, dd_st->bpm_c); text_right(&font_big, W - dd_L->margin + 2, Y + dd_L->big_dy, s, C_TEXT, C_BG); }
}

static void draw_top(int have_track)
{
    const int W = dd_L->w, gap = dd_L->margin + 2;
    fill(0, 0, W, dd_L->top_h, C_PANEL);
    fill(0, 0, 3, dd_L->top_h, C_LIME);                        /* audiodestrukt's lime, a sliver */
    text(&font_small, dd_L->title_x, dd_L->title_y, have_track ? dd_tr->title : "no track", have_track ? C_TEXT : C_DIM, C_PANEL);
    int x = W - dd_L->margin;
    if (dd_st->flags & SL_LOOPING) { x -= text_width(&font_label, "LOOP"); text(&font_label, x, dd_L->flag_y, "LOOP", C_LIME, C_PANEL); x -= gap; }
    if (dd_st->flags & SL_REVERSE) { x -= text_width(&font_label, "REV"); text(&font_label, x, dd_L->flag_y, "REV", C_HEAD, C_PANEL); x -= gap; }
    if (!(dd_st->flags & SL_PLAYING) && (dd_st->flags & SL_LOADED)) { x -= text_width(&font_label, "PAUSE"); text(&font_label, x, dd_L->flag_y, "PAUSE", C_DIM, C_PANEL); }
}

void dd_draw(const dd_frame *f, uint32_t pos)
{
    dd_fb = f->fb; dd_L = f->l; dd_st = f->st; dd_tr = f->tr; dd_peaks = f->peaks; dd_peaks_max = f->peaks_max;
    draw_top(f->have_track);
    draw_wave(pos);
    fill(0, dd_L->wave_y1, dd_L->w, dd_L->ov_y0, C_BG);
    draw_overview(pos);
    fill(0, dd_L->ov_y1, dd_L->w, dd_L->info_y, C_BG);
    draw_info(pos);
}
