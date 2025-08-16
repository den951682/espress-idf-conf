#pragma once
#include <functional>
#include "esp_spp_api.h"
#include "esp_gap_bt_api.h"

class BtSppServer {
public:
    enum class Event {
        ControllerEnabled,
        BluedroidEnabled,
        GapReady,
        SppInited,
        SppStarted,
        ClientConnected,
        ClientDisconnected,
        Error
    };

    using OnEvent     = std::function<void(Event, int err)>;     
    using OnFdReady   = std::function<void(int fd)>;             
    using OnAddrShown = std::function<void(const uint8_t bt_addr[6])>;

    BtSppServer() = default;
    esp_err_t start(const char* serverName);
    void stop();

    void setOnEvent(OnEvent cb)         { on_event_ = std::move(cb); }
    void setOnFdReady(OnFdReady cb)     { on_fd_ready_ = std::move(cb); }
    void setOnAddr(OnAddrShown cb)      { on_addr_ = std::move(cb); }

private:
    const char* name;
    bool started_ = false;
    int g_fd = -1;

    OnEvent on_event_;
    OnFdReady on_fd_ready_;
    OnAddrShown on_addr_;

    static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
    static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

    static BtSppServer* self_; 
};
