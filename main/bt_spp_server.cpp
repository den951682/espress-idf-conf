#include "bt_spp_server.hpp"
#include <string.h>
#include <inttypes.h>
#include "esp_etm.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_vfs.h"      
#include "sdkconfig.h"

static const char* TAG = "BtSppServer";
BtSppServer* BtSppServer::self_ = nullptr;

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

esp_err_t BtSppServer::start() {
	char bda_str[18] = {0};
    self_ = this;
    esp_err_t ret;

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
		ESP_LOGE(TAG, "%s initialize controller failed", __func__);
		return ret;
	}

	ESP_LOGI(TAG, "Mode value = %d", ESP_BT_MODE_CLASSIC_BT);

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK && on_event_) on_event_(Event::ControllerEnabled, ret);
    if (ret != ESP_OK) {
		ESP_LOGE(TAG, "%s enable controller failed: %d", __func__, ret);
		return ret;
	}

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
		ESP_LOGE(TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
		return ret;
	}

    ret = esp_bluedroid_enable();
    if (ret == ESP_OK && on_event_) on_event_(Event::BluedroidEnabled, ret);
    if (ret != ESP_OK) {
		ESP_LOGE(TAG, "%s enable bluedroid failed", __func__);
		return ret;
	}

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(&BtSppServer::gap_cb));
    
    uint8_t iocap = CONFIG_BT_SSP_IO_CAP; 
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)));
    
    if (on_event_) on_event_(Event::GapReady, ESP_OK);

    ret = esp_spp_register_callback(&BtSppServer::spp_cb);
    if (ret != ESP_OK) {
		 ESP_LOGE(TAG, "%s spp register failed", __func__);
		 return ret;
	}
	
	esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_VFS,          
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 512
    };

    ret = esp_spp_enhanced_init(&spp_cfg);
    if (ret == ESP_OK && on_event_) on_event_(Event::SppInited, ret);
    if (ret != ESP_OK) {
		 ESP_LOGE(TAG, "%s spp init failed", __func__);
		 return ret;
	}

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    return ret;
}

void BtSppServer::stop() {
    if (started_) {
        esp_spp_deinit();
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        started_ = false;
    }
}


void BtSppServer::gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
	switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
			if (self_ && self_->on_addr_) self_->on_addr_((const uint8_t*)param->auth_cmpl.bda);
            ESP_LOGI(TAG, "authentication success: %s", param->auth_cmpl.device_name);
            ESP_LOG_BUFFER_HEX(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %06" PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%06" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
        break;
    default: {
        ESP_LOGI(TAG, "event: %d", event);
        break;
    }
    }
    return;
}

void BtSppServer::spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    if (!self_) return;
    char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");
            esp_spp_vfs_register();
        } else {
            ESP_LOGE(TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_OPEN_EVT");
        break;
    case ESP_SPP_CLOSE_EVT:
        if (self_->g_fd >= 0) {
        	close(self_->g_fd);
        	self_->g_fd = -1;
    	}
        if (self_->on_event_) self_->on_event_(Event::ClientDisconnected, ESP_OK);
        ESP_LOGI(TAG, "ESP_SPP_CLOSE_EVT status:%d handle:%" PRIu32" close_by_remote:%d", param->close.status,
                 param->close.handle, param->close.async);
        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "ESP_SPP_START_EVT handle:%" PRIu32" sec_id:%d scn:%d", param->start.handle, param->start.sec_id,
                     param->start.scn);
            esp_bt_gap_set_device_name(CONFIG_BT_SERVER_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            if (self_->on_event_) self_->on_event_(Event::SppStarted, param->start.status);
        } else {
            ESP_LOGE(TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%" PRIu32", rem_bda:[%s]", param->srv_open.status,
                 param->srv_open.handle, bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        if (param->srv_open.status == ESP_SPP_SUCCESS) {
            int fd = param->srv_open.fd; 
            self_-> g_fd = fd;
            if (self_->on_event_) self_->on_event_(Event::ClientConnected, ESP_OK);
            if (self_->on_fd_ready_) self_->on_fd_ready_(fd);
        }
        break;
    case ESP_SPP_VFS_REGISTER_EVT:
        if (param->vfs_register.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "ESP_SPP_VFS_REGISTER_EVT");
            esp_spp_sec_t sec_mask = CONFIG_BT_SPP_SECURE_MODE ? ESP_SPP_SEC_AUTHENTICATE : ESP_SPP_SEC_NONE;
            esp_err_t ret = esp_spp_start_srv(sec_mask, ESP_SPP_ROLE_SLAVE, 0, CONFIG_BT_SERVER_NAME);
            if (ret == ESP_OK && self_->on_event_) self_->on_event_(Event::SppStarted, ret);
            self_->started_ = (ret == ESP_OK);
        } else {
           ESP_LOGE(TAG, "ESP_SPP_VFS_REGISTER_EVT status:%d", param->vfs_register.status);
        }
        break;
    default:
        break;
    }
}
