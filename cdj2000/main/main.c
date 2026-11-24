// freedj on the CDJ-2000's main board (SH7763, SH-4A).  Milestone 1: our
// own image boots and talks on the serial console.
#include <stdint.h>
#include "scif.h"

extern char _end[];

int main(void)
{
    for (int ch = 0; ch < 2; ch++) {    // which SCIF is the console isn't known yet: say it on both
        scif_init(ch);
        scif_puts(ch, "\nfreedj cdj2000: main board up (SCIF");
        scif_putc(ch, (char)('0' + ch));
        scif_puts(ch, ")\n  image end ");
        scif_hex(ch, (uint32_t)_end);
        scif_puts(ch, "\n");
    }
    for (;;) {}
}
