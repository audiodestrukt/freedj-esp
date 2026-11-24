#include "player.h"
#include "dsp.h"
#include "con.h"
#include <stdio.h>
#include <string.h>

namespace player {
namespace {
FILE *file;
uint32_t data_at, data_len, pos;                // bytes: where the PCM starts, how much, how far in
bool on;
inline uint32_t le32(const uint8_t *p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24; }
} // namespace

bool load(const char *path)
{
    if (file) fclose(file);
    file = fopen(path, "rb");
    if (!file) return false;
    uint8_t h[12];
    if (fread(h, 1, 12, file) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(file); file = nullptr; return false; }
    bool fmt_ok = false;
    for (uint32_t at = 12;;) {                  // the chunks: "fmt " then "data"
        uint8_t c[8];
        if (fseek(file, static_cast<long>(at), SEEK_SET) || fread(c, 1, 8, file) != 8) break;
        const uint32_t len = le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t f[16];
            if (fread(f, 1, 16, file) != 16) break;
            fmt_ok = (f[0] | f[1] << 8) == 1 && (f[2] | f[3] << 8) == 2 && le32(f + 4) == 44100 && (f[14] | f[15] << 8) == 16;
        } else if (!memcmp(c, "data", 4)) {
            data_at = at + 8;
            data_len = len;
            if (!fmt_ok) break;
            pos = 0;
            on = false;
            fseek(file, static_cast<long>(data_at), SEEK_SET);
            con_printf("player: %s, %u s of 16-bit 44.1 kHz stereo\n", path, data_len / 176400);
            return true;
        }
        at += 8 + len + (len & 1);
    }
    con_printf("player: %s is not 16-bit 44.1 kHz stereo PCM\n", path);
    fclose(file);
    file = nullptr;
    return false;
}

void play(bool p) { on = p && file; }
bool playing() { return on; }
bool loaded() { return file != nullptr; }
uint32_t position_ms() { return static_cast<uint32_t>(static_cast<uint64_t>(pos) * 1000 / 176400); }

void cue()
{
    on = false;
    pos = 0;
    if (file) fseek(file, static_cast<long>(data_at), SEEK_SET);
    dsp::flush();
}

void pump()
{
    if (!on || !file) return;
    static int16_t buf[2 * 512];
    int room = dsp::room();
    while (room >= 64 && pos < data_len) {
        int frames = room < 512 ? room : 512;
        if (static_cast<uint32_t>(frames) * 4 > data_len - pos) frames = static_cast<int>((data_len - pos) / 4);
        const size_t got = fread(buf, 4, static_cast<size_t>(frames), file);
        if (!got) break;
        dsp::push(buf, static_cast<int>(got));
        pos += static_cast<uint32_t>(got) * 4;
        room -= static_cast<int>(got);
    }
    if (pos >= data_len) on = false;            // the end: stop there
}
} // namespace player
