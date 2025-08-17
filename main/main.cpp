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
#include "uptime_task.cpp"
#include "send_delayed.cpp"

using namespace paramstore;

enum class AppCommandType : uint8_t {
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
    std::variant<std::monostate, Data> data;
};

QueueHandle_t appQueue = nullptr;
SerialLineReader reader;
BtSppServer bt;
FdConnection* g_conn = nullptr;
ParameterStore store;
ParameterSync parameterSync(store);
JoystickTask joystickTask(store);
LedBlinkTask blinkTask(store, GPIO_NUM_2);
UptimeTask uptime(store);

static void setupConnection(int fd) {
	delete g_conn; g_conn = nullptr;
    std::string passPhrase = store.getString(ParameterId::PassPhrase);
    g_conn = new FdConnection(fd, passPhrase.c_str());
    g_conn->setReadyCallback([](){
		parameterSync.setConnection(g_conn);
        AppCommand* cmd = new AppCommand{AppCommandType::SendAllParameters, {}};
        xQueueSend(appQueue, &cmd, 0);
	});
    g_conn->setCloseCallback([](){
		parameterSync.removeConnection();
	});
	g_conn->setDataCallback([](const uint8_t* data, size_t len){
		 ESP_LOGI("APP", "Data received:");
		 ESP_LOG_BUFFER_HEX("APP", data, len);
		 Data d;
         d.bytes.assign(data, data + len);
         AppCommand* cmd = new AppCommand{AppCommandType::DataReceived, d};
         xQueueSend(appQueue, &cmd, 0);
    });
        
    g_conn->setLineCallback([](const std::string& line){
        ESP_LOGI("APP", "RX line: %s", line.c_str());
        g_conn->sendLine("OK");
    });
    
    if (g_conn->start() != ESP_OK) { 
		ESP_LOGE("APP", "start failed"); 
		delete g_conn; g_conn = nullptr;
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
        setupConnection(fd);
    });
    std::string name = store.getString(paramstore::ParameterId::DeviceName);
    bt.start(name.c_str());
}

static void startReader() {
	reader.start([](const std::string& line) {
        ESP_LOGI("MAIN", "Got line: %s", line.c_str());
        if(g_conn != nullptr) g_conn->sendLine(line);
    });
}


static void sendMessageToConnection(const char* text) {
	ESP_LOGI("APP", "Send message %s", text);
	pModel_Message msg = pModel_Message_init_zero;
    size_t n = std::min(strlen(text), sizeof(msg.text.bytes));
    msg.text.size = n;
    memcpy(msg.text.bytes, text, n);
    uint8_t buffer[512];
    pb_ostream_t ostream = pb_ostream_from_buffer(buffer + 1, sizeof(buffer) - 1);
	buffer[0] = static_cast<uint8_t>(MessageType::Message);
	pb_encode(&ostream, pModel_Message_fields, &msg);	
	if(g_conn) {
        g_conn -> enqueueSend(buffer, ostream.bytes_written + 1);
    }
}

static void handleDataReceivedCommand(std::vector<uint8_t> data) {
	if (!data.empty()) {
		auto type = static_cast<MessageType>(data[0]);                       	        	
		const uint8_t* payload = data.data() + 1;
    	size_t payloadLen = data.size() - 1;
    	if(type == MessageType::SetInt || type == MessageType::SetInt ||
    		type == MessageType::SetString || type == MessageType::SetBoolean) {
			auto paramSetType = static_cast<ParamSetType>(data[0]);
			bool ok = parameterSync.handleSetParameter(paramSetType, payload, payloadLen, 	[](SetParam setParam){
					if(setParam == SetParam::Passphrase) {
						sendMessageToConnection("З'єднання буде закрито. Підключись з новою Pass-фразою. Не забудь її змінити на Android-стороні.");
					    AppCommand* cmd = new AppCommand{ AppCommandType::RestartConnection, {} };
                        sendDelayed(appQueue, cmd, 1000);
					}
					if(setParam == SetParam::ServerName) {
						sendMessageToConnection("Сервер буде перезапущено з новою назвою. Перепідключись."); 
						AppCommand* cmd = new AppCommand{ AppCommandType::RestartServer, {} };
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
				case AppCommandType::DataReceived:
                	if (std::holds_alternative<Data>(cmd -> data)) {
                    	handleDataReceivedCommand(std::get<Data>(cmd -> data).bytes);
   					} else {
						ESP_LOGW("APP", "Wrong  AppCommandType::DataReceived");
				    }
                	break;
                
                case AppCommandType::RestartConnection:
                    if(g_conn) {
						g_conn -> stop();
						g_conn = nullptr;
					}
                	break;
                	
                case AppCommandType::RestartServer:
                     if(g_conn) {
						g_conn -> stop();
						g_conn = nullptr;
					}
					bt.stop();
					start_bt();
                	break;
                	
                case AppCommandType::SendAllParameters:
                    parameterSync.sendAllParametersInfo();
                    parameterSync.sendAllParameters();
                    break;

                default:
                    break;
                delete cmd;
            }
        }
    }
}

extern "C" void app_main(void) {
	appQueue = xQueueCreate(16, sizeof(AppCommand));
    xTaskCreatePinnedToCore(appTask, "appTask", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
    setupStore();
    start_bt();
    startReader();
    blinkTask.start();
    joystickTask.start();
    uptime.start();
}	
