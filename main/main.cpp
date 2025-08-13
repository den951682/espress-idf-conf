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

SerialLineReader reader;
BtSppServer bt;
FdConnection* g_conn = nullptr;

static void start_nvs() {
	esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void start_bt() {
	bt.setOnEvent([](BtSppServer::Event e, int err){
        ESP_LOGI("APP", "Event=%d err=0x%x", (int)e, err);
    });

    bt.setOnFdReady([](int fd){
        ESP_LOGI("APP", "FD ready: %d", fd);
        delete g_conn; g_conn = nullptr;
        g_conn = new FdConnection(fd);
        g_conn->setLineCallback([](const std::string& line){
           ESP_LOGI("APP", "RX line: %s", line.c_str());
           g_conn->sendLine("OK");
        });
        if (g_conn->start() != ESP_OK) { 
		   ESP_LOGE("APP", "start failed"); 
		   delete g_conn; g_conn = nullptr;
	    }          
    });
    bt.start();
}

extern "C" void app_main(void) {
    start_nvs();
    start_bt();
    reader.start([](const std::string& line) {
        ESP_LOGI("MAIN", "Got line: %s", line.c_str());
        if(g_conn != nullptr) g_conn->sendLine(line);
    });
}	
