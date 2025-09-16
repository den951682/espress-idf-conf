#pragma once

#include <string>
#include <functional>
#include <sys/types.h>
#include <atomic>
#include "esp_err.h"
#include "lora_connection_task.cpp"
#include "protocol/protocol.hpp"
#include <memory>

class LoraRouter {
public:
    explicit LoraRouter(LoraConnectionTask&  loraConnection);
    ~LoraRouter();

    esp_err_t routeToLora(int32_t loraAddress, const uint8_t* data, size_t len);

private:
    LoraConnectionTask&  loraConnection_;
};
