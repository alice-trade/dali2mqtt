#ifndef DALIMQTT_DALICOMMANDS_HXX
#define DALIMQTT_DALICOMMANDS_HXX
#include "dali/DaliСommon.hxx"

namespace daliMQTT::Commands {

    enum class CmdType : uint8_t {
        DirectArcPower = 0, // DACP (0xxxxxxx)
        StandardCmd    = 1  // Command (1xxxxxxx)
    };

    // Standard DALI 16-bit Commands (IEC 62386-102)
    enum class OpCode : uint8_t {
        Off                     = 0x00,
        Up                      = 0x01,
        Down                    = 0x02,
        StepUp                  = 0x03,
        StepDown                = 0x04,
        RecallMaxLevel          = 0x05,
        RecallMinLevel          = 0x06,
        StepDownAndOff          = 0x07,
        OnAndStepUp             = 0x08,
        EnableDapcSequence      = 0x09,
        GoToScene0              = 0x10,
        GoToScene1              = 0x11,
        GoToScene2              = 0x12,
        GoToScene3              = 0x13,
        GoToScene4              = 0x14,
        GoToScene5              = 0x15,
        GoToScene6              = 0x16,
        GoToScene7              = 0x17,
        GoToScene8              = 0x18,
        GoToScene9              = 0x19,
        GoToScene10             = 0x1A,
        GoToScene11             = 0x1B,
        GoToScene12             = 0x1C,
        GoToScene13             = 0x1D,
        GoToScene14             = 0x1E,
        GoToScene15             = 0x1F,
        Reset                   = 0x20,
        StoreActualLevel        = 0x21,
        SavePersistentVariables = 0x22,
        SetOperatingMode        = 0x23,
        ResetMemoryBank         = 0x24,
        IdentifyDevice          = 0x25,
        SetMaxLevel             = 0x2A,
        SetMinLevel             = 0x2B,
        SetSystemFailureLevel   = 0x2C,
        SetPowerOnLevel         = 0x2D,
        SetFadeTime             = 0x2E,
        SetFadeRate             = 0x2F,
        SetExtendedFadeTime     = 0x30,
        // TODO ...
        QueryStatus             = 0x90,
        QueryControlGear        = 0x91,
        QueryLampFailure        = 0x92,
        QueryLampPowerOn        = 0x93,
        QueryLimitError         = 0x94,
        QueryResetState         = 0x95,
        QueryMissingShortAddr   = 0x96,
        QueryVersionNumber      = 0x97,
        QueryContentDtr0        = 0x98,
        QueryDeviceType         = 0x99,
        QueryPhysicalMinLevel   = 0x9A,
        QueryPowerFailure       = 0x9B,
        QueryContentDtr1        = 0x9C,
        QueryContentDtr2        = 0x9D,
        QueryActualLevel        = 0xA0,
        QueryMaxLevel           = 0xA1,
        QueryMinLevel           = 0xA2,
        QueryPowerOnLevel       = 0xA3,
        QuerySystemFailureLevel = 0xA4,
        QueryFadeTimeFadeRate   = 0xA5,
        QueryGroups0_7          = 0xC0,
        QueryGroups8_15         = 0xC1,
        QueryRandomAddrH        = 0xC2,
        QueryRandomAddrM        = 0xC3,
        QueryRandomAddrL        = 0xC4,
        ReadMemoryLocation      = 0xC5
    };

    // Special Commands (IEC 62386-102)
    enum class SpecialOpCode : uint8_t {
        Terminate             = 0xA1,
        Dtr0                  = 0xA3,
        Initialise            = 0xA5,
        Randomise             = 0xA7,
        Compare               = 0xA9,
        Withdraw              = 0xAB,
        Ping                  = 0xAF,
        SearchAddrH           = 0xB1,
        SearchAddrM           = 0xB3,
        SearchAddrL           = 0xB5,
        ProgramShortAddr      = 0xB7,
        VerifyShortAddr       = 0xB9,
        QueryShortAddr        = 0xBB,
        PhysicalSelection     = 0xBD,
        EnableDeviceTypeX     = 0xC1,
        Dtr1                  = 0xC3,
        Dtr2                  = 0xC5,
        WriteMemoryLocation   = 0xC7,
        WriteMemoryLocNoReply = 0xC9
    };

    // DT8 Colour Control (IEC 62386-209)
    enum class DT8OpCode : uint8_t {
        SetTempTc             = 0xE7,
        SetTempRGB            = 0xEB,
        Activate              = 0xE2,
        QueryColourStatus     = 0xF7,
        QueryColourType       = 0xF8,
        QueryColourValue      = 0xF9
    };

    // Helper to construct 16-bit frame
    [[nodiscard]] constexpr uint16_t makeFrame16(const uint8_t addressByte, const uint8_t opcodeByte) {
        return (static_cast<uint16_t>(addressByte) << 8) | opcodeByte;
    }

    // Helper to construct standard command address byte
    [[nodiscard]] constexpr uint8_t makeAddressByte(DaliAddressType type, uint8_t address, CmdType cmdType) {
        uint8_t byte = 0;
        switch (type) {
        case DaliAddressType::Short:
            byte = (address & 0x3F) << 1;
            break;
        case DaliAddressType::Group:
            byte = 0x80 | ((address & 0x0F) << 1);
            break;
        case DaliAddressType::Broadcast:
            byte = 0xFE;
            break;
        case DaliAddressType::Special:
            byte = 0xA0 | (address & 0x1F);
            break;
        }
        if (cmdType == CmdType::StandardCmd) byte |= 0x01;
        return byte;
    }

    struct Frame {
        uint32_t data{0};
        uint8_t bits{16};
    };

    namespace Factory {
        [[nodiscard]] inline constexpr Frame DACP(uint8_t shortAddr, uint8_t level) {
            return { makeFrame16(makeAddressByte(AddressType::Short, shortAddr, CmdType::DirectArcPower), level), 16 };
        }
        [[nodiscard]] inline constexpr Frame DACPGroup(uint8_t groupAddr, uint8_t level) {
            return { makeFrame16(makeAddressByte(AddressType::Group, groupAddr, CmdType::DirectArcPower), level), 16 };
        }
        [[nodiscard]] inline constexpr Frame DACPBroadcast(uint8_t level) {
            return { makeFrame16(makeAddressByte(AddressType::Broadcast, 0, CmdType::DirectArcPower), level), 16 };
        }

        [[nodiscard]] inline constexpr Frame Command(uint8_t shortAddr, OpCode cmd) {
            return { makeFrame16(makeAddressByte(AddressType::Short, shortAddr, CmdType::StandardCmd), static_cast<uint8_t>(cmd)), 16 };
        }
        [[nodiscard]] inline constexpr Frame CommandGroup(uint8_t groupAddr, OpCode cmd) {
            return { makeFrame16(makeAddressByte(AddressType::Group, groupAddr, CmdType::StandardCmd), static_cast<uint8_t>(cmd)), 16 };
        }
        [[nodiscard]] inline constexpr Frame CommandBroadcast(OpCode cmd) {
            return { makeFrame16(makeAddressByte(AddressType::Broadcast, 0, CmdType::StandardCmd), static_cast<uint8_t>(cmd)), 16 };
        }

        [[nodiscard]] inline constexpr Frame Special(SpecialOpCode cmd, uint8_t data) {
            return { makeFrame16(static_cast<uint8_t>(cmd), data), 16 };
        }

        [[nodiscard]] inline constexpr Frame InputDeviceCmd(uint8_t addrByte, uint8_t instByte, uint8_t opCode) {
            uint32_t data = (static_cast<uint32_t>(addrByte) << 16) | (static_cast<uint32_t>(instByte) << 8) | opCode;
            return { data, 24 };
        }
    }
}

#endif // DALIMQTT_DALICOMMANDS_HXX