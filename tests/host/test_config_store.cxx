// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "system/ConfigStore.hxx"

namespace HostTestUtils { void resetNvs(); }

using namespace daliMQTT;

TEST_CASE("ConfigStore In-Memory NVS Lifecycle", "[config][nvs][host]") {
    HostTestUtils::resetNvs();

    SECTION("Initial Load with empty NVS -> Defaults applied") {
        ConfigStore store;
        REQUIRE(store.init() == ESP_OK);
        REQUIRE(store.load() == ESP_OK);

        auto cfg = store.get();
        CHECK(cfg->mqttBaseTopic == "dali_bridge");
        CHECK(cfg->httpUser == "admin");
        CHECK(cfg->configuredFlag == false);
        CHECK_FALSE(store.isConfigured());
        CHECK(cfg->clientId.starts_with("dali_"));
    }

    SECTION("Save, Mutate, Reload across instances") {
        {
            ConfigStore store1;
            store1.init();
            store1.load();

            ConfigStructure custom = *store1.get();
            custom.wifiSsid = "Office_WiFi";
            custom.wifiPass = "SuperSecretPass";
            custom.mqttUri = "mqtts://10.0.0.1:8883";
            custom.mqttCaCert = "-----BEGIN CERTIFICATE-----\nABCDEF\n-----END CERTIFICATE-----";
            custom.buses[0].enabled = true;
            custom.buses[0].rxPin = 4;
            custom.buses[0].txPin = 5;
            custom.daliPollIntervalMs = 60000;
            custom.hassDiscoveryEnabled = true;

            REQUIRE(store1.save(custom) == ESP_OK);
            CHECK(store1.isConfigured() == true);
        }

        {
            ConfigStore store2;
            REQUIRE(store2.init() == ESP_OK);
            REQUIRE(store2.load() == ESP_OK);

            auto loaded = store2.get();
            CHECK(loaded->wifiSsid == "Office_WiFi");
            CHECK(loaded->wifiPass == "SuperSecretPass");
            CHECK(loaded->mqttUri == "mqtts://10.0.0.1:8883");
            CHECK(loaded->mqttCaCert.contains("BEGIN CERTIFICATE"));
            CHECK(loaded->buses[0].rxPin == 4);
            CHECK(loaded->buses[0].txPin == 5);
            CHECK(loaded->daliPollIntervalMs == 60000);
            CHECK(loaded->hassDiscoveryEnabled == true);
            CHECK(loaded->configuredFlag == true);
            CHECK(store2.isConfigured() == true);
        }
    }

    SECTION("Factory Reset clears stored parameters") {
        ConfigStore store;
        store.init();

        ConfigStructure custom = *store.get();
        custom.wifiSsid = "TempSSID";
        store.save(custom);

        REQUIRE(store.factoryReset() == ESP_OK);

        ConfigStore freshStore;
        freshStore.init();
        freshStore.load();
        CHECK(freshStore.get()->wifiSsid.empty());
        CHECK(freshStore.isConfigured() == false);
    }
}