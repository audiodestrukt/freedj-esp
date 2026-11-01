// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
// LVGL's allocator (CONFIG_LV_USE_CUSTOM_MALLOC). Plain malloc puts every
// small allocation in internal RAM, and a list of a few hundred tracks (a
// button and a label per row) used it all up. But LVGL also allocates small
// blocks for every frame it draws, and moving those to PSRAM cost the
// waveform a quarter of its frame rate (and slowed the audio core through
// PSRAM contention). So: internal RAM by default, PSRAM only while
// lvgl_mem_psram is set, around building long-lived objects like the list.
#include <stdbool.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

volatile bool lvgl_mem_psram;

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void* lv_malloc_core(size_t size)
{
    if (lvgl_mem_psram) {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return malloc(size);
}

// heap_caps_realloc keeps a block in the region it is already in.
void* lv_realloc_core(void* p, size_t new_size)
{
    if (!p) return lv_malloc_core(new_size);
    return heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void* p) { free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t* mon) { (void)mon; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
