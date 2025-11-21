#ifndef DALIMQTT_DALISCENEMANAGEMENT_HXX
#define DALIMQTT_DALISCENEMANAGEMENT_HXX

namespace daliMQTT
{
    // Map: short_address -> brightness_level (0-254)
    using SceneDeviceLevels = std::map<uint8_t, uint8_t>;

    class DaliSceneManagement {
    public:
        DaliSceneManagement(const DaliSceneManagement&) = delete;
        DaliSceneManagement& operator=(const DaliSceneManagement&) = delete;

        static DaliSceneManagement& getInstance() {
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