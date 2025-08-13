#pragma once

#include <string>
#include <functional>
#include <sys/types.h>
#include <mutex>
#include <atomic>
#include "esp_err.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}


class FdConnection {
public:
    using LineCallback = std::function<void(const std::string&)>;

    explicit FdConnection(int fd,
                            const char* taskName = "conn_read",
                            uint16_t stackSize = 4096,
                            UBaseType_t priority = tskIDLE_PRIORITY + 3,
                            BaseType_t core = tskNO_AFFINITY);
    ~FdConnection();

    FdConnection(const FdConnection&) = delete;
    FdConnection& operator=(const FdConnection&) = delete;

    FdConnection(FdConnection&& other) noexcept;
    FdConnection& operator=(FdConnection&& other) noexcept;

    void setLineCallback(LineCallback cb);
    bool isRunning() const;

    esp_err_t start();
    void stop();

    // Send API
    ssize_t sendBytes(const uint8_t* data, size_t len);
    ssize_t sendString(const std::string& s);
    ssize_t sendLine(const std::string& s); 

private:
    static constexpr size_t MAX_ACCUM = 8 * 1024;
    static void taskTrampoline(void* arg);
    void taskLoop();
    void moveFrom(FdConnection& other) noexcept;

    ssize_t writeAll(const uint8_t* data, size_t len);

    std::atomic<int> _fd{-1};
    const char* _taskName;
    uint16_t _stack;
    UBaseType_t _prio;
    BaseType_t _core;

    std::atomic<bool> _running{false};
    TaskHandle_t _task{nullptr};

    std::mutex _writeMtx;
    LineCallback _onLine;
    std::string _accum;
};
