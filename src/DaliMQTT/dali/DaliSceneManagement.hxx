// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DALIMQTT_DALISCENEMANAGEMENT_HXX
#define DALIMQTT_DALISCENEMANAGEMENT_HXX

namespace daliMQTT
{
    using SceneDeviceLevels = std::array<uint8_t, 64>;

    class DaliSceneManagement {
    public:
        DaliSceneManagement() = default;

        static esp_err_t activateScene(uint8_t bus_id, uint8_t sceneId) ;
        static esp_err_t saveScene(uint8_t bus_id, uint8_t sceneId, const SceneDeviceLevels& levels) ;
        [[nodiscard]] static SceneDeviceLevels getSceneLevels(uint8_t bus_id, uint8_t sceneId);

    private:
    };

} // daliMQTT

#endif //DALIMQTT_DALISCENEMANAGEMENT_HXX