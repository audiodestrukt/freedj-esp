#pragma once
// lwIP's BSD socket calls, UDP only, on main/netstack.cpp.  select() waits by
// yielding to the other tasks (every yield polls the network).
#include <stdint.h>
#include "net_hw.h"
#include "coop.h"
#include "tmu.h"

#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define SOL_SOCKET 0xfff
#define SO_REUSEADDR 0x0004
#define SO_BROADCAST 0x0020
#define INADDR_ANY 0u
typedef uint32_t socklen_t;
struct in_addr { uint32_t s_addr; };
struct sockaddr { uint16_t sa_family; char sa_data[14]; };
struct sockaddr_in { uint16_t sin_family; uint16_t sin_port; struct in_addr sin_addr; char sin_zero[8]; };
struct timeval { long tv_sec; long tv_usec; };
typedef struct { uint32_t bits; } fd_set;
#define FD_ZERO(s) ((s)->bits = 0)
#define FD_SET(fd, s) ((s)->bits |= 1u << (fd))
#define FD_ISSET(fd, s) (((s)->bits >> (fd)) & 1u)

static inline uint16_t htons(uint16_t v) { return __builtin_bswap16(v); }
static inline uint16_t ntohs(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }

static inline int socket(int d, int t, int p) { (void)d; (void)t; (void)p; return udp_open(); }
static inline int setsockopt(int s, int l, int o, const void *v, socklen_t n) { (void)s; (void)l; (void)o; (void)v; (void)n; return 0; }
static inline int bind(int s, const struct sockaddr *a, socklen_t n)
{
    (void)n;
    return udp_bind(s, ((const struct sockaddr_in *)a)->sin_port);
}
static inline int sendto(int s, const void *d, int len, int f, const struct sockaddr *a, socklen_t n)
{
    (void)f; (void)n;
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    return udp_send(s, in->sin_addr.s_addr, in->sin_port, d, len);
}
static inline int recvfrom(int s, void *b, int len, int f, struct sockaddr *a, socklen_t *n)
{
    (void)f;
    uint32_t ip = 0; uint16_t port = 0;
    const int got = udp_recv(s, b, len, &ip, &port);
    if (got >= 0 && a) {
        struct sockaddr_in *in = (struct sockaddr_in *)a;
        in->sin_family = AF_INET; in->sin_port = port; in->sin_addr.s_addr = ip;
        if (n) *n = sizeof *in;
    }
    return got;
}
static inline int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv)
{
    (void)wr; (void)ex;
    const fd_set want = *rd;
    const int64_t until = tmu_us64() + (tv ? (int64_t)tv->tv_sec * 1000000 + tv->tv_usec : (int64_t)1 << 62);
    for (;;) {
        int n = 0;
        FD_ZERO(rd);
        for (int s = 0; s < nfds; s++)
            if (FD_ISSET(s, &want) && udp_ready(s)) { FD_SET(s, rd); n++; }
        if (n || tmu_us64() >= until) return n;
        coop_yield();
    }
}
