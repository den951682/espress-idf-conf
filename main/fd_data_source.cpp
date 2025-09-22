#include <unistd.h>
#include <errno.h>
#include <cstring>
#include "esp_log.h"
#include "data_source.cpp"

class FdDataSource : public DataSource {
public:
    explicit FdDataSource(int fd) : fd_(fd) {}

    ssize_t write(const uint8_t* data, size_t len) override {
        ssize_t ret = ::write(fd_, data, len);
        if (ret < 0) {
            ESP_LOGE("FdDataSource", "write error: %s", strerror(errno));
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

private:
    int fd_;
};
