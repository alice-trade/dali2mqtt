#include "unity.h"
#include "DaliMQTT/mqtt/MQTTClient.hxx"
#include <string>

using namespace daliMQTT;

// --- Test State ---
static bool onConnected_called = false;
static bool onDisconnected_called = false;
static bool onData_called = false;
static std::string last_topic;
static std::string last_data;

// --- Test Helper Functions ---
void reset_flags() {
    onConnected_called = false;
    onDisconnected_called = false;
    onData_called = false;
    last_topic.clear();
    last_data.clear();
}

// --- Test Cases ---
TEST_CASE("MQTTClient callbacks are triggered correctly", "[mqtt]") {
    reset_flags();

    auto& mqtt = MQTTClient::getInstance();

    // Назначаем колбэки
    mqtt.onConnected = []() { onConnected_called = true; };
    mqtt.onDisconnected = []() { onDisconnected_called = true; };
    mqtt.onData = [](const std::string& topic, const std::string& data) {
        onData_called = true;
        last_topic = topic;
        last_data = data;
    };

    // --- Имитируем событие CONNECTED ---
    esp_mqtt_event_t connect_event = {};
    connect_event.event_id = MQTT_EVENT_CONNECTED;
    // Получаем приватный метод через friend-класс или делаем его public для теста
    // В данном случае, так как он static, мы можем вызвать его напрямую.
    MQTTClient::mqttEventHandler(&mqtt, nullptr, MQTT_EVENT_CONNECTED, &connect_event);
    TEST_ASSERT_TRUE(onConnected_called);
    TEST_ASSERT_FALSE(onDisconnected_called);
    TEST_ASSERT_FALSE(onData_called);
    reset_flags();

    // --- Имитируем событие DATA ---
    esp_mqtt_event_t data_event = {};
    data_event.event_id = MQTT_EVENT_DATA;
    data_event.topic = (char*)"test/topic";
    data_event.topic_len = strlen(data_event.topic);
    data_event.data = (char*)"test_payload";
    data_event.data_len = strlen(data_event.data);
    MQTTClient::mqttEventHandler(&mqtt, nullptr, MQTT_EVENT_DATA, &data_event);

    TEST_ASSERT_TRUE(onData_called);
    TEST_ASSERT_EQUAL_STRING("test/topic", last_topic.c_str());
    TEST_ASSERT_EQUAL_STRING("test_payload", last_data.c_str());
    reset_flags();

    // --- Имитируем событие DISCONNECTED ---
    esp_mqtt_event_t disconnect_event = {};
    disconnect_event.event_id = MQTT_EVENT_DISCONNECTED;
    MQTTClient::mqttEventHandler(&mqtt, nullptr, MQTT_EVENT_DISCONNECTED, &disconnect_event);
    TEST_ASSERT_TRUE(onDisconnected_called);
    reset_flags();
}

// --- Test Runner ---
extern "C" void run_mqtt_client_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mqtt_client_callbacks_are_triggered_correctly);
    UNITY_END();
}