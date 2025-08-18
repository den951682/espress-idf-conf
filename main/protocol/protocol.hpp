#pragma once
#include <vector>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Protocol {
public:
    using ReadyCallback = std::function<void()>;
    using QueueCallback = std::function<void(std::vector<uint8_t>)>;
    using WriteCallback = std::function<void(const uint8_t* data, size_t len)>;

    virtual ~Protocol() = default;

    virtual void init(WriteCallback writeCb, QueueCallback recvCb) = 0;

    virtual void appendReceived(const uint8_t* data, size_t len) = 0;

    virtual bool send(const uint8_t* data, size_t len) = 0;
    
    void setReadyCallback(ReadyCallback cb) { readyCallback = std::move(cb); }
    
 protected:
    ReadyCallback readyCallback;
    WriteCallback writeCb;
    QueueCallback recvCb;
    SemaphoreHandle_t sendReady;
    std::vector<uint8_t> buffer;
};
