#pragma once
#include <vector>
#include <string>
#include "esp_log.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

class CryptoEcdhAes {
public:
	enum class Mode {
        PASSPHRASE,
        EPHEMERAL
    };
    explicit CryptoEcdhAes(Mode mode, const char* passPhrase = nullptr);
    ~CryptoEcdhAes();

    bool generate_keypair();
    std::vector<uint8_t> get_public_key_raw();
    void get_encoded_public_key(char* out, size_t out_len);
    bool apply_other_public(const std::vector<uint8_t>& other_pubkey_b64);
    
    bool derive_key_from_passphrase(const std::string& passphrase, const std::vector<uint8_t>& salt);

    std::vector<uint8_t> encrypt_data_whole(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt_data_whole(const std::vector<uint8_t>& encrypted);

private:
    Mode mode;
    mbedtls_ecdh_context ecdh;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_gcm_context aes_ctx;
    uint8_t aes_key[16]; 

    bool aes_ready = false;
    bool ecdh_ready = false;

    bool aes_init_with_key(const uint8_t* key, size_t key_len);
};
