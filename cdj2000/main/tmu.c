// TMU0 counting down from 0xFFFFFFFF at Pphi/4: 54 MHz / 4 = 13.5 MHz on the
// CDJ-2000, 41.67 MHz / 4 on the XDJ-1000MK2 (their peripheral clocks, as
// their board models give them).  TSTR 0x04,
// TCOR0 0x08, TCNT0 0x0C, TCR0 0x10 from 0xFFD80000.  The counter wraps
// every 318 s; tmu_us64() carries the wraps, so it must be called more
// often than that (the main loop and every yield do).
#include "tmu.h"
#include "board.h"

// Ticks to microseconds as an exact fraction of the tick rate (Pphi/4).
#if BOARD_PCLK_HZ == 54000000
#define TICK_NUM 2u      /* 13.5 ticks a microsecond */
#define TICK_DEN 27u
#elif BOARD_PCLK_HZ == 41666667
#define TICK_NUM 12u     /* 10.4167 ticks a microsecond (125/12) */
#define TICK_DEN 125u
#else
#error "no tick fraction for this clock"
#endif

#define TMU      0xFFD80000u
#define TSTR     (*(volatile uint8_t *)(TMU + 0x04))
#define TCOR0    (*(volatile uint32_t *)(TMU + 0x08))
#define TCNT0    (*(volatile uint32_t *)(TMU + 0x0C))
#define TCR0     (*(volatile uint16_t *)(TMU + 0x10))

static uint64_t ticks_hi;           // ticks before the last wrap of the low word
static uint32_t last_lo;

void tmu_init(void)
{
    TSTR &= (uint8_t)~1u;
    TCR0 = 0;                   // Pphi/4, no interrupt
    TCOR0 = 0xFFFFFFFFu;
    TCNT0 = 0xFFFFFFFFu;
    ticks_hi = 0;
    last_lo = 0;
    TSTR |= 1u;
}

int64_t tmu_us64(void)
{
    uint32_t lo = 0xFFFFFFFFu - TCNT0;
    if (lo < last_lo) ticks_hi += 0x100000000ull;
    last_lo = lo;
    uint64_t t = ticks_hi + lo;
    return (int64_t)(t * TICK_NUM / TICK_DEN);
}

uint32_t tmu_us(void) { return (uint32_t)tmu_us64(); }

void tmu_delay_us(uint32_t us)
{
    int64_t t0 = tmu_us64();
    while (tmu_us64() - t0 < (int64_t)us) {}
}
