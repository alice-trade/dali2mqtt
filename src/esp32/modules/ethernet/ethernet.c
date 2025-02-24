//
// Created by danil on 23.02.2025.
//

#include "ethernet.h"
#include <stdio.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <driver/gpio.h>
#include "sdkconfig.h" // For configuration defines (e.g., CONFIG_ETH_*)

static const char *TAG = "EthernetManager";

static esp_eth_handle_t eth_handle = nullptr;
static esp_netif_t *eth_netif = nullptr;

// Event handler for Ethernet events.
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    uint8_t mac_addr[6] = {0};

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }


// Event handler for IP events (specifically, DHCP).
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

esp_err_t eth_init() {
    // Initialize TCP/IP stack.
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
       ESP_LOGE(TAG,"Failed to init netif: %s", esp_err_to_name(ret));
       return ret;
    }

    // Create an event loop.
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create default Ethernet netif instance
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&cfg);

    // Set default hostname
    ESP_ERROR_CHECK(esp_netif_set_hostname(eth_netif, "esp32-dali"));


   // Init MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.phy_addr = CONFIG_ETH_PHY_ADDRESS;
    phy_config.reset_gpio_num = CONFIG_ETH_PHY_RST_GPIO;
    mac_config.smi_mdc_gpio_num = CONFIG_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
    #if CONFIG_ETH_PHY_IP101
        esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    #elif CONFIG_ETH_PHY_RTL8201
        esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
    #elif CONFIG_ETH_PHY_LAN87XX
        esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    #elif CONFIG_ETH_PHY_DP83848
        esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    #elif CONFIG_ETH_PHY_KSZ80XX
        esp_eth_phy_t *phy = esp_eth_phy_new_ksz80xx(&phy_config);
    #endif
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    ret = esp_eth_driver_install(&config, ð_handle);
        if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Failed to install eth driver: %s", esp_err_to_name(ret));
        return ret;
    }

    // Attach Ethernet driver to TCP/IP stack.
    ret = esp_netif_attach(eth_netif, esp_eth_new_netif_glue