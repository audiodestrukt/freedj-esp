#pragma once
#include <stdint.h>
typedef struct { uint32_t addr; } esp_ip4_addr_t;     // network byte order
#define IPSTR "%d.%d.%d.%d"
#define IP2STR(a) ((const uint8_t *)(a))[0], ((const uint8_t *)(a))[1], ((const uint8_t *)(a))[2], ((const uint8_t *)(a))[3]
