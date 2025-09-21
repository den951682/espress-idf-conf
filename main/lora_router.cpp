#include "lora_router.hpp"
#include "esp_log.h"
#include "lora_data_source.cpp"

static const char* TAG = "LoraRouter";

LoraRouter::LoraRouter(LoraConnectionTask&  loraConnection)
    : loraConnection_(loraConnection) 
{
    ESP_LOGI(TAG, "LoraRouter created");
    xTaskCreatePinnedToCore(
        &LoraRouter::routerTaskEntry,
        "lora_router",
        4096,
        this,
        tskIDLE_PRIORITY + 2,
        &taskHandle_,
        tskNO_AFFINITY
    );
}

LoraRouter::~LoraRouter() {
    ESP_LOGI(TAG, "LoraRouter destroyed");
    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }
}

esp_err_t LoraRouter::routeToLora(int32_t loraAddress, const uint8_t* data, size_t len) {
	if(musSendAddress) {
		loraConnection_.sendAddress(loraAddress);
		musSendAddress = false;
	}
	while(!loraConnection_.sendMessage(data, len, loraAddress)){
		vTaskDelay(pdMS_TO_TICKS(5));	
	};
    return ESP_OK;
}

void LoraRouter::setOnMessageCallback(MessageCallback cb) {
    onMessage_ = std::move(cb);
}

void LoraRouter::setOnDataSourceCallback(DataSourceCallback cb) {
    onDataSource_ = std::move(cb);
}

void LoraRouter::disableDataSource(bool value) {
    _dataSourceDisabled.store(value);
    if (value) musSendAddress = true;
}

void LoraRouter::routerTaskEntry(void* arg) {
    auto* self = static_cast<LoraRouter*>(arg);
    self->routerTaskLoop();
}

void LoraRouter::routerTaskLoop() {
    while (true) {
        if (loraConnection_.rxQueue_ != nullptr) {
			if(!_dataSource.load()){
	            LoraMessage msg;
	            if (xQueueReceive(loraConnection_.rxQueue_, &msg, portMAX_DELAY) == pdTRUE) {	                
	                if (!_dataSourceDisabled.load()) {
						ESP_LOGI(TAG, "Create LoraDataSource");
						DataSource* ds = _dataSource.load();
					  	if(!ds)	{
							uint16_t addr = (static_cast<uint16_t>(msg.data[0]) << 8) |
                					static_cast<uint16_t>(msg.data[1]);
                			ESP_LOGI(TAG, "Got srcAddr=0x%04X (%u)", (unsigned)addr, (unsigned)addr);
			    			ds = new LoraDataSource(loraConnection_, addr); 
							_dataSource.store(ds);
							if(onDataSource_) onDataSource_(ds);
							if (msg.len > 2) {
					            LoraMessage newMsg;
					            newMsg.len = msg.len - 2;
					            memcpy(newMsg.data.data(), msg.data.data() + 2, newMsg.len);
					            newMsg.dstAddr = msg.dstAddr;
					     
					            if (loraConnection_.rxQueue_) {
					                if (xQueueSendToFront(loraConnection_.rxQueue_, &newMsg, 0) != pdTRUE) {
					                    ESP_LOGW(TAG, "Failed to push message back to queue");
					                } else {
					                    ESP_LOGI(TAG, "Message pushed to front of queue, len=%u", newMsg.len);
					                }
					            }
					        }
						}
					} else if (onMessage_) {
	                    onMessage_(msg.data.data(), msg.len);
	                }
	            }
            }
        } 
		vTaskDelay(pdMS_TO_TICKS(100));
    }
}
