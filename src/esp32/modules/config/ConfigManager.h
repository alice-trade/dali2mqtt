//
// Created by danil on 23.02.2025.
//

#ifndef LIGHTNINGCONFIG_H
#define LIGHTNINGCONFIG_H

#include <esp_err.h>

esp_err_t config_manager_init(void);
const char* config_manager_get_mqtt_broker_url(void);
const char* config_manager_get_mqtt_client_id(void);
int config_manager_get_dali_rx_gpio(void);
int config_manager_get_dali_tx_gpio(void);
int config_manager_get_status_poll_interval_sec(void);
const char* config_manager_get_mqtt_command_topic_base(void);
const char* config_manager_get_mqtt_status_topic_base(void);

#endif // CONFIG_MANAGER_H

#endif //LIGHTNINGCONFIG_H
