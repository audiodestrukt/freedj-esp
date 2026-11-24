// The SH7763's SCIF, polled (Renesas SH7763 hardware manual, SCIF chapter).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void scif_init(int ch);
void scif_putc(int ch, char c);
void scif_puts(int ch, const char *s);
void scif_hex(int ch, uint32_t v);
void scif_dec(int ch, int32_t v);
#ifdef __cplusplus
}
#endif
