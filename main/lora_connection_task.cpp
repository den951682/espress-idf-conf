#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "parameter_store.cpp"
#include <inttypes.h>
#include <cstdint>
#include <bitset>

#define LORA_UART_NUM UART_NUM_1
#define TXD_PIN GPIO_NUM_17
#define RXD_PIN GPIO_NUM_16
#define LORA_AUX_GPIO GPIO_NUM_18
#define LORA_M0_GPIO GPIO_NUM_19
#define LORA_M1_GPIO GPIO_NUM_22

struct LoraMessage {
    static constexpr size_t MAX_LEN = 256;
    uint8_t type = 0x01;               
    uint8_t msgId = 0;                
    uint16_t dstAddr = 0;
    size_t len = 0;
    std::array<uint8_t, MAX_LEN> data{};
};

using TxDoneCallback = std::function<void()>;

class LoraConnectionTask {
public:
	LoraConnectionTask(paramstore::ParameterStore& store)
        : store_(store) {}
    
    void start(const char* name = "LoraTxTask", uint32_t stackSize = 4096, UBaseType_t priority = 5) {
		gpio_config_t io_conf = {};
	    io_conf.mode = GPIO_MODE_OUTPUT;
	    io_conf.pin_bit_mask = (1ULL << LORA_M0_GPIO) | (1ULL << LORA_M1_GPIO);
	    gpio_config(&io_conf);
	    
	    gpio_config_t aux_conf = {};
		aux_conf.intr_type = GPIO_INTR_DISABLE;
		aux_conf.mode = GPIO_MODE_INPUT;
		aux_conf.pin_bit_mask = (1ULL << LORA_AUX_GPIO);
		aux_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		aux_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		gpio_config(&aux_conf);
    
        const uart_config_t uart_config = {
		    .baud_rate = 9600,
		    .data_bits = UART_DATA_8_BITS,
		    .parity    = UART_PARITY_DISABLE,
		    .stop_bits = UART_STOP_BITS_1,
		    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		    .rx_flow_ctrl_thresh = 0,
		    .source_clk = UART_SCLK_DEFAULT,
		    .flags = 0
		};
	    uart_driver_install(LORA_UART_NUM, 1024, 0, 0, NULL, 0);
	    uart_param_config(LORA_UART_NUM, &uart_config);
	    uart_set_pin(LORA_UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

		txQueue_ = xQueueCreate(1, sizeof(LoraMessage));
        rxQueue_ = xQueueCreate(1, sizeof(LoraMessage));
        ackToSendQueue_ = xQueueCreate(1, sizeof(LoraMessage));
        ackQueue_ = xQueueCreate(1, sizeof(uint8_t));
        
        store_.onChange(paramstore::ParameterId::LoraChannel, [this](uint32_t id, const paramstore::Value& newValue){
            channel = std::get<int32_t>(newValue);
            ESP_LOGI(TAG, "Lora channel changed: %" PRId32, channel);
            isConfigured = false;
        });
        store_.onChange(paramstore::ParameterId::LoraAddress, [this](uint32_t id, const paramstore::Value& newValue){
            address = std::get<int32_t>(newValue);
            ESP_LOGI(TAG, "Lora address changed: %" PRId32, address);
            isConfigured = false;
        });
        
        xTaskCreatePinnedToCore(
            &LoraConnectionTask::txTaskEntry,
            name,
            stackSize,
            this,
            priority,
            &txTaskHandle_,
            tskNO_AFFINITY
        );
        
        xTaskCreatePinnedToCore(
            &LoraConnectionTask::rxTaskEntry,
            "LoraRxTask",
            4096,
            this,
            priority,
            &rxTaskHandle_,
            tskNO_AFFINITY
        );
        
        xTaskCreatePinnedToCore(
            &LoraConnectionTask::ackToSendTaskEntry,
            "LoraAckToSendTask",
            4096,
            this,
            priority,
            &ackToSendTaskHandle_,
            tskNO_AFFINITY
        );
    }

    void stop() {
        if (txTaskHandle_) {
            vTaskDelete(txTaskHandle_);
            txTaskHandle_ = nullptr;
        }
        if (rxTaskHandle_) {
            vTaskDelete(rxTaskHandle_);
            rxTaskHandle_ = nullptr;
        }
        if (ackToSendTaskHandle_) {
            vTaskDelete(ackToSendTaskHandle_);
            ackToSendTaskHandle_ = nullptr;
        }
    }
    
    void sendAddress(uint16_t destAddr = 0xffff) {
	    uint8_t buf[2];
	    buf[0] = static_cast<uint8_t>((address >> 8) & 0xFF);  
	    buf[1] = static_cast<uint8_t>(address & 0xFF); 
	    addressForAck = destAddr; 
	    sendMessage(true, buf, sizeof(buf), destAddr);
	}
    
    bool sendMessage(bool withAck, const uint8_t* data, size_t len, uint16_t destAddr = 0xffff) {
        if (txQueue_) {
			if (uxQueueSpacesAvailable(txQueue_) == 0) {	
	    		//ESP_LOGI("LoraConnection", "sendMessage queue is full");
	        	return false;
	    	}
	    	/*
	    	ESP_LOGI("LoraConnection", 
	         "sendMessage called: len=%u, destAddr=0x%04X, srcAddr=0x%04X",
	         (unsigned)len, (unsigned)destAddr, (unsigned)address);
	        
		    if (len > 0 && data) {
		        ESP_LOG_BUFFER_HEX("LoraConnection", data, len);
		    } else {
		        ESP_LOGW("LoraConnection", "sendMessage called with empty data");
		    }
		    */
			LoraMessage msg;
			msg.type = 0x00;
			if(withAck) msg.type = 0x01; 
			msg.msgId = nextMsgId_++;
	        msg.len = std::min(len, msg.data.size());
	        memcpy(msg.data.data(), data, msg.len);
	        msg.dstAddr = destAddr;
	        return xQueueSend(txQueue_, &msg, 0) == pdTRUE;
        }
        return false;
    }
    
     bool isReady() {
	    return gpio_get_level(LORA_AUX_GPIO) == 1;
	 }
	 
	 uint32_t estimateTxTimeMs(size_t payloadLen) {
        int airRateIndex = reg5 & 0b111;
        int airRate = airRateTable[airRateIndex];
        size_t totalLen = payloadLen + 6;
        uint32_t durationMs = static_cast<uint32_t>((totalLen * 8.0f / airRate) * 1000.0f);
        /*ESP_LOGI(TAG, "Payload=%" PRIu32 " bytes, Total=%" PRIu32 " bytes, AirRate=%d bps, Time=%" PRIu32 " ms",
             static_cast<uint32_t>(payloadLen),
             static_cast<uint32_t>(totalLen),
             airRate,
             durationMs); */    
        return durationMs;
    }
	 
	 void setTxDoneCallback(TxDoneCallback cb) { _txDoneCB = std::move(cb); }

   	 QueueHandle_t rxQueue_ = nullptr;
   	 QueueHandle_t txQueue_ = nullptr;
   	 int32_t addressForAck = 0;
    
private:	
    inline static const int airRateTable[8] = {
        2400, 2400, 2400, 4800, 9600, 19200, 38400, 62500
    };
    
	static constexpr const char* TAG = "LoraTask";

    static void txTaskEntry(void* arg) {
        auto* self = static_cast<LoraConnectionTask*>(arg);
        self -> txRun();
    }
    
    static void rxTaskEntry(void* arg) {
        auto* self = static_cast<LoraConnectionTask*>(arg);
        self->rxRun();
    }
    
    static void ackToSendTaskEntry(void* arg) {
        auto* self = static_cast<LoraConnectionTask*>(arg);
        self->ackToSendRun();
    }

	void txRun() {
        ESP_LOGI(TAG, "started");
        uint8_t ackId;
        bool hasAck = false;
        LoraMessage m;
        int retries = 0;
		int base = 100;
		int backoff = 0;
        while (true) {
			if (!waitForAux(2000)) {
				 ESP_LOGI(TAG, "LoRa TX in progress");
				 vTaskDelay(pdMS_TO_TICKS(20)); 
				 continue;
			}
			if (isConfigured && txQueue_) {
		         if(xQueueReceive(txQueue_, &m, 0) == pdTRUE) {
					retries = 0;
					vTaskDelay(pdMS_TO_TICKS(4)); 
					std::array<uint8_t, 6 + LoraMessage::MAX_LEN> buf{};
	
				    buf[0] = static_cast<uint8_t>((m.dstAddr >> 8) & 0xFF);  // ADDH
				    buf[1] = static_cast<uint8_t>(m.dstAddr & 0xFF);         // ADDL
				    buf[2] = static_cast<uint8_t>(channel & 0xFF);        // CHAN
				    buf[3] = m.type;
				    buf[4] = m.msgId; 
					buf[5] = static_cast<uint8_t>(m.len); ;
				    size_t totalLen = m.len + 6;
				    memcpy(buf.data() + 6, m.data.data(), m.len);
				    ESP_LOGI(TAG, "LoRa TX started: dst=0x%04X, chan=%" PRId32 ", type=0x%02X, msgId=%u, len=%zu",
					         m.dstAddr,
					         channel,         
					         m.type,
					         m.msgId,
					         totalLen);
				    ESP_LOG_BUFFER_HEX(TAG, buf.data(), totalLen);
	   				while (xQueueReceive(ackQueue_, &ackId, 0) == pdTRUE) {}
	   				if(m.type == 1) hasAck = false; else hasAck = true;
	   				do {
						retries++;
						if(retries > 1) ESP_LOGI(TAG, "Lora TX retry %d", retries);
						if(retries > 5) retries = 5;
						xSemaphoreTake(mutex, portMAX_DELAY);
					    uart_write_bytes(LORA_UART_NUM, buf.data(), totalLen);
					    xSemaphoreGive(mutex);
					    if(waitForAux(250)) {
					    	ESP_LOGI(TAG, "LoRa TX done, destAddr=0x%04X, chan=%ld,  msgId=%u, len=%zu", m.dstAddr, static_cast<long>(channel),  m.msgId, totalLen);
					    } else {
							ESP_LOGI(TAG, "LoRa TX in progress, destAddr=0x%04X, chan=%ld, len=%zu", m.dstAddr, static_cast<long>(channel), totalLen);
						};
						if(!hasAck) {
						    if(xQueueReceive(ackQueue_, &ackId,  pdMS_TO_TICKS(2000)) == pdTRUE) {
								hasAck = (ackId == m.msgId);
							} else {
								hasAck = false;
								ESP_LOGI(TAG, "Ack for msgId=%u false",  m.msgId);
							}
							if(hasAck) {
								ESP_LOGI(TAG, "Ack for msgId=%u true", m.msgId);
							} else {
								ESP_LOGE(TAG, "Ack for msgId=%u false", m.msgId);
							}
						}
						backoff = (rand() % (1 << retries)) * base;
						vTaskDelay(pdMS_TO_TICKS(backoff));
					} while(!hasAck);
				
					if(_txDoneCB && waitForAux(2000) && uxQueueSpacesAvailable(txQueue_) > 0) {
						_txDoneCB();
					}
				} 
			}
           
            
			TickType_t now = xTaskGetTickCount();
			if (!isConfigured || lastSuccessTick == 0 || (now - lastSuccessTick) > pdMS_TO_TICKS(60000)) {                 
				bool ok = lora_check_connected();
			    if (ok) {
				    if (!isConfigured) {
			    	    if(lora_configure()){
				            isConfigured = true;
				            ESP_LOGI(TAG, "LoRa is configured and active.");
			            } 
			        }
			        lastSuccessTick = now; 
			    } else {
			        if (isConfigured) {
			            ESP_LOGW(TAG, "LoRa disconnected, clearing flag.");
			        }
			        isConfigured = false;
			    }
			}
		   	vTaskDelay(pdMS_TO_TICKS(20)); 
	    }
    }
    
    void ackToSendRun() {
        ESP_LOGI(TAG, "ack to send started");
        LoraMessage m;
        std::array<uint8_t, 6> buf{};
        while (true) {
			if (!waitForAux(2000)) {
				 ESP_LOGI(TAG, "LoRa TX in progress");
				 vTaskDelay(pdMS_TO_TICKS(20)); 
				 continue;
			}
	        if (isConfigured && ackToSendQueue_ && xQueueReceive(ackToSendQueue_, &m, 0) == pdTRUE) {
				vTaskDelay(pdMS_TO_TICKS(4)); 
			    buf[0] = static_cast<uint8_t>((m.dstAddr >> 8) & 0xFF);  // ADDH
			    buf[1] = static_cast<uint8_t>(m.dstAddr & 0xFF);         // ADDL
			    buf[2] = static_cast<uint8_t>(channel & 0xFF);        // CHAN
			    buf[3] = 0x02;
			    buf[4] = m.msgId; 
				buf[5] = 0;
			    size_t totalLen = 6;
			    ESP_LOGI(TAG, "LoRa ack TX started: dst=0x%04X, chan=%" PRId32 ", type=0x%02X, msgId=%u",
				         m.dstAddr,
				         channel,         
				         m.type,
				         m.msgId);
			    ESP_LOG_BUFFER_HEX(TAG, buf.data(), totalLen);
			    xSemaphoreTake(mutex, portMAX_DELAY);
			    vTaskDelay(pdMS_TO_TICKS(30));
			    uart_write_bytes(LORA_UART_NUM, buf.data(), totalLen);
			    vTaskDelay(pdMS_TO_TICKS(30));
			    xSemaphoreGive(mutex);
			    if(waitForAux(250)) {
				    ESP_LOGI(TAG, "LoRa ack TX done, destAddr=0x%04X, chan=%ld", m.dstAddr, static_cast<long>(channel));
				} else {
					ESP_LOGI(TAG, "LoRa ack TX in progress, destAddr=0x%04X, chan=%ld", m.dstAddr, static_cast<long>(channel));
				};
			}            
		   	vTaskDelay(pdMS_TO_TICKS(20)); 
	    }
    }
    
    void rxRun() {
	    ESP_LOGI(TAG, "RX task started");
	    std::array<uint8_t, 256> buf{};
	    int len = 0;
	    while (true) {
			if(isConfigured) {
		        len = len +  uart_read_bytes(LORA_UART_NUM,
                                      buf.data() + len,
                                      buf.size() - len,
                                      25 / portTICK_PERIOD_MS);

	            if (len > 0) {
	                for (;;) {
	                    if (len >= static_cast<int>(buf.size())) break;
	
	                    int more = uart_read_bytes(LORA_UART_NUM,
	                                               buf.data() + len,
	                                               buf.size() - len,
	                                               10 / portTICK_PERIOD_MS);
	                    if (more <= 0) break;
	                    len += more;
	                }
	                if (len < 3) {
					    continue; 
					}
	                
	                ESP_LOGI(TAG, "Lora RX: %d bytes", len);
	                ESP_LOG_BUFFER_HEX(TAG, buf.data(), len);
	                lastSuccessTick = xTaskGetTickCount();
					uint8_t payloadLen = buf[2];
					
					if (len < 3 + payloadLen) {
        				continue; 
    				}
    				LoraMessage m;
		            m.type = buf[0];
					m.msgId = buf[1];
					m.len = payloadLen;
					ESP_LOGI(TAG, "RX header: type=0x%02X (%s), msgId=%u",
					         m.type,
					         (m.type == 0x00 ? "MSG_NO_ACK" :
					         (m.type == 0x01 ? "MSG_WITH_ACK" :
					         (m.type == 0x02 ? "ACK" : "???"))),
					         m.msgId);
					memcpy(m.data.data(), buf.data() + 3, m.len);
               	    len = len - payloadLen - 3;
               	    if(len > 0) {
						size_t shift = payloadLen + 3;
						if (shift < buf.size()) {
						    std::memmove(buf.data(),
						                 buf.data() + shift,
						                 buf.size() - shift);
						} else {
						    len = 0;
						}   
					}
               	    if(m.type == 0x02) {
						if (ackQueue_) {
					        xQueueSend(ackQueue_, &m.msgId, portMAX_DELAY);
					    }
					    ESP_LOGI(TAG, "Got ACK for msgId=%u", m.msgId);
						continue;
				    };
               	    if(m.type == 0x01 && addressForAck > 0) {
						LoraMessage ack;
					    ack.type = 0x02;
					    ack.msgId = m.msgId;
					    ack.dstAddr = addressForAck;
					    ack.len = 0;
					    xQueueSend(ackToSendQueue_, &ack, portMAX_DELAY);   
					}
		            if (rxQueue_) {
		                xQueueSend(rxQueue_, &m, portMAX_DELAY);
		            }
		        } else {
					//ESP_LOGW(TAG, "NO RX %s", isReady() ? "ready" : "busy");
				}
	        }
	        vTaskDelay(pdMS_TO_TICKS(10));
	    }
	}
    
    bool lora_check_connected() {
		if(waitForAux()) {
			lora_set_mode(true, true);
		    uint8_t cmd[3] = {0xC1, 0x00, 0x07};
		    uint8_t resp[16];
		    uart_flush(LORA_UART_NUM);
		    uart_write_bytes(LORA_UART_NUM, (const char*)cmd, 3);
		
		    int len = uart_read_bytes(LORA_UART_NUM, resp, sizeof(resp), 200 / portTICK_PERIOD_MS);
		    lora_set_mode(false, false);
		    if (len > 0) {
		        ESP_LOGI(TAG, "Connected got %d bytes from LoRa", len);
		        ESP_LOG_BUFFER_HEX(TAG, resp, len);
		        return true;
		    } else {
		        ESP_LOGW(TAG, "Not connected No response from LoRa");
		        return false;
	    	}    
    	} else {
			ESP_LOGW(TAG, "Not connected timeout");
			return false;
		}
	}
	
	bool lora_configure() {
	    ESP_LOGI(TAG, "Configuring LoRa module...");
	    lora_set_mode(true, true); 
	    if (!waitForAux(500)) {
	        ESP_LOGW(TAG, "Configuration timeout (AUX low too long)");
	        lora_set_mode(false, false); 
	        return false;
	    }
	
	    uart_set_baudrate(LORA_UART_NUM, 9600);
	      
	    uint8_t addh = (address >> 8) & 0xFF;
	    uint8_t addl = address & 0xFF;
	    uint8_t chan = channel & 0xFF;
	
	    reg5 = setLow3Bits(0b01100000, 0);
	    ESP_LOGI(TAG, "reg5 = 0x%02X", reg5);
	    uint8_t cfg[11] = {
	        0xC0, 
	        0x00,
	        0x08,
	        addh, 
	        addl, 
	        reg5, //0x62 2400bps,
	        0x00,
	        chan,
	        0x53, //0x43 - disable LBT, 0x53 - enable LBT
	        0x00,
	        0x00
	    };
	    ESP_LOGI(TAG, "LoRa configuration");
	    ESP_LOG_BUFFER_HEX(TAG, cfg, sizeof(cfg));
	    uart_flush(LORA_UART_NUM);
	    uart_flush_input(LORA_UART_NUM);
	    uart_write_bytes(LORA_UART_NUM, cfg, sizeof(cfg));
	
	    waitForAux(500);
	
	    uint8_t resp[16];
	    int len = uart_read_bytes(LORA_UART_NUM, resp, sizeof(resp), 500 / portTICK_PERIOD_MS);
	    lora_set_mode(false, false);
	
	    if (len > 0) {
	        ESP_LOGI(TAG, "LoRa response (%d bytes):", len);
	        ESP_LOG_BUFFER_HEX(TAG, resp, len);
	        if(resp[0]==0xff && resp[1]==0xff && resp[2]==0xff) {
				ESP_LOGE(TAG, "LoRa not configured: WRONG configuration");
	        	return false;
			} else {
	        	ESP_LOGI(TAG, "LoRa configured");
	        	return true;
	        }
	    } else {
	        ESP_LOGW(TAG, "No response from LoRa during config");
	        return false;
	    }
	}

	
	void lora_set_mode(bool m0, bool m1) {
	    gpio_set_level(LORA_M0_GPIO, m0 ? 1 : 0);
	    gpio_set_level(LORA_M1_GPIO, m1 ? 1 : 0);
	    vTaskDelay(pdMS_TO_TICKS(50)); 
	}
    
    bool waitForAux(TickType_t timeoutMs = 500) {
	    TickType_t start = xTaskGetTickCount(); ;
	    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeoutMs)) {
	        if (gpio_get_level(LORA_AUX_GPIO) == 1) {
	            return true; 
	        }
	        vTaskDelay(pdMS_TO_TICKS(20)); 
	    }
	    return false;
	}
	
    
	inline uint8_t setLow3Bits(uint8_t byte, uint8_t value) {
	    return (byte & ~0b111) | (value & 0b111);
	}

	paramstore::ParameterStore& store_;
    TaskHandle_t txTaskHandle_ = nullptr;
    TaskHandle_t rxTaskHandle_ = nullptr;
    TaskHandle_t ackToSendTaskHandle_ = nullptr;
    QueueHandle_t ackToSendQueue_ = nullptr;
    QueueHandle_t ackQueue_ = nullptr;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    volatile bool isConfigured = false;
    volatile TickType_t lastSuccessTick = 0;
    int32_t address = 1;
    int32_t channel = 18;
    TxDoneCallback _txDoneCB;
    uint8_t nextMsgId_ = 0;
    uint8_t reg5;
};
