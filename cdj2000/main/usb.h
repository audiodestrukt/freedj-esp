// The CDJ-2000's USB host (the SH7763's R8A66597-family module at
// 0xFE400000) and a mass-storage (bulk-only, SCSI) device on it: enough to
// read the stick's sectors.  Polled.  See docs/usb.md.
#pragma once
#include <stdint.h>

namespace usb {
// Wait up to `ms` for a device, reset it, enumerate it, and find a
// mass-storage interface.  True when a stick is ready to read.
bool open_stick(uint32_t ms);
// The stick's size, from READ CAPACITY: blocks and bytes per block.
uint32_t blocks();
uint32_t block_size();
// Read `count` 512-byte sectors from `lba`.  True on success.
bool read(uint32_t lba, uint32_t count, void *dst);
} // namespace usb
