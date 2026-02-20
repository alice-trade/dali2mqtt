// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DALIMQTT_DALISCENEMANAGEMENT_HXX
#define DALIMQTT_DALISCENEMANAGEMENT_HXX

namespace daliMQTT
{
    using SceneDeviceLevels = std::array<uint8_t, 64>;

    class DaliSceneManagement {
    public:
        DaliSceneManagement(const DaliSceneManagement&) = delete;
        DaliSceneManagement& operator=(const DaliSceneManagement&) = delete;

        static DaliSceneManagement& Instance() {
            static DaliSceneManagement instance;
            return instance;
        }

        void init();

        esp_err_t activateScene(uint8_t sceneId) const;
        esp_err_t saveScene(uint8_t sceneId, const SceneDeviceLevels& levels) const;
        [[nodiscard]] SceneDeviceLevels getSceneLevels(uint8_t sceneId) const;

    private:
        DaliSceneManagement() = default;
    };

} // daliMQTT

#endif //DALIMQTT_DALISCENEMANAGEMENT_HXX