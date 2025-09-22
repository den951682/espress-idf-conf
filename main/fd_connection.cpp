#include "fd_connection.hpp"

#include <sys/_stdint.h>
#include <vector>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include "protocol/ecdh_aes_protocol.hpp"
#include "protocol/passphrase_aes_protocol.hpp"
#include "protocol/raw_protocol.hpp"
#include "protocol/config_protocol.hpp"
#include "esp_log.h"
#include "utils.hpp"
#include <memory>
	
namespace {
    static const char* TAG = "FdConnection";
}

FdConnection::FdConnection(DataSource* dataSource,
							   LoraRouter* loraRouter,
							   const char* passPhrase,
                               const char* taskName,
                               uint16_t stackSize,
                               UBaseType_t priority,
                               BaseType_t core
                          )
    : _dataSource(dataSource), loraRouter_(loraRouter), _passPhrase(passPhrase), _taskName(taskName), _stack(stackSize), _prio(priority), _core(core) {
		ESP_LOGI(TAG, "Connection constructor");
	}

FdConnection::~FdConnection() { 
	ESP_LOGI(TAG, "Connection destructor");
	if(_loraAddress.load() > 0) {
		ESP_LOGI("FdConnection", "Closed token for lora");
		uint8_t token[3];
	    token[0] = 0xff;
	    token[1] = 0xfe;         
	    token[2] = 0xfd;             
		loraRouter_ -> routeToLora(_loraAddress, token, 3);
	} 
	loraRouter_ -> disableDataSource(false);
}

FdConnection::FdConnection(FdConnection&& other) noexcept { moveFrom(other); }

bool FdConnection::isRunning() const { return _running.load(); }

esp_err_t FdConnection::start() {
	ESP_LOGI(TAG, "Connection start");
    if (_running.load()) return ESP_OK;
    _running.store(true);
    _guarded.store(false);
    protocol = /*std::make_unique<EcdhAesProtocol>(_passPhrase);*/createProtocol(_passPhrase);
    ESP_LOGI(TAG, "protocol new=%p", protocol.get());
    protocol.get() -> setReadyCallback([this](){if(_readyCallback) _readyCallback();});
	sendQueue = xQueueCreate(1, sizeof(SendItem*));
    startSendTask();
    BaseType_t ok = xTaskCreatePinnedToCore(&FdConnection::taskTrampoline,
                                            _taskName,
                                            _stack,
                                            this,
                                            _prio,
                                            &_task,
                                            _core);
    if (ok != pdPASS) {
        _running.store(false);
        _closeCB();
        _task = nullptr;
        ESP_LOGE(TAG, "Failed to create read task (err=%ld)", (long)ok);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Connection started");
    return ESP_OK;
}

void FdConnection::stop() {
    if (!_running.exchange(false)) return; 
    ESP_LOGI(TAG, "Connection stop");
    protocol.get() -> close();
    DataSource* ds = _dataSource.exchange(nullptr);
    if(ds) {
	    ds -> close();
	    delete ds;
    }

     if (sendQueue) {
        SendItem* poison = nullptr;
        xQueueSend(sendQueue, &poison, 0);
        ESP_LOGI(TAG, "Connection send poison pill");
    }
    
   while(_sendTask) vTaskDelay(1);
    
   vQueueDelete(sendQueue);
   sendQueue=nullptr;
   ESP_LOGI(TAG, "Connection stop end");
   if (!_closeCbSent.exchange(true) && _closeCB) _closeCB();
}

ssize_t FdConnection::writeAll(const uint8_t* data, size_t len) {
	ESP_LOGI(TAG, "Send bytes");
	ESP_LOG_BUFFER_HEX(TAG, data, len);
    size_t total = 0;
    DataSource* ds = _dataSource.load();
    if (!ds) return -1;
    while (total < len) {
        ssize_t n = ds -> write(data + total, len - total);
        if (n > 0) {
            total += static_cast<size_t>(n);
            continue;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { vTaskDelay(1); continue; }
            ESP_LOGE(TAG, "write() failed: errno=%d (%s)", errno, strerror(errno));
            return -1;
        }
        ESP_LOGW(TAG, "write() returned 0 (peer closed?)");
        return -1;
    }
    return static_cast<ssize_t>(total);
}

bool FdConnection::isReady() {
	DataSource* ds= _dataSource.load();
	if(ds){
		return ds -> isReady();
	} else {
		return false;
	}
}

ssize_t FdConnection::sendBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    std::lock_guard<std::mutex> lock(_writeMtx);
    return writeAll(data, len);
}

ssize_t FdConnection::sendString(const std::string& s) {
	if(_guarded) return -1;
    return sendBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

ssize_t FdConnection::sendLine(const std::string& s) {
	if(_guarded) return -1;
    std::lock_guard<std::mutex> lock(_writeMtx);
    ssize_t a = writeAll(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    if (a < 0) return a;
    const char nl = '\n';
    ssize_t b = writeAll(reinterpret_cast<const uint8_t*>(&nl), 1);
    return (b == 1) ? (a + 1) : -1;
}

bool FdConnection::enqueueSend(const uint8_t* data, size_t len) {
	if(_running.load()){
		if (uxQueueSpacesAvailable(sendQueue) == 0) {
	        return false;
	    }
    	auto* item = new SendItem{std::vector<uint8_t>(data, data + len)};
    	if (xQueueSend(sendQueue, &item, 0) == pdTRUE) {
            return true;  
        } else {
            delete item;  
            return false;
        }
    }
    return false;
}

void FdConnection::taskTrampoline(void* arg) {
    auto* self = static_cast<FdConnection*>(arg);
    self->taskLoop();
    self->_task = nullptr;
    vTaskDelete(nullptr);
}

void FdConnection::taskLoop() {
    ESP_LOGI(TAG, "read task started");
    std::vector<uint8_t> buf(512);
    std::vector<uint8_t> accum;
    accum.reserve(MAX_ACCUM);
    while (_running.load()) {
        DataSource* ds = _dataSource.load();
        if (!ds) break; 

        ssize_t n = ds -> read(buf.data(), buf.size());
        if (n > 0) {
			if(_loraAddress.load() > 0) {
				//ESP_LOGI("FdConnection", "will route line 173");
                //ESP_LOG_BUFFER_HEX("FdConnection", buf.data(),n);
     			loraRouter_ -> routeToLora(_loraAddress, buf.data(), n);
				continue;
			} else if(_guarded) {
				protocol.get() -> appendReceived(buf.data(), n);
				continue;
			} else {
            	accum.insert(accum.end(), buf.begin(), buf.begin() + n);

            	size_t start = 0;
           		for (size_t i = 0; i < accum.size(); i++) {
               	 if (accum[i] == '\n') {
                	std::vector<uint8_t> lineBytes(accum.begin() + start, accum.begin() + i);
                    std::string line = utils::toCleanString(lineBytes);
                    start = i + 1;
                    
                    auto pos = line.find("lora");
					if (pos != std::string::npos) {
					    int num = std::stoi(line.substr(pos + 4));
					    ESP_LOGI("FdConnection", "Will route to LoRa %d", num);
					    _loraAddress.store(num); 
					    accum.erase(accum.begin(), accum.begin() + i + 1);
					    loraRouter_ -> disableDataSource(true);
					    //ESP_LOGI("FdConnection", "will route line 197");
                        //ESP_LOG_BUFFER_HEX("FdConnection", accum.data(), accum.size());
					 	loraRouter_ -> routeToLora(_loraAddress, accum.data(), accum.size());
					    break;
					}

                    if(line.ends_with("guard")) {
						protocol.get()->init(
			    			[this](const uint8_t* data, size_t len) {
			       					 	FdConnection::sendBytes(data, len);
			    					 },
			    			[this](std::vector<uint8_t> msg) {
			       					 	ESP_LOGI("FdConnection", "Got message size=%u, data=%s", msg.size(), toHex(msg).c_str());
			       					 	if(_dataCB) {
											_dataCB(msg.data(), msg.size());	
										}
			   						 }
					  );
					  _guarded.store(true); 
					  accum.erase(accum.begin(), accum.begin() + i + 1);
					  protocol.get() -> appendReceived(accum.data(), accum.size());
					  break;						
					}
					
                    if (_onLine) {
                        _onLine(line);
                    }
                }
           	   }

            if (start > 0) {
                accum.erase(accum.begin(), accum.begin() + start);
            }

            if (accum.size() > MAX_ACCUM) {
                ESP_LOGW(TAG, "Dropping %zu bytes of unterminated line", accum.size());
                accum.clear();
            }
            continue;
        }
        }
        if (n == 0) {
            vTaskDelay(1);
			continue;
		}

        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) { vTaskDelay(1); continue; }
        ESP_LOGE(TAG, "read() failed: errno=%d (%s)", errno, strerror(errno));
        break;
    }
    DataSource* ds = _dataSource.exchange(nullptr);
    if (ds) { 
		ds -> close();
		delete ds;
	}
    ESP_LOGI(TAG, "read task exit");
    stop();
}

void FdConnection::startSendTask() {
	ESP_LOGI(TAG, "Connection startSendTask");
    xTaskCreatePinnedToCore(&FdConnection::sendTask, "conn_send", 4096, this, _prio, &_sendTask, _core);
}

void FdConnection::sendTask(void* arg) {
	ESP_LOGI(TAG, "Connection sendTask started");
	auto* self = static_cast<FdConnection*>(arg);
    SendItem* item;
    while (self->_running.load()) {
        if (xQueueReceive(self->sendQueue, &item, portMAX_DELAY) == pdTRUE) {
			if (!item) break;
			ESP_LOGI(TAG, "SendTask got item, length=%zu", item->data.size());
			if(self -> protocol.get() && self -> _running) self->protocol.get()->send(item->data.data(), item->data.size());
            delete item;
        }
    }
    ESP_LOGI(TAG, "Connection sendTask exit");
    self->_sendTask = nullptr; 
    vTaskDelete(nullptr); 
}

FdConnection& FdConnection::operator=(FdConnection&& other) noexcept {
    if (this != &other) {
        stop();                
        moveFrom(other);
    }
    return *this;
}

void FdConnection::moveFrom(FdConnection& other) noexcept {
    _dataSource.store(other._dataSource.exchange(nullptr));
    _passPhrase = other._passPhrase;
    _taskName   = other._taskName;
    _stack      = other._stack;
    _prio       = other._prio;
    _core       = other._core;

    _running.store(other._running.exchange(false));
    _guarded.store(other._guarded.exchange(false));
    _closeCbSent.store(other._closeCbSent.exchange(false));


    _task     = other._task;     other._task = nullptr;
    _sendTask = other._sendTask; other._sendTask = nullptr;
    sendQueue = other.sendQueue; other.sendQueue = nullptr;

    _dataCB       = std::move(other._dataCB);
    _onLine       = std::move(other._onLine);
    _readyCallback= std::move(other._readyCallback);
    _closeCB      = std::move(other._closeCB);
    protocol = std::move(other.protocol);
}


std::string FdConnection::toHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : data) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}
