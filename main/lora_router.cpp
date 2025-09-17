#include "lora_router.hpp"
#include "esp_log.h"

static const char* TAG = "LoraRouter";

LoraRouter::LoraRouter(LoraConnectionTask&  loraConnection)
    : loraConnection_(loraConnection) 
{
    ESP_LOGI(TAG, "LoraRouter created");
    xTaskCreatePinnedToCore(
        &LoraRouter::routerTaskEntry,
        "lora_router",
        4096,
        this,
        tskIDLE_PRIORITY + 2,
        &taskHandle_,
        tskNO_AFFINITY
    );
}

LoraRouter::~LoraRouter() {
    ESP_LOGI(TAG, "LoraRouter destroyed");
    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }
}

esp_err_t LoraRouter::routeToLora(int32_t loraAddress, const uint8_t* data, size_t len) {
    ESP_LOGI(TAG, "Routing to LoRa address %ld, length=%u", (long)loraAddress, (unsigned)len);
    ESP_LOG_BUFFER_HEX("LoraRouter", data, len);
    loraConnection_.sendMessage(data, len, loraAddress);
    return ESP_OK;
}

void LoraRouter::setOnMessageCallback(MessageCallback cb) {
    onMessage_ = std::move(cb);
}

void LoraRouter::disableFd(bool value) {
    _fdDisabled.store(value);
}

void LoraRouter::routerTaskEntry(void* arg) {
    auto* self = static_cast<LoraRouter*>(arg);
    self->routerTaskLoop();
}

void LoraRouter::routerTaskLoop() {
    while (true) {
        if (loraConnection_.rxQueue_ != nullptr) {
            LoraMessage msg;
            if (xQueueReceive(loraConnection_.rxQueue_, &msg, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "Router received message from %ld, len=%u",
                         (long)msg.srcAddr, (unsigned)msg.len);
                ESP_LOG_BUFFER_HEX(TAG, msg.data.data(), msg.len);
                
                if (!_fdDisabled.load()) {
					if(sockPair_[0] == -1) {
					    
				    }
				} else if (onMessage_) {
                    onMessage_(msg.data.data(), msg.len, msg.srcAddr);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
