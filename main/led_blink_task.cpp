#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "esp_log.h"
#include "parameter_store.cpp"
#include <sys/_stdint.h>

class LedBlinkTask {
public:
    LedBlinkTask(paramstore::ParameterStore& store, gpio_num_t pin)
        : store_(store), pin_(pin) {}

    void start(const char* name = "LedBlinkTask", uint32_t stackSize = 2048, UBaseType_t priority = 5) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << pin_;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);

        store_.onChange(paramstore::ParameterId::LedEnabled, [this](uint32_t id, const paramstore::Value& newValue){
            ledEnabled_ = std::get<bool>(newValue);
            ESP_LOGI(TAG, "LedEnabled changed: %d", ledEnabled_);
        });
        store_.onChange(paramstore::ParameterId::BlinkCount, [this](uint32_t id, const paramstore::Value& newValue){
            blinkCount_ = std::get<int32_t>(newValue);
            ESP_LOGI(TAG, "BlinkCount changed: %" PRId32, blinkCount_);
        });

        xTaskCreatePinnedToCore(
            &LedBlinkTask::taskEntry,
            name,
            stackSize,
            this,
            priority,
            &taskHandle_,
            tskNO_AFFINITY
        );
    }

    void stop() {
        if (taskHandle_) {
            vTaskDelete(taskHandle_);
            taskHandle_ = nullptr;
        }
    }

private:
    static constexpr const char* TAG = "LedBlinkTask";

    static void taskEntry(void* arg) {
        auto* self = static_cast<LedBlinkTask*>(arg);
        self -> run();
    }

    void run() {
        ESP_LOGI(TAG, "started");
        while (true) {
            if (ledEnabled_) {
                for (int i = 0; i < blinkCount_; i++) {
                    gpio_set_level(pin_, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(pin_, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                gpio_set_level(pin_, 1); 
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
    }

    paramstore::ParameterStore& store_;
    gpio_num_t pin_;
    TaskHandle_t taskHandle_ = nullptr;

    bool ledEnabled_ = true;
    int32_t blinkCount_ = 3;
};
