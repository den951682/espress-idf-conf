#pragma once
#include "protocol.hpp"
#include <string>

class RawProtocol : public Protocol {
public:
    RawProtocol(std::string passPhrase);
    ~RawProtocol() override;

    void init(WriteCallback writeCb, QueueCallback recvCb) override;
    void appendReceived(const uint8_t* data, size_t len) override;
    bool send(const uint8_t* data, size_t len) override;

private:
    bool handshakeReceived = false;
    std::string _passPhrase;
    
    void sendCode(uint8_t code);
    void sendHandshake();
    bool parseHandshake(const std::vector<uint8_t>& frame);
};
