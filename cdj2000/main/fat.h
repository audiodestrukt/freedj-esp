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
// By first cluster, as an NFS file handle carries them (main/nfsd.cpp):
// cluster 0 is the root directory.
Dir  dir_at(uint32_t cluster);
bool lookup(uint32_t dir_cluster, const char *name, Dirent &e);   // one name, no slashes
inline File file_at(uint32_t first, uint32_t size) { return File{first, size, 0, first, 0}; }
uint32_t cluster_bytes();
uint32_t total_clusters();
} // namespace fat
