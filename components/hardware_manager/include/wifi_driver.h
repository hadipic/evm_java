#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H

#include "esp_wifi.h"
#include "esp_event.h"

typedef struct {
    bool wifi_ap_active;
    bool wifi_sta_active;
    char ap_ssid[32];
    char ap_password[64];
    char sta_ssid[32];
    char sta_password[64];
    uint8_t wifi_channel;
    int8_t wifi_power;
} wifi_app_config_t;

typedef struct {
    char ssid[32];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} wifi_network_info_t;

// توابع عمومی
esp_err_t wifi_driver_init(void);
esp_err_t wifi_driver_start(void);
esp_err_t wifi_driver_stop(void);
bool wifi_driver_is_connected(void);
bool wifi_driver_is_ready(void);

bool wifi_driver_is_wifi_connected(void);
bool wifi_driver_is_sta_connected(void);
uint32_t wifi_driver_get_scan_age(void);

// توابع کنترل
esp_err_t wifi_driver_configure_sta(const char* ssid, const char* password);
esp_err_t wifi_driver_disconnect_sta(void);
esp_err_t wifi_driver_set_mode(wifi_mode_t mode);
esp_err_t wifi_driver_set_power(int8_t power);

// توابع اسکن
esp_err_t wifi_driver_scan_networks(void);
uint16_t wifi_driver_get_scan_count(void);
esp_err_t wifi_driver_get_scan_results(wifi_network_info_t* results, uint16_t max_count);
const char* wifi_driver_get_auth_mode_string(wifi_auth_mode_t authmode);

// توابع اطلاعات
const char* wifi_driver_get_ap_ip(void);
const char* wifi_driver_get_sta_ip(void);
const wifi_app_config_t* wifi_driver_get_config(void);

// توابع پیشرفته
esp_err_t wifi_driver_auto_connect(void);
void wifi_driver_maintain_connection(void);
esp_err_t wifi_driver_set_auto_reconnect(bool enable);
esp_err_t wifi_driver_set_ap_config(const char* ssid, const char* password, uint8_t channel);

#endif