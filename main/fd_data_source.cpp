#include <unistd.h>
#include <errno.h>
#include <cstring>
#include "esp_log.h"
#include "data_source.cpp"

class FdDataSource : public DataSource {
public:
    explicit FdDataSource(int fd) : fd_(fd) {}

    ssize_t write(bool withAck, const uint8_t* data, size_t len) override {
        ssize_t ret = ::write(fd_, data, len);
        if (ret < 0) {
            ESP_LOGE("FdDataSource", "write error: %s", strerror(errno));
        }
        if(_txDoneCB) {
			_txDoneCB();
		}
        return ret;
    }

    ssize_t read(uint8_t* buf, size_t maxLen) override {
        ssize_t ret = ::read(fd_, buf, maxLen);
        if (ret < 0) {
            ESP_LOGE("FdDataSource", "read error: %s", strerror(errno));
        }
        return ret;
    }
    
    void close() override {
		if(fd_>0) {
			ESP_LOGW("FdDataSource", "local stop: closing fd=%d", fd_);
        	::close(fd_);
		}
	}
	
	bool isReady() override{
		return true;
	}
	
	uint32_t estimateTxTimeMs(size_t payloadLen) override {
	    size_t totalBits = payloadLen * 10;
	    uint32_t baudRate = 115200;
	    double durationSec = static_cast<double>(totalBits) / static_cast<double>(baudRate);
	    uint32_t durationMs = static_cast<uint32_t>(durationSec * 1000.0);
	
	    /*ESP_LOGI("FdDataSource",
	             "Payload=%" PRIu32 " bytes, BaudRate=%" PRIu32 " bps, Time=%" PRIu32 " ms",
	             static_cast<uint32_t>(payloadLen),
	             baudRate,
	             durationMs);*/
	
	    return durationMs;
	}

private:
    int fd_;
};
