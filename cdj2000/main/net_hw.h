// The CDJ-2000's Ethernet (the SH7763's E-DMAC + EtherC at 0xFEF00000) and
// a small IPv4 stack on it: ARP, a link-local address, UDP sockets (a
// datagram larger than a frame goes out in IP fragments; none are taken in),
// and a reset for every TCP connection attempt (we listen on none).  The
// sockets back the lwIP shim (shim/lwip/sockets.h) that freedj-esp's Link
// code is written against; net:: (p4/deck/main/net.h) is implemented here.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void net_init(const uint8_t mac[6]);
void net_poll(void);                    // take received frames; called from every yield

// UDP sockets.  Addresses and ports in network byte order, as in sockaddr_in.
int  udp_open(void);                    // -1 when all are taken
int  udp_bind(int s, uint16_t port);    // 0 on success
int  udp_send(int s, uint32_t ip, uint16_t port, const void *data, int len);
int  udp_recv(int s, void *buf, int max, uint32_t *ip, uint16_t *port);  // -1 when nothing waits
int  udp_ready(int s);                  // a datagram waits
#ifdef __cplusplus
}
#endif
