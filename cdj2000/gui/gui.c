// The CDJ-2000's display processor (an ADSP-BF531): our program for it.
//
// At boot it shows the splash (tools/splash.py, in its flash at +1 MB) from
// the first frame.  Then it draws the deck's screen from what the main board
// sends over SPORT1 (common/screen_link.h): a scrolling waveform round a
// fixed playhead, with the beat grid, the cue and loop; the whole track's
// overview with where the playhead is; the time left, the tempo, the BPM and
// the beat in the bar.  It keeps the track's peaks itself, and moves the
// playhead on between updates by its own cycle counter, so the waveform
// scrolls smoothly at its frame rate while the main board sends only ~30
// small updates a second (the link runs at 2.9 Mbit/s).
//
// Two framebuffers in SDRAM: DMA0 feeds the PPI from one (2-D, autobuffer)
// while we draw the other, then its start address is swapped, which takes
// at its next frame.  Register layout: the ADSP-BF531 hardware reference.
// See docs/screen.md.
#include <stdint.h>
#include "screen_link.h"
#include "font.h"

#define MMR16(off) (*(volatile uint16_t *)(0xFFC00000u + (off)))
#define MMR32(off) (*(volatile uint32_t *)(0xFFC00000u + (off)))
enum {
    DMA0_START_ADDR = 0xC04, DMA0_CONFIG = 0xC08, DMA0_X_COUNT = 0xC10, DMA0_X_MODIFY = 0xC14,
    DMA0_Y_COUNT = 0xC18, DMA0_Y_MODIFY = 0xC1C,
    DMA3_START_ADDR = 0xCC4, DMA3_CONFIG = 0xCC8, DMA3_X_COUNT = 0xCD0, DMA3_X_MODIFY = 0xCD4,
    DMA3_IRQ_STATUS = 0xCE8,
    SPORT1_RCR1 = 0x920,
    PPI_CONTROL = 0x1000, PPI_COUNT = 0x1008, PPI_DELAY = 0x100C, PPI_FRAME = 0x1010,
};
enum { W = 480, H = 255 };

#define FLASH       0x20000000u
#define SPLASH_AT   (FLASH + 0x100000u)
static uint16_t *const fbs[2] = {(uint16_t *)0x00100000u, (uint16_t *)0x00140000u};
static uint8_t *const peaks = (uint8_t *)0x00200000u;       // up to 1 M peaks: 50 min at 128 frames a peak
#define PEAKS_MAX   0x100000u
static uint8_t *const rx = (uint8_t *)0x00300000u;           // the receive buffer, one packet

void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

// ── time: the core's cycle counter, 400 MHz ──
static uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }
#define CYCLES_PER_MS 400000u

// ── colours (RGB555) ──
#define RGB(r, g, b) ((uint16_t)(((r) >> 3) << 10 | ((g) >> 3) << 5 | ((b) >> 3)))
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

static uint16_t *fb;                    // the one being drawn

static void fill(int x0, int y0, int x1, int y1, uint16_t c)      // [x0, x1) x [y0, y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) {
        uint16_t *p = fb + y * W;
        for (int x = x0; x < x1; x++) p[x] = c;
    }
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a)          // a 0..255
{
    const unsigned r = ((fg >> 10 & 31) * a + (bg >> 10 & 31) * (255 - a)) / 255;
    const unsigned g = ((fg >> 5 & 31) * a + (bg >> 5 & 31) * (255 - a)) / 255;
    const unsigned b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
    return (uint16_t)(r << 10 | g << 5 | b);
}

static int text_width(const font *f, const char *s)
{
    int w = 0;
    for (; *s; s++) if (*s >= 32 && *s < 127) w += f->w[*s - 32];
    return w;
}

// Text at (x, y), its cell's top; on a plain background `bg`.  Returns the x after it.
static int text(const font *f, int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        const int c = *s - 32, w = f->w[c];
        const uint8_t *g = f->data + f->off[c];
        for (int r = 0; r < f->h; r++) {
            if (y + r < 0 || y + r >= H) continue;
            uint16_t *p = fb + (y + r) * W;
            for (int i = 0; i < w; i++) {
                const unsigned a = g[r * w + i];
                if (a && x + i >= 0 && x + i < W) p[x + i] = a == 255 ? fg : blend(fg, bg, a);
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

// ── numbers into text, without a libc ──
static char *put_u(char *p, uint32_t v, int digits)       // zero-padded to `digits`
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v || n < digits);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void fmt_time(char *p, uint32_t ms, int minus)      // "-3:21.4"
{
    if (minus) *p++ = '-';
    p = put_u(p, ms / 60000, 1);
    *p++ = ':';
    p = put_u(p, ms / 1000 % 60, 2);
    *p++ = '.';
    put_u(p, ms / 100 % 10, 1);
}

static void fmt_signed_c(char *p, int v, const char *unit)  // hundredths: "+1.25%"
{
    *p++ = v < 0 ? '-' : '+';
    if (v < 0) v = -v;
    p = put_u(p, (uint32_t)v / 100, 1);
    *p++ = '.';
    p = put_u(p, (uint32_t)v % 100, 2);
    while (*unit) *p++ = *unit++;
    *p = 0;
}

static void fmt_c(char *p, uint32_t v)                      // hundredths to one decimal: "126.0"
{
    p = put_u(p, v / 100, 1);
    *p++ = '.';
    put_u(p, v % 100 / 10, 1);
}

// ── what the main board has told us ──
static sl_state st;
static sl_track tr;
static uint32_t st_at;                  // cycles when `st` came
static int have_track, have_state;

static void take(const uint8_t *pkt)
{
    const sl_header *h = (const sl_header *)pkt;
    if (h->magic != SL_MAGIC) return;
    const uint8_t *body = pkt + sizeof(sl_header);
    switch (h->type) {
    case SL_STATE:
        memcpy(&st, body, sizeof st);
        st_at = cycles();
        have_state = 1;
        break;
    case SL_TRACK:
        memcpy(&tr, body, sizeof tr);
        tr.title[sizeof tr.title - 1] = 0;
        memset(peaks, 0, tr.peaks < PEAKS_MAX ? tr.peaks : PEAKS_MAX);
        have_track = 1;
        break;
    case SL_PEAKS: {
        const sl_peaks *pk = (const sl_peaks *)body;
        uint32_t n = pk->count > SL_PEAKS_MAX ? SL_PEAKS_MAX : pk->count;
        if (pk->first >= PEAKS_MAX) break;
        if (pk->first + n > PEAKS_MAX) n = PEAKS_MAX - pk->first;
        memcpy(peaks + pk->first, pk->v, n);
        break;
    }
    }
}

// SPORT1's receive DMA (channel 3): one packet at a time, exactly its size.
static void rx_arm(void)
{
    MMR32(DMA3_START_ADDR) = (uint32_t)rx;
    MMR16(DMA3_X_COUNT) = SL_PACKET / 2;
    MMR16(DMA3_X_MODIFY) = 2;
    MMR16(DMA3_CONFIG) = 0x0080 | 0x0004 | 0x0002 | 0x0001;   // DI_EN | WDSIZE 16 | WNR (to memory) | DMAEN
}

static void rx_poll(void)
{
    for (int k = 0; k < 16 && (MMR16(DMA3_IRQ_STATUS) & 1); k++) {     // DMA_DONE: a packet is in
        MMR16(DMA3_IRQ_STATUS) = 1;
        MMR16(DMA3_CONFIG) = 0;
        take(rx);
        rx_arm();
    }
}

// ── the screen ──
// Layout (480 x 234 visible):
//   0-22    the title; PAUSE, REV, LOOP
//   26-126  the waveform round the playhead (x = HEAD_X), the grid, cue, hot cues, loop
//   132-156 the overview: the whole track, where we are, the cue
//   164-234 REMAIN, the beat in the bar, TEMPO, BPM
enum { HEAD_X = 200, WAVE_Y0 = 26, WAVE_Y1 = 126, OV_Y0 = 132, OV_Y1 = 156, INFO_Y = 164 };
#define WAVE_MS_PER_PX 9                // about 4.3 s across

static uint32_t now_ms(void)
{
    if (!(st.flags & SL_PLAYING)) return st.pos_ms;
    const uint32_t dt = (cycles() - st_at) / CYCLES_PER_MS;             // ms since the update
    const uint32_t ahead = (uint32_t)((uint64_t)dt * (uint32_t)(st.speed_ppm < 0 ? 0 : st.speed_ppm) / 1000000u);
    const uint32_t p = (st.flags & SL_REVERSE) ? (st.pos_ms > ahead ? st.pos_ms - ahead : 0) : st.pos_ms + ahead;
    return p > tr.length_ms ? tr.length_ms : p;
}

static uint32_t peak_of_ms(uint32_t ms)
{
    return tr.rate ? (uint32_t)((uint64_t)ms * tr.rate / 1000u / (tr.peak_frames ? tr.peak_frames : 128)) : 0;
}

static int x_of(uint32_t ms, uint32_t pos)
{
    return HEAD_X + ((int32_t)(ms - pos)) / WAVE_MS_PER_PX;
}

static void draw_wave(uint32_t pos)
{
    const int mid = (WAVE_Y0 + WAVE_Y1) / 2, half = (WAVE_Y1 - WAVE_Y0) / 2 - 2;
    fill(0, WAVE_Y0, W, WAVE_Y1, C_BG);
    if ((st.flags & SL_LOOPING) && st.loop_out_ms > st.loop_in_ms)
        fill(x_of(st.loop_in_ms, pos), WAVE_Y0, x_of(st.loop_out_ms, pos), WAVE_Y1, C_LOOP);
    for (int x = 0; x < W; x++) {
        const int32_t t = (int32_t)pos + (x - HEAD_X) * WAVE_MS_PER_PX;
        if (t < 0 || (uint32_t)t >= tr.length_ms) continue;
        const uint32_t a = peak_of_ms((uint32_t)t), b = peak_of_ms((uint32_t)t + WAVE_MS_PER_PX);
        unsigned v = 0;
        for (uint32_t i = a; i <= b && i < PEAKS_MAX; i++) if (peaks[i] > v) v = peaks[i];
        const int h = (int)(v * half / 255);
        if (!h) continue;
        const uint16_t c = x < HEAD_X ? C_PLAYED : C_WAVE;
        uint16_t *p = fb + (mid - h) * W + x;
        for (int y = -h; y <= h; y++, p += W) *p = (y > -h / 3 && y < h / 3 && x >= HEAD_X) ? C_WAVEHI : c;
    }
    // The grid: a tick at each beat, the bar's first beat brighter and longer.
    if (tr.beat_us) {
        const int64_t first = tr.first_beat_ms;
        const int64_t view0 = (int64_t)pos - HEAD_X * WAVE_MS_PER_PX, view1 = view0 + W * WAVE_MS_PER_PX;
        int64_t k = (view0 - first) * 1000 / (int64_t)tr.beat_us;
        if (k < 0) k = 0;
        for (;; k++) {
            const int64_t ms = first + k * (int64_t)tr.beat_us / 1000;
            if (ms > view1) break;
            const int x = x_of((uint32_t)ms, pos);
            const int bar = (k % 4) == 0;
            fill(x, WAVE_Y0, x + 1, WAVE_Y0 + (bar ? 9 : 5), bar ? C_BAR : C_BEAT);
            fill(x, WAVE_Y1 - (bar ? 9 : 5), x + 1, WAVE_Y1, bar ? C_BAR : C_BEAT);
        }
    }
    const int cx = x_of(st.cue_ms, pos);                 // the cue: a triangle on the top edge
    for (int i = 0; i < 6; i++) fill(cx - 5 + i, WAVE_Y0 + i, cx + 6 - i, WAVE_Y0 + i + 1, C_CUE);
    for (int k = 0; k < 3; k++)
        if (st.hot_ms[k] != 0xFFFFFFFFu) {
            const int hx = x_of(st.hot_ms[k], pos);
            fill(hx, WAVE_Y1 - 12, hx + 2, WAVE_Y1, C_LIME);
            char l[2] = {(char)('A' + k), 0};
            text(&font_label, hx + 3, WAVE_Y1 - 13, l, C_LIME, C_BG);
        }
    fill(HEAD_X - 1, WAVE_Y0, HEAD_X + 1, WAVE_Y1, C_HEAD);   // the playhead
}

static void draw_overview(uint32_t pos)
{
    const int mid = (OV_Y0 + OV_Y1) / 2, half = (OV_Y1 - OV_Y0) / 2 - 1;
    fill(0, OV_Y0, W, OV_Y1, C_PANEL);
    if (!tr.length_ms || !tr.peaks) return;
    const uint32_t total = tr.peaks < PEAKS_MAX ? tr.peaks : PEAKS_MAX;
    const int here = (int)((uint64_t)pos * W / tr.length_ms);
    for (int x = 0; x < W; x++) {
        const uint32_t a = (uint32_t)((uint64_t)x * total / W), b = (uint32_t)((uint64_t)(x + 1) * total / W);
        unsigned v = 0;
        for (uint32_t i = a; i < b; i += 4) if (peaks[i] > v) v = peaks[i];      // every 4th: fast enough, close enough
        const int h = (int)(v * half / 255);
        fill(x, mid - h, x + 1, mid + h + 1, x < here ? C_PLAYED : C_WAVE);
    }
    const int cx = (int)((uint64_t)st.cue_ms * W / tr.length_ms);
    fill(cx, OV_Y0, cx + 1, OV_Y1, C_CUE);
    fill(here - 1, OV_Y0 - 2, here + 1, OV_Y1 + 2, C_TEXT);
}

static void draw_info(uint32_t pos)
{
    char s[24];
    fill(0, INFO_Y, W, H, C_BG);
    text(&font_label, 8, INFO_Y, "REMAIN", C_DIM, C_BG);
    fmt_time(s, tr.length_ms > pos ? tr.length_ms - pos : 0, 1);
    text(&font_big, 6, INFO_Y + 14, s, C_TEXT, C_BG);
    // The beat in the bar: four boxes, the current one lit (the first red).
    for (int i = 0; i < 4; i++) {
        const int x = 184 + i * 18;
        const int on = st.beat_in_bar == i + 1;
        fill(x, INFO_Y + 24, x + 14, INFO_Y + 38, on ? (i == 0 ? C_HEAD : C_TEXT) : C_PANEL);
    }
    text(&font_label, 272, INFO_Y, "TEMPO", C_DIM, C_BG);
    if (st.range) {
        char r[8];
        if (st.range >= 100) { r[0] = 'W'; r[1] = 'I'; r[2] = 'D'; r[3] = 'E'; r[4] = 0; }
        else { char *p = put_u(r, st.range, 1); *p++ = '%'; *p = 0; }
        text(&font_label, 272 + text_width(&font_label, "TEMPO ") + 2, INFO_Y, r, C_DIM, C_BG);
    }
    fmt_signed_c(s, (st.flags & SL_RESET) ? 0 : st.tempo_c, "%");
    text(&font_small, 272, INFO_Y + 16, s, C_TEXT, C_BG);
    if (st.flags & SL_KEYLOCK) text(&font_label, 272, INFO_Y + 36, "MASTER TEMPO", C_LIME, C_BG);
    if (st.flags & SL_RESET) text(&font_label, 272, INFO_Y + 50, "TEMPO RESET", C_CUE, C_BG);
    text_right(&font_label, W - 8, INFO_Y, "BPM", C_DIM, C_BG);
    if (st.bpm_c) { fmt_c(s, st.bpm_c); text_right(&font_big, W - 6, INFO_Y + 14, s, C_TEXT, C_BG); }
}

static void draw_top(void)
{
    fill(0, 0, W, 22, C_PANEL);
    fill(0, 0, 3, 22, C_LIME);                               // audiodestrukt's lime, a sliver
    text(&font_small, 8, 3, have_track ? tr.title : "no track", have_track ? C_TEXT : C_DIM, C_PANEL);
    int x = W - 8;
    if (st.flags & SL_LOOPING) { x -= text_width(&font_label, "LOOP"); text(&font_label, x, 5, "LOOP", C_LIME, C_PANEL); x -= 10; }
    if (st.flags & SL_REVERSE) { x -= text_width(&font_label, "REV"); text(&font_label, x, 5, "REV", C_HEAD, C_PANEL); x -= 10; }
    if (!(st.flags & SL_PLAYING) && (st.flags & SL_LOADED)) { x -= text_width(&font_label, "PAUSE"); text(&font_label, x, 5, "PAUSE", C_DIM, C_PANEL); }
}

static void draw(void)
{
    const uint32_t pos = now_ms();
    draw_top();
    draw_wave(pos);
    fill(0, WAVE_Y1, W, OV_Y0, C_BG);
    draw_overview(pos);
    fill(0, OV_Y1, W, INFO_Y, C_BG);
    draw_info(pos);
}

static void ppi_start(uint16_t *first)
{
    MMR32(DMA0_START_ADDR) = (uint32_t)first;
    MMR16(DMA0_X_COUNT) = W;
    MMR16(DMA0_X_MODIFY) = 2;
    MMR16(DMA0_Y_COUNT) = H;
    MMR16(DMA0_Y_MODIFY) = 2;
    MMR16(DMA0_CONFIG) = 0x1000 | 0x0010 | 0x0004 | 0x0001;   // FLOW autobuffer | DMA2D | WDSIZE 16 | DMAEN
    // ⚠ The panel's own sync (its timers, PPI_DELAY) is to be matched on a deck.
    MMR16(PPI_COUNT) = W - 1;
    MMR16(PPI_DELAY) = 0;
    MMR16(PPI_FRAME) = H;
    MMR16(PPI_CONTROL) = 0x3800 | 0x000C | 0x0002 | 0x0001;   // DLEN 16 | XFR_TYPE | PORT_DIR out | PORT_EN
}

void main(void)
{
    // ⚠ On a deck, the PLL and the SDRAM controller want setting up first;
    // the emulator has SDRAM from power-on.  See docs/screen.md.
    __asm__ volatile("R0 = SYSCFG; BITSET(R0, 1); SYSCFG = R0;" ::: "R0");   // CCEN: the cycle counter runs

    // The splash, from the first frame.
    const uint32_t *src = (const uint32_t *)SPLASH_AT;
    uint32_t *dst = (uint32_t *)fbs[0];
    for (unsigned i = 0; i < W * H / 2; i++) dst[i] = src[i];
    ppi_start(fbs[0]);

    MMR16(SPORT1_RCR1) = 0x0001;                             // RSPEN: the receiver on
    rx_arm();
    int shown = 0;
    uint32_t next = cycles();
    for (;;) {
        rx_poll();
        if (!have_state || !have_track) continue;            // the splash stays until a track is loaded
        if ((int32_t)(cycles() - next) < 0) continue;
        next = cycles() + CYCLES_PER_MS * 1000 / 30;         // 30 frames a second
        fb = fbs[shown ^ 1];
        draw();
        shown ^= 1;
        MMR32(DMA0_START_ADDR) = (uint32_t)fb;               // shown from the PPI's next frame
    }
}
