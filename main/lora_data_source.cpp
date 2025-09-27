#include "freertos/queue.h"
#include "lora_connection_task.cpp"

class LoraDataSource : public DataSource {
public:
    explicit LoraDataSource(LoraConnectionTask& conn, uint16_t addr)
        : conn_(conn), defaultAddr_(addr) {
			conn_.setTxDoneCallback([this](){
		        if(_txDoneCB) {
					_txDoneCB();
				}
		    });
		}

    ssize_t write(bool withAck, const uint8_t* data, size_t len) override {
        while(!conn_.sendMessage(withAck, data, len, defaultAddr_)){
			vTaskDelay(pdMS_TO_TICKS(5));	
		};
        return static_cast<ssize_t>(len);
    }

    ssize_t read(uint8_t* buf, size_t maxLen) override {
        if (conn_.rxQueue_ == nullptr) {
            return -1;
        }
        LoraMessage msg;
        if (xQueueReceive(conn_.rxQueue_, &msg, 0) == pdTRUE) {
            size_t copyLen = (msg.len < maxLen) ? msg.len : maxLen;
            memcpy(buf, msg.data.data(), copyLen);
            //ESP_LOGI("LoraDataSource", "RX msg: len=%u", (unsigned)msg.len);
    		//ESP_LOG_BUFFER_HEX("LoraDataSource", buf, copyLen);
            return static_cast<ssize_t>(copyLen);
        }
        return 0; 
    }
    
    void close() override {
		
	}
	
	bool isReady() override {
		return conn_.isReady();
	}
	
	uint32_t estimateTxTimeMs(size_t payloadLen) override {
		return conn_.estimateTxTimeMs(payloadLen);
	}

private:
    LoraConnectionTask& conn_;
    uint16_t defaultAddr_ = -1;
};
