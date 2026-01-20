#include "mqtt/MQTTCommandProcess.hxx"
#include "mqtt/MQTTCommandHandler.hxx"

namespace daliMQTT {
    static constexpr char TAG[] = "MQTTCommandProcess";
    static constexpr size_t RING_BUFFER_SIZE = 12 * 1024;

    struct RingBufHeader {
        uint16_t topic_len;
        uint16_t payload_len;
    };

    void MQTTCommandProcess::init() {
        if (m_ringbuf) return;
        m_ringbuf = xRingbufferCreate(RING_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
        if (m_ringbuf == nullptr) {
            ESP_LOGE(TAG, "Failed to create Ring Buffer.");
            return;
        }
        xTaskCreate(CommandProcessTask, "mqtt_cmd_task", 6144, this, 5, nullptr);
        ESP_LOGI(TAG, "DALI Command Processor started");
    }

    bool MQTTCommandProcess::enqueueMqttMessage(const char* topic, const int topic_len, const char* data, const int data_len) const {
        if (!m_ringbuf) return false;
        constexpr size_t header_size = sizeof(RingBufHeader);
        const size_t total_size = header_size + topic_len + data_len;
        void* item_ptr = nullptr;
        const esp_err_t sent = xRingbufferSendAcquire(m_ringbuf, &item_ptr, total_size, pdMS_TO_TICKS(10));

        if (sent != ESP_OK) {
            ESP_LOGW(TAG, "Ring Buffer busy/full, dropped message");
            return false;
        }

        auto* header = reinterpret_cast<RingBufHeader*>(item_ptr);
        header->topic_len = static_cast<uint16_t>(topic_len);
        header->payload_len = static_cast<uint16_t>(data_len);

        char* data_start = static_cast<char*>(item_ptr) + header_size;
        memcpy(data_start, topic, topic_len);
        memcpy(data_start + topic_len, data, data_len);

        xRingbufferSendComplete(m_ringbuf, item_ptr);

        return true;
    }

    [[noreturn]] void MQTTCommandProcess::CommandProcessTask(void* arg) {
        const auto* self = static_cast<MQTTCommandProcess*>(arg);
        size_t item_size = 0;

        while (true) {
            void* item = xRingbufferReceive(self->m_ringbuf, &item_size, portMAX_DELAY);
            if (item != nullptr) {
                const auto* header = static_cast<RingBufHeader*>(item);
                const char* data_ptr = static_cast<char*>(item) + sizeof(RingBufHeader);
                if (item_size == sizeof(RingBufHeader) + header->topic_len + header->payload_len) {
                    std::string topic_str(data_ptr, header->topic_len);
                    std::string payload_str(data_ptr + header->topic_len, header->payload_len);
                    MQTTCommandHandler::handle(topic_str, payload_str);
                } else {
                    ESP_LOGE(TAG, "RingBuffer item size mismatch! Expected %u, got %u",
                        (sizeof(RingBufHeader) + header->topic_len + header->payload_len),
                        item_size);
                }
                vRingbufferReturnItem(self->m_ringbuf, item);
            }
        }
    }
} // daliMQTT