#pragma once
#include <stdint.h>
#include "coop.h"
typedef void *TaskHandle_t;
static inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                          int prio, TaskHandle_t *handle, int core)
{
    (void)prio; (void)core;
    if (handle) *handle = 0;
    coop_spawn(fn, arg, stack, name);
    return 1;
}
