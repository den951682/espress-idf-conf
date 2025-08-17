#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "parameter_store.cpp"
#include "esp_adc/adc_oneshot.h"

#define JOY_X_PIN    34
#define JOY_Y_PIN    35
#define JOY_SW_PIN   32

class JoystickTask {
public:
    JoystickTask(paramstore::ParameterStore& params) : store_(params) {}

    void start(const char* name = "JoystickTask", uint32_t stackSize = 2048, UBaseType_t priority = 5) {
        xTaskCreatePinnedToCore(
            &JoystickTask::taskEntry,
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
    static void taskEntry(void* arg) {
        auto* self = static_cast<JoystickTask*>(arg);
        self->run();
    }

    void run() {
        adc_oneshot_unit_handle_t adc1_handle;
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    		.ulp_mode = ADC_ULP_MODE_DISABLE
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &chan_cfg));
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_7, &chan_cfg));

        gpio_set_direction((gpio_num_t)JOY_SW_PIN, GPIO_MODE_INPUT);
        gpio_pullup_en((gpio_num_t)JOY_SW_PIN);

        while (true) {
            int rawX = 0, rawY = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &rawX));
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_7, &rawY));
            rawX = std::clamp(rawX, 0, 4095);
            rawY = std::clamp(rawY, 0, 4095);
            //int sw   = gpio_get_level((gpio_num_t)JOY_SW_PIN);
            store_.setInt(paramstore::ParameterId::JoystickX, rawX);
            store_.setInt(paramstore::ParameterId::JoystickY, rawY);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    paramstore::ParameterStore& store_;
    TaskHandle_t taskHandle_ = nullptr;
};
