#ifndef DALIMQTT_DALICOMMON_HXX
#define DALIMQTT_DALICOMMON_HXX
namespace daliMQTT::common {

    constexpr uint32_t BaudRate = 1200;
    constexpr uint32_t TimeElement_Us = 416;
    constexpr uint32_t StopBit_Us = 2 * TimeElement_Us;

    enum class FrameType : uint8_t {
        Backward = 8,
        Standard = 16,
        InputDevice = 24
    };

    enum class AddressType {
        Short,
        Group,
        Broadcast,
        Special
    };

    enum class Command : uint16_t {
        Off = 0x00,
        Up = 0x01,
        Down = 0x02,
        StepUp = 0x03,
        StepDown = 0x04,
        RecallMax = 0x05,
        RecallMin = 0x06,
        StepDownAndOff = 0x07,
        OnAndStepUp = 0x08,
        Reset = 0x20,
        QueryStatus = 0x90,
        QueryActualLevel = 0xA0,
        // todo (incomplete)
    };

    enum class DriverStatus {
        Ok,
        BusBusy,
        Timeout,
        FrameError,
        Collision,
        QueueFull
    };

    struct DaliResult {
        DriverStatus status;
        uint32_t data;
        uint8_t bit_length;
    };

}
#endif //DALIMQTT_DALICOMMON_HXX