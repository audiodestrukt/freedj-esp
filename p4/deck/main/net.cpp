// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.h"

#include <string.h>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "board.h"

namespace net {

static const char* TAG = "net";
static esp_netif_t* s_netif;
static esp_eth_handle_t s_eth;
static volatile bool s_link;

static void onEth(void*, esp_event_base_t, int32_t id, void*)
{
    if (id == ETHERNET_EVENT_CONNECTED) { s_link = true; ESP_LOGI(TAG, "link up"); }
    else if (id == ETHERNET_EVENT_DISCONNECTED) { s_link = false; ESP_LOGW(TAG, "link down"); }
}

static void onIp(void*, esp_event_base_t, int32_t, void* data)
{
    const auto* e = (const ip_event_got_ip_t*)data;
    ESP_LOGI(TAG, "address " IPSTR " / " IPSTR " (DHCP)", IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.netmask));
}

void begin()
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);

    eth_mac_config_t mc = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t ec = ETH_ESP32_EMAC_DEFAULT_CONFIG();   // P4 defaults: MDC 31, MDIO 52, RMII clock in on 50
    esp_eth_mac_t* emac = esp_eth_mac_new_esp32(&ec, &mc);
    eth_phy_config_t pc = ETH_PHY_DEFAULT_CONFIG();
    pc.phy_addr = board::ETH_PHY_ADDR;
    pc.reset_gpio_num = board::ETH_PHY_RST;
    esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&pc);
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(emac, phy);
    if (esp_eth_driver_install(&cfg, &s_eth) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet init failed (no PHY?)");
        return;
    }
    esp_netif_config_t nc = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&nc);
    esp_netif_attach(s_netif, esp_eth_new_netif_glue(s_eth));
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEth, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onIp, nullptr);
    ESP_ERROR_CHECK(esp_eth_start(s_eth));
    ESP_LOGI(TAG, "Ethernet started");
}

static esp_netif_ip_info_t info()
{
    esp_netif_ip_info_t i = {};
    if (s_netif) esp_netif_get_ip_info(s_netif, &i);
    return i;
}

uint32_t ip() { return info().ip.addr; }
uint32_t netmask() { return info().netmask.addr; }
bool linkUp() { return s_link; }

void mac(uint8_t out[6])
{
    if (!s_eth || esp_eth_ioctl(s_eth, ETH_CMD_G_MAC_ADDR, out) != ESP_OK) esp_read_mac(out, ESP_MAC_ETH);
}

} // namespace net
