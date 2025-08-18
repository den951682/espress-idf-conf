#include "raw_protocol.hpp"
#include <cstddef>
#include <cstring>
#include <stdint.h>
#include <string>
#include "esp_log.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "proto-model/Handshake.pb.h"

static const char* TAG = "RawProtocol";

RawProtocol::RawProtocol(std::string passPhrase): _passPhrase(passPhrase) {
    sendReady = xSemaphoreCreateBinary();
}

RawProtocol::~RawProtocol() {
    if (sendReady) vSemaphoreDelete(sendReady);
}

void RawProtocol::init(WriteCallback writeCb, QueueCallback recvCb) {
    this->writeCb = writeCb;
    this->recvCb = recvCb;
    buffer.clear();
    handshakeReceived = false;
    xSemaphoreTake(sendReady, 0); 
    sendHandshake();
}

void RawProtocol::appendReceived(const uint8_t* data, size_t len) {
    buffer.insert(buffer.end(), data, data + len);
    while (true) {
        if (buffer.empty()) return;
        uint8_t frameLen = buffer[0];
        if (buffer.size() < frameLen + 1) {
            return; 
        }
        std::vector<uint8_t> frame(buffer.begin() + 1, buffer.begin() + 1 + frameLen);
        buffer.erase(buffer.begin(), buffer.begin() + 1 + frameLen);
        if (!handshakeReceived) {
            if (parseHandshake(frame)) {
                handshakeReceived = true;
                xSemaphoreGive(sendReady);
                ESP_LOGI(TAG, "Handshake complete");
                if(readyCallback) readyCallback();
            } else {
				sendCode(1);
			}
        } else {          
            if (recvCb) recvCb(frame);
        }
    }
}

bool RawProtocol::send(const uint8_t* data, size_t len) {
    if (xSemaphoreTake(sendReady, pdMS_TO_TICKS(5000)) == pdFALSE) {
        ESP_LOGW(TAG, "Send blocked: handshake not complete");
        return false;
    }
    xSemaphoreGive(sendReady);
    uint8_t hdr = len;
    writeCb(&hdr, 1);
    writeCb(data, len);
    return true;
}

void RawProtocol::sendCode(uint8_t code) {
    writeCb(&code, 1);
}

void RawProtocol::sendHandshake() {
	//no header
	uint8_t headerLen = 0;
	writeCb(&headerLen, 1);
     
    pModel_HandshakeRequest req = pModel_HandshakeRequest_init_zero;
    strncpy(req.text, "HANDSHAKE", sizeof(req.text) - 1);

    uint8_t buffer[pModel_HandshakeRequest_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, pModel_HandshakeRequest_fields, &req)) {
        ESP_LOGE(TAG, "Handshake encode failed: %s", PB_GET_ERROR(&stream));
    }  
     
    uint8_t len = static_cast<uint8_t>(stream.bytes_written);
    writeCb(&len, 1);
    writeCb(buffer, len);
}

bool RawProtocol::parseHandshake(const std::vector<uint8_t>& frame) {
    pModel_HandshakeResponse resp = pModel_HandshakeResponse_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(frame.data(), frame.size());
    if (!pb_decode(&istream, pModel_HandshakeResponse_fields, &resp)) {
        ESP_LOGE(TAG, "Handshake decode failed: %s", PB_GET_ERROR(&istream));
        return false;
    }
    return strcmp(resp.text, _passPhrase.c_str()) == 0;
}
