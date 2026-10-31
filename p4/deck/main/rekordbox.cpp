#include "rekordbox.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

namespace rekordbox {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

// UTF-16BE to UTF-8, stopping at NUL.
static std::string utf16be(const uint8_t* p, size_t bytes)
{
    std::string s;
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        uint32_t c = be16(p + i);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {          // surrogate pair
            const uint32_t lo = be16(p + i + 2);
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | (c >> 6 & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xF0 | c >> 18); s += (char)(0x80 | (c >> 12 & 0x3F)); s += (char)(0x80 | (c >> 6 & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

bool readAnlz(const char* path, std::string* audioRel, std::vector<Beat>* beats, std::vector<uint8_t>* preview)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[12];
    bool ok = fread(h, 1, 12, f) == 12 && !memcmp(h, "PMAI", 4);
    long off = ok ? (long)be32(h + 4) : 0;
    const long fileLen = ok ? (long)be32(h + 8) : 0;
    bool needPath = audioRel != nullptr, needGrid = beats != nullptr, needPreview = preview != nullptr;
    // Sections: tag, header length, total length (all big-endian).
    while (ok && (needPath || needGrid || needPreview) && off + 12 <= fileLen) {
        if (fseek(f, off, SEEK_SET) || fread(h, 1, 12, f) != 12) break;
        const uint32_t hdr = be32(h + 4), len = be32(h + 8);
        if (len < 12 || hdr < 12 || hdr > len) break;
        if (needPath && !memcmp(h, "PPTH", 4) && len < 4096) {
            std::vector<uint8_t> b(len - 12);
            if (fread(b.data(), 1, b.size(), f) == b.size() && b.size() >= 4) {
                const uint32_t n = be32(b.data());           // path length in bytes
                const size_t start = hdr - 12;               // path starts after the header
                if (start <= b.size()) *audioRel = utf16be(b.data() + start, std::min<size_t>(n, b.size() - start));
            }
            needPath = false;
        } else if (needGrid && !memcmp(h, "PQTZ", 4)) {
            std::vector<uint8_t> b(len - 12);
            if (fread(b.data(), 1, b.size(), f) == b.size() && b.size() >= 12) {
                const uint32_t n = be32(b.data() + 8);
                const size_t start = hdr - 12;
                beats->clear();
                beats->reserve(n);
                for (uint32_t i = 0; i < n && start + 8 * (i + 1) <= b.size(); i++) {
                    const uint8_t* e = b.data() + start + 8 * i;
                    beats->push_back({be32(e + 4), be16(e + 2), (uint8_t)be16(e)});
                }
            }
            needGrid = false;
        } else if (needPreview && !memcmp(h, "PWAV", 4) && len < 4096) {
            std::vector<uint8_t> b(len - 12);
            if (fread(b.data(), 1, b.size(), f) == b.size() && b.size() >= 4) {
                const uint32_t n = be32(b.data());           // number of columns (400)
                const size_t start = hdr - 12;
                if (start <= b.size()) preview->assign(b.begin() + start, b.begin() + std::min<size_t>(start + n, b.size()));
            }
            needPreview = false;
        }
        off += len;
    }
    fclose(f);
    return ok;
}

// Walks PIONEER/USBANLZ/Pxxx/xxxxxxxx/ for ANLZ0000.DAT. Returns false once
// the callback asks to stop.
static bool walk(const std::string& root, const std::string& dir, int depth, const TrackFn& fn)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return true;
    bool go = true;
    while (go) {
        struct dirent* e = readdir(d);
        if (!e) break;
        if (e->d_name[0] == '.') continue;
        const std::string p = dir + "/" + e->d_name;
        if (!strcasecmp(e->d_name, "ANLZ0000.DAT")) {
            std::string rel;
            std::vector<Beat> grid;
            if (readAnlz(p.c_str(), &rel, &grid) && !rel.empty())
                go = fn((root + rel).c_str(), p.c_str(), grid.empty() ? 0 : grid[0].bpm100 / 100.0f);
        } else if (depth < 2 && e->d_type != DT_REG) {
            go = walk(root, p, depth + 1, fn);
        }
    }
    closedir(d);
    return go;
}

void scan(const char* root, const TrackFn& fn)
{
    walk(root, std::string(root) + "/PIONEER/USBANLZ", 0, fn);
}

// Index of the last beat at or before `ms` (0 if before the first).
static size_t beatBefore(const std::vector<Beat>& b, double ms)
{
    size_t lo = 0, hi = b.size();
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (b[mid].ms <= ms) lo = mid; else hi = mid;
    }
    return lo;
}

double Grid::beatAt(double ms) const
{
    if (empty()) return 0;
    const size_t n = beats.size();
    size_t i = beatBefore(beats, ms);
    if (i >= n - 1) i = n - 2;                       // extrapolate past the end from the last interval
    const double t0 = beats[i].ms, t1 = beats[i + 1].ms;
    return i + (ms - t0) / (t1 - t0);                // before the first beat this goes negative
}

double Grid::msAt(double beat) const
{
    if (empty()) return 0;
    const size_t n = beats.size();
    long i = (long)floor(beat);
    if (i < 0) i = 0;
    if (i > (long)n - 2) i = (long)n - 2;
    const double t0 = beats[i].ms, t1 = beats[i + 1].ms;
    return t0 + (beat - i) * (t1 - t0);
}

float Grid::bpmAt(double ms) const
{
    if (empty()) return 0;
    return beats[beatBefore(beats, ms)].bpm100 / 100.0f;
}

int Grid::inBarAt(long i) const
{
    if (empty()) return 1;
    const long n = (long)beats.size();
    if (i >= 0 && i < n) return beats[i].inBar;
    // Outside the grid: count on from the nearest end.
    const long ref = i < 0 ? 0 : n - 1;
    const long k = ((beats[ref].inBar - 1 + (i - ref)) % 4 + 4) % 4;
    return (int)k + 1;
}

} // namespace rekordbox
