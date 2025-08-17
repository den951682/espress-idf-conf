#include "passphrase_aes_protocol.hpp"
#include "crypto_ecdh_aes.hpp"
#include <cstring>
#include <stdint.h>
#include <string>
#include "esp_log.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "proto-model/Handshake.pb.h"

static const char* TAG = "PassphraseAesProtocol";

PassphraseAesProtocol::PassphraseAesProtocol(const char* passPhrase): crypto(CryptoEcdhAes::Mode::PASSPHRASE, passPhrase) {
    sendReady = xSemaphoreCreateBinary();
}

PassphraseAesProtocol::~PassphraseAesProtocol() {
    if (sendReady) vSemaphoreDelete(sendReady);
}

void PassphraseAesProtocol::init(WriteCallback writeCb, QueueCallback recvCb) {
    this->writeCb = writeCb;
    this->recvCb = recvCb;
    buffer.clear();
    handshakeReceived = false;
    xSemaphoreTake(sendReady, 0); 
    sendHandshake();
}

void PassphraseAesProtocol::appendReceived(const uint8_t* data, size_t len) {
    buffer.insert(buffer.end(), data, data + len);
    while (true) {
        if (buffer.empty()) return;
        uint8_t frameLen = buffer[0];
        if (buffer.size() < frameLen + 1) {
            return; 
        }
        std::vector<uint8_t> frame(buffer.begin() + 1, buffer.begin() + 1 + frameLen);
        buffer.erase(buffer.begin(), buffer.begin() + 1 + frameLen);
        auto decrypted = decryptFrame(frame);
        if (!handshakeReceived) {
            if (parseHandshake(decrypted)) {
                handshakeReceived = true;
                xSemaphoreGive(sendReady);
                ESP_LOGI(TAG, "Handshake complete");
                if(readyCallback) readyCallback();
            }
        } else {          
            if (recvCb) recvCb(decrypted);
        }
    }
}

bool PassphraseAesProtocol::send(const uint8_t* data, size_t len) {
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

void PassphraseAesProtocol::sendCode(uint8_t code) {
    writeCb(&code, 1);
}

void PassphraseAesProtocol::sendHandshake() {
	//no header
	uint8_t headerLen = 0;
	writeCb(&headerLen, 1);
	
	ESP_LOGI(TAG, "Sending HANDSHAKE");
     
    pModel_HandshakeRequest req = pModel_HandshakeRequest_init_zero;
    strncpy(req.text, "HANDSHAKE", sizeof(req.text) - 1);

    uint8_t buffer[pModel_HandshakeRequest_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, pModel_HandshakeRequest_fields, &req)) {
        ESP_LOGE(TAG, "Handshake encode failed: %s", PB_GET_ERROR(&stream));
        sendCode(5);
    }  
     
    size_t total_len = stream.bytes_written;
    std::vector<uint8_t> msg_vec(buffer, buffer + total_len);
    std::vector<uint8_t> enc = crypto.encrypt_data_whole(msg_vec);
    uint8_t len = enc.size();
    writeCb(&len, 1);
    writeCb(enc.data(), len);
}

bool PassphraseAesProtocol::parseHandshake(const std::vector<uint8_t>& frame) {
    pModel_HandshakeResponse resp = pModel_HandshakeResponse_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(frame.data(), frame.size());
    if (!pb_decode(&istream, pModel_HandshakeResponse_fields, &resp)) {
        ESP_LOGE(TAG, "Handshake decode failed: %s", PB_GET_ERROR(&istream));
        return false;
    }

    ESP_LOGI(TAG, "Handshake text: %s", resp.text);
    return strcmp(resp.text, "HANDSHAKE") == 0;
}

std::vector<uint8_t> PassphraseAesProtocol::encryptFrame(const std::vector<uint8_t>& plain) {
	std::vector<uint8_t> enc = crypto.encrypt_data_whole(plain);
    if(enc.empty()) sendCode(2);
    return enc; 
}

std::vector<uint8_t> PassphraseAesProtocol::decryptFrame(const std::vector<uint8_t>& enc) {
    std::vector<uint8_t> dec = crypto.decrypt_data_whole(enc);
    if(dec.empty()) sendCode(2);
    return dec; 
}
