#pragma once
#include "tmu.h"
static inline int64_t esp_timer_get_time(void) { return tmu_us64(); }
