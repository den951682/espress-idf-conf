#include "config_protocol.hpp"
#include "ecdh_aes_protocol.hpp"
#include "passphrase_aes_protocol.hpp"
#include "raw_protocol.hpp"
#include "sdkconfig.h"

Protocol* createProtocol(const char* passPhrase) {
#if defined(CONFIG_PROTOCOL_EPHEMERAL)
    return new EcdhAesProtocol(passPhrase);
#elif defined(CONFIG_PROTOCOL_PASSPHRASE)
    return new PassphraseAesProtocol(passPhrase);
#elif defined(CONFIG_PROTOCOL_RAW)
    return new RawProtocol(passPhrase);
#else
    return new EcdhAesProtocol(passPhrase);
#endif
}

bool isFast() {
#if defined(CONFIG_PROTOCOL_EPHEMERAL) || defined(CONFIG_PROTOCOL_PASSPHRASE)
    return false;
#else
    return true;
#endif
}
