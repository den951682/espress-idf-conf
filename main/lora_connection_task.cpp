#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "parameter_store.cpp"
#include <inttypes.h>

#define LORA_UART_NUM UART_NUM_1
#define TXD_PIN GPIO_NUM_17
#define RXD_PIN GPIO_NUM_16
#define LORA_AUX_GPIO GPIO_NUM_18
#define LORA_M0_GPIO GPIO_NUM_19
#define LORA_M1_GPIO GPIO_NUM_22

struct LoraMessage {
    static constexpr size_t MAX_LEN = 256;

    uint16_t addr = 0;                       
    size_t len = 0;                          
    std::array<uint8_t, MAX_LEN> data{};    
};

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

		txQueue_ = xQueueCreate(10, sizeof(LoraMessage));
        rxQueue_ = xQueueCreate(10, sizeof(LoraMessage));
        
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
    }
    
    bool sendMessage(const uint8_t* data, size_t len, uint16_t destAddr = 0xffff) {
        if (txQueue_) {
			LoraMessage msg;
	        msg.len = std::min(len, msg.data.size());
	        memcpy(msg.data.data(), data, msg.len);
	        msg.addr = destAddr;
	        return xQueueSend(txQueue_, &msg, 0) == pdTRUE;
        }
        return false;
    }

private:	
	static constexpr const char* TAG = "LoraTask";

    static void txTaskEntry(void* arg) {
        auto* self = static_cast<LoraConnectionTask*>(arg);
        self -> txRun();
    }
    
    static void rxTaskEntry(void* arg) {
        auto* self = static_cast<LoraConnectionTask*>(arg);
        self->rxRun();
    }

	void txRun() {
        ESP_LOGI(TAG, "started");
        while (true) {
			LoraMessage m;
	        if (isConfigured && txQueue_ && xQueueReceive(txQueue_, &m, 0) == pdTRUE) {
				std::array<uint8_t, 3 + LoraMessage::MAX_LEN> buf{};

			    buf[0] = static_cast<uint8_t>((m.addr >> 8) & 0xFF);  // ADDH
			    buf[1] = static_cast<uint8_t>(m.addr & 0xFF);         // ADDL
			    buf[2] = static_cast<uint8_t>(channel & 0xFF);        // CHAN
			
			    size_t totalLen = std::min(m.len, m.data.size()) + 3;
			    memcpy(buf.data() + 3, m.data.data(), std::min(m.len, m.data.size()));
			    //ESP_LOG_BUFFER_HEX(TAG, buf.data(), totalLen);
			    uart_write_bytes(LORA_UART_NUM, buf.data(), totalLen);
			    ESP_LOGI(TAG, "LoRa TX done, addr=0x%04X, chan=%ld, len=%zu",
             	m.addr, static_cast<long>(channel), m.len);
			} else if (txQueue_ && m.len > 0){
				xQueueSendToFront(txQueue_, &m, 0);
			    ESP_LOGW(TAG, "LoRa not ready for TX");
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
		   	vTaskDelay(pdMS_TO_TICKS(100)); 
	    }
    }
    
    void rxRun() {
	    ESP_LOGI(TAG, "RX task started");
	    std::array<uint8_t, 256> buf{};
	    
	    while (true) {
			if(isConfigured) {
		        int len = uart_read_bytes(LORA_UART_NUM,
                                      buf.data(),
                                      buf.size(),
                                      500 / portTICK_PERIOD_MS);

	            if (len > 0) {
	                for (;;) {
	                    if (len >= static_cast<int>(buf.size())) break;
	
	                    int more = uart_read_bytes(LORA_UART_NUM,
	                                               buf.data() + len,
	                                               buf.size() - len,
	                                               20 / portTICK_PERIOD_MS);
	                    if (more <= 0) break;
	                    len += more;
	                }
	
	                ESP_LOG_BUFFER_HEX(TAG, buf.data(), len);
	                lastSuccessTick = xTaskGetTickCount();
		            LoraMessage m;
               		m.len = std::min(static_cast<size_t>(len), m.data.size());
                	memcpy(m.data.data(), buf.data(), m.len);

                	ESP_LOGI(TAG, "RX: %.*s", static_cast<int>(m.len), m.data.data());
	                
		            if (rxQueue_) {
		                xQueueSend(rxQueue_, &m, 0);
		            }
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
	
	    uint8_t cfg[11] = {
	        0xC0, 
	        0x00,
	        0x08,
	        addh, 
	        addl, 
	        0x62,
	        0x00,
	        chan,
	        0x43,
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
	        vTaskDelay(pdMS_TO_TICKS(5)); 
	    }
	    return false;
	}

	paramstore::ParameterStore& store_;
    TaskHandle_t txTaskHandle_ = nullptr;
    TaskHandle_t rxTaskHandle_ = nullptr;
    QueueHandle_t txQueue_ = nullptr;
    QueueHandle_t rxQueue_ = nullptr;
    volatile bool isConfigured = false;
    volatile TickType_t lastSuccessTick = 0;
    int32_t address = 1;
    int32_t channel = 18;
};
