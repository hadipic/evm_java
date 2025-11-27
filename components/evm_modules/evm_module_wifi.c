#include "evm_module_wifi.h"
#include "esp_log.h"
#include "hardware_manager.h"
#include "wifi_driver.h"
#include <string.h>

static const char *TAG = "EVM_WIFI";

// ==================== توابع JavaScript ====================

static void js_wifi_staIP(js_State *J) {
    const char* ip = hardware_get_sta_ip();
    js_pushstring(J, ip ? ip : "0.0.0.0");
}

static void js_wifi_apIP(js_State *J) {
    const char* ip = hardware_get_ap_ip();
    js_pushstring(J, ip ? ip : "192.168.4.1");
}

static void js_wifi_init(js_State *J) {
    js_pushboolean(J, hardware_is_wifi_ready());
}

static void js_wifi_mode(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    const char *mode = js_tostring(J, 1);
    wifi_mode_t wifi_mode;
    
    if (strcmp(mode, "sta") == 0) wifi_mode = WIFI_MODE_STA;
    else if (strcmp(mode, "ap") == 0) wifi_mode = WIFI_MODE_AP;
    else if (strcmp(mode, "both") == 0) wifi_mode = WIFI_MODE_APSTA;
    else {
        js_pushboolean(J, 0);
        return;
    }
    
    esp_err_t ret = hardware_wifi_set_mode(wifi_mode);
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_sta(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    const char *ssid = js_tostring(J, 1);
    const char *pass = js_isundefined(J, 2) ? "" : js_tostring(J, 2);
    
    esp_err_t ret = hardware_configure_sta(ssid, pass);
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_ap(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    const char *ssid = js_tostring(J, 1);
    const char *pass = js_isundefined(J, 2) ? "" : js_tostring(J, 2);
    int channel = js_isundefined(J, 3) ? 6 : js_toint32(J, 3);
    
    esp_err_t ret = hardware_wifi_set_ap_config(ssid, pass, channel);
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_power(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    int8_t power = (int8_t)js_tonumber(J, 1);
    esp_err_t ret = hardware_wifi_set_power(power);
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_status(js_State *J) {
    js_newobject(J);
    
    // وضعیت STA
    js_pushboolean(J, hardware_wifi_is_sta_connected());
    js_setproperty(J, -2, "sta");
    
    // وضعیت AP
    js_pushboolean(J, hardware_is_wifi_ready());
    js_setproperty(J, -2, "ap");
    
    // وضعیت کلی
    js_pushboolean(J, hardware_is_wifi_ready());
    js_setproperty(J, -2, "ready");
    
    // آدرس‌های IP
    js_pushstring(J, hardware_get_ap_ip());
    js_setproperty(J, -2, "apIP");
    
    js_pushstring(J, hardware_get_sta_ip());
    js_setproperty(J, -2, "staIP");
    
    // اطلاعات اضافی - استفاده از wifi_app_config_t
    const wifi_app_config_t* config = hardware_wifi_get_config();
    if (config) {
        js_pushstring(J, config->ap_ssid);
        js_setproperty(J, -2, "apSSID");
        
        js_pushstring(J, config->sta_ssid);
        js_setproperty(J, -2, "staSSID");
    }
}

static void js_wifi_scan(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_newarray(J);
        return;
    }
    
    esp_err_t ret = hardware_wifi_scan_networks();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %d", ret);
        js_newarray(J);
        return;
    }
    
    uint16_t count = hardware_wifi_get_scan_count();
    
    // استفاده از wifi_ap_record_t
    wifi_ap_record_t networks[16];
    
    if (hardware_wifi_get_scan_results(networks, 16) != ESP_OK) {
        js_newarray(J);
        return;
    }
    
    js_newarray(J);
    for (int i = 0; i < count && i < 16; i++) {
        js_newobject(J);
        
        js_pushstring(J, (char*)networks[i].ssid);
        js_setproperty(J, -2, "ssid");
        
        js_pushnumber(J, networks[i].rssi);
        js_setproperty(J, -2, "rssi");
        
        js_pushnumber(J, networks[i].primary);
        js_setproperty(J, -2, "channel");
        
        const char* auth = hardware_wifi_get_auth_mode_string(networks[i].authmode);
        js_pushstring(J, auth);
        js_setproperty(J, -2, "auth");
        
        js_setindex(J, -2, i);
    }
}

// توابع پیشرفته
static void js_wifi_autoScan(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    esp_err_t ret = hardware_wifi_scan_networks();
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_autoConnect(js_State *J) {
    if (!hardware_is_wifi_ready()) {
        js_pushboolean(J, 0);
        return;
    }
    
    esp_err_t ret = hardware_wifi_auto_connect();
    js_pushboolean(J, ret == ESP_OK);
}

static void js_wifi_isStaConnected(js_State *J) {
    js_pushboolean(J, hardware_wifi_is_sta_connected());
}

static void js_wifi_maintain(js_State *J) {
    hardware_wifi_maintain_connection();
    js_pushboolean(J, 1);
}

static void js_wifi_setAutoReconnect(js_State *J) {
    bool enable = js_toboolean(J, 1);
    esp_err_t ret = hardware_wifi_set_auto_reconnect(enable);
    js_pushboolean(J, ret == ESP_OK);
}

// تابع جدید برای دریافت اطلاعات WiFi
static void js_wifi_info(js_State *J) {
    js_newobject(J);
    
    js_pushboolean(J, hardware_is_wifi_ready());
    js_setproperty(J, -2, "initialized");
    
    js_pushstring(J, hardware_get_ap_ip());
    js_setproperty(J, -2, "apIP");
    
    js_pushstring(J, hardware_get_sta_ip());
    js_setproperty(J, -2, "staIP");
    
    uint32_t scan_age = hardware_wifi_get_scan_age();
    js_pushnumber(J, scan_age);
    js_setproperty(J, -2, "lastScanAge");
    
    // استفاده از wifi_app_config_t
    const wifi_app_config_t* config = hardware_wifi_get_config();
    if (config) {
        js_pushstring(J, config->ap_ssid);
        js_setproperty(J, -2, "apSSID");
        
        js_pushnumber(J, config->wifi_channel);
        js_setproperty(J, -2, "channel");
        
        js_pushnumber(J, config->wifi_power);
        js_setproperty(J, -2, "power");
    }
}

// ثبت ماژول
esp_err_t evm_module_wifi_register(js_State *J) {
    ESP_LOGI(TAG, "🔧 Registering WiFi EVM module...");
    
    js_newobject(J);

    // ثبت توابع اصلی
    js_newcfunction(J, js_wifi_init, "init", 0);
    js_setproperty(J, -2, "init");

    js_newcfunction(J, js_wifi_mode, "mode", 1);
    js_setproperty(J, -2, "mode");

    js_newcfunction(J, js_wifi_sta, "sta", 2);
    js_setproperty(J, -2, "sta");

    js_newcfunction(J, js_wifi_ap, "ap", 3);
    js_setproperty(J, -2, "ap");

    js_newcfunction(J, js_wifi_power, "power", 1);
    js_setproperty(J, -2, "power");

    js_newcfunction(J, js_wifi_status, "status", 0);
    js_setproperty(J, -2, "status");

    js_newcfunction(J, js_wifi_scan, "scan", 0);
    js_setproperty(J, -2, "scan");

    // توابع پیشرفته
    js_newcfunction(J, js_wifi_autoScan, "autoScan", 0);
    js_setproperty(J, -2, "autoScan");

    js_newcfunction(J, js_wifi_autoConnect, "autoConnect", 0);
    js_setproperty(J, -2, "autoConnect");

    js_newcfunction(J, js_wifi_isStaConnected, "isStaConnected", 0);
    js_setproperty(J, -2, "isStaConnected");

    js_newcfunction(J, js_wifi_maintain, "maintain", 0);
    js_setproperty(J, -2, "maintain");

    js_newcfunction(J, js_wifi_setAutoReconnect, "setAutoReconnect", 1);
    js_setproperty(J, -2, "setAutoReconnect");

    // توابع IP
    js_newcfunction(J, js_wifi_staIP, "staIP", 0);
    js_setproperty(J, -2, "staIP");

    js_newcfunction(J, js_wifi_apIP, "apIP", 0);
    js_setproperty(J, -2, "apIP");

    // تابع اطلاعات
    js_newcfunction(J, js_wifi_info, "info", 0);
    js_setproperty(J, -2, "info");

    js_setglobal(J, "WiFi");
    
    ESP_LOGI(TAG, "✅ WiFi module registered successfully");
    return ESP_OK;
}