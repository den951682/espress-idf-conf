#pragma once
#include "protocol.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

class EcdhAesProtocol : public Protocol {
public:
    EcdhAesProtocol();
    ~EcdhAesProtocol() override;

    void init(WriteCallback writeCb, QueueCallback recvCb) override;
    void appendReceived(const uint8_t* data, size_t len) override;
    bool send(const uint8_t* data, size_t len) override;

private:
    WriteCallback writeCb;
    QueueCallback recvCb;
    SemaphoreHandle_t sendReady;
    std::vector<uint8_t> buffer;
    bool handshakeReceived = false;

    void sendHandshake();
    bool parseHandshake(const std::vector<uint8_t>& frame);
    std::vector<uint8_t> encryptFrame(const std::vector<uint8_t>& plain);
    std::vector<uint8_t> decryptFrame(const std::vector<uint8_t>& enc);
};
