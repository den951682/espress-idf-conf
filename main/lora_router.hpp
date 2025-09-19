#pragma once

#include <string>
#include <functional>
#include <sys/types.h>
#include <atomic>
#include "esp_err.h"
#include "lora_connection_task.cpp"
#include "protocol/protocol.hpp"
#include <memory>
#include "data_source.cpp"
#include <sys/socket.h>
#include <unistd.h>

class LoraRouter {
public:
	using MessageCallback = std::function<void(const uint8_t* data, size_t len)>;
	
	using DataSourceCallback = std::function<void(DataSource* ds)>;

    explicit LoraRouter(LoraConnectionTask&  loraConnection);
    ~LoraRouter();

    esp_err_t routeToLora(int32_t loraAddress, const uint8_t* data, size_t len);
    
    void setOnMessageCallback(MessageCallback cb);
    
    void setOnDataSourceCallback(DataSourceCallback cb);

	void disableDataSource(bool value);
	
private:
	static void routerTaskEntry(void* arg);
    void routerTaskLoop();
    LoraConnectionTask&  loraConnection_;
    TaskHandle_t taskHandle_{nullptr};
    MessageCallback onMessage_;
    DataSourceCallback onDataSource_;
    std::atomic<bool> _dataSourceDisabled{false};
    std::atomic<DataSource*> _dataSource{nullptr};
    bool musSendAddress = false;
};
