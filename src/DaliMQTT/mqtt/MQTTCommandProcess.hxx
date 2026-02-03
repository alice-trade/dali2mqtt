// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALICOMMANDPROCESSOR_HXX
#define DALIMQTT_DALICOMMANDPROCESSOR_HXX

namespace daliMQTT {
    struct __attribute__((packed)) RingBufHeader;

    class MQTTCommandProcess {
        public:
            MQTTCommandProcess(const MQTTCommandProcess&) = delete;
            MQTTCommandProcess& operator=(const MQTTCommandProcess&) = delete;
            static MQTTCommandProcess& Instance() {
                static MQTTCommandProcess instance;
                return instance;
            }

            void init();
            bool enqueueMqttMessage(const char* topic, int topic_len, const char* data, int data_len) const;

        private:
            MQTTCommandProcess() = default;

            [[noreturn]] static void CommandProcessTask(void* arg);

            RingbufHandle_t m_ringbuf{nullptr};
    };
} // daliMQTT

#endif //DALIMQTT_DALICOMMANDPROCESSOR_HXX