#include "crypto_ecdh_aes.hpp"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include <cstring>
#include <sys/_intsup.h>
#include <vector>

static const char* TAG = "CryptoEcdhAes";

CryptoEcdhAes::CryptoEcdhAes(Mode mode, const char* passPhrase) : mode(mode) {
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_entropy_init(&entropy);
	mbedtls_gcm_init(&aes_ctx);
	const char* pers = "ecdh_aes";
	mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
	                          (const unsigned char*)pers, strlen(pers));
	if(mode == Mode::PASSPHRASE) {
		 std::vector<uint8_t> salt = {0, 1, 2, 3, 4, 5, 6, 7};
		 derive_key_from_passphrase(passPhrase, salt);
	} else {
	   	 mbedtls_ecdh_init(&ecdh);
	   	 generate_keypair();
    }
    ESP_LOGI("CryptoEcdhAes", "Initialized in mode: %s",
             (mode == Mode::PASSPHRASE) ? "PASSPHRASE" : "EPHEMERAL");
}

CryptoEcdhAes::~CryptoEcdhAes() {
	if(mode != Mode::PASSPHRASE) {
		mbedtls_ecdh_free(&ecdh);
    }
    mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
    mbedtls_gcm_free(&aes_ctx);
}

bool CryptoEcdhAes::generate_keypair() {
    int ret = mbedtls_ecp_group_load(&ecdh.private_ctx.private_mbed_ecdh.private_grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ecp_group_load failed: %d", ret);
        return false;
    }
    ret = mbedtls_ecdh_gen_public(&ecdh.private_ctx.private_mbed_ecdh.private_grp, &ecdh.private_ctx.private_mbed_ecdh.private_d,
     &ecdh.private_ctx.private_mbed_ecdh.private_Q,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ecdh_gen_public failed: %d", ret);
        return false;
    }
    ecdh_ready = true;
    return true;
}

std::vector<uint8_t> CryptoEcdhAes::get_public_key_raw() {
    std::vector<uint8_t> buf(65); 
    size_t olen = 0;

    int ret = mbedtls_ecp_point_write_binary(
        &ecdh.private_ctx.private_mbed_ecdh.private_grp,
        &ecdh.private_ctx.private_mbed_ecdh.private_Q,
        MBEDTLS_ECP_PF_UNCOMPRESSED,   
        &olen,
        buf.data(), buf.size()
    );

    if (ret != 0 || olen == 0) {
        ESP_LOGE(TAG, "mbedtls_ecp_point_write_binary failed: %d", ret);
        return {};
    }

    buf.resize(olen);
    return buf;
}


void CryptoEcdhAes::get_encoded_public_key(char* out, size_t out_len) {
    const std::vector<uint8_t>& pk = get_public_key_raw();
    size_t olen = 0;
    int ret = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(out), out_len, &olen,
        pk.data(), pk.size()
    );
    if (ret != 0) {
        ESP_LOGE("ECDH", "Base64 encode failed: %d", ret);
        out[0] = '\0';
        return;
    }
}

bool CryptoEcdhAes::apply_other_public(const std::vector<uint8_t>& other_pubkey_b64) {
    size_t olen = 0;
    std::vector<uint8_t> other_pubkey_raw(200); 
    int ret = mbedtls_base64_decode(
        other_pubkey_raw.data(), other_pubkey_raw.size(), &olen,
        other_pubkey_b64.data(), other_pubkey_b64.size()
    );
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        return false;
    }
    
    other_pubkey_raw.resize(olen);
    mbedtls_ecp_point Qp;
    mbedtls_ecp_point_init(&Qp);

    ret = mbedtls_ecp_point_read_binary(&ecdh.private_ctx.private_mbed_ecdh.private_grp, &Qp,
                                            other_pubkey_raw.data(), other_pubkey_raw.size());
    if (ret != 0) {
        ESP_LOGE(TAG, "ecp_point_read_binary failed: %d", ret);
        mbedtls_ecp_point_free(&Qp);
        return false;
    }

    mbedtls_mpi shared_secret;
    mbedtls_mpi_init(&shared_secret);

    ret = mbedtls_ecdh_compute_shared(&ecdh.private_ctx.private_mbed_ecdh.private_grp, &shared_secret, &Qp, &ecdh.private_ctx.private_mbed_ecdh.private_d,
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ecp_point_free(&Qp);

    if (ret != 0) {
        ESP_LOGE(TAG, "ecdh_compute_shared failed: %d", ret);
        mbedtls_mpi_free(&shared_secret);
        return false;
    }

    std::vector<uint8_t> secret(mbedtls_mpi_size(&shared_secret));
    mbedtls_mpi_write_binary(&shared_secret, secret.data(), secret.size());
    mbedtls_mpi_free(&shared_secret);

    uint8_t hash[32];
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&md_ctx);
    mbedtls_md_update(&md_ctx, secret.data(), secret.size());
    mbedtls_md_finish(&md_ctx, hash);
    mbedtls_md_free(&md_ctx);

    return aes_init_with_key(hash, 16);
}

std::vector<uint8_t> CryptoEcdhAes::encrypt_data_whole(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> iv(12);
    mbedtls_ctr_drbg_random(&ctr_drbg, iv.data(), iv.size());

    std::vector<uint8_t> output(data.size());
    std::vector<uint8_t> tag(16);

    mbedtls_gcm_crypt_and_tag(&aes_ctx, MBEDTLS_GCM_ENCRYPT,
                              data.size(), iv.data(), iv.size(),
                              nullptr, 0, data.data(),
                              output.data(), tag.size(), tag.data());

    std::vector<uint8_t> full;
    full.reserve(iv.size() + output.size() + tag.size());
    full.insert(full.end(), iv.begin(), iv.end());
    full.insert(full.end(), output.begin(), output.end());
    full.insert(full.end(), tag.begin(), tag.end());
    return full;
}

std::vector<uint8_t> CryptoEcdhAes::decrypt_data_whole(const std::vector<uint8_t>& encrypted) {
    if (encrypted.size() < 12 + 16) {
        return {};
    }
    const uint8_t* iv = encrypted.data();
    size_t iv_len = 12;

    const uint8_t* tag = encrypted.data() + encrypted.size() - 16;
    const uint8_t* ciphertext = encrypted.data() + iv_len;
    size_t ciphertext_len = encrypted.size() - iv_len - 16;

    std::vector<uint8_t> output(ciphertext_len);

    int ret = mbedtls_gcm_auth_decrypt(&aes_ctx, ciphertext_len,
                                       iv, iv_len, nullptr, 0,
                                       tag, 16, ciphertext, output.data());
    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_auth_decrypt failed: %d", ret);
        return {};
    }
    return output;
}

bool CryptoEcdhAes::derive_key_from_passphrase(const std::string& passphrase, const std::vector<uint8_t>& salt) {
    uint8_t out[32];
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                        (const unsigned char*)passphrase.data(),
                                        passphrase.size(),
                                        salt.data(),
                                        salt.size(),
                                        10000,
                                        sizeof(out),
                                        out);
    if (ret != 0) {
        ESP_LOGE(TAG, "pbkdf2 failed: %d", ret);
        return false;
    }
    return aes_init_with_key(out, 32);
}

bool CryptoEcdhAes::aes_init_with_key(const uint8_t* key, size_t key_len) {
    memcpy(aes_key, key, key_len);
    int ret = mbedtls_gcm_setkey(&aes_ctx, MBEDTLS_CIPHER_ID_AES, aes_key, key_len * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "gcm_setkey failed: %d", ret);
        return false;
    }
    aes_ready = true;
    return true;
}

