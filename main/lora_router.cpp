#include "lora_router.hpp"
#include "esp_log.h"

static const char* TAG = "LoraRouter";

LoraRouter::LoraRouter(LoraConnectionTask&  loraConnection)
    : loraConnection_(loraConnection) 
{
    ESP_LOGI(TAG, "LoraRouter created");
}

LoraRouter::~LoraRouter() {
    ESP_LOGI(TAG, "LoraRouter destroyed");
}

esp_err_t LoraRouter::routeToLora(int32_t loraAddress, const uint8_t* data, size_t len) {
    ESP_LOGI(TAG, "Routing to LoRa address %ld, length=%u", (long)loraAddress, (unsigned)len);
    ESP_LOG_BUFFER_HEX("LoraRouter", data, len);
    loraConnection_.sendMessage(data, len, loraAddress);
    return ESP_OK;
}
