// Cooperative tasks: each has its own stack and runs until it yields.  Enough
// to run freedj-esp's Link task (a FreeRTOS task blocking in select()) beside
// the main loop without an RTOS; every yield also polls the network.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void coop_spawn(void (*fn)(void *), void *arg, uint32_t stack_bytes, const char *name);
void coop_yield(void);
#ifdef __cplusplus
}
#endif
