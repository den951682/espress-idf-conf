#include <cstring>
#include <stdio.h>
#include <stdbool.h>
#include <string>
#include <sys/_stdint.h>
#include <sys/unistd.h>
#include "Parameters.pb.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "serial_line_reader.hpp"
#include "bt_spp_server.hpp"
#include "fd_connection.hpp"
#include "parameter_store.cpp"
#include "parameter_sync.cpp"
#include "message_type.cpp"
#include "joystick_task.cpp"
#include "led_blink_task.cpp"
#include "lora_connection_task.cpp"
#include "lora_router.hpp"
#include "uptime_task.cpp"
#include "send_delayed.cpp"
#include "data_source.cpp"
#include "fd_data_source.cpp"

using namespace paramstore;

enum class AppCommandType : uint8_t {
	CleanupConnection,
	DataReceived,
	RestartConnection,
	RestartServer,
    SendAllParameters,
};

struct Data {
    std::vector<uint8_t> bytes;
};

struct AppCommand {
    AppCommandType type;
    int meta;
    std::variant<std::monostate, Data> data;
};

constexpr int CONN_BLUETOOTH = 0;
constexpr int CONN_LORA      = 1;

QueueHandle_t appQueue = nullptr;
SerialLineReader reader;
BtSppServer bt;
FdConnection* g_conn[2] = { nullptr, nullptr };
ParameterStore store;
ParameterSync parameterSync(store);
JoystickTask joystickTask(store);
LedBlinkTask blinkTask(store, GPIO_NUM_2);
LoraConnectionTask loraConnectionTask(store);
LoraRouter loraRouter(loraConnectionTask);
UptimeTask uptime(store);

static void setupConnection(int type, DataSource* ds) {
	if (g_conn[type]) {
		delete g_conn[type];
		g_conn[type] = nullptr;
	}
    std::string passPhrase = store.getString(ParameterId::PassPhrase);
    g_conn[type] = new FdConnection(ds, &loraRouter, passPhrase.c_str());
    g_conn[type] -> setReadyCallback([type](){
		parameterSync.setConnection(type, g_conn[type]);
        AppCommand* cmd = new AppCommand{AppCommandType::SendAllParameters, type,{}};
        xQueueSend(appQueue, &cmd, 0);
	});
    g_conn[type] -> setCloseCallback([type](){
		ESP_LOGI("APP", "Close Connection callback");
		parameterSync.removeConnection(type);
		AppCommand* cmd = new AppCommand{AppCommandType::CleanupConnection, type, {}};
        xQueueSend(appQueue, &cmd, 0);
	});
	g_conn[type] -> setDataCallback([type](const uint8_t* data, size_t len){
		 ESP_LOGI("APP", "Data received:");
		 ESP_LOG_BUFFER_HEX("APP", data, len);
		 Data d;
         d.bytes.assign(data, data + len);
         AppCommand* cmd = new AppCommand{AppCommandType::DataReceived, type, d};
         xQueueSend(appQueue, &cmd, 0);
    });
        
    g_conn[type] -> setLineCallback([type](const std::string& line){
        ESP_LOGI("APP", "RX line: %s", line.c_str());
        g_conn[type] -> sendLine("OK");
    });
    
    if (g_conn[type] -> start() != ESP_OK) { 
		ESP_LOGE("APP", "start failed"); 
		delete g_conn[type]; g_conn[type] = nullptr;
	}
}

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
        DataSource* ds = new FdDataSource(fd);
        setupConnection(CONN_BLUETOOTH, ds);
    });
    std::string name = store.getString(paramstore::ParameterId::DeviceName);
    bt.start(name.c_str());
}

static void startReader() {
	reader.start([](const std::string& line) {
        ESP_LOGI("MAIN", "Got line: %s", line.c_str());
        if(g_conn[0] != nullptr) g_conn[0] -> sendLine(line);
        if(g_conn[1] != nullptr) g_conn[1] -> sendLine(line);
    });
}

static void setupLoraRouter() {
	loraRouter.setOnMessageCallback([](const uint8_t* data, size_t len) {
	    ESP_LOGI("App", "Got LoRa packet len=%u", (unsigned)len);
	    if(g_conn[CONN_BLUETOOTH]) {
        	g_conn[CONN_BLUETOOTH] -> writeAll(data, len);
        }
	});
	loraRouter.setOnDataSourceCallback([](DataSource* dataSource) {
	    setupConnection(CONN_LORA, dataSource);   
	});
}

static void sendMessageToConnection(int connType, const char* text) {
	ESP_LOGI("APP", "Send message %s", text);
	pModel_Message msg = pModel_Message_init_zero;
    size_t n = std::min(strlen(text), sizeof(msg.text.bytes));
    msg.text.size = n;
    memcpy(msg.text.bytes, text, n);
    uint8_t buffer[512];
    pb_ostream_t ostream = pb_ostream_from_buffer(buffer + 1, sizeof(buffer) - 1);
	buffer[0] = static_cast<uint8_t>(MessageType::Message);
	pb_encode(&ostream, pModel_Message_fields, &msg);	
	if(g_conn[connType]) {
        g_conn[connType] -> enqueueSend(buffer, ostream.bytes_written + 1);
    }
}

static void handleDataReceivedCommand(int connType, std::vector<uint8_t> data) {
	if (!data.empty()) {
		auto type = static_cast<MessageType>(data[0]);                       	        	
		const uint8_t* payload = data.data() + 1;
    	size_t payloadLen = data.size() - 1;
    	if(type == MessageType::SetInt || type == MessageType::SetInt ||
    		type == MessageType::SetString || type == MessageType::SetBoolean) {
			auto paramSetType = static_cast<ParamSetType>(data[0]);
			bool ok = parameterSync.handleSetParameter(paramSetType, payload, payloadLen, 	[connType](SetParam setParam){
					if(setParam == SetParam::Passphrase) {
						sendMessageToConnection(connType, "З'єднання буде закрито. Підключись з новою Pass-фразою. Не забудь її змінити на Android-стороні.");
					    AppCommand* cmd = new AppCommand{ AppCommandType::RestartConnection, connType, {} };
                        sendDelayed(appQueue, cmd, 1000);
					}
					if(setParam == SetParam::ServerName) {
						sendMessageToConnection(connType, "Сервер буде перезапущено з новою назвою. Перепідключись."); 
						AppCommand* cmd = new AppCommand{ AppCommandType::RestartServer, connType, {} };
                        sendDelayed(appQueue, cmd, 1000);
					}
				});
    		if (!ok) {
        		ESP_LOGW("APP", "handleSetParameter failed for type=%d", (int)paramSetType);
    		}
		} else {
			ESP_LOGW("APP", "Unsupported DataReceived type=%d", (int)type); 
		}
	} else {
	 	ESP_LOGW("APP", "DataReceived without data");
	}
}       		

void appTask(void* arg) {
    AppCommand* cmd;
    for (;;) {
        if (xQueueReceive(appQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd -> type) {
				case AppCommandType::CleanupConnection:
    				if (g_conn[cmd -> meta]) {
						g_conn[cmd -> meta] -> stop();
						delete g_conn[cmd -> meta];
						g_conn[cmd -> meta] = nullptr;
					}
    				break;
    
				case AppCommandType::DataReceived:
                	if (std::holds_alternative<Data>(cmd -> data)) {
                    	handleDataReceivedCommand(cmd -> meta, std::get<Data>(cmd -> data).bytes);
   					} else {
						ESP_LOGW("APP", "Wrong  AppCommandType::DataReceived");
				    }
                	break;
                              	
                case AppCommandType::RestartConnection:
                    g_conn[cmd -> meta] -> stop();
                	break;
                	
                case AppCommandType::RestartServer:
                    g_conn[cmd -> meta] -> stop();
					bt.stop();
					start_bt();
                	break;
                	
                case AppCommandType::SendAllParameters:
                    parameterSync.sendAllParametersInfo(cmd -> meta);
                    break;

                default:
                    break;
            }
            delete cmd;
        }
    }
}

extern "C" void app_main(void) {
	appQueue = xQueueCreate(16, sizeof(AppCommand*));
    xTaskCreatePinnedToCore(appTask, "CommandTask", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
    setupStore();
    setupLoraRouter();
    start_bt();
    startReader();
    blinkTask.start();
    joystickTask.start();
    loraConnectionTask.start();
    uptime.start();
}	
