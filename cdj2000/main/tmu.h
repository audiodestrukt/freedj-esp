// Time from the SH7763's TMU0, free running (Renesas SH7763 manual, TMU).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void tmu_init(void);
uint32_t tmu_us(void);              // microseconds since tmu_init(), wraps at ~71 minutes
void tmu_delay_us(uint32_t us);
#ifdef __cplusplus
}
#endif
