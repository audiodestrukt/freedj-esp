#pragma once
// One heap here (main/runtime.cpp), so every capability is the same memory.
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DMA      (1 << 3)
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t size, unsigned caps) { (void)caps; return calloc(n, size); }
static inline void heap_caps_free(void *p) { free(p); }
