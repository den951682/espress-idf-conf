#include "parameter_store.cpp"
#include "fd_connection.hpp"
#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "message_type.cpp"

enum class ParamSetType : uint8_t {
    SetInt     = 0x04,
    SetFloat   = 0x05,
    SetString  = 0x06,
    SetBoolean = 0x07
};

using HandlerFunc = bool(*)(const uint8_t*, size_t);

class ParameterSync {
public:
    ParameterSync(paramstore::ParameterStore& store) : store_(store)
    {
        store_.onAnyChange([this](uint32_t id, const paramstore::Value& val){
            sendParameterValue(id, val);
        });
    }
    
    bool handleSetParameter(ParamSetType type, const uint8_t* data, size_t datalen) {
    switch (type) {
        case ParamSetType::SetInt: {
            pModel_IntParameter msg = pModel_IntParameter_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(data, datalen);
            if (!pb_decode(&stream, pModel_IntParameter_fields, &msg)) {
                ESP_LOGE(TAG, "Failed to decode IntParameter: %s", PB_GET_ERROR(&stream));
                return false;
            }
            store_.setInt(static_cast<paramstore::ParameterId>(msg.id), static_cast<int32_t>(msg.value));
            return true;
        }

        case ParamSetType::SetFloat: {
            pModel_FloatParameter msg = pModel_FloatParameter_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(data, datalen);
            if (!pb_decode(&stream, pModel_FloatParameter_fields, &msg)) {
                ESP_LOGE(TAG, "Failed to decode FloatParameter: %s", PB_GET_ERROR(&stream));
                return false;
            }
            store_.setFloat(static_cast<paramstore::ParameterId>(msg.id), static_cast<float>(msg.value));
            return true;
        }

        case ParamSetType::SetString: {
            pModel_StringParameter msg = pModel_StringParameter_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(data, datalen);
            if (!pb_decode(&stream, pModel_StringParameter_fields, &msg)) {
                ESP_LOGE(TAG, "Failed to decode StringParameter: %s", PB_GET_ERROR(&stream));
                return false;
            }
            std::string value(reinterpret_cast<char*>(msg.value.bytes), msg.value.size);
            store_.setString(static_cast<paramstore::ParameterId>(msg.id), value);
            return true;
        }

        case ParamSetType::SetBoolean: {
            pModel_BooleanParameter msg = pModel_BooleanParameter_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(data, datalen);
            if (!pb_decode(&stream, pModel_BooleanParameter_fields, &msg)) {
                ESP_LOGE(TAG, "Failed to decode BooleanParameter: %s", PB_GET_ERROR(&stream));
                return false;
            }
            store_.setBool(static_cast<paramstore::ParameterId>(msg.id), static_cast<bool>(msg.value));
            return true;
        }

        default:
            ESP_LOGW(TAG, "Unknown parameter set type: %d", static_cast<int>(type));
            return false;
    }
}

    void sendParameterValue(uint32_t id, const paramstore::Value& val) {
        uint8_t buffer[128];
        pb_ostream_t ostream = pb_ostream_from_buffer(buffer + 1, sizeof(buffer) - 1);

        if (std::holds_alternative<int32_t>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Int);
            pModel_IntParameter msg;
            if (!toValueMessage(id, msg)) return;
            pb_encode(&ostream, pModel_IntParameter_fields, &msg);
        }
        else if (std::holds_alternative<float>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Float);
            pModel_FloatParameter msg;
            if (!toValueMessage(id, msg)) return;
            pb_encode(&ostream, pModel_FloatParameter_fields, &msg);
        }
        else if (std::holds_alternative<std::string>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::String);
            pModel_StringParameter msg;
            if (!toValueMessage(id, msg)) return;
            pb_encode(&ostream, pModel_StringParameter_fields, &msg);
        }
        else if (std::holds_alternative<bool>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Boolean);
            pModel_BooleanParameter msg;
            if (!toValueMessage(id, msg)) return;
            pb_encode(&ostream, pModel_BooleanParameter_fields, &msg);
        }
        if(connection_) {
          	connection_ -> enqueueSend(buffer, ostream.bytes_written + 1);
        }
    }
 
    void sendParameterInfo(uint32_t id, const paramstore::Meta& meta) {
        uint8_t buffer[512];
        pb_ostream_t ostream = pb_ostream_from_buffer(buffer + 1, sizeof(buffer) - 1);
		buffer[0] = static_cast<uint8_t>(MessageType::ParameterInfo);
        pModel_ParameterInfo out = pModel_ParameterInfo_init_zero;
        out.id = meta.id;
        out.editable = meta.editable;
        out.min_value = meta.minValue;
        out.max_value = meta.maxValue;
        out.type = static_cast<uint32_t>(meta.type);
               
        size_t n = std::min(meta.name.size(), sizeof(out.name.bytes));
        out.name.size = n;
        memcpy(out.name.bytes, meta.name.data(), n);
     
        n = std::min(meta.description.size(), sizeof(out.description.bytes));
        out.description.size = n;
        memcpy(out.description.bytes, meta.description.data(), n);

		pb_encode(&ostream, pModel_ParameterInfo_fields, &out);
		if(connection_) {
			ESP_LOG_BUFFER_HEX(TAG, buffer, ostream.bytes_written + 1);
        	connection_ -> enqueueSend(buffer, ostream.bytes_written + 1);
        }
    }

    void sendAllParameters() {
        for (auto& meta : store_.listMeta()) {
            const auto& e = store_.get(meta.id);
            sendParameterValue(meta.id, e.value);
        }
    }
    
    void sendAllParametersInfo() {
        for (auto& meta : store_.listMeta()) {
            sendParameterInfo(meta.id, meta);
        }
    }
    
    void setConnection(FdConnection* connection) {
		connection_ = connection;
	}
	
	void removeConnection() {
		connection_ = nullptr;
	}

private:
    static constexpr const char* TAG = "ParameterSync";

    paramstore::ParameterStore& store_;
    FdConnection* connection_;
    
    bool toValueMessage(uint32_t id, pModel_IntParameter &msg) const {
        const paramstore::Entry &e = store_.get(id);
        if (e.meta.type != paramstore::ParamType::Int) return false;
        msg = pModel_IntParameter_init_zero;
        msg.id = id;
        msg.value = std::get<int32_t>(e.value);
        return true;
    }

    bool toValueMessage(uint32_t id, pModel_FloatParameter &msg) const {
        const paramstore::Entry &e = store_.get(id);
        if (e.meta.type != paramstore::ParamType::Float) return false;
        msg = pModel_FloatParameter_init_zero;
        msg.id = id;
        msg.value = std::get<float>(e.value);
        return true;
    }
    
    bool toValueMessage(uint32_t id, pModel_StringParameter &msg) const {
        const paramstore::Entry &e = store_.get(id);
        if (e.meta.type != paramstore::ParamType::String) return false;
        msg = pModel_StringParameter_init_zero;
        msg.id = id;
        const auto &str = std::get<std::string>(e.value);
        size_t n = std::min(str.size(), sizeof(msg.value.bytes));
        msg.value.size = n;
        memcpy(msg.value.bytes, str.data(), n);
        return true;
    }
    
    bool toValueMessage(uint32_t id, pModel_BooleanParameter &msg) const {
        const paramstore::Entry &e = store_.get(id);
        if (e.meta.type != paramstore::ParamType::Bool) return false;
        msg = pModel_BooleanParameter_init_zero;
        msg.id = id;
        msg.value = std::get<bool>(e.value);
        return true;
    }
};
