/* The main boards this image is built for (make BOARD=...): what differs
 * between them, from cdj-nxs2-qemu's board models and the SoCs' manuals.
 * Both are SH-4A: the CDJ-2000's SH7763 and the XDJ-1000MK2's SH7724.
 * SCIF0..2 (0xFFE00000 on) and TMU0 (0xFFD80000) are at the same places on
 * both; DRAM, the clock and the Ethernet and USB blocks are not.  Plain
 * #defines: start.S includes it too. */
#pragma once

#if defined(BOARD_XDJ1000MK2)
#define BOARD_NAME          "XDJ-1000MK2"
#define BOARD_PCLK_HZ       41666667        /* the peripheral clock: TMU0 ticks at a quarter of it */
#define BOARD_STACK_TOP     0xB7FFFFC0      /* DRAM 0x08000000, 256 MB, through P2 */
#define BOARD_HEAP_TOP      0xB7F00000
#define BOARD_ETH_BASE      0xA4600000u     /* the SH7724's EtherC/E-DMAC (RMII) */
#define BOARD_ETH_LMON_LOW  0               /* PSR.LMON reads 1 with a cable in */
#define BOARD_USB_BASE      0xA4D90000u     /* the second USB block: the stick port */
#define BOARD_PANEL_FRAME   32              /* the panel CPU's frame, sync 0x8F last */
#define BOARD_HAS_DSP       0               /* a C6747: our DSP program is the C6727's, not ported yet */
#define BOARD_HAS_BLACKFIN  0               /* the SH7724 drives the LCD itself (its LCDC) */
/* The tempo slider: bytes 4-5 against mid travel.  ⚠ The emulator's XDJ
 * model fills no centre reference in bytes 6-7 (its CDJ-2000 model does);
 * where the real panel reports the slider is not checked yet. */
#define BOARD_TEMPO_CENTRE_REF 0
#else
#define BOARD_CDJ2000       1
#define BOARD_NAME          "CDJ-2000"
#define BOARD_PCLK_HZ       54000000
#define BOARD_STACK_TOP     0xA7FFFFC0      /* DRAM 0x04000000, 64 MB, through P2 */
#define BOARD_HEAP_TOP      0xA7F00000
#define BOARD_ETH_BASE      0xFEF00000u
#define BOARD_ETH_LMON_LOW  1               /* PSR.LMON reads 0 with a cable in on the SH7763 boards */
#define BOARD_USB_BASE      0xFE400000u
#define BOARD_PANEL_FRAME   24
#define BOARD_HAS_DSP       1
#define BOARD_HAS_BLACKFIN  1
#define BOARD_TEMPO_CENTRE_REF 1            /* bytes 4-5 against the centre reference in 6-7 */
#endif
