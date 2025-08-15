#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <sys/_stdint.h>
#include <vector>
#include <variant>
#include <functional>
#include <memory>
#include <mutex>
#include <algorithm>
#include "sdkconfig.h"
#include "Parameters.pb.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace paramstore {

static constexpr const char* TAG = "ParameterStore";
static constexpr const char* NVS_NAMESPACE = "params";

static constexpr size_t kMaxStrValueBytes   = 64;   // StringParameter.value

enum class ParamType : uint32_t { Int = 0, Float = 1, String = 2, Bool = 3 };

using Value = std::variant<int32_t, float, std::string, bool>;
using ChangeCallback = std::function<void(uint32_t id, const Value& newValue)>;

enum class ParameterId : uint32_t {
    PassPhrase       = 0,
    DeviceName       = 1,
    LedEnabled       = 2,
    BlinkCount       = 3,
    Uptime           = 4,
    JoystickX        = 5,
    JoystickY        = 6,
    ExampleText      = 7,
    ExampleBool      = 8
};

struct Meta {
    uint32_t    id{};
    std::string name;        
    std::string description; 
    bool        editable{true};
    float       minValue{0.f};
    float       maxValue{0.f};
    ParamType   type{ParamType::Int};
};

struct Entry {
    Meta  meta;
    Value value; 
};

class ParameterStore {
public:
    ParameterStore() = default;
    ~ParameterStore() {
        close();
    }

    esp_err_t begin(const char* nvsNamespace = NVS_NAMESPACE) {
        std::lock_guard<std::mutex> lk(mu_);
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        if (err != ESP_OK) return err;

        err = nvs_open(nvsNamespace, NVS_READWRITE, &nvs_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    }

    void close() {
        std::lock_guard<std::mutex> lk(mu_);
        if (nvs_) {
            nvs_close(nvs_);
            nvs_ = 0;
        }
    }

    void addIntParam(ParameterId id,
    				 int32_t def, 
    				 const std::string& name,
                     const std::string& descr, 
                     int32_t minV, 
                     int32_t maxV, 
                     bool editable) {
		Entry e;
        e.meta = Meta{ static_cast<uint32_t>(id), name, descr, editable, static_cast<float>(minV), static_cast<float>(maxV), ParamType::Int };
        e.value = def;
        params_[static_cast<uint32_t>(id)] = std::move(e);
    }

    void addFloatParam(ParameterId id, 
    				   float def, 
    				   const std::string& name,
                       const std::string& descr,
                       float minV, 
                       float maxV,
                       bool editable) {
        Entry e;
        e.meta = Meta{ static_cast<uint32_t>(id), name, descr, editable, minV, maxV, ParamType::Float };
        e.value = def;
        params_[static_cast<uint32_t>(id)] = std::move(e);
    }

    void addStringParam(ParameterId id,
    					const std::string& def, 
    					const std::string& name,
                        const std::string& descr, 
                        bool editable) {
        Entry e;
        e.meta = Meta{ static_cast<uint32_t>(id), name, descr, editable, 0.f, 0.f, ParamType::String };
        e.value = def;
        params_[static_cast<uint32_t>(id)] = std::move(e);
    }

    void addBoolParam(ParameterId id, 
    				  bool def, 
    				  const std::string& name,
                      const std::string& descr, 
                      bool editable) {
        Entry e;
        e.meta = Meta{ static_cast<uint32_t>(id), name, descr, editable, 0.f, 0.f, ParamType::Bool };
        e.value = def;
        params_[static_cast<uint32_t>(id)] = std::move(e);
    }

    void loadFromNvs() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto &kv : params_) {
            Entry &e = kv.second;
            if (!e.meta.editable) continue;
            switch (e.meta.type) {
                case ParamType::Int: {
                    int32_t v = std::get<int32_t>(e.value);
                    if (nvs_get_i32_(e.meta.id, v) == ESP_OK) e.value = v; else nvs_set_i32_(e.meta.id, std::get<int32_t>(e.value));
                } break;
                case ParamType::Float: {
                    float v = std::get<float>(e.value);
                    if (nvs_get_float_(e.meta.id, v) == ESP_OK) e.value = v; else nvs_set_float_(e.meta.id, std::get<float>(e.value));
                } break;
                case ParamType::String: {
                    std::string v = std::get<std::string>(e.value);
                    if (nvs_get_string_(e.meta.id, v) == ESP_OK) e.value = v; else nvs_set_string_(e.meta.id, v);
                } break;
                case ParamType::Bool: {
                    bool b = std::get<bool>(e.value) ? 0 : 1;
                    if (nvs_get_bool_(e.meta.id, b) == ESP_OK) e.value = (b != 0); else nvs_set_bool_(e.meta.id, b);
                } break;
            }
        }
        nvs_commit(nvs_);
    }

    void saveEditableToNvs() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto &kv : params_) {
            Entry &e = kv.second;
            if (!e.meta.editable) continue;
            switch (e.meta.type) {
                case ParamType::Int:   nvs_set_i32_(e.meta.id, std::get<int32_t>(e.value)); break;
                case ParamType::Float: nvs_set_float_(e.meta.id, std::get<float>(e.value)); break;
                case ParamType::String:nvs_set_string_(e.meta.id, std::get<std::string>(e.value)); break;
                case ParamType::Bool:  nvs_set_bool_(e.meta.id, std::get<bool>(e.value)); break;
            }
        }
        nvs_commit(nvs_);
    }

    void onChange(ParameterId id, ChangeCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        perId_[static_cast<uint32_t>(id)].push_back(std::move(cb));
    }
    void onAnyChange(ChangeCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        global_.push_back(std::move(cb));
    }

    esp_err_t setInt(ParameterId id, int32_t v) { return setValue_(static_cast<uint32_t>(id), v); }
    esp_err_t setFloat(ParameterId id, float v) { return setValue_(static_cast<uint32_t>(id), v); }
    esp_err_t setString(ParameterId id, const std::string& v) { return setValue_(static_cast<uint32_t>(id), v); }
    esp_err_t setBool(ParameterId id, bool v) { return setValue_(static_cast<uint32_t>(id), v); }

    int32_t     getInt(ParameterId id)   { return std::get<int32_t>(at_(static_cast<uint32_t>(id)).value); }
    float       getFloat(ParameterId id) { return std::get<float>(at_(static_cast<uint32_t>(id)).value); }
    std::string getString(ParameterId id){ return std::get<std::string>(at_(static_cast<uint32_t>(id)).value); }
    bool        getBool(ParameterId id)  { return std::get<bool>(at_(static_cast<uint32_t>(id)).value); }
    Value       getValue(ParameterId id)  { return at_(static_cast<uint32_t>(id)).value; }
    Entry       get(uint32_t id)  { return at_(id); }

    std::vector<Meta> listMeta() const {
        std::vector<Meta> v;
        v.reserve(params_.size());
        for (auto &kv: params_) v.push_back(kv.second.meta);
        return v;
    }

    void setupDefaults() {
		const std::string &passPhrase = CONFIG_PASSPHRASE;
		const std::string &deviceName = CONFIG_BT_SERVER_NAME;
        addStringParam(ParameterId::PassPhrase, passPhrase, "Pass-фраза", "На її основі генерується симетричний ключ для обміну повідомлень", true);
        addStringParam(ParameterId::DeviceName, deviceName, "Назва Bluetooth пристрою", "Відображається у результатах сканування пристроїв", true);
        addBoolParam  (ParameterId::LedEnabled, true, "LED увімкнено", "Увімкни діод", true);
        addIntParam   (ParameterId::BlinkCount, 3, "Кількість мигань", "Кількість послідовних коротких мигань розділених паузою", 1, 9, true);

        addIntParam   (ParameterId::Uptime, 0, "Час від запуску", "Демонстрація динамічного оновлення параметру", 0, 99999, false);
        addIntParam   (ParameterId::JoystickX, 2048, "Джойстик X", "Положення джойстика по осі X", 0, 4095, false);
        addIntParam   (ParameterId::JoystickY, 2048, "Джойстик Y", "Положення джойстика по осі Y", 0, 4095, false);
        addStringParam(ParameterId::ExampleText, "Значення", "Приклад Текст", "Приклад відображення текстового параметру", false);
        addBoolParam  (ParameterId::ExampleBool, true, "Приклад Буль", "Приклад відображення булевого параметру", false);
    }

private:
    static std::string key_(uint32_t id, char prefix) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%c%u", prefix, static_cast<unsigned>(id));
        return std::string(buf);
    }

    esp_err_t nvs_set_i32_(uint32_t id, int32_t v) {
        auto k = key_(id, 'i');
        return nvs_set_i32(nvs_, k.c_str(), v);
    }
    esp_err_t nvs_get_i32_(uint32_t id, int32_t &out) {
        auto k = key_(id, 'i');
        return nvs_get_i32(nvs_, k.c_str(), &out);
    }

    esp_err_t nvs_set_float_(uint32_t id, float v) {
        auto k = key_(id, 'f');
        return nvs_set_blob(nvs_, k.c_str(), &v, sizeof(v));
    }
    esp_err_t nvs_get_float_(uint32_t id, float &out) {
        auto k = key_(id, 'f');
        size_t sz = sizeof(out);
        return nvs_get_blob(nvs_, k.c_str(), &out, &sz);
    }

    esp_err_t nvs_set_bool_(uint32_t id, bool v) {
        auto k = key_(id, 'b');
        return nvs_set_u8(nvs_, k.c_str(), v ? 1 : 0);
    }
    esp_err_t nvs_get_bool_(uint32_t id, bool &out) {
        auto k = key_(id, 'b');
        uint8_t res;
        return nvs_get_u8(nvs_, k.c_str(), &res);
        out = res == 1;
    }

    esp_err_t nvs_set_string_(uint32_t id, const std::string &s) {
        auto k = key_(id, 's');
        return nvs_set_str(nvs_, k.c_str(), s.c_str());
    }
    esp_err_t nvs_get_string_(uint32_t id, std::string &out) {
        auto k = key_(id, 's');
        size_t needed = 0;
        esp_err_t err = nvs_get_str(nvs_, k.c_str(), nullptr, &needed);
        if (err != ESP_OK) return err;
        std::string tmp(needed ? needed - 1 : 0, '\0');
        err = nvs_get_str(nvs_, k.c_str(), tmp.data(), &needed);
        if (err == ESP_OK) out = std::move(tmp);
        return err;
    }

    Entry& at_(uint32_t id) {
        auto it = params_.find(id);
        if (it == params_.end()) {
            ESP_LOGE(TAG, "Unknown parameter id=%u", (unsigned)id);
            abort();
        }
        return it->second;
    }
    
    const Entry& at_(uint32_t id) const {
        auto it = params_.find(id);
        if (it == params_.end()) {
            ESP_LOGE(TAG, "Unknown parameter id=%u", (unsigned)id);
            abort();
        }
        return it->second;
    }

    int32_t clampInt_(uint32_t id, int32_t v) const {
        const Entry &e = at_(id);
        if (e.meta.type != ParamType::Int) return v;
        int32_t mn = static_cast<int32_t>(e.meta.minValue);
        int32_t mx = static_cast<int32_t>(e.meta.maxValue);
        if (mn <= mx) v = std::min(std::max(v, mn), mx);
        return v;
    }
    
    float clampFloat_(uint32_t id, float v) const {
        const Entry &e = at_(id);
        if (e.meta.type != ParamType::Float) return v;
        if (e.meta.minValue <= e.meta.maxValue) v = std::min(std::max(v, e.meta.minValue), e.meta.maxValue);
        return v;
    }

    template<typename T>
    esp_err_t setValue_(uint32_t id, T v) {
        std::lock_guard<std::mutex> lk(mu_);
        Entry &e = at_(id);
        
        if (!std::holds_alternative<T>(e.value)) return ESP_ERR_INVALID_ARG;

        if constexpr (std::is_same_v<T, int32_t>) v = clampInt_(id, v);
        if constexpr (std::is_same_v<T, float>)   v = clampFloat_(id, v);
        if constexpr (std::is_same_v<T, std::string>) {
            if (v.size() > kMaxStrValueBytes) v.resize(kMaxStrValueBytes); 
        }

        if (std::get<T>(e.value) == v) return ESP_OK; 
        e.value = v;

        if (e.meta.editable) {
            switch (e.meta.type) {
                case ParamType::Int:   nvs_set_i32_(id, std::get<int32_t>(e.value)); break;
                case ParamType::Float: nvs_set_float_(id, std::get<float>(e.value)); break;
                case ParamType::String:nvs_set_string_(id, std::get<std::string>(e.value)); break;
                case ParamType::Bool:  nvs_set_bool_(id, std::get<bool>(e.value)); break;
            }
            nvs_commit(nvs_);
        }

        fireCallbacks_(id, e.value);
        return ESP_OK;
    }

    void fireCallbacks_(uint32_t id, const Value &v) {
        if (auto it = perId_.find(id); it != perId_.end()) {
            for (auto &cb : it->second) cb(id, v);
        }
        
        for (auto &cb : global_) cb(id, v);
    }

private:
    mutable std::mutex mu_{};
    nvs_handle_t nvs_{0};
    std::map<uint32_t, Entry> params_{};
    std::map<uint32_t, std::vector<ChangeCallback>> perId_{};
    std::vector<ChangeCallback> global_{};
};
} 