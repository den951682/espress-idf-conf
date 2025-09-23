#include "parameter_store.cpp"
#include "fd_connection.hpp"
#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "message_type.cpp"
#include <set>
#include "freertos/semphr.h"
#include <inttypes.h>

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

enum class Action : uint32_t {
    Values      = 0x0, 
    Infos       = 0x1, 
    Custom1     = 0x2,
    Custom2     = 0x3
};

constexpr uint32_t ACTION_BITS = 4;                
constexpr uint32_t ACTION_MASK = (1u << 4) - 1;

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

    size_t writeParameterValue(uint32_t id, const paramstore::Value& val, uint8_t* buffer, size_t maxLen) {
		if (maxLen < 4) return 0;
        pb_ostream_t ostream = pb_ostream_from_buffer(buffer + 1, maxLen - 1);
        
        if (std::holds_alternative<int32_t>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Int);
            pModel_IntParameter msg;
            if (!toValueMessage(id, msg)) return 0;
            pb_encode(&ostream, pModel_IntParameter_fields, &msg);
        }
        else if (std::holds_alternative<float>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Float);
            pModel_FloatParameter msg;
            if (!toValueMessage(id, msg)) return 0;
            pb_encode(&ostream, pModel_FloatParameter_fields, &msg);
        }
        else if (std::holds_alternative<std::string>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::String);
            pModel_StringParameter msg;
            if (!toValueMessage(id, msg)) return 0;
            pb_encode(&ostream, pModel_StringParameter_fields, &msg);
        }
        else if (std::holds_alternative<bool>(val)) {
			buffer[0] = static_cast<uint8_t>(MessageType::Boolean);
            pModel_BooleanParameter msg;
            if (!toValueMessage(id, msg)) return 0;
            pb_encode(&ostream, pModel_BooleanParameter_fields, &msg);
        }
        //ESP_LOGI(TAG, "write value for %u %d bytes", (unsigned)id , (int)(ostream.bytes_written + 1));
        return ostream.bytes_written + 1;
    }
 
    void sendParameterInfo(int connType, uint32_t id, const paramstore::Meta& meta) {
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
        if(connection_[connType]) {
			//ESP_LOGI(TAG, "try send info for %u", (unsigned)id);
			bool sent = false;
			while (!sent) {
			    if (resetFlag_[connType]) return;
			    sent = connection_[connType]->enqueueSend(buffer, ostream.bytes_written + 1);
			    if (!sent) {
			        vTaskDelay(pdMS_TO_TICKS(20));
			    }
			}
			//ESP_LOGI(TAG, "parameter %u sent successfully", (unsigned)id);
        }
    }
    
    void sendAllParametersInfo(int connType) {
        if (!idQueue_) return;
        resetFlag_[connType] = true;
        uint32_t encoded = encodeIdAction(connType, Action::Infos);
        //ESP_LOGI(TAG, "sendAllParametersInfo %d", connType);
        xQueueSend(idQueue_, &encoded, 0);
    }
    
    void setConnection(int connType, FdConnection* connection) {
		connection_[connType] = connection;
		connection -> setTxDoneCallback([this, connType](){
				xSemaphoreTake(mutex, portMAX_DELAY);
				hasIds = !toSyncIds_[connType].empty();
				xSemaphoreGive(mutex);
			    if(readyForParameterValues[connType] && hasIds && !isBusy) {
					//ESP_LOGI(TAG, "TxDoneCallback %d", connType);
	       		 	uint32_t encoded = encodeIdAction(connType, Action::Values);
					xQueueSend(idQueue_, &encoded, 0);
				}
    		}		
		);		
	}
	
	void removeConnection(int connType) {
		connection_[connType] = nullptr;
		resetFlag_[connType] = true;
		readyForParameterValues[connType] = false;
	}
	
	 void enqueueId(uint32_t id) {
        if (!idQueue_) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        toSyncIds_[0].insert(id);
        toSyncIds_[1].insert(id);
        xSemaphoreGive(mutex);
        if(connection_[0] && readyForParameterValues[0] && connection_[0] -> isReady() && !isBusy) {
	       	uint32_t encoded = encodeIdAction(0, Action::Values);
			xQueueSend(idQueue_, &encoded, 0);
		}
		if(connection_[1] && readyForParameterValues[1] && connection_[1] -> isReady()  && !isBusy) {
	       	uint32_t encoded = encodeIdAction(1, Action::Values);
			xQueueSend(idQueue_, &encoded, 0);
		}
    }

private:
    static constexpr const char* TAG = "ParameterSync";
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    paramstore::ParameterStore& store_;
    FdConnection* connection_[2] = {nullptr, nullptr};
    
    TaskHandle_t taskHandle_ = nullptr;
    std::set<uint32_t> toSyncIds_[2];
    QueueHandle_t idQueue_ = nullptr;
    bool resetFlag_[2] = {false, false};
    bool readyForParameterValues[2] = {false, false};
	bool hasIds = false; 
	volatile bool isBusy = true;
	
	void startTask(const char* name = "ParameterSyncTask", 
	                   uint32_t stackSize = 4096, 
	                   UBaseType_t priority = 5) {
        idQueue_ = xQueueCreate(4, sizeof(uint32_t));
        xTaskCreatePinnedToCore(&ParameterSync::taskEntry, name,
                                stackSize, this, priority, &taskHandle_, 
                                tskNO_AFFINITY);
    }

    static void taskEntry(void* arg) {
        auto* self = static_cast<ParameterSync*>(arg);
        self -> taskLoop();
    }
    
    void taskLoop() {
		uint32_t enc;
        while (true) {
            if (xQueueReceive(idQueue_, &enc, portMAX_DELAY) == pdTRUE) {
				isBusy = true;
				//ESP_LOGI(TAG, "enc=0x%08" PRIX32 " (%" PRIu32 ")", enc, enc);
				uint32_t id = decodeId(enc);
    			Action act = decodeAction(enc);
    			resetFlag_[id] = false;
    			//ESP_LOGI(TAG, "decoded: id=%" PRIu32 ", action=%" PRIu32, id, (uint32_t)act);
				if(connection_[id]) {
					switch (act) {
				        case Action::Infos:
				            sendAllParametersInfoInternal(id);
							if(!resetFlag_[id]) readyForParameterValues[id] = true;
				            break;
				        case Action::Values:
				            updateChangedParameters(id);
				            break;
				        default:
				            break;
				    }
				}
				isBusy = false;
            } 
        }
        vTaskDelete(nullptr);
    }
    
    void updateChangedParameters(uint32_t connId) {
	    xSemaphoreTake(mutex, portMAX_DELAY);
	    auto idsCopy = toSyncIds_[connId];
	    xSemaphoreGive(mutex);
		uint8_t tmp[128];
		//ESP_LOGI(TAG, "idsCopy size=%u", (unsigned)idsCopy.size());
		for (uint32_t id : idsCopy) {
		    ESP_LOGI(TAG, "idsCopy element=%u", (unsigned)id);
		}
		for (uint32_t paramId : idsCopy) {
	    	const auto& entry = store_.get(paramId);
	        size_t written = writeParameterValue(paramId, entry.value, tmp, sizeof(tmp));
	        if (written == 0) continue; 

        	bool sent = false;
        	if (connection_[connId]) {
	            do {
	                if (resetFlag_[connId]) return;  
	                sent = connection_[connId] -> enqueueSend(tmp, written);
	                if (!sent/* && !entry.meta.canLoss*/) {
	                    vTaskDelay(pdMS_TO_TICKS(5));
	                }
	            } while (!sent/* && !entry.meta.canLoss*/);
	        }
			if (sent || entry.meta.canLoss) {
				xSemaphoreTake(mutex, portMAX_DELAY);
	            toSyncIds_[connId].erase(paramId);
	            xSemaphoreGive(mutex);
	        }
	    }
	}
    
    void sendAllParametersInfoInternal(int connType) {
        for (auto& meta : store_.listMeta()) {
			xSemaphoreTake(mutex, portMAX_DELAY);
			toSyncIds_[connType].insert(meta.id);
			xSemaphoreGive(mutex);
            sendParameterInfo(connType, meta.id, meta);  
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
    
    inline uint32_t encodeIdAction(uint32_t id, Action act) {
	    return (id << ACTION_BITS) | (static_cast<uint32_t>(act) & ACTION_MASK);
	}
	
	inline uint32_t decodeId(uint32_t value) {
	    return value >> ACTION_BITS;
	}
	
	inline Action decodeAction(uint32_t value) {
	    return static_cast<Action>(value & ACTION_MASK);
	}
};
