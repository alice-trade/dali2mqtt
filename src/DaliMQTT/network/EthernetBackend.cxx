// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/EthernetBackend.hxx"
#include "system/SystemEvent.hxx"
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>

#if defined(CONFIG_DALI2MQTT_ETH_TYPE_INTERNAL_RMII)
#include <esp_eth_mac_esp.h>
#include <esp_eth_phy_lan87xx.h>
#endif

namespace daliMQTT {

static constexpr char TAG[] = "Ethernet";

EthernetBackend::~EthernetBackend() {
    stop();
}

esp_err_t EthernetBackend::init() {
    if (m_initialized) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop init failed");

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    m_ethNetif = esp_netif_new(&cfg);
    if (!m_ethNetif) {
        ESP_LOGE(TAG, "Failed to create Ethernet Netif");
        return ESP_FAIL;
    }

#if defined(CONFIG_DALI2MQTT_ETH_TYPE_SPI_W5500)
    ESP_RETURN_ON_ERROR(setupW5500(), TAG, "W5500 Init Failed");
#elif defined(CONFIG_DALI2MQTT_ETH_TYPE_INTERNAL_RMII)
    ESP_RETURN_ON_ERROR(setupInternalRMII(), TAG, "Internal RMII Init Failed");
#else
    ESP_LOGE(TAG, "No Ethernet hardware configured!");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &ethEventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ethEventHandler, this, nullptr));

    m_initialized = true;
    ESP_LOGI(TAG, "Ethernet subsystem initialized.");
    return ESP_OK;
}

esp_err_t EthernetBackend::setupW5500() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_DALI2MQTT_ETH_SPI_MOSI_PIN,
        .miso_io_num = CONFIG_DALI2MQTT_ETH_SPI_MISO_PIN,
        .sclk_io_num = CONFIG_DALI2MQTT_ETH_SPI_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(static_cast<spi_host_device_t>(CONFIG_DALI2MQTT_ETH_SPI_HOST), &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    spi_device_interface_config_t devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = CONFIG_DALI2MQTT_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_DALI2MQTT_ETH_SPI_CS_PIN,
        .queue_size = 20
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(static_cast<spi_host_device_t>(CONFIG_DALI2MQTT_ETH_SPI_HOST), &devcfg);
    w5500_config.int_gpio_num = CONFIG_DALI2MQTT_ETH_SPI_INT_PIN;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = CONFIG_DALI2MQTT_ETH_SPI_RST_PIN;

    esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &m_ethHandle), TAG, "Install driver failed");

    uint8_t base_mac[6];
    esp_read_mac(base_mac, ESP_MAC_ETH);
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(m_ethHandle, ETH_CMD_S_MAC_ADDR, base_mac), TAG, "Set MAC failed");

    return esp_netif_attach(m_ethNetif, esp_eth_new_netif_glue(m_ethHandle));
}

#if defined(CONFIG_DALI2MQTT_ETH_TYPE_INTERNAL_RMII)
esp_err_t EthernetBackend::setupInternalRMII() {
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config); // для LAN8720

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &m_ethHandle), TAG, "Install driver failed");
    return esp_netif_attach(m_ethNetif, esp_eth_new_netif_glue(m_ethHandle));
}
#endif

esp_err_t EthernetBackend::start(const ConfigStructure&) {
    if (!m_ethHandle) return ESP_ERR_INVALID_STATE;
    m_status.store(NetworkStatus::Connecting);
    return esp_eth_start(m_ethHandle);
}

void EthernetBackend::stop() {
    if (m_ethHandle) {
        esp_eth_stop(m_ethHandle);
        m_status.store(NetworkStatus::Disconnected);
    }
}

etl::string<16> EthernetBackend::getIpAddress() const {
    if (m_status.load() != NetworkStatus::Connected || !m_ethNetif) {
        return "0.0.0.0";
    }
    esp_netif_ip_info_t ipInfo{};
    if (esp_netif_get_ip_info(m_ethNetif, &ipInfo) == ESP_OK) {
        char buf[16];
        esp_ip4addr_ntoa(&ipInfo.ip, buf, sizeof(buf));
        return buf;
    }
    return "0.0.0.0";
}

void EthernetBackend::startMdns(const char* hostname, const char* clientId) {
    if (m_mdnsStarted) return;
    if (mdns_init() != ESP_OK) return;

    mdns_hostname_set(hostname);
    char instanceName[64];
    snprintf(instanceName, sizeof(instanceName), "DALI Bridge (%s)", clientId);
    mdns_instance_name_set(instanceName);

    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    ESP_LOGI(TAG, "mDNS responder started: http://%s.local", hostname);
    m_mdnsStarted = true;
}

void EthernetBackend::ethEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    auto* self = static_cast<EthernetBackend*>(arg);
    if (!self) return;

    if (eventBase == ETH_EVENT) {
        switch (eventId) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link UP");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet Link DOWN");
            self->m_status.store(NetworkStatus::Disconnected);
            if (self->m_eventQueue) {
                constexpr auto ev = SystemEventType::NetworkDisconnected;
                xQueueSend(self->m_eventQueue, &ev, 0);
            }
            break;
        default:
            break;
        }
    } else if (eventBase == IP_EVENT && eventId == IP_EVENT_ETH_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(eventData);
        self->m_status.store(NetworkStatus::Connected);
        ESP_LOGI(TAG, "Ethernet Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        if (self->m_eventQueue) {
            constexpr auto ev = SystemEventType::NetworkConnected;
            xQueueSend(self->m_eventQueue, &ev, 0);
        }
    }
}

} // namespace daliMQTT