#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "parameter_store.cpp"

class UptimeTask {
public:
    UptimeTask(paramstore::ParameterStore& store) : store_(store) {}

    void start(const char* name = "UptimeTask", uint32_t stackSize = 2048, UBaseType_t priority = 5) {
        xTaskCreatePinnedToCore(
            &UptimeTask::taskEntry,   
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
        auto* self = static_cast<UptimeTask*>(arg);
        self -> run();
    }

    void run() {
        ESP_LOGI("UptimeTask", "started");
        int32_t counter = 0;
        while (true) {
            esp_err_t err = store_.setInt(paramstore::ParameterId::Uptime, counter++);
            if (err != ESP_OK) {
                ESP_LOGW("UptimeTask", "setInt failed: %s", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); 
        }
    }

    paramstore::ParameterStore& store_;
    TaskHandle_t taskHandle_ = nullptr;
};
