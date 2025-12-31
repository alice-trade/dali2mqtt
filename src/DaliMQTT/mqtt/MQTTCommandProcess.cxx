#include "mqtt/MQTTCommandProcess.hxx"
#include "mqtt/MQTTCommandHandler.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "MQTTCommandProcess";
    void MQTTCommandProcess::init() {
        if (m_queue) return;
        m_queue = xQueueCreate(64, sizeof(MqttMessage*));
        xTaskCreate(CommandProcessTask, "mqtt_cmd_task", 4096, this, 5, nullptr);
        ESP_LOGI(TAG, "DALI Command Processor started");
    }

    bool MQTTCommandProcess::enqueueMqttMessage(const char* topic, const int topic_len, const char* data, const int data_len) const {
        if (!m_queue) return false;

        auto* msg = new MqttMessage;
        msg->topic.assign(topic, topic_len);
        msg->payload.assign(data, data_len);

        const BaseType_t sent = xQueueSend(m_queue, &msg, 0);
        if (sent != pdPASS) {
            ESP_LOGE(TAG, "Queue full, dropping message: %s", msg->topic.c_str());
            delete msg;
            return false;
        }
        return true;
    }

    [[noreturn]] void MQTTCommandProcess::CommandProcessTask(void* arg) {
        const auto* self = static_cast<MQTTCommandProcess*>(arg);
        MqttMessage* msg = nullptr;

        while (true) {
            const BaseType_t xReturned = xQueueReceive(self->m_queue, &msg, portMAX_DELAY);
            if (xReturned == pdPASS) {
                if (msg) {
                    MQTTCommandHandler::handle(msg->topic, msg->payload);
                    delete msg;
                }
            }
        }
    }
} // daliMQTT