#include <cstring>
#include <stdio.h>
#include <stdbool.h>
#include <sys/unistd.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "serial_line_reader.hpp"
#include "bt_spp_server.hpp"
#include "fd_connection.hpp"
#include "parameter_store.cpp"
#include "parameter_sync.cpp"

using namespace paramstore;

enum class AppCommandType : uint8_t {
    SendAllParameters,
};

struct CustomData {
    std::string message;
    std::vector<uint8_t> payload;
};

struct AppCommand {
    AppCommandType type;
    std::variant<std::monostate, CustomData> data;
};

QueueHandle_t appQueue = nullptr;
SerialLineReader reader;
BtSppServer bt;
FdConnection* g_conn = nullptr;
ParameterStore store;
ParameterSync parameterSync(store);

static void setupStore() {
	ESP_ERROR_CHECK(store.begin());
    store.setupDefaults();
    store.loadFromNvs();
}

static void start_bt() {
	bt.setOnEvent([](BtSppServer::Event e, int err){
        ESP_LOGI("APP", "Event=%d err=0x%x", (int)e, err);
    });

    bt.setOnFdReady([](int fd){
        ESP_LOGI("APP", "FD ready: %d", fd);
        delete g_conn; g_conn = nullptr;
        g_conn = new FdConnection(fd);
      
        g_conn->setCloseCallback([](){
			parameterSync.removeConnection();
		});
        g_conn->setLineCallback([](const std::string& line){
           ESP_LOGI("APP", "RX line: %s", line.c_str());
           g_conn->sendLine("OK");
        });
        if (g_conn->start() != ESP_OK) { 
		   ESP_LOGE("APP", "start failed"); 
		   delete g_conn; g_conn = nullptr;
	    }   
	    parameterSync.setConnection(g_conn);
        AppCommand cmd{AppCommandType::SendAllParameters, {}};
        xQueueSend(appQueue, &cmd, 0);
    });
    bt.start();
}

void appTask(void* arg) {
    AppCommand cmd;
    for (;;) {
        if (xQueueReceive(appQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case AppCommandType::SendAllParameters:
                    parameterSync.sendAllParametersInfo();
                    parameterSync.sendAllParameters();
                    break;

                default:
                    break;
            }
        }
    }
}

extern "C" void app_main(void) {
	appQueue = xQueueCreate(16, sizeof(AppCommand));
    xTaskCreatePinnedToCore(appTask, "appTask", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
    setupStore();
    start_bt();
    reader.start([](const std::string& line) {
        ESP_LOGI("MAIN", "Got line: %s", line.c_str());
        if(g_conn != nullptr) g_conn->sendLine(line);
    });
}	
