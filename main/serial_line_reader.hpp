#pragma once
#include <string>
#include <functional>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class SerialLineReader {
public:
    using LineCallback = std::function<void(const std::string&)>;

    SerialLineReader(uart_port_t port = UART_NUM_0, int baud = 115200, size_t bufSize = 1024);
    ~SerialLineReader();

    void start(LineCallback cb);
    void stop();

private:
    static void taskFunc(void* pvParams);
    void taskLoop();

    uart_port_t _port;
    int _baud;
    size_t _bufSize;
    TaskHandle_t _taskHandle;
    LineCallback _callback;
};
