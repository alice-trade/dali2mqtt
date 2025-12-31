#include "unity.h"
#include "mqtt/MQTTClient.hxx"
#include "mqtt/MQTTCommandProcess.hxx"
#include "system/ConfigManager.hxx"

using namespace daliMQTT;

static void test_mqtt_client_init_state() {
    auto& client = MQTTClient::getInstance();

    TEST_ASSERT_EQUAL(MqttStatus::DISCONNECTED, client.getStatus());

    client.init("mqtt://127.0.0.1", "test_id", "/status", "", "");

    TEST_ASSERT_EQUAL(MqttStatus::DISCONNECTED, client.getStatus());
}

static void test_command_processor_queue() {
    auto& processor = MQTTCommandProcess::getInstance();
    processor.init();

    const char* topic = "test/topic";
    const char* payload = "{}";

    bool result = processor.enqueueMqttMessage(topic, strlen(topic), payload, strlen(payload));
    TEST_ASSERT_TRUE(result);
}

void run_mqtt_logic_tests() {
    RUN_TEST(test_mqtt_client_init_state);
    RUN_TEST(test_command_processor_queue);
}