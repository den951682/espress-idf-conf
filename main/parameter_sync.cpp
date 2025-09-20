#include "parameter_store.cpp"
#include "fd_connection.hpp"
#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "message_type.cpp"
#include <set>
#include "freertos/semphr.h"

enum class ParamSetType : uint8_t {
    SetInt     = 0x04,
    SetFloat   = 0x05,
    SetString  = 0x06,
    SetBoolean = 0x07
};

enum class SetParam : uint8_t {
    Passphrase     = 0x01,
    ServerName   = 0x02,
};

using SetParameterCallback = std::function<void(const SetParam& setParam)>;;

class ParameterSync {
public:
    ParameterSync(paramstore::ParameterStore& store) : store_(store)
    {
        store_.onAnyChange([this](uint32_t id, const paramstore::Value& val){
        	enqueueId(id);
        });
        startTask();
    }
    
    bool handleSetParameter(ParamSetType type, const uint8_t* data, size_t datalen, SetParameterCallback cb) {
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
            if(static_cast<paramstore::ParameterId>(msg.id) == paramstore::ParameterId::DeviceName) {
				cb(SetParam::ServerName);
			}
			if(static_cast<paramstore::ParameterId>(msg.id) == paramstore::ParameterId::PassPhrase) {
				cb(SetParam::Passphrase);
			}
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
        if(connection_[0]) {
			const paramstore::Entry &e = store_.get(id);
			while(!(connection_[0] -> enqueueSend(buffer, ostream.bytes_written + 1)) && !e.meta.canLoss){
				if(resetFlag_) return;
				vTaskDelay(pdMS_TO_TICKS(5));
			};
        }
        if(connection_[1]) {
			const paramstore::Entry &e = store_.get(id);
			//ESP_LOGI(TAG, "try send value for %u", (unsigned)id);
			bool sent = false;
			do {
			    if (resetFlag_) return;
			    sent = connection_[1]->enqueueSend(buffer, ostream.bytes_written + 1);
			    if (!sent/* && !e.meta.canLoss*/) {
			        vTaskDelay(pdMS_TO_TICKS(5));
			    }
			} while (!sent/* && !e.meta.canLoss*/);
			//ESP_LOGI(TAG, "send info for %u %s", (unsigned)id, sent ? "success" : "failed");
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
		if(connection_[0]) {
			while(!(connection_[0] -> enqueueSend(buffer, ostream.bytes_written + 1))){
				if(resetFlag_) return;
				vTaskDelay(pdMS_TO_TICKS(5));
			};
        }
        if(connection_[1]) {
			//ESP_LOGI(TAG, "try send info for %u", (unsigned)id);
			bool sent = false;
			while (!sent) {
			    if (resetFlag_) return;
			    sent = connection_[1]->enqueueSend(buffer, ostream.bytes_written + 1);
			    if (!sent) {
			        vTaskDelay(pdMS_TO_TICKS(5));
			    }
			}
			//ESP_LOGI(TAG, "parameter %u sent successfully", (unsigned)id);
        }
    }

    void sendAllParameters() {
        for (auto& meta : store_.listMeta()) {
            enqueueId(meta.id);
        }
    }
    
    void sendAllParametersInfo() {
        if (!idQueue_) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        resetFlag_ = true;
        toSyncIds_.clear();
        xQueueReset(idQueue_);
        uint32_t marker = UINT32_MAX;
        xQueueSend(idQueue_, &marker, 0);
        xSemaphoreGive(mutex);
    }
    
    void setConnection(int connType, FdConnection* connection) {
		connection_[connType] = connection;
	}
	
	void removeConnection(int connType) {
		connection_[connType] = nullptr;
	}
	
	 void enqueueId(uint32_t id) {
        if (!idQueue_) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (toSyncIds_.count(id) == 0) {
            if (xQueueSend(idQueue_, &id, 0) == pdTRUE) {
                toSyncIds_.insert(id);
            }
        }
        xSemaphoreGive(mutex);
    }

private:
    static constexpr const char* TAG = "ParameterSync";
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    paramstore::ParameterStore& store_;
    FdConnection* connection_[2] = {nullptr, nullptr};
    
    TaskHandle_t taskHandle_ = nullptr;
    std::set<uint32_t> toSyncIds_;
    QueueHandle_t idQueue_ = nullptr;
    bool resetFlag_ = false;

	 void startTask(const char* name = "ParameterSyncTask", 
	                   uint32_t stackSize = 4096, 
	                   UBaseType_t priority = 5) {
		//must have size greater params_ in ParamStore
        idQueue_ = xQueueCreate(32, sizeof(uint32_t));
        xTaskCreatePinnedToCore(&ParameterSync::taskEntry, name,
                                stackSize, this, priority, &taskHandle_, 
                                tskNO_AFFINITY);
    }

    static void taskEntry(void* arg) {
        auto* self = static_cast<ParameterSync*>(arg);
        self -> taskLoop();
    }
    
    void taskLoop() {
		uint32_t id;
        while (true) {
            if (xQueueReceive(idQueue_, &id, portMAX_DELAY) == pdTRUE) {
				resetFlag_ = false;
                if (id == UINT32_MAX) {
                    sendAllParametersInfoInternal();
                } else {
                    const auto& e = store_.get(id);
                    sendParameterValue(id, e.value);
                }
                xSemaphoreTake(mutex, portMAX_DELAY);
                toSyncIds_.erase(id);
                xSemaphoreGive(mutex);
            } 
        }
        vTaskDelete(nullptr);
    }
    
    void sendAllParametersInfoInternal() {
        for (auto& meta : store_.listMeta()) {
            sendParameterInfo(meta.id, meta);
        }
    }
    
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
