// FAT, read-only (Microsoft's FAT32 File System Specification layout: the
// BPB, FAT12/16/32 by cluster count, 32-byte directory entries, long-name
// entries with attribute 0x0F carrying 13 UCS-2 characters each).
#include "fat.h"
#include "usb.h"
#include <string.h>

namespace fat {
namespace {
uint32_t part_lba, fat_lba, root_lba, data_lba, root_entries, root_cluster, clusters;
uint32_t spc;           // sectors per cluster
int type;               // 12, 16 or 32

uint8_t sec_buf[512];
uint32_t sec_cached = 0xFFFFFFFFu;

const uint8_t *sector(uint32_t lba)
{
    if (lba != sec_cached) {
        if (!usb::read(lba, 1, sec_buf)) { sec_cached = 0xFFFFFFFFu; return nullptr; }
        sec_cached = lba;
    }
    return sec_buf;
}

inline uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
inline uint32_t le32(const uint8_t *p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24; }

uint32_t next_cluster(uint32_t c)
{
    if (type == 12) {
        const uint32_t off = c + c / 2;
        const uint8_t *a = sector(fat_lba + off / 512);
        if (!a) return 0x0FFFFFFF;
        uint16_t lo = a[off % 512], hi;
        if (off % 512 == 511) { const uint8_t *b = sector(fat_lba + off / 512 + 1); if (!b) return 0x0FFFFFFF; hi = b[0]; }
        else hi = a[off % 512 + 1];
        const uint16_t v = static_cast<uint16_t>(lo | hi << 8);
        const uint32_t n = (c & 1) ? v >> 4 : v & 0x0FFF;
        return n >= 0xFF8 ? 0x0FFFFFFF : n;
    }
    const uint32_t off = c * (type == 16 ? 2 : 4);
    const uint8_t *s = sector(fat_lba + off / 512);
    if (!s) return 0x0FFFFFFF;
    if (type == 16) { const uint32_t n = le16(s + off % 512); return n >= 0xFFF8 ? 0x0FFFFFFF : n; }
    return le32(s + off % 512) & 0x0FFFFFFF;
}

inline bool is_end(uint32_t c) { return c < 2 || c >= 0x0FFFFFF8 || c >= clusters + 2; }
inline uint32_t cluster_lba(uint32_t c) { return data_lba + (c - 2) * spc; }

int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
bool same(const char *a, const char *b, int bn)
{
    int i = 0;
    for (; i < bn && a[i]; i++) if (lower(a[i]) != lower(b[i])) return false;
    return i == bn && !a[i];
}

// The 32-byte entry at a position in a directory; null at its end.
const uint8_t *entry_at(const Dir &d, uint32_t index)
{
    uint32_t lba;
    if (d.fixed_root) {
        if (index >= root_entries) return nullptr;
        lba = root_lba + index / 16;
    } else {
        uint32_t c = d.cluster, per = spc * 16;
        for (uint32_t k = index / per; k; k--) { c = next_cluster(c); if (is_end(c)) return nullptr; }
        lba = cluster_lba(c) + (index % per) / 16;
    }
    const uint8_t *s = sector(lba);
    return s ? s + (index % 16) * 32 : nullptr;
}

bool find(const char *path, Dirent &out)
{
    Dir d{root_cluster, 0, type != 32};
    out = Dirent{};
    out.dir = true;
    out.cluster = root_cluster;
    while (*path == '/') path++;
    while (*path) {
        const char *end = path;
        while (*end && *end != '/') end++;
        Dirent e;
        bool found = false;
        while (readdir(d, e)) if (same(e.name, path, static_cast<int>(end - path))) { found = true; break; }
        if (!found) return false;
        out = e;
        path = end;
        while (*path == '/') path++;
        if (*path) {
            if (!e.dir) return false;
            d = Dir{e.cluster ? e.cluster : root_cluster, 0, e.cluster == 0 && type != 32};
        }
    }
    return true;
}
} // namespace

bool mount()
{
    const uint8_t *s = sector(0);
    if (!s || s[510] != 0x55 || s[511] != 0xAA) return false;
    part_lba = 0;
    // A partition table, unless sector 0 is itself a boot sector (a "superfloppy").
    if (!(s[0] == 0xEB || s[0] == 0xE9) || le16(s + 11) != 512) {
        for (int i = 0; i < 4; i++) {
            const uint8_t t = s[446 + 16 * i + 4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E) { part_lba = le32(s + 446 + 16 * i + 8); break; }
        }
        if (!part_lba) return false;
        s = sector(part_lba);
        if (!s) return false;
    }
    if (le16(s + 11) != 512) return false;
    spc = s[13];
    const uint32_t reserved = le16(s + 14), nfats = s[16];
    root_entries = le16(s + 17);
    const uint32_t total = le16(s + 19) ? le16(s + 19) : le32(s + 32);
    const uint32_t fat_size = le16(s + 22) ? le16(s + 22) : le32(s + 36);
    fat_lba = part_lba + reserved;
    root_lba = fat_lba + nfats * fat_size;
    data_lba = root_lba + (root_entries * 32 + 511) / 512;
    clusters = (total - (data_lba - part_lba)) / spc;
    type = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
    root_cluster = type == 32 ? le32(s + 44) : 0;
    return spc != 0;
}

bool opendir(const char *path, Dir &d)
{
    Dirent e;
    if (!find(path, e) || !e.dir) return false;
    d = Dir{e.cluster ? e.cluster : root_cluster, 0, e.cluster == 0 && type != 32};
    return true;
}

bool readdir(Dir &d, Dirent &e)
{
    char lfn[256];
    int lfn_parts = 0;
    for (;;) {
        const uint8_t *p = entry_at(d, d.index);
        if (!p || p[0] == 0) return false;
        d.index++;
        if (p[0] == 0xE5) { lfn_parts = 0; continue; }
        if (p[11] == 0x0F) {                    // a long-name part: 13 UCS-2 characters
            const int seq = (p[0] & 0x1F) - 1;
            if (p[0] & 0x40) { memset(lfn, 0, sizeof lfn); lfn_parts = 0; }
            static const int at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (int i = 0; i < 13 && seq * 13 + i < 255; i++) {
                const uint16_t c = le16(p + at[i]);
                if (c == 0 || c == 0xFFFF) break;
                lfn[seq * 13 + i] = c < 0x80 ? static_cast<char>(c) : '?';
            }
            lfn_parts++;
            continue;
        }
        if (p[11] & 0x08) { lfn_parts = 0; continue; }   // the volume label
        if (lfn_parts) {
            strncpy(e.name, lfn, sizeof e.name - 1);
            e.name[sizeof e.name - 1] = 0;
        } else {                                // 8.3, lower-cased where the NT flags say
            int n = 0;
            for (int i = 0; i < 8 && p[i] != ' '; i++) e.name[n++] = static_cast<char>((p[12] & 0x08) ? lower(p[i]) : p[i]);
            if (p[8] != ' ') {
                e.name[n++] = '.';
                for (int i = 8; i < 11 && p[i] != ' '; i++) e.name[n++] = static_cast<char>((p[12] & 0x10) ? lower(p[i]) : p[i]);
            }
            e.name[n] = 0;
        }
        lfn_parts = 0;
        e.dir = p[11] & 0x10;
        e.size = le32(p + 28);
        e.cluster = static_cast<uint32_t>(le16(p + 26)) | (type == 32 ? static_cast<uint32_t>(le16(p + 20)) << 16 : 0);
        return true;
    }
}

bool open(const char *path, File &f)
{
    Dirent e;
    if (!find(path, e) || e.dir) return false;
    f = File{e.cluster, e.size, 0, e.cluster, 0};
    return true;
}

int read(File &f, void *dst, uint32_t n)
{
    uint8_t *out = static_cast<uint8_t *>(dst);
    const uint32_t csize = spc * 512;
    if (f.pos >= f.size) return 0;
    if (n > f.size - f.pos) n = f.size - f.pos;
    uint32_t done = 0;
    while (done < n) {
        const uint32_t want = f.pos / csize;
        if (want < f.clus_index) { f.clus = f.first; f.clus_index = 0; }     // seeked back: walk again
        while (f.clus_index < want) { f.clus = next_cluster(f.clus); f.clus_index++; if (is_end(f.clus)) return -1; }
        const uint32_t in = f.pos % csize, lba = cluster_lba(f.clus) + in / 512, off = in % 512;
        const uint8_t *s = sector(lba);
        if (!s) return -1;
        uint32_t k = 512 - off;
        if (k > n - done) k = n - done;
        memcpy(out + done, s + off, k);
        done += k;
        f.pos += k;
    }
    return static_cast<int>(done);
}
} // namespace fat
