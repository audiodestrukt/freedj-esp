// Time from the SH7763's TMU0, free running (Renesas SH7763 manual, TMU).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void tmu_init(void);
int64_t tmu_us64(void);             // microseconds since tmu_init(); call at least every 5 minutes
uint32_t tmu_us(void);              // the same, low 32 bits
void tmu_delay_us(uint32_t us);
#ifdef __cplusplus
}
#endif
