#include "ecdh_aes_protocol.hpp"
#include <cstring>
#include <string>

static const char* TAG = "EcdhAesProtocol";

EcdhAesProtocol::EcdhAesProtocol() {
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
    sendHandshake();
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
        ESP_LOG_BUFFER_HEX(TAG, frame.data(), frame.size());
        buffer.erase(buffer.begin(), buffer.begin() + 1 + frameLen);
        
        if (!handshakeReceived) {
            if (parseHandshake(frame)) {
                handshakeReceived = true;
                xSemaphoreGive(sendReady);
                ESP_LOGI(TAG, "Handshake complete");
            }
        } else {
            auto decrypted = decryptFrame(frame);
            if (recvCb) recvCb(decrypted);
        }
    }
}

bool EcdhAesProtocol::send(const uint8_t* data, size_t len) {
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

void EcdhAesProtocol::sendHandshake() {
    const char* msg = "HANDSHAKE";
    uint8_t len = strlen(msg);
    writeCb(&len, 1);
    writeCb(reinterpret_cast<const uint8_t*>(msg), len);
}

bool EcdhAesProtocol::parseHandshake(const std::vector<uint8_t>& frame) {
    // TODO: тут розбір ключа, встановлення AES
    // зараз просто перевірка тексту
    std::string text(frame.begin(), frame.end());
    return (text == "HANDSHAKE_OK");
}

std::vector<uint8_t> EcdhAesProtocol::encryptFrame(const std::vector<uint8_t>& plain) {
    // TODO: AES шифрування
    return plain; // поки без шифру
}

std::vector<uint8_t> EcdhAesProtocol::decryptFrame(const std::vector<uint8_t>& enc) {
    // TODO: AES розшифрування
    return enc; // поки без шифру
}
