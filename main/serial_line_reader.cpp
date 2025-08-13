#include "serial_line_reader.hpp"
#include "esp_log.h"
#include <cstring>

SerialLineReader::SerialLineReader(uart_port_t port, int baud, size_t bufSize)
    : _port(port), _baud(baud), _bufSize(bufSize), _taskHandle(nullptr) 
{
    uart_config_t uart_config = {
        .baud_rate = _baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {}
    };
    uart_driver_install(_port, _bufSize * 2, 0, 0, nullptr, 0);
    uart_param_config(_port, &uart_config);
    uart_set_pin(_port, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

SerialLineReader::~SerialLineReader() {
    stop();
    uart_driver_delete(_port);
}

void SerialLineReader::start(LineCallback cb) {
    _callback = cb;
    if (_taskHandle == nullptr) {
        xTaskCreate(taskFunc, "SerialLineReaderTask", 4096, this, 5, &_taskHandle);
    }
}

void SerialLineReader::stop() {
    if (_taskHandle) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
}

void SerialLineReader::taskFunc(void* pvParams) {
    static_cast<SerialLineReader*>(pvParams)->taskLoop();
}

void SerialLineReader::taskLoop() {
    uint8_t* data = new uint8_t[_bufSize];
    std::string line;
    line.reserve(_bufSize);

    while (true) {
        int len = uart_read_bytes(_port, data, _bufSize - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = static_cast<char>(data[i]);
                if (c == '\r') continue;
                if (c == '\n') {
                    if (!line.empty() && _callback) {
                        _callback(line);
                    }
                    line.clear();
                } else {
                    if (line.size() < _bufSize - 1) {
                        line.push_back(c);
                    }
                }
            }
        }
    }
    delete[] data;
}
