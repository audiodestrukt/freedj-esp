// NFSv2 over UDP on the stick (see nfsd.h).
//
// File handles are 32 bytes: "FDJ2", the entry's first cluster (0 = the
// root directory), its size, and whether it is a directory.  A FAT entry
// has no number of its own, so the first cluster doubles as the file id
// (the root is 1; an empty file, with no cluster, takes a number from the
// name instead).
#include "nfsd.h"
#include "net_hw.h"
#include "coop.h"
#include "con.h"
#include "fat.h"
#include <string.h>

namespace nfsd {
namespace {
constexpr uint16_t kPortmapPort = 111, kRpcPort = 2049;
constexpr uint32_t PROG_PORTMAP = 100000, PROG_NFS = 100003, PROG_MOUNT = 100005;
constexpr uint32_t NFS_OK = 0, NFSERR_NOENT = 2, NFSERR_IO = 5, NFSERR_NOTDIR = 20, NFSERR_STALE = 70;
constexpr uint32_t kMaxRead = 32 * 1024;     // players read 32 KB at a time
const char kExport[] = "/C/";                // a player's USB

uint8_t in[1200];
uint8_t out[kMaxRead + 256];
int pm_sock = -1, rpc_sock = -1;

inline uint32_t be32(const uint8_t *p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct Writer {
    uint8_t *p; int n;
    void u32(uint32_t v) { p[n] = v >> 24; p[n + 1] = v >> 16; p[n + 2] = v >> 8; p[n + 3] = v; n += 4; }
    void bytes(const void *b, int len) { memcpy(p + n, b, len); n += len; }
    void opaque(const void *b, int len) { u32(len); bytes(b, len); while (n & 3) p[n++] = 0; }
};

struct Reader {
    const uint8_t *p; int n, at;
    bool ok = true;
    uint32_t u32() { if (at + 4 > n) { ok = false; return 0; } const uint32_t v = be32(p + at); at += 4; return v; }
    const uint8_t *opaque(int &len) {
        len = static_cast<int>(u32());
        if (!ok || len < 0 || at + len > n) { ok = false; return nullptr; }
        const uint8_t *b = p + at; at += (len + 3) & ~3; return b;
    }
    const uint8_t *fixed(int len) { if (at + len > n) { ok = false; return nullptr; } const uint8_t *b = p + at; at += len; return b; }
};

// A name off the wire: UTF-16LE, as players send it, else plain bytes.
void unwide(const uint8_t *b, int len, char *name, int max)
{
    bool wide = len >= 2 && !(len & 1);
    for (int i = 1; wide && i < len; i += 2) if (b[i]) wide = false;
    int n = 0;
    if (wide) for (int i = 0; i + 1 < len && n < max - 1; i += 2) name[n++] = static_cast<char>(b[i]);
    else for (int i = 0; i < len && n < max - 1; i++) name[n++] = static_cast<char>(b[i]);
    name[n] = 0;
}

void wide(Writer &w, const char *s)
{
    uint8_t b[512];
    int n = 0;
    for (; *s && n < static_cast<int>(sizeof b) - 1; s++) { b[n++] = static_cast<uint8_t>(*s); b[n++] = 0; }
    w.opaque(b, n);
}

struct Handle { uint32_t cluster, size; bool dir; uint32_t id; };

void put_fh(Writer &w, const Handle &h)
{
    uint8_t f[32] = {'F', 'D', 'J', '2'};
    for (int i = 0; i < 4; i++) {
        f[4 + i] = static_cast<uint8_t>(h.cluster >> (24 - 8 * i));
        f[8 + i] = static_cast<uint8_t>(h.size >> (24 - 8 * i));
        f[13 + i] = static_cast<uint8_t>(h.id >> (24 - 8 * i));
    }
    f[12] = h.dir;
    w.bytes(f, 32);
}

bool get_fh(Reader &r, Handle &h)
{
    const uint8_t *f = r.fixed(32);
    if (!f || memcmp(f, "FDJ2", 4)) return false;
    h = Handle{be32(f + 4), be32(f + 8), f[12] != 0, be32(f + 13)};
    return true;
}

uint32_t name_id(const char *s)            // FNV-1a, kept clear of cluster numbers
{
    uint32_t v = 2166136261u;
    for (; *s; s++) v = (v ^ static_cast<uint8_t>(*s)) * 16777619u;
    return v | 0x80000000u;
}

Handle handle_of(const fat::Dirent &e)
{
    return Handle{e.cluster, e.dir ? 0 : e.size, e.dir, e.cluster ? e.cluster : name_id(e.name)};
}

const Handle kRoot{0, 0, true, 1};

void fattr(Writer &w, const Handle &h)
{
    w.u32(h.dir ? 2 : 1);                            // NFDIR / NFREG
    w.u32(h.dir ? 0040555 : 0100444);                // mode: read-only
    w.u32(1); w.u32(0); w.u32(0);                    // nlink uid gid
    w.u32(h.size);
    w.u32(8192);                                     // blocksize
    w.u32(0);                                        // rdev
    w.u32((h.size + 511) / 512);                     // blocks
    w.u32(1);                                        // fsid
    w.u32(h.id);                                     // fileid
    for (int i = 0; i < 6; i++) w.u32(0);            // atime mtime ctime: FAT's are not worth reading
}

// The RPC reply header: accepted, AUTH_NULL verifier, and `stat`
// (0 SUCCESS, 1 PROG_UNAVAIL, 3 PROC_UNAVAIL).
void reply_head(Writer &w, uint32_t xid, uint32_t stat)
{
    w.u32(xid); w.u32(1); w.u32(0); w.u32(0); w.u32(0); w.u32(stat);
}

// One reading position kept between READs, so a file read from the front
// doesn't walk its cluster chain from the start for every 32 KB.
fat::File cursor{};
bool cursor_ok;

int nfs_call(uint32_t proc, Reader &r, Writer &w)
{
    Handle h;
    switch (proc) {
    case 0: return 0;                                // NULL
    case 1:                                          // GETATTR
        if (!get_fh(r, h)) { w.u32(NFSERR_STALE); return 0; }
        w.u32(NFS_OK); fattr(w, h); return 0;
    case 4: {                                        // LOOKUP
        int len; const uint8_t *b;
        if (!get_fh(r, h) || !(b = r.opaque(len))) { w.u32(NFSERR_STALE); return 0; }
        if (!h.dir) { w.u32(NFSERR_NOTDIR); return 0; }
        char name[256];
        unwide(b, len, name, sizeof name);
        fat::Dirent e;
        if (!strcmp(name, ".") ) { w.u32(NFS_OK); put_fh(w, h); fattr(w, h); return 0; }
        if (!fat::lookup(h.cluster, name, e)) { w.u32(NFSERR_NOENT); return 0; }
        const Handle c = e.dir && !e.cluster ? kRoot : handle_of(e);   // ".." of a top folder
        w.u32(NFS_OK); put_fh(w, c); fattr(w, c); return 0;
    }
    case 6: {                                        // READ
        if (!get_fh(r, h)) { w.u32(NFSERR_STALE); return 0; }
        const uint32_t off = r.u32(), want = r.u32();
        if (h.dir) { w.u32(NFSERR_IO); return 0; }
        uint32_t n = want < kMaxRead ? want : kMaxRead;
        if (off >= h.size) n = 0; else if (n > h.size - off) n = h.size - off;
        if (!cursor_ok || cursor.first != h.cluster || cursor.size != h.size || cursor.pos > off) { cursor = fat::file_at(h.cluster, h.size); cursor_ok = true; }
        cursor.pos = off;
        w.u32(NFS_OK); fattr(w, h);
        w.u32(n);
        const int got = n ? fat::read(cursor, w.p + w.n, n) : 0;
        if (got != static_cast<int>(n)) { cursor_ok = false; w.n = 0; return -1; }   // a USB error: say nothing, the client retries
        w.n += got;
        while (w.n & 3) w.p[w.n++] = 0;
        return 0;
    }
    case 16: {                                       // READDIR: cookie = the next entry's index
        if (!get_fh(r, h)) { w.u32(NFSERR_STALE); return 0; }
        const uint32_t cookie = r.u32(), count = r.u32();
        if (!h.dir) { w.u32(NFSERR_NOTDIR); return 0; }
        fat::Dir d = fat::dir_at(h.cluster);
        fat::Dirent e;
        w.u32(NFS_OK);
        const int limit = w.n + static_cast<int>(count < 1000 ? count : 1000) - 8;
        uint32_t i = 0;
        bool eof = true;
        while (fat::readdir(d, e)) {
            if (i++ < cookie) continue;
            const int need = 4 + 4 + 4 + ((static_cast<int>(strlen(e.name)) * 2 + 3) & ~3) + 4;
            if (w.n + need > limit) { eof = false; break; }
            w.u32(1); w.u32(e.cluster ? e.cluster : name_id(e.name)); wide(w, e.name); w.u32(i);
        }
        w.u32(0); w.u32(eof);
        return 0;
    }
    case 17:                                         // STATFS
        w.u32(NFS_OK); w.u32(8192); w.u32(fat::cluster_bytes()); w.u32(fat::total_clusters()); w.u32(0); w.u32(0);
        return 0;
    default: return 1;                               // PROC_UNAVAIL
    }
}

int mount_call(uint32_t proc, Reader &r, Writer &w, uint32_t from)
{
    switch (proc) {
    case 0: case 3: return 0;                        // NULL, UMNT
    case 1: {                                        // MNT: any name mounts the stick
        int len;
        const uint8_t *b = r.opaque(len);
        char name[64] = "";
        if (b) unwide(b, len, name, sizeof name);
        con_printf("nfsd: MNT %s from %u.%u.%u.%u\n", name, from & 255, from >> 8 & 255, from >> 16 & 255, from >> 24);
        w.u32(0); put_fh(w, kRoot); return 0;
    }
    case 5: {                                        // EXPORT: "/C/" to the link-local net, as a player names it
        const uint8_t e[] = {'/', 0, 'C', 0, '/', 0};
        w.u32(1); w.opaque(e, sizeof e);
        const char group[] = "169.254.0.0/255.255.0.0";
        w.u32(1); w.opaque(group, sizeof group - 1); w.u32(0);
        w.u32(0);
        return 0;
    }
    default: return 1;
    }
}

int portmap_call(uint32_t proc, Reader &r, Writer &w)
{
    switch (proc) {
    case 0: return 0;
    case 3: {                                        // GETPORT
        const uint32_t prog = r.u32(); r.u32(); const uint32_t prot = r.u32();
        w.u32((prog == PROG_NFS || prog == PROG_MOUNT) && prot == 17 ? kRpcPort : 0);
        return 0;
    }
    default: return 1;
    }
}

void serve(int sock)
{
    uint32_t ip; uint16_t port;
    const int n = udp_recv(sock, in, sizeof in, &ip, &port);
    if (n < 24) return;
    Reader r{in, n, 0};
    const uint32_t xid = r.u32();
    if (r.u32() != 0 || r.u32() != 2) return;        // a CALL, RPC version 2
    const uint32_t prog = r.u32(), vers = r.u32(), proc = r.u32();
    int len;
    r.u32(); r.opaque(len);                          // credentials: any
    r.u32(); r.opaque(len);                          // verifier
    if (!r.ok) return;
    Writer w{out, 0};
    reply_head(w, xid, 0);
    const int body = w.n;
    int st;
    if (sock == pm_sock && prog == PROG_PORTMAP) st = portmap_call(proc, r, w);
    else if (sock == rpc_sock && prog == PROG_NFS && vers == 2) st = nfs_call(proc, r, w);
    else if (sock == rpc_sock && prog == PROG_MOUNT && vers == 1) st = mount_call(proc, r, w, ip);
    else st = 2;
    if (st < 0) return;
    if (st > 0) { w.n = body - 4; w.u32(st == 1 ? 3 : 1); }    // PROC_UNAVAIL / PROG_UNAVAIL, no body
    udp_send(sock, ip, port, out, w.n);
}

void task(void *)
{
    for (;;) {
        if (udp_ready(pm_sock)) serve(pm_sock);
        if (udp_ready(rpc_sock)) serve(rpc_sock);
        coop_yield();
    }
}
} // namespace

void begin()
{
    pm_sock = udp_open();
    rpc_sock = udp_open();
    if (pm_sock < 0 || rpc_sock < 0) { con_printf("nfsd: no sockets\n"); return; }
    udp_bind(pm_sock, __builtin_bswap16(kPortmapPort));
    udp_bind(rpc_sock, __builtin_bswap16(kRpcPort));
    coop_spawn(task, nullptr, 8192, "nfsd");
    con_printf("nfsd: serving the stick as %s (portmap UDP %u, mount + nfs UDP %u)\n", kExport, kPortmapPort, kRpcPort);
}
} // namespace nfsd
