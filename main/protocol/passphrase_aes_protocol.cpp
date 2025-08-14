#include "passphrase_aes_protocol.hpp"
#include "crypto_ecdh_aes.hpp"
#include <cstring>
#include <stdint.h>
#include <string>

static const char* TAG = "PassphraseAesProtocol";

PassphraseAesProtocol::PassphraseAesProtocol() {
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
        ESP_LOG_BUFFER_HEX(TAG, decrypted.data(), decrypted.size());
        
        if (!handshakeReceived) {
            if (parseHandshake(frame)) {
                handshakeReceived = true;
                xSemaphoreGive(sendReady);
                ESP_LOGI(TAG, "Handshake complete");
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
    const char* msg = "HANDSHAKE";
    std::vector<uint8_t> msg_vec(msg, msg + strlen(msg));
    std::vector<uint8_t> enc = crypto.encrypt_data_whole(msg_vec);
    uint8_t len = enc.size();
    writeCb(&len, 1);
    writeCb(enc.data(), len);
}

bool PassphraseAesProtocol::parseHandshake(const std::vector<uint8_t>& frame) {
    // TODO: тут розбір ключа, встановлення AES
    // зараз просто перевірка тексту
    std::string text(frame.begin(), frame.end());
    return (text == "HANDSHAKE_OK");
}

std::vector<uint8_t> PassphraseAesProtocol::encryptFrame(const std::vector<uint8_t>& plain) {
    // TODO: AES шифрування
    return plain; // поки без шифру
}

std::vector<uint8_t> PassphraseAesProtocol::decryptFrame(const std::vector<uint8_t>& enc) {
    std::vector<uint8_t> dec = crypto.decrypt_data_whole(enc);
    if(dec.empty()) sendCode(2);
    return dec; 
}
