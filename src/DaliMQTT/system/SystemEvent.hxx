#ifndef DALIMQTT_SYSTEMEVENT_HXX
#define DALIMQTT_SYSTEMEVENT_HXX
#include <cstdint>

namespace daliMQTT {

enum class SystemEventType : uint8_t {
    NetworkConnected,
    NetworkDisconnected,
    MqttConnected,
    MqttDisconnected,
    FactoryResetRequested
};
enum class NetworkStatus : uint8_t {
    Disconnected,
    Connecting,
    Connected,
    ProvisioningAp
};
} // namespace daliMQTT
#endif // DALIMQTT_SYSTEMEVENT_HXX
