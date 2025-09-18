#pragma once

#include <string>
#include <functional>
#include <sys/types.h>
#include <atomic>
#include "esp_err.h"
#include "lora_connection_task.cpp"
#include "protocol/protocol.hpp"
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

class LoraRouter {
public:
	using MessageCallback = std::function<void(const uint8_t* data, size_t len, int32_t fromAddr)>;

    explicit LoraRouter(LoraConnectionTask&  loraConnection);
    ~LoraRouter();

    esp_err_t routeToLora(int32_t loraAddress, const uint8_t* data, size_t len);
    
    void setOnMessageCallback(MessageCallback cb);

	void disableFd(bool value);
	
private:
	static void routerTaskEntry(void* arg);
    void routerTaskLoop();
    LoraConnectionTask&  loraConnection_;
    TaskHandle_t taskHandle_{nullptr};
    MessageCallback onMessage_;
    std::atomic<bool> _fdDisabled{false};
};
