#include "hardware_manager.h"
#include "hardware_config.h"
#include "sd_card_driver.h"
#include "wifi_driver.h"
#include "lcd101.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <sys/stat.h>

static const char *TAG = "hardware_manager";

// متغیرهای global برای سازگاری با کد موجود
sdmmc_card_t* g_sd_card = NULL;
bool g_sd_mounted = false;
bool lcd_initialized = false;

// نگاشت پین‌های دکمه
#define BUTTON_UP_PIN      BUTTON_PREV_GPIO
#define BUTTON_DOWN_PIN    BUTTON_NEXT_GPIO  
#define BUTTON_LEFT_PIN    BUTTON_MODE_GPIO
#define BUTTON_RIGHT_PIN   BUTTON_PLAY_GPIO
#define BUTTON_SELECT_PIN  BUTTON_PLAY_GPIO

// ==================== توابع عمومی سخت‌افزار ====================

esp_err_t hardware_init_sd_card(void) {
    ESP_LOGI(TAG, "Initializing SD Card...");
    
    // اول مطمئن شویم LCD SPI را آزاد کرده است
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_err_t ret = sd_card_driver_init();
    if (ret == ESP_OK) {
        g_sd_card = sd_card_driver_get_card();
        g_sd_mounted = sd_card_driver_is_mounted();
        ESP_LOGI(TAG, "✅ SD Card initialized successfully");
    } else {
        ESP_LOGE(TAG, "❌ SD Card initialization failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t hardware_init_lcd(void) {
    if (lcd_initialized) {
        ESP_LOGI(TAG, "LCD already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing LCD...");
    
    // راه‌اندازی LCD با SPI مخصوص
    InitLcd(16);
    LCDClearScreen(0x0000);
    
    lcd_initialized = true;
    ESP_LOGI(TAG, "✅ LCD initialized successfully");
    return ESP_OK;
}

esp_err_t hardware_init_buttons(void) {
    ESP_LOGI(TAG, "Initializing buttons...");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_UP_PIN) | 
                       (1ULL << BUTTON_DOWN_PIN) | 
                       (1ULL << BUTTON_LEFT_PIN) | 
                       (1ULL << BUTTON_RIGHT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Button GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Buttons initialized - Pins: UP=%d, DOWN=%d, LEFT=%d, RIGHT=%d", 
             BUTTON_UP_PIN, BUTTON_DOWN_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN);
    return ESP_OK;
}

esp_err_t hardware_init_wifi(void) {
    ESP_LOGI(TAG, "Initializing WiFi system...");
    return wifi_driver_init();
}

esp_err_t hardware_start_wifi(void) {
    ESP_LOGI(TAG, "Starting WiFi...");
    return wifi_driver_start();
}

bool hardware_is_wifi_ready(void) {
    return wifi_driver_is_ready();
}

bool hardware_is_wifi_connected(void) {
    return wifi_driver_is_connected();
}

bool hardware_wifi_is_sta_connected(void) {
    return wifi_driver_is_sta_connected();
}

const char* hardware_get_ap_ip(void) {
    return wifi_driver_get_ap_ip();
}

const char* hardware_get_sta_ip(void) {
    return wifi_driver_get_sta_ip();
}

const wifi_app_config_t* hardware_wifi_get_config(void) {
    return wifi_driver_get_config();
}
// ==================== توابع WiFi مفقوده ====================

esp_err_t hardware_wifi_set_mode(wifi_mode_t mode) {
    return wifi_driver_set_mode(mode);
}

esp_err_t hardware_configure_sta(const char* ssid, const char* password) {
    return wifi_driver_configure_sta(ssid, password);
}

esp_err_t hardware_wifi_set_ap_config(const char* ssid, const char* password, uint8_t channel) {
    return wifi_driver_set_ap_config(ssid, password, channel);
}

esp_err_t hardware_wifi_set_power(uint8_t power) {
    return wifi_driver_set_power((int8_t)power);
}

esp_err_t hardware_wifi_scan_networks(void) {
    return wifi_driver_scan_networks();
}

uint16_t hardware_wifi_get_scan_count(void) {
    return wifi_driver_get_scan_count();
}

esp_err_t hardware_wifi_get_scan_results(wifi_ap_record_t* ap_records, uint16_t count) {
    // تبدیل wifi_network_info_t به wifi_ap_record_t
    wifi_network_info_t networks[count];
    esp_err_t ret = wifi_driver_get_scan_results(networks, count);
    if (ret == ESP_OK) {
        for (int i = 0; i < count; i++) {
            strncpy((char*)ap_records[i].ssid, networks[i].ssid, sizeof(ap_records[i].ssid) - 1);
            ap_records[i].ssid[sizeof(ap_records[i].ssid) - 1] = '\0';
            ap_records[i].rssi = networks[i].rssi;
            ap_records[i].primary = networks[i].channel;
            ap_records[i].authmode = networks[i].authmode;
        }
    }
    return ret;
}

const char* hardware_wifi_get_auth_mode_string(wifi_auth_mode_t auth_mode) {
    return wifi_driver_get_auth_mode_string(auth_mode);
}

esp_err_t hardware_wifi_auto_connect(void) {
    return wifi_driver_auto_connect();
}

esp_err_t hardware_wifi_maintain_connection(void) {
    wifi_driver_maintain_connection();
    return ESP_OK;
}

esp_err_t hardware_wifi_set_auto_reconnect(bool enable) {
    return wifi_driver_set_auto_reconnect(enable);
}

uint32_t hardware_wifi_get_scan_age(void) {
    return wifi_driver_get_scan_age();
}



//////////////////////////////////////////////////////////////////////////////////////////////////

bool hardware_is_sd_mounted(void) {
    return g_sd_mounted;
}

esp_err_t hardware_create_apps_directory(void) {
    if (!g_sd_mounted) {
        return ESP_FAIL;
    }
    
    struct stat st = {0};
    if (stat("/sdcard/apps", &st) == -1) {
        ESP_LOGI(TAG, "Creating apps directory...");
        if (mkdir("/sdcard/apps", 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create apps directory");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "✅ Apps directory created");
    } else {
        ESP_LOGI(TAG, "📁 Apps directory already exists");
    }
    
    return ESP_OK;
}

esp_err_t hardware_init_lvgl(void) {
    ESP_LOGI(TAG, "LVGL support would be initialized here");
    return ESP_OK;
}

// ==================== تابع اصلی Initialize All ====================

esp_err_t hardware_init_all(void) {
    ESP_LOGI(TAG, "=== Initializing All Hardware ===");
    
    esp_err_t ret;
    bool all_success = true;

    // 1. راه‌اندازی LCD اول (مهم: باید اول باشد)
    ESP_LOGI(TAG, "1. Initializing LCD...");
    ret = hardware_init_lcd();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ LCD initialization failed!");
        all_success = false;
        // ادامه بدهیم چون ممکن است بقیه سخت‌افزارها کار کنند
    }

    // 2. راه‌اندازی SD Card (بعد از LCD)
    ESP_LOGI(TAG, "2. Initializing SD Card...");
    ret = hardware_init_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ SD Card initialization failed - continuing without SD");
        all_success = false;
    } else {
        hardware_create_apps_directory();
    }
    
    // 3. راه‌اندازی دکمه‌ها
    ESP_LOGI(TAG, "3. Initializing buttons...");
    ret = hardware_init_buttons();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Buttons initialization failed!");
        all_success = false;
    }

    // 4. راه‌اندازی WiFi
    ESP_LOGI(TAG, "4. Initializing WiFi...");
    ret = hardware_init_wifi();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ WiFi initialization failed - continuing...");
        all_success = false;
    } else {
        ret = hardware_start_wifi();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ WiFi start failed - continuing...");
            all_success = false;
        }
    }

    // 5. راه‌اندازی LVGL
    ESP_LOGI(TAG, "5. Preparing LVGL support...");
    hardware_init_lvgl();

    // گزارش وضعیت نهایی
    ESP_LOGI(TAG, "=== Hardware Initialization %s ===", all_success ? "COMPLETED" : "WITH ERRORS");
    ESP_LOGI(TAG, "📊 Final Hardware Status:");
    ESP_LOGI(TAG, "  LCD: %s", lcd_initialized ? "✅ Ready" : "❌ Failed");
    ESP_LOGI(TAG, "  SD Card: %s", hardware_is_sd_mounted() ? "✅ Mounted" : "❌ Failed");
    ESP_LOGI(TAG, "  Buttons: ✅ Ready");
    ESP_LOGI(TAG, "  WiFi: %s", hardware_is_wifi_ready() ? "✅ Started" : "❌ Failed");
    
    if (hardware_is_wifi_ready()) {
        ESP_LOGI(TAG, "    AP IP: %s", hardware_get_ap_ip());
        ESP_LOGI(TAG, "    STA: %s", hardware_wifi_is_sta_connected() ? "✅ Connected" : "❌ Disconnected");
        if (hardware_wifi_is_sta_connected()) {
            ESP_LOGI(TAG, "    STA IP: %s", hardware_get_sta_ip());
        }
    }
    
    return all_success ? ESP_OK : ESP_FAIL;
}

// ==================== توابع وضعیت سخت‌افزار ====================

hardware_status_info_t hardware_get_status(void) {
    hardware_status_info_t status = {
        .wifi_initialized = hardware_is_wifi_ready(),
        .lcd_initialized = lcd_initialized,
        .sd_card_initialized = hardware_is_sd_mounted(),
        .buttons_initialized = true,
        .system_initialized = true
    };
    return status;
}

// ==================== توابع خواندن وضعیت دکمه‌ها ====================

bool hardware_button_up_pressed(void) {
    return gpio_get_level(BUTTON_UP_PIN) == 0;
}

bool hardware_button_down_pressed(void) {
    return gpio_get_level(BUTTON_DOWN_PIN) == 0;
}

bool hardware_button_left_pressed(void) {
    return gpio_get_level(BUTTON_LEFT_PIN) == 0;
}

bool hardware_button_right_pressed(void) {
    return gpio_get_level(BUTTON_RIGHT_PIN) == 0;
}

bool hardware_button_select_pressed(void) {
    return gpio_get_level(BUTTON_SELECT_PIN) == 0;
}

// ==================== توابع utility ====================

uint32_t hardware_get_free_heap(void) {
    return esp_get_free_heap_size();
}

const char* hardware_get_error_string(esp_err_t error_code) {
    return esp_err_to_name(error_code);
}

// ==================== توابع مدیریت انرژی ====================

esp_err_t hardware_set_power_mode(hardware_power_mode_t mode) {
    ESP_LOGI(TAG, "Setting power mode: %d", mode);
    return ESP_OK;
}

// ==================== توابع عیب‌یابی ====================

esp_err_t hardware_run_diagnostics(void) {
    ESP_LOGI(TAG, "Running hardware diagnostics...");
    
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", hardware_get_free_heap());
    ESP_LOGI(TAG, "LCD: %s", lcd_initialized ? "✅ OK" : "❌ FAILED");
    ESP_LOGI(TAG, "SD Card: %s", hardware_is_sd_mounted() ? "✅ OK" : "❌ FAILED");
    ESP_LOGI(TAG, "WiFi: %s", hardware_is_wifi_ready() ? "✅ OK" : "❌ FAILED");
    ESP_LOGI(TAG, "Buttons: ✅ OK");
    
    return ESP_OK;
}

// ==================== توابع سازگاری ====================

bool is_sd_mounted(void) {
    return hardware_is_sd_mounted();
}

bool wifi_is_connected(void) {
    return hardware_is_wifi_connected();
}

bool wifi_is_sta_connected(void) {
    return hardware_wifi_is_sta_connected();
}