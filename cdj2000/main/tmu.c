// TMU0 counting down from 0xFFFFFFFF at Pphi/4: 54 MHz / 4 = 13.5 MHz on the
// CDJ-2000 (its peripheral clock, as its board model gives it).  TSTR 0x04,
// TCOR0 0x08, TCNT0 0x0C, TCR0 0x10 from 0xFFD80000.
#include "tmu.h"

#define TMU      0xFFD80000u
#define TSTR     (*(volatile uint8_t *)(TMU + 0x04))
#define TCOR0    (*(volatile uint32_t *)(TMU + 0x08))
#define TCNT0    (*(volatile uint32_t *)(TMU + 0x0C))
#define TCR0     (*(volatile uint16_t *)(TMU + 0x10))

void tmu_init(void)
{
    TSTR &= (uint8_t)~1u;
    TCR0 = 0;                   // Pphi/4, no interrupt
    TCOR0 = 0xFFFFFFFFu;
    TCNT0 = 0xFFFFFFFFu;
    TSTR |= 1u;
}

uint32_t tmu_us(void)
{
    uint32_t t = 0xFFFFFFFFu - TCNT0;           // 13.5 ticks a microsecond: × 2 / 27
    return t / 27u * 2u + (t % 27u) * 2u / 27u;
}

void tmu_delay_us(uint32_t us)
{
    uint32_t t0 = tmu_us();
    while (tmu_us() - t0 < us) {}
}
