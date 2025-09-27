#pragma once
#include <cstddef>
#include <cstdint>

using DsTxDoneCallback = std::function<void()>;

class DataSource {
public:
    virtual ~DataSource() = default;

    virtual ssize_t write(bool withAck, const uint8_t* data, size_t len) = 0;

    virtual ssize_t read(uint8_t* buf, size_t maxLen) = 0;
    
    virtual void close() = 0;
    
    virtual bool isReady() = 0;
    
    virtual uint32_t estimateTxTimeMs(size_t payloadLen) = 0;
    
    void setTxDoneCallback(DsTxDoneCallback cb) { _txDoneCB = std::move(cb); }
    
protected:
	DsTxDoneCallback _txDoneCB;
};
