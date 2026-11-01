// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// On-board Ethernet (ESP32-P4 EMAC + IP101 PHY over RMII). DHCP when there's
// a router; lwIP falls back to a 169.254.x.x link-local address when there
// isn't (a cable straight to a Pioneer player, which does the same).
// Call begin() from core 0 so the EMAC interrupt and rx task stay off the
// audio core.
#include <stdint.h>

namespace net {
void begin();
// Our IPv4 address and netmask (network byte order), 0 until we have one.
uint32_t ip();
uint32_t netmask();
void mac(uint8_t out[6]);
bool linkUp();
} // namespace net
