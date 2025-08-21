#pragma once
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include "esp_log.h"

class Protocol {
public:
    using ReadyCallback = std::function<void()>;
    using QueueCallback = std::function<void(std::vector<uint8_t>)>;
    using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;

    Protocol() {
        sendReady = xSemaphoreCreateBinaryStatic(&sendReadyStorage_);
        if (!sendReady) {
            ESP_LOGE("Protocol", "Failed to create sendReady semaphore");
        }
    }

    virtual ~Protocol() {
		ESP_LOGI("Protocol", "destructor this=%p sendReady=%p", this, sendReady); 
        sendReady = nullptr;
    }
    
    Protocol(const Protocol&) = delete;
    Protocol& operator=(const Protocol&) = delete;
    Protocol(Protocol&&) = delete;
    Protocol& operator=(Protocol&&) = delete;

    virtual void init(WriteCallback writeCb, QueueCallback recvCb) = 0;

    virtual void appendReceived(const uint8_t* data, size_t len) = 0;

    virtual bool send(const uint8_t* data, size_t len) = 0;
    
    void close() { 
		isClosed.store(true);
		ESP_LOGI("Protocol", "Protocol closed");
	}
    
    void setReadyCallback(ReadyCallback cb) { readyCallback = std::move(cb); }
    
 protected:
    ReadyCallback readyCallback;
    WriteCallback writeCb;
    QueueCallback recvCb;
    SemaphoreHandle_t sendReady = nullptr;
    std::vector<uint8_t> buffer;
    std::atomic<bool> isClosed{false};
 private:
    StaticSemaphore_t  sendReadyStorage_; 
};
