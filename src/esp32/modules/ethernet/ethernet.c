//
// Created by danil on 23.02.2025.
//

#include "ethernet.h"
#include <stdio.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_eth.h>
#include <driver/gpio.h>
#include "sdkconfig.h" // For configuration defines (e.g., CONFIG_ETH_*)

static const char *TAG = "EthernetManager";

// TODO: To be reimplemented