// A read-only FAT12/16/32 reader with long file names, over usb::read().
// Enough for a rekordbox stick: open a path, read and seek a file, list a
// directory.  Names compare without regard to case, as on the deck.
#pragma once
#include <stdint.h>

namespace fat {
struct Dirent { char name[256]; bool dir; uint32_t size, cluster; };
struct File { uint32_t first, size, pos, clus, clus_index; };
struct Dir { uint32_t cluster; uint32_t index; bool fixed_root; };

bool mount();                                   // the first FAT partition, or a stick without a partition table
bool open(const char *path, File &f);           // a file (not a directory)
int  read(File &f, void *dst, uint32_t n);      // bytes read, -1 on an error
bool opendir(const char *path, Dir &d);
bool readdir(Dir &d, Dirent &e);                // false at the end
} // namespace fat
