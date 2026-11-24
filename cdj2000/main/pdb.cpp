// export.pdb's track table (see pdb.h).
#include "pdb.h"
#include <stdio.h>
#include <string.h>

namespace pdb {
namespace {
inline uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
inline uint32_t le32(const uint8_t *p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24; }

constexpr uint32_t kTracks = 0;             // table type 0: tracks
constexpr uint32_t kHeader = 0x28;          // the page header; the row heap follows
constexpr int kAnalyzePath = 14;            // a track row's 21 string offsets (at 0x5e): 14 = the analysis path
uint8_t page[8192];

// A DeviceSQL string at `p` (inside the page), as ASCII: a short one (first
// byte odd: its length << 1 | 1, the byte included), or a long one (0x40
// ASCII / 0x90 UTF-16LE, then its length with the 4-byte header, a pad byte).
bool str(const uint8_t *p, const uint8_t *end, char *out, int max)
{
    if (p >= end) return false;
    int n = 0;
    if (p[0] & 1) {
        const int len = (p[0] >> 1) - 1;
        if (p + 1 + len > end) return false;
        for (int i = 0; i < len && n < max - 1; i++) out[n++] = static_cast<char>(p[1 + i]);
    } else {
        if (p + 4 > end) return false;
        const int len = le16(p + 1) - 4;
        if (len < 0 || p + 4 + len > end) return false;
        if (p[0] == 0x40) for (int i = 0; i < len && n < max - 1; i++) out[n++] = static_cast<char>(p[4 + i]);
        else if (p[0] == 0x90) for (int i = 0; i + 1 < len && n < max - 1; i += 2) { const uint16_t c = le16(p + 4 + i); out[n++] = c < 0x80 ? static_cast<char>(c) : '?'; }
        else return false;
    }
    out[n] = 0;
    return true;
}
} // namespace

bool tracks(const char *path, const TrackFn &fn)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    uint8_t h[0x1c + 16 * 32];
    const size_t got = fread(h, 1, sizeof h, f);
    const uint32_t page_size = got >= 0x1c ? le32(h + 4) : 0, ntables = got >= 0x1c ? le32(h + 8) : 0;
    if (page_size < 512 || page_size > sizeof page || 0x1c + 16 * ntables > got) { fclose(f); return false; }
    uint32_t first = 0, last = 0;
    bool found = false;
    for (uint32_t t = 0; t < ntables; t++) {
        const uint8_t *e = h + 0x1c + 16 * t;
        if (le32(e) == kTracks) { first = le32(e + 8); last = le32(e + 12); found = true; break; }
    }
    // Follow the table's pages (its first holds no rows), stopping at its
    // last, or after as many pages as the file could hold.
    for (uint32_t pg = first, steps = 0; found && steps < 1u << 16; steps++) {
        if (fseek(f, static_cast<long>(pg * page_size), SEEK_SET) || fread(page, 1, page_size, f) != page_size) break;
        const uint8_t *end = page + page_size;
        const uint32_t next = le32(page + 0x0c);
        const uint8_t flags = page[0x1b];
        uint32_t rows = page[0x18];
        const uint16_t large = le16(page + 0x22);
        if (large > rows && large != 0x1fff) rows = large;
        if (le32(page + 0x08) == kTracks && !(flags & 0x40) && rows) {
            // Row groups of 16 from the page's end, each: 16 offsets (the
            // first row's last), the presence flags, a padding word.
            for (uint32_t g = 0; g * 16 < rows; g++) {
                const uint8_t *ge = end - g * 0x24;
                const uint16_t present = le16(ge - 4);
                for (int i = 0; i < 16; i++) {
                    if (!(present & (1 << i))) continue;
                    const uint8_t *row = page + kHeader + le16(ge - 4 - 2 * (i + 1));
                    if (row + 0x5e + 2 * 21 > end) continue;
                    char anlz[256];
                    if (str(row + le16(row + 0x5e + 2 * kAnalyzePath), end, anlz, sizeof anlz)) fn(le32(row + 0x48), anlz);
                }
            }
        }
        if (pg == last || next == pg) break;
        pg = next;
    }
    fclose(f);
    return found;
}
} // namespace pdb
