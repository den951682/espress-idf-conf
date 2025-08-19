#pragma once

#include <string>
#include <functional>
#include <sys/types.h>
#include <mutex>
#include <atomic>
#include "esp_err.h"
#include "protocol/protocol.hpp"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

struct SendItem {
    std::vector<uint8_t> data;
};


class FdConnection {
public:
    using DataCallback = std::function<void(const uint8_t* data, size_t len)>;
    using LineCallback = std::function<void(const std::string&)>;
    using ReadyCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;
    explicit FdConnection(int fd,	
    					    const char* passPhrase = nullptr,
                            const char* taskName = "conn_read",
                            uint16_t stackSize = 8192,
                            UBaseType_t priority = tskIDLE_PRIORITY + 3,
                            BaseType_t core = tskNO_AFFINITY);
    ~FdConnection();

    FdConnection(const FdConnection&) = delete;
    FdConnection& operator=(const FdConnection&) = delete;

    FdConnection(FdConnection&& other) noexcept;
    FdConnection& operator=(FdConnection&& other) noexcept;

    void setDataCallback(DataCallback cb) { _dataCB = std::move(cb); }
    void setLineCallback(LineCallback cb) { _onLine = std::move(cb); }
    void setReadyCallback(ReadyCallback cb) { _readyCallback = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { _closeCB = std::move(cb); }
    bool isRunning() const;

    esp_err_t start();
    void stop();

    // Send API
    ssize_t sendBytes(const uint8_t* data, size_t len);
    ssize_t sendString(const std::string& s);
    ssize_t sendLine(const std::string& s); 
    void enqueueSend(const uint8_t* data, size_t len);

private:
    static constexpr size_t MAX_ACCUM = 8 * 1024;
    static void taskTrampoline(void* arg);
    void taskLoop();
    void startSendTask();
    static void sendTask(void* arg);
    void moveFrom(FdConnection& other) noexcept;

    ssize_t writeAll(const uint8_t* data, size_t len);
    static std::string toHex(const std::vector<uint8_t>& data);

    Protocol* protocol;
    QueueHandle_t sendQueue;
    std::atomic<int> _fd{-1};
    const char* _passPhrase;
    const char* _taskName;
    uint16_t _stack;
    UBaseType_t _prio;
    BaseType_t _core;

    std::atomic<bool> _running{false};
    std::atomic<bool> _guarded{false};
    TaskHandle_t _task{nullptr};

    std::mutex _writeMtx;
    DataCallback _dataCB;
    LineCallback _onLine;
    ReadyCallback _readyCallback;
    CloseCallback _closeCB;
};
