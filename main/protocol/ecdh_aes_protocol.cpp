#include "ecdh_aes_protocol.hpp"
#include "crypto_ecdh_aes.hpp"
#include <cstring>
#include <stdint.h>
#include <string>
#include "esp_log.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "proto-model/Handshake.pb.h"

static const char* TAG = "EcdhAesProtocol";

EcdhAesProtocol::EcdhAesProtocol(std::string passPhrase): crypto(CryptoEcdhAes::Mode::EPHEMERAL), _passPhrase(passPhrase) {
    sendReady = xSemaphoreCreateBinary();
}

EcdhAesProtocol::~EcdhAesProtocol() {
    if (sendReady) vSemaphoreDelete(sendReady);
}

void EcdhAesProtocol::init(WriteCallback writeCb, QueueCallback recvCb) {
    this->writeCb = writeCb;
    this->recvCb = recvCb;
    buffer.clear();
    handshakeReceived = false;
    xSemaphoreTake(sendReady, 0); 
    //no header
	uint8_t headerLen = 0;
	writeCb(&headerLen, 1);
}

void EcdhAesProtocol::appendReceived(const uint8_t* data, size_t len) {
    buffer.insert(buffer.end(), data, data + len);
    while (true) {
        if (buffer.empty()) return;
        uint8_t frameLen = buffer[0];
        if (buffer.size() < frameLen + 1) {
            return; 
        }
        std::vector<uint8_t> frame(buffer.begin() + 1, buffer.begin() + 1 + frameLen);
        buffer.erase(buffer.begin(), buffer.begin() + 1 + frameLen);
        std::vector<uint8_t> decrypted;
        if(handshakeReceived) decrypted = decryptFrame(frame); else decrypted = frame;
        if (!handshakeReceived) {
            if (parseHandshake(decrypted)) {
                handshakeReceived = true;
                ESP_LOGI(TAG, "Handshake complete");
                if(readyCallback) readyCallback();
                sendHandshake();
                xSemaphoreGive(sendReady);
            } else {
				sendCode(0x11);
			}
        } else {          
            if (recvCb) recvCb(decrypted);
        }
    }
}

bool EcdhAesProtocol::send(const uint8_t* data, size_t len) {
    if (xSemaphoreTake(sendReady, pdMS_TO_TICKS(5000)) == pdFALSE) {
        ESP_LOGW(TAG, "Send blocked: handshake not complete");
        return false;
    }
    xSemaphoreGive(sendReady);
    auto encrypted = encryptFrame({data, data + len});
    uint8_t hdr = encrypted.size();
    writeCb(&hdr, 1);
    writeCb(encrypted.data(), encrypted.size());
    return true;
}

void EcdhAesProtocol::sendCode(uint8_t code) {
    writeCb(&code, 1);
}

void EcdhAesProtocol::sendHandshake() {
	ESP_LOGI(TAG, "Sending HANDSHAKE");
     
    pModel_HandshakeRequest req = pModel_HandshakeRequest_init_zero;
    char pk_base64[256];  
    crypto.get_encoded_public_key(pk_base64, sizeof(pk_base64));
    strncpy(req.text2, pk_base64, sizeof(req.text2) - 1);
    req.text2[sizeof(req.text2) - 1] = '\0'; 
    uint8_t buffer[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, pModel_HandshakeRequest_fields, &req)) {
        ESP_LOGE(TAG, "Handshake encode failed: %s", PB_GET_ERROR(&stream));
        sendCode(5);
    }  
    uint8_t len = stream.bytes_written;
    writeCb(&len, 1);
    writeCb(buffer, len);
}

bool EcdhAesProtocol::parseHandshake(const std::vector<uint8_t>& frame) {
    pModel_HandshakeRequest resp = pModel_HandshakeRequest_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(frame.data(), frame.size());
    if (!pb_decode(&istream, pModel_HandshakeRequest_fields, &resp)) {
        ESP_LOGE(TAG, "Handshake decode failed: %s", PB_GET_ERROR(&istream));
        return false;
    }
    bool checkPP = strcmp(resp.text, _passPhrase.c_str()) == 0;
    if(!checkPP) {
		ESP_LOGW(TAG, "Bound phrase is wrong");
		return false;
	}
    std::vector<uint8_t> b64(resp.text2, resp.text2 + strlen(resp.text2));
    bool res = crypto.apply_other_public(b64);
    return res;
}

std::vector<uint8_t> EcdhAesProtocol::encryptFrame(const std::vector<uint8_t>& plain) {
	std::vector<uint8_t> enc = crypto.encrypt_data_whole(plain);
    if(enc.empty()) sendCode(2);
    return enc; 
}

std::vector<uint8_t> EcdhAesProtocol::decryptFrame(const std::vector<uint8_t>& enc) {
    std::vector<uint8_t> dec = crypto.decrypt_data_whole(enc);
    if(dec.empty()) sendCode(2);
    return dec; 
}
