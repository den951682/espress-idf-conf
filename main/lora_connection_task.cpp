#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "parameter_store.cpp"

#define LORA_UART_NUM UART_NUM_1
#define TXD_PIN GPIO_NUM_17
#define RXD_PIN GPIO_NUM_16
#define LORA_AUX_GPIO GPIO_NUM_18
#define LORA_M0_GPIO GPIO_NUM_19
#define LORA_M1_GPIO GPIO_NUM_22

struct LoraMessage {
    char data[64];
    size_t len;
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
    
    bool sendMessage(const std::string& text) {
        if (txQueue_) {
			LoraMessage msg;
			msg.len = std::min(text.size(), sizeof(msg.data) - 1);
	        memcpy(msg.data, text.c_str(), msg.len);
	        msg.data[msg.len] = '\0';	
	        return xQueueSend(txQueue_, &msg, 0) == pdTRUE;
        }
        return false;
    }

    bool receiveMessage(std::string& out) {
	    if (rxQueue_) {
	        LoraMessage m;
	        if (xQueueReceive(rxQueue_, &m, 0) == pdTRUE) {
	            out.assign(m.data, m.len);
	            return true;
	        }
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
				if (waitForAux(500)) {
			        uart_write_bytes(LORA_UART_NUM, m.data, m.len);
			        ESP_LOGW(TAG, "LoRa TX done");
			    } else {
					xQueueSendToFront(txQueue_, &m, 0);
			        ESP_LOGW(TAG, "LoRa not ready for TX");
			    }
            } 
            
			TickType_t now = xTaskGetTickCount();
			if (lastSuccessTick == 0 || (now - lastSuccessTick) > pdMS_TO_TICKS(60000)) {                 
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
	    uint8_t buf[256];
	
	    while (true) {
			if(isConfigured) {
		        int len = uart_read_bytes(LORA_UART_NUM, buf, sizeof(buf), 500 / portTICK_PERIOD_MS);
		        if (len > 0) {
					lastSuccessTick = xTaskGetTickCount(); 
		            LoraMessage m;
		            m.len = std::min((size_t)len, sizeof(m.data) - 1);
		            memcpy(m.data, buf, m.len);
		            m.data[m.len] = '\0'; 	
		            ESP_LOGI(TAG, "RX: %s", m.data);
		
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
		    uint8_t cmd[3] = {0xC1, 0x00, 0x00};
		    uint8_t resp[16];
		    uart_flush(LORA_UART_NUM);
		    uart_write_bytes(LORA_UART_NUM, (const char*)cmd, 3);
		
		    int len = uart_read_bytes(LORA_UART_NUM, resp, sizeof(resp), 200 / portTICK_PERIOD_MS);
		    lora_set_mode(false, false);
		    if (len > 0) {
		        ESP_LOGI(TAG, "Connected got %d bytes from LoRa", len);
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
	
	    if (!waitForAux(500)) {
	        ESP_LOGW(TAG, "Configuration timeout (AUX low too long)");
	        return false;
	    }
	
	    lora_set_mode(true, true); 
	
	    uint8_t cfg[9] = {0xC2, 0x00, 0x01, 0x00, 0x1A, 0x17, 0x44, 0x32, 0x00};
	    uart_flush(LORA_UART_NUM);
	    uart_write_bytes(LORA_UART_NUM, (const char*)cfg, sizeof(cfg));
	
	    waitForAux(500);
	
	    uint8_t resp[16];
	    int len = uart_read_bytes(LORA_UART_NUM, resp, sizeof(resp), 500 / portTICK_PERIOD_MS);
	    lora_set_mode(false, false);
	
	    if (len > 0) {
	        ESP_LOGI(TAG, "LoRa response (%d bytes):", len);
	        ESP_LOG_BUFFER_HEX(TAG, resp, len);
	        ESP_LOGI(TAG, "LoRa configured");
	        return true;
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
};
