#include "config_protocol.hpp"
#include "ecdh_aes_protocol.hpp"
#include "passphrase_aes_protocol.hpp"
#include "raw_protocol.hpp"
#include "sdkconfig.h"

std::unique_ptr<Protocol> createProtocol(const char* passPhrase) {
#if defined(CONFIG_PROTOCOL_EPHEMERAL)
    return std::make_unique<EcdhAesProtocol>(passPhrase);
#elif defined(CONFIG_PROTOCOL_PASSPHRASE)
    return std::make_unique<PassphraseAesProtocol>(passPhrase);
#elif defined(CONFIG_PROTOCOL_RAW)
    return std::make_unique<RawProtocol>(passPhrase);
#else
    return std::make_unique<EcdhAesProtocol>(passPhrase);
#endif
}

bool isFast() {
#if defined(CONFIG_PROTOCOL_EPHEMERAL) || defined(CONFIG_PROTOCOL_PASSPHRASE)
    return false;
#else
    return true;
#endif
}
