// The SH7763's SCIF, polled: enough for a console (Renesas SH7763 hardware
// manual, SCIF chapter: SCSCR TE, SCFSR TDFE/TEND, SCFTDR).
#include "scif.h"

#define SCIF_BASE(ch)  (0xFFE00000u + 0x10000u * (unsigned)(ch))
#define REG16(ch, o)   (*(volatile uint16_t *)(SCIF_BASE(ch) + (o)))
#define REG8(ch, o)    (*(volatile uint8_t *)(SCIF_BASE(ch) + (o)))
#define SCSCR   0x08
#define SCFTDR  0x0C
#define SCFSR   0x10
#define SCFCR   0x18
#define SCSCR_TE    0x0020
#define SCSCR_RE    0x0010
#define SCFSR_TEND  0x0040
#define SCFSR_TDFE  0x0020

void scif_init(int ch)
{
    // Keep whatever baud rate and format the loader set (SCSMR/SCBRR);
    // just make sure the transmitter and receiver are on.
    REG16(ch, SCSCR) |= SCSCR_TE | SCSCR_RE;
}

void scif_putc(int ch, char c)
{
    if (c == '\n') scif_putc(ch, '\r');
    while (!(REG16(ch, SCFSR) & SCFSR_TDFE)) {}
    REG8(ch, SCFTDR) = (uint8_t)c;
    REG16(ch, SCFSR) &= (uint16_t)~(SCFSR_TDFE | SCFSR_TEND);
}

void scif_puts(int ch, const char *s)
{
    while (*s) scif_putc(ch, *s++);
}

void scif_hex(int ch, uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    scif_puts(ch, "0x");
    for (int i = 28; i >= 0; i -= 4) scif_putc(ch, d[(v >> i) & 15]);
}
