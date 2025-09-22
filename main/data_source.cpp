#pragma once
#include <cstddef>
#include <cstdint>

class DataSource {
public:
    virtual ~DataSource() = default;

    virtual ssize_t write(const uint8_t* data, size_t len) = 0;

    virtual ssize_t read(uint8_t* buf, size_t maxLen) = 0;
    
    virtual void close() = 0;
    
    virtual bool isReady() = 0;
};
