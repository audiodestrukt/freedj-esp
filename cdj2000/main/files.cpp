// stdio's file calls and dirent's directory calls (as glibc's headers declare
// them) on the FAT reader, so freedj-esp code that reads files builds
// unchanged.  The stick is mounted at /usb, as on the P4 deck.
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include "fat.h"

namespace {
constexpr const char kMount[] = "/usb";
struct OurFile { fat::File f; bool used; };
struct OurDir { fat::Dir d; struct dirent e; bool used; };
OurFile files[8];
OurDir dirs[4];

const char *on_stick(const char *path)
{
    const size_t n = sizeof kMount - 1;
    if (strncmp(path, kMount, n) || (path[n] && path[n] != '/')) return nullptr;
    return path + n;
}
} // namespace

extern "C" {
FILE *fopen(const char *path, const char *mode)
{
    const char *p = on_stick(path);
    if (!p || mode[0] != 'r') return nullptr;
    for (auto &f : files) {
        if (f.used) continue;
        if (!fat::open(p, f.f)) return nullptr;
        f.used = true;
        return reinterpret_cast<FILE *>(&f);
    }
    return nullptr;
}

size_t fread(void *dst, size_t size, size_t n, FILE *fp)
{
    if (!fp || !size) return 0;
    const int got = fat::read(reinterpret_cast<OurFile *>(fp)->f, dst, static_cast<uint32_t>(size * n));
    return got < 0 ? 0 : static_cast<size_t>(got) / size;
}

int fseek(FILE *fp, long off, int whence)
{
    if (!fp) return -1;
    fat::File &f = reinterpret_cast<OurFile *>(fp)->f;
    const long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<long>(f.pos) : static_cast<long>(f.size);
    if (base + off < 0) return -1;
    f.pos = static_cast<uint32_t>(base + off);
    return 0;
}

// Reads go straight to the FAT reader's sector cache: no stdio buffer to size.
int setvbuf(FILE *, char *, int, size_t) { return 0; }

long ftell(FILE *fp) { return fp ? static_cast<long>(reinterpret_cast<OurFile *>(fp)->f.pos) : -1; }

int fclose(FILE *fp)
{
    if (fp) reinterpret_cast<OurFile *>(fp)->used = false;
    return 0;
}

DIR *opendir(const char *path)
{
    const char *p = on_stick(path);
    if (!p) return nullptr;
    for (auto &d : dirs) {
        if (d.used) continue;
        if (!fat::opendir(*p ? p : "/", d.d)) return nullptr;
        d.used = true;
        return reinterpret_cast<DIR *>(&d);
    }
    return nullptr;
}

struct dirent *readdir(DIR *dp)
{
    OurDir &d = *reinterpret_cast<OurDir *>(dp);
    fat::Dirent e;
    do {
        if (!fat::readdir(d.d, e)) return nullptr;
    } while (!strcmp(e.name, ".") || !strcmp(e.name, ".."));
    memset(&d.e, 0, sizeof d.e);
    strncpy(d.e.d_name, e.name, sizeof d.e.d_name - 1);
    d.e.d_type = e.dir ? DT_DIR : DT_REG;
    return &d.e;
}

int closedir(DIR *dp)
{
    if (dp) reinterpret_cast<OurDir *>(dp)->used = false;
    return 0;
}
} // extern "C"
