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

// The drawing itself is shared with the XDJ-1000MK2 (common/deck_draw.c,
// RGB555 here), compiled into this one file: the Blackfin build is a
// single translation unit.
#include "deck_draw.c"

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
static uint32_t now_ms(void)
{
    if (!(st.flags & SL_PLAYING)) return st.pos_ms;
    const uint32_t dt = (cycles() - st_at) / CYCLES_PER_MS;             // ms since the update
    const uint32_t ahead = (uint32_t)((uint64_t)dt * (uint32_t)(st.speed_ppm < 0 ? 0 : st.speed_ppm) / 1000000u);
    const uint32_t p = (st.flags & SL_REVERSE) ? (st.pos_ms > ahead ? st.pos_ms - ahead : 0) : st.pos_ms + ahead;
    return p > tr.length_ms ? tr.length_ms : p;
}

static uint16_t *fb;                    // the one being drawn

static void draw(void)
{
    const dd_frame f = {fb, &dd_cdj2000, &st, &tr, peaks, PEAKS_MAX, have_track};
    dd_draw(&f, now_ms());
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
