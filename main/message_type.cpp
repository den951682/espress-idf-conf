#include <sys/_stdint.h>

enum class MessageType : uint8_t {
    HandshakeRequest  = 0x00,
    HandshakeResponse = 0x01,
    ParameterInfo     = 0x02,
    Disconnect        = 0x03,

    Int               = 0x08,
    Float             = 0x09,
    String            = 0x10,
    Boolean           = 0x11,
    Message           = 0x12
};
