#include <cstring>
#include <stdio.h>
#include <stdbool.h>
#include <sys/unistd.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "bt_spp_server.hpp"

BtSppServer bt;

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
        const char msg[] = "Hello from ESP32!\n";
        write(fd, msg, strlen(msg));        
    });
    bt.start();
}

extern "C" void app_main(void) {
    start_nvs();
    start_bt();
}	
