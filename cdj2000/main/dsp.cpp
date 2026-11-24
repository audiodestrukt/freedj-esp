// The DSP behind the host port.  The main board sees a 64 KB window onto
// DSP memory at 0x0C0C0000 (area 3), which at power-on maps the DSP's
// internal RAM from 0x10000000.  The DSP's ROM loader takes a program put
// there: its entry at 0x10000714 and a non-zero start word at 0x10000718.
#include "dsp.h"
#include "tmu.h"
#include <string.h>

extern "C" const uint8_t dsp_image[], dsp_image_end[];   // build/dsp.bin (main/dsp_blob.S)

namespace dsp {
namespace {
constexpr uint32_t kWindow = 0xAC0C0000u;       // P2, uncached
constexpr uint32_t kIram = 0x10000000u, kLoad = 0x10002000u, kCtl = 0x10007F00u, kRing = 0x10008000u;
constexpr uint32_t kMagic = 0x314A4446u, kMask = 8191;
inline volatile uint32_t &word(uint32_t dsp_addr) { return *reinterpret_cast<volatile uint32_t *>(kWindow + (dsp_addr - kIram)); }
} // namespace

bool boot()
{
    const uint32_t n = static_cast<uint32_t>(dsp_image_end - dsp_image);
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        memcpy(&w, dsp_image + i, n - i < 4 ? n - i : 4);
        word(kLoad + i) = w;
    }
    word(kIram + 0x714) = kLoad;                // the ROM loader's entry word
    word(kIram + 0x718) = 1;                    // and its start word
    const int64_t until = tmu_us64() + 500000;
    while (tmu_us64() < until) if (word(kCtl) == kMagic) return true;
    return false;
}

int room()
{
    const uint32_t w = word(kCtl + 4), r = word(kCtl + 8);
    return static_cast<int>(((r - w - 1) & kMask) / 2);
}

int queued()
{
    const uint32_t w = word(kCtl + 4), r = word(kCtl + 8);
    return static_cast<int>(((w - r) & kMask) / 2);
}

void push(const int16_t *lr, int frames)
{
    uint32_t w = word(kCtl + 4);
    for (int i = 0; i < 2 * frames; i++) {
        word(kRing + 4 * w) = static_cast<uint32_t>(static_cast<uint16_t>(lr[i])) << 16;
        w = (w + 1) & kMask;
    }
    word(kCtl + 4) = w;                         // after the samples: the DSP sees them whole
}

void flush() { word(kCtl + 4) = word(kCtl + 8); }

uint32_t halves() { return word(kCtl + 12); }
} // namespace dsp
