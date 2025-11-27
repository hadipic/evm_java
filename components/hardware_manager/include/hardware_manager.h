#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <inttypes.h>
#include <stdbool.h>

// ============================================================================
// شامل کردن تمام فایل‌های هدر موجود
// ============================================================================

#include "lcd101.h"
#include "sd_card_driver.h"  
#include "wifi_driver.h"
#include "hardware_config.h"

// ============================================================================
// تعاریف عمومی
// ============================================================================

typedef enum {
    HARDWARE_RESULT_OK = 0,
    HARDWARE_RESULT_FAIL = -1,
    HARDWARE_RESULT_NOT_INITIALIZED = -2,
    HARDWARE_RESULT_ALREADY_INITIALIZED = -3
} hardware_result_t;

// ساختار وضعیت سخت‌افزار
typedef struct {
    bool wifi_initialized;
    bool lcd_initialized;
    bool sd_card_initialized;
    bool buttons_initialized;
    bool system_initialized;
} hardware_status_info_t;

// انواع حالت‌های قدرت
typedef enum {
    POWER_MODE_PERFORMANCE,
    POWER_MODE_BALANCED, 
    POWER_MODE_POWER_SAVE
} hardware_power_mode_t;

// ============================================================================
// توابع عمومی مدیریت سخت‌افزار
// ============================================================================

// مدیریت کلی سیستم
esp_err_t hardware_init_all(void);
esp_err_t hardware_deinit_all(void);
hardware_status_info_t hardware_get_status(void);

// مدیریت منابع و قدرت
esp_err_t hardware_suspend_all(void);
esp_err_t hardware_resume_all(void);
esp_err_t hardware_set_power_mode(hardware_power_mode_t mode);

// اطلاعات سیستم
const char* hardware_get_version(void);
uint32_t hardware_get_free_heap(void);
void hardware_log_status(void);
const char* hardware_get_error_string(esp_err_t error_code);

// ============================================================================
// توابع سخت‌افزارهای خاص
// ============================================================================

// LCD Functions
esp_err_t hardware_init_lcd(void);

// SD Card Functions  
esp_err_t hardware_init_sd_card(void);
bool hardware_is_sd_mounted(void);
esp_err_t hardware_create_apps_directory(void);

// Button Functions
esp_err_t hardware_init_buttons(void);
bool hardware_button_up_pressed(void);
bool hardware_button_down_pressed(void);
bool hardware_button_left_pressed(void);
bool hardware_button_right_pressed(void);
bool hardware_button_select_pressed(void);

// LVGL Functions
esp_err_t hardware_init_lvgl(void);

// WiFi Functions
esp_err_t hardware_init_wifi(void);
esp_err_t hardware_start_wifi(void);
bool hardware_is_wifi_ready(void);
bool hardware_wifi_is_sta_connected(void);
bool hardware_is_wifi_connected(void);

// WiFi Configuration Functions
esp_err_t hardware_wifi_set_mode(wifi_mode_t mode);
esp_err_t hardware_configure_sta(const char* ssid, const char* password);
esp_err_t hardware_wifi_set_ap_config(const char* ssid, const char* password, uint8_t channel);
esp_err_t hardware_wifi_set_power(uint8_t power);
esp_err_t hardware_wifi_scan_networks(void);
uint16_t hardware_wifi_get_scan_count(void);
esp_err_t hardware_wifi_get_scan_results(wifi_ap_record_t* ap_records, uint16_t count);
const char* hardware_wifi_get_auth_mode_string(wifi_auth_mode_t auth_mode);
esp_err_t hardware_wifi_auto_connect(void);
esp_err_t hardware_wifi_maintain_connection(void);
esp_err_t hardware_wifi_set_auto_reconnect(bool enable);
uint32_t hardware_wifi_get_scan_age(void);



// ============================================================================
// توابع یکپارچه‌سازی برای سازگاری با کدهای موجود
// ============================================================================

// توابع عمومی
bool is_sd_mounted(void);
bool wifi_is_connected(void);
bool wifi_is_sta_connected(void);

// توابع IP
const char* hardware_get_ap_ip(void);
const char* hardware_get_sta_ip(void);

// توابع پیکربندی
const wifi_app_config_t* hardware_wifi_get_config(void);

// توابع عیب‌یابی
esp_err_t hardware_run_diagnostics(void);

#endif // HARDWARE_MANAGER_H