// The CDJ-2000's display processor (an ADSP-BF531): our program for it.
// Milestone 6, step 1: the boot splash.  The splash (tools/splash.py) is in
// the display board's flash at +1 MB; copy it into the framebuffer in SDRAM
// and run the PPI from that framebuffer through DMA0, a frame after frame
// (autobuffer), so the panel shows it from the moment the board boots.
// Register layout: the ADSP-BF531 hardware reference (system MMRs from
// 0xFFC00000: DMA0 at +0xC00, the PPI at +0x1000).  See docs/screen.md.
#include <stdint.h>

#define MMR16(off) (*(volatile uint16_t *)(0xFFC00000u + (off)))
#define MMR32(off) (*(volatile uint32_t *)(0xFFC00000u + (off)))

enum {
    DMA0_START_ADDR = 0xC04, DMA0_CONFIG = 0xC08, DMA0_X_COUNT = 0xC10, DMA0_X_MODIFY = 0xC14,
    DMA0_Y_COUNT = 0xC18, DMA0_Y_MODIFY = 0xC1C,
    PPI_CONTROL = 0x1000, PPI_COUNT = 0x1008, PPI_DELAY = 0x100C, PPI_FRAME = 0x1010,
};
enum { W = 480, H = 255 };

#define FLASH       0x20000000u
#define SPLASH_AT   (FLASH + 0x100000u)            // where tools/mkflash.py puts it
#define FB          ((uint16_t *)0x00100000u)      // the framebuffer, in SDRAM

void main(void)
{
    // ⚠ On a deck, the PLL and the SDRAM controller want setting up first;
    // the emulator has SDRAM from power-on.  See docs/screen.md.
    const uint32_t *src = (const uint32_t *)SPLASH_AT;
    uint32_t *dst = (uint32_t *)FB;
    for (unsigned i = 0; i < W * H / 2; i++) dst[i] = src[i];

    // DMA0 reads the framebuffer row after row for the PPI: 16-bit words,
    // two dimensions, autobuffer (it starts again at the top after the last row).
    MMR32(DMA0_START_ADDR) = (uint32_t)FB;
    MMR16(DMA0_X_COUNT) = W;
    MMR16(DMA0_X_MODIFY) = 2;
    MMR16(DMA0_Y_COUNT) = H;
    MMR16(DMA0_Y_MODIFY) = 2;
    MMR16(DMA0_CONFIG) = 0x1000 | 0x0010 | 0x0004 | 0x0001;   // FLOW autobuffer | DMA2D | WDSIZE 16 | DMAEN
    // The PPI out, 16 bits a pixel, a line of W.  ⚠ The panel's own sync
    // (its timers, PPI_DELAY) is to be matched on a deck.
    MMR16(PPI_COUNT) = W - 1;
    MMR16(PPI_DELAY) = 0;
    MMR16(PPI_FRAME) = H;
    MMR16(PPI_CONTROL) = 0x3800 | 0x000C | 0x0002 | 0x0001;   // DLEN 16 | XFR_TYPE | PORT_DIR out | PORT_EN

    for (;;) __asm__ volatile("idle;");
}
