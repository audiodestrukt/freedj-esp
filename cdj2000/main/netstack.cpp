// Ethernet and a minimal IPv4/UDP stack for the CDJ-2000 main board.
//
// The E-DMAC (Renesas SH7763 manual, E-DMAC and EtherC chapters) walks
// rings of 16-byte descriptors in memory: word 0 is status (TACT/RACT = the
// controller owns it, TDLE/RDLE = last in the ring, TFP/RFP = frame
// position), word 1 the buffer length (high half; RD1's low half is the
// received length), word 2 the buffer's physical address.  We run from the
// uncached P2 alias, so the rings and buffers need no cache maintenance.
#include "net_hw.h"
#include "net.h"            // freedj-esp's net:: interface (p4/deck/main)
#include <string.h>

namespace {
// ── E-DMAC + EtherC ──
constexpr uint32_t kEth = 0xFEF00000u;
inline volatile uint32_t &reg(uint32_t o) { return *reinterpret_cast<volatile uint32_t *>(kEth + o); }
constexpr uint32_t EDMR = 0x000, EDTRR = 0x008, EDRRR = 0x010, TDLAR = 0x018, RDLAR = 0x020, EESR = 0x028,
                   RMCR = 0x058, ECMR = 0x100, ECSR = 0x110, PSR = 0x128, MAHR = 0x1C0, MALR = 0x1C8;
constexpr uint32_t ACT = 0x80000000u, DLE = 0x40000000u, FP1 = 0x20000000u, FP0 = 0x10000000u;
constexpr int kTx = 8, kRx = 16, kBuf = 1536;
struct alignas(16) Desc { volatile uint32_t d0, d1, d2, pad; };
alignas(16) Desc txd[kTx], rxd[kRx];
alignas(32) uint8_t txb[kTx][kBuf], rxb[kRx][kBuf];
int tx_i, rx_i;
uint8_t our_mac[6];
inline uint32_t phys(const void *p) { return reinterpret_cast<uint32_t>(p) & 0x1FFFFFFFu; }

void eth_send(const uint8_t *f, int len)
{
    Desc &d = txd[tx_i];
    for (int spin = 0; d.d0 & ACT; spin++) if (spin > 1000000) return;     // ring full: drop
    if (len < 60) { memset(txb[tx_i] + len, 0, 60 - len); }
    memcpy(txb[tx_i], f, len);
    d.d1 = static_cast<uint32_t>(len < 60 ? 60 : len) << 16;
    d.d0 = ACT | FP1 | FP0 | (tx_i == kTx - 1 ? DLE : 0);
    reg(EDTRR) = 1;
    tx_i = (tx_i + 1) % kTx;
}

// ── IPv4 ──
uint32_t our_ip, our_mask;                      // network byte order
struct ArpEntry { uint32_t ip; uint8_t mac[6]; };
ArpEntry arp[16];
int arp_next;
const uint8_t kBcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

inline uint16_t be16(const uint8_t *p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
inline void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
inline uint32_t ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }   // stays in network order

void arp_learn(uint32_t ip, const uint8_t *mac)
{
    if (!ip || ip == 0xFFFFFFFFu) return;
    for (auto &e : arp) if (e.ip == ip) { memcpy(e.mac, mac, 6); return; }
    arp[arp_next].ip = ip;
    memcpy(arp[arp_next].mac, mac, 6);
    arp_next = (arp_next + 1) % 16;
}

void arp_send(uint16_t op, const uint8_t *tha, uint32_t tip)
{
    uint8_t f[42];
    memcpy(f, op == 1 ? kBcast : tha, 6);
    memcpy(f + 6, our_mac, 6);
    put16(f + 12, 0x0806);
    put16(f + 14, 1); put16(f + 16, 0x0800); f[18] = 6; f[19] = 4; put16(f + 20, op);
    memcpy(f + 22, our_mac, 6); memcpy(f + 28, &our_ip, 4);
    memset(f + 32, 0, 6); if (op == 2) memcpy(f + 32, tha, 6);
    memcpy(f + 38, &tip, 4);
    eth_send(f, 42);
}

uint16_t csum(const uint8_t *p, int n, uint32_t sum = 0)
{
    for (int i = 0; i + 1 < n; i += 2) sum += be16(p + i);
    if (n & 1) sum += static_cast<uint32_t>(p[n - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

// ── UDP sockets ──
constexpr int kSockets = 6, kQueue = 8, kDgram = 1200;
struct Dgram { uint32_t ip; uint16_t port; uint16_t len; uint8_t data[kDgram]; };
struct Sock { bool open; uint16_t port; Dgram q[kQueue]; int head, count; };
Sock socks[kSockets];
uint16_t next_ephemeral = 0xC000;

void udp_in(const uint8_t *ip, const uint8_t *u, int ulen)
{
    const uint16_t dport = static_cast<uint16_t>(u[2] << 8 | u[3]);
    const int n = be16(u + 4) - 8;
    if (n < 0 || n + 8 > ulen) return;
    for (auto &s : socks) {
        if (!s.open || s.port != __builtin_bswap16(dport)) continue;
        if (s.count == kQueue) return;                  // full: drop, as a socket would
        Dgram &d = s.q[(s.head + s.count) % kQueue];
        d.ip = ld32(ip + 12);
        memcpy(&d.port, u, 2);
        d.len = static_cast<uint16_t>(n > kDgram ? kDgram : n);
        memcpy(d.data, u + 8, d.len);
        s.count++;
        return;
    }
}

void frame_in(const uint8_t *f, int len)
{
    if (len < 14) return;
    const uint16_t type = be16(f + 12);
    if (type == 0x0806 && len >= 42 && be16(f + 20) <= 2) {
        uint32_t sip = ld32(f + 28), tip = ld32(f + 38);
        arp_learn(sip, f + 22);
        if (be16(f + 20) == 1 && our_ip && tip == our_ip) arp_send(2, f + 22, sip);
        return;
    }
    if (type != 0x0800 || len < 34) return;
    const uint8_t *ip = f + 14;
    const int ihl = (ip[0] & 15) * 4, tot = be16(ip + 2);
    if ((ip[0] >> 4) != 4 || tot > len - 14 || (be16(ip + 6) & 0x3FFF)) return;   // no fragments
    arp_learn(ld32(ip + 12), f + 6);
    if (ip[9] == 17) udp_in(ip, ip + ihl, tot - ihl);
}
} // namespace

extern "C" void net_init(const uint8_t mac[6])
{
    memcpy(our_mac, mac, 6);
    // A link-local address from the MAC (RFC 3927's range), as a player on a
    // cable with no DHCP server ends up with. ⚠ No probe for a clash yet.
    const uint8_t a[4] = {169, 254, static_cast<uint8_t>(mac[4] ? mac[4] : 1), static_cast<uint8_t>(mac[5] ? mac[5] : 1)};
    memcpy(&our_ip, a, 4);
    const uint8_t m[4] = {255, 255, 0, 0};
    memcpy(&our_mask, m, 4);

    reg(EDMR) = 1;                                      // software reset
    for (int i = 0; i < 100000 && (reg(EDMR) & 1); i++) {}
    reg(EDMR) = 0x40;                                   // 16-byte descriptors, payload not byte-swapped
    for (int i = 0; i < kTx; i++) { txd[i].d0 = i == kTx - 1 ? DLE : 0; txd[i].d1 = 0; txd[i].d2 = phys(txb[i]); }
    for (int i = 0; i < kRx; i++) { rxd[i].d0 = ACT | (i == kRx - 1 ? DLE : 0); rxd[i].d1 = kBuf << 16; rxd[i].d2 = phys(rxb[i]); }
    tx_i = rx_i = 0;
    reg(TDLAR) = phys(txd);
    reg(RDLAR) = phys(rxd);
    reg(RMCR) = 1;                                      // keep receiving after each frame
    reg(MAHR) = static_cast<uint32_t>(mac[0]) << 24 | mac[1] << 16 | mac[2] << 8 | mac[3];
    reg(MALR) = static_cast<uint32_t>(mac[4]) << 8 | mac[5];
    reg(EESR) = 0xFFFFFFFFu;
    reg(ECSR) = 0xFFFFFFFFu;
    reg(ECMR) = 0x00000062;                             // RE | TE | full duplex
    reg(EDRRR) = 1;
    arp_send(1, kBcast, our_ip);                        // announce our address (gratuitous ARP)
}

extern "C" void net_poll(void)
{
    for (int n = 0; n < kRx; n++) {
        Desc &d = rxd[rx_i];
        if (d.d0 & ACT) break;
        frame_in(rxb[rx_i], static_cast<int>(d.d1 & 0xFFFF));
        d.d1 = kBuf << 16;
        d.d0 = ACT | (rx_i == kRx - 1 ? DLE : 0);
        rx_i = (rx_i + 1) % kRx;
    }
    reg(EESR) = 0xFFFFFFFFu;
    reg(EDRRR) = 1;
}

extern "C" int udp_open(void)
{
    for (int i = 0; i < kSockets; i++) if (!socks[i].open) { socks[i] = Sock{}; socks[i].open = true; return i; }
    return -1;
}

extern "C" int udp_bind(int s, uint16_t port)
{
    if (s < 0 || s >= kSockets) return -1;
    socks[s].port = port;
    return 0;
}

extern "C" int udp_send(int s, uint32_t dst, uint16_t port, const void *data, int len)
{
    if (s < 0 || s >= kSockets || !our_ip || len > 1472) return -1;
    if (!socks[s].port) socks[s].port = __builtin_bswap16(next_ephemeral++);
    const uint8_t *mac = nullptr;
    const bool bcast = dst == 0xFFFFFFFFu || (dst | our_mask) == 0xFFFFFFFFu;
    if (bcast) mac = kBcast;
    else for (auto &e : arp) if (e.ip == dst) { mac = e.mac; break; }
    if (!mac) { arp_send(1, kBcast, dst); return -1; }  // not resolved yet: ask, drop this one
    static uint8_t f[1514];
    memcpy(f, mac, 6); memcpy(f + 6, our_mac, 6); put16(f + 12, 0x0800);
    uint8_t *ip = f + 14, *u = ip + 20;
    static uint16_t ident;
    ip[0] = 0x45; ip[1] = 0; put16(ip + 2, static_cast<uint16_t>(28 + len)); put16(ip + 4, ident++);
    put16(ip + 6, 0x4000); ip[8] = 64; ip[9] = 17; put16(ip + 10, 0);
    memcpy(ip + 12, &our_ip, 4); memcpy(ip + 16, &dst, 4);
    put16(ip + 10, csum(ip, 20));
    memcpy(u, &socks[s].port, 2); memcpy(u + 2, &port, 2); put16(u + 4, static_cast<uint16_t>(8 + len)); put16(u + 6, 0);
    memcpy(u + 8, data, len);
    // The UDP checksum over the pseudo-header (0 would also be legal on IPv4).
    uint32_t ps = be16(ip + 12) + be16(ip + 14) + be16(ip + 16) + be16(ip + 18) + 17 + 8 + len;
    uint16_t c = csum(u, 8 + len, ps);
    put16(u + 6, c ? c : 0xFFFF);
    eth_send(f, 42 + len);
    return len;
}

extern "C" int udp_recv(int s, void *buf, int max, uint32_t *ip, uint16_t *port)
{
    if (s < 0 || s >= kSockets || !socks[s].count) return -1;
    Sock &k = socks[s];
    Dgram &d = k.q[k.head];
    const int n = d.len < max ? d.len : max;
    memcpy(buf, d.data, n);
    if (ip) *ip = d.ip;
    if (port) *port = d.port;
    k.head = (k.head + 1) % kQueue;
    k.count--;
    return n;
}

extern "C" int udp_ready(int s) { return s >= 0 && s < kSockets && socks[s].count > 0; }

// freedj-esp's net:: (p4/deck/main/net.h) on this stack.
namespace net {
void begin() {}
uint32_t ip() { return our_ip; }
uint32_t netmask() { return our_mask; }
void mac(uint8_t out[6]) { memcpy(out, our_mac, 6); }
bool linkUp() { return !(reg(PSR) & 1); }   // LMON reads 0 with a cable in on the SH7763 boards
} // namespace net
