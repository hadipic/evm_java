#include "wifi_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "lwip/dns.h"

#include <string.h>

static const char *TAG = "wifi_driver";

// متغیرهای WiFi - استفاده از wifi_app_config_t به جای wifi_config_t
static wifi_app_config_t current_wifi_config;
static bool wifi_initialized = false;
static bool wifi_started = false;
static bool sta_connected = false;
static bool ap_started = false;
static esp_netif_t* ap_netif = NULL;
static esp_netif_t* sta_netif = NULL;

// متغیرهای مدیریت WiFi
static wifi_ap_record_t *g_scan_results = NULL;
static uint16_t g_scan_count = 0;
static uint32_t g_last_scan_time = 0;
static uint32_t g_last_connect_attempt = 0;
static bool g_auto_reconnect = true;

// کانفیگ پیش‌فرض WiFi - استفاده از wifi_app_config_t
static const wifi_app_config_t default_wifi_config = {
    .wifi_ap_active = true,
    .wifi_sta_active = false,
    .ap_ssid = "esp32",
    .ap_password = "12345678",
    .sta_ssid = "Shop-electronic",
    .sta_password ="Bashniji@#1401", 
    .wifi_channel = 6,
    .wifi_power = 20
};



// ==================== Event Handler ====================

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ap_started = true;
                ESP_LOGI(TAG, "✅ WiFi AP Started");
                break;

            case WIFI_EVENT_AP_STOP:
                ap_started = false;
                ESP_LOGI(TAG, "⏹️ WiFi AP Stopped");
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGI(TAG, "Device connected to AP | MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID:%d",
               event->mac[0], event->mac[1], event->mac[2], 
               event->mac[3], event->mac[4], event->mac[5], event->aid);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGW(TAG, "Device disconnected from AP | MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID:%d",
                event->mac[0], event->mac[1], event->mac[2],
               event->mac[3], event->mac[4], event->mac[5], event->aid);
                break;
            }

            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "🔌 WiFi STA Started, connecting to AP...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "📡 STA Connected to AP, waiting for IP...");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "🚫 STA Disconnected, retrying...");
                sta_connected = false;
                if (g_auto_reconnect) {
                    esp_wifi_connect();  // تلاش مجدد
                }
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "✅ STA Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                sta_connected = true;
                break;
            }

            case IP_EVENT_AP_STAIPASSIGNED:
                ESP_LOGI(TAG, "📶 IP assigned to connected client");
                break;

            default:
                break;
        }
    }
}

// ==================== توابع اصلی WiFi ====================

esp_err_t wifi_driver_init(void) {
    if (wifi_initialized) return ESP_OK;

    ESP_LOGI(TAG, "🔄 Initializing WiFi (Dual Mode - AP + STA)...");

    // مقداردهی اولیه NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "🔄 Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // مقداردهی اولیه شبکه و event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ساخت netif ها
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif  = esp_netif_create_default_wifi_ap();

    // پیکربندی اولیه WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 4;
    cfg.dynamic_rx_buf_num = 2;
    cfg.static_tx_buf_num = 4;
    cfg.cache_tx_buf_num  = 2;
    cfg.nvs_enable = 1;
    cfg.wifi_task_core_id = 1;

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // ثبت Event handler ها
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // کپی کانفیگ پیش‌فرض
    memcpy(&current_wifi_config, &default_wifi_config, sizeof(wifi_app_config_t));
    
    wifi_initialized = true;
    ESP_LOGI(TAG, "✅ WiFi Dual Mode Initialized");
    return ESP_OK;
}

esp_err_t wifi_driver_start(void) {
    if (!wifi_initialized) {
        ESP_LOGE(TAG, "❌ WiFi not initialized");
        return ESP_FAIL;
    }

    if (wifi_started) {
        ESP_LOGW(TAG, "⚠️ WiFi already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "📡 Starting WiFi (Dual Mode: AP + STA)...");

    // فعال سازی Dual Mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // ---------- تنظیمات AP ----------
    wifi_config_t ap_config = { 0 };
    strcpy((char*)ap_config.ap.ssid, current_wifi_config.ap_ssid);
    strcpy((char*)ap_config.ap.password, current_wifi_config.ap_password);
    ap_config.ap.ssid_len = strlen(current_wifi_config.ap_ssid);
    ap_config.ap.channel = current_wifi_config.wifi_channel;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 200;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // ---------- تنظیمات STA ----------
    wifi_config_t sta_config = { 0 };
    strcpy((char*)sta_config.sta.ssid, current_wifi_config.sta_ssid);
    strcpy((char*)sta_config.sta.password, current_wifi_config.sta_password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // شروع WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_started = true;

    ESP_LOGI(TAG, "✅ WiFi Dual Mode Started (AP + STA Active)");

    return ESP_OK;
}

esp_err_t wifi_driver_stop(void) {
    if (!wifi_initialized || !wifi_started) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🛑 Stopping WiFi...");
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        wifi_started = false;
        sta_connected = false;
        ap_started = false;
        ESP_LOGI(TAG, "✅ WiFi stopped");
    } else {
        ESP_LOGE(TAG, "❌ Failed to stop WiFi: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// ==================== توابع وضعیت ====================

bool wifi_driver_is_connected(void) {
    return sta_connected || ap_started;
}

bool wifi_driver_is_ready(void) {
    return wifi_initialized && wifi_started;
}

const char* wifi_driver_get_ap_ip(void) {
    if (!ap_netif) return "192.168.4.1";
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        static char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        return ip_str;
    }
    return "192.168.4.1";
}

const char* wifi_driver_get_sta_ip(void) {
    if (!sta_netif || !sta_connected) return "0.0.0.0";
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
        static char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        return ip_str;
    }
    return "0.0.0.0";
}

// ==================== توابع کنترل STA ====================

esp_err_t wifi_driver_configure_sta(const char* ssid, const char* password) {
    if (!wifi_driver_is_ready()) {
        return ESP_FAIL;
    }

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';
    sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret == ESP_OK) {
        // تغییر mode به APSTA و اتصال
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_wifi_connect();
        ESP_LOGI(TAG, "🔌 STA Configured: %s", ssid);
        
        // به روزرسانی config
        current_wifi_config.wifi_sta_active = true;
        strncpy(current_wifi_config.sta_ssid, ssid, sizeof(current_wifi_config.sta_ssid) - 1);
        strncpy(current_wifi_config.sta_password, password, sizeof(current_wifi_config.sta_password) - 1);
    } else {
        ESP_LOGE(TAG, "STA config failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t wifi_driver_disconnect_sta(void) {
    if (!wifi_initialized || !wifi_started) {
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        sta_connected = false;
        ESP_LOGI(TAG, "🔌 STA Disconnected");
    }
    return ret;
}

// ==================== توابع اسکن شبکه ====================

esp_err_t wifi_driver_scan_networks(void) {
    if (!wifi_driver_is_ready()) {
        return ESP_FAIL;
    }

    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    wifi_mode_t original_mode = current_mode;
    
    ESP_LOGI(TAG, "🔍 Starting network scan (current mode: %d)...", current_mode);
    
    // در حالت AP نمی‌توان اسکن کرد - باید به حالت STA تغییر کنیم
    if (current_mode == WIFI_MODE_AP) {
        ESP_LOGI(TAG, "🔄 Switching to STA mode for scanning...");
        esp_wifi_set_mode(WIFI_MODE_STA);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // آزاد کردن نتایج قبلی
    if (g_scan_results) {
        free(g_scan_results);
        g_scan_results = NULL;
    }
    
    // اسکن با تنظیمات بهتر
    wifi_scan_config_t cfg = { 
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 300, .max = 500 } }
    };
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_err_t ret = esp_wifi_scan_start(&cfg, true);
    ESP_LOGI(TAG, "Scan completed with result: %s", esp_err_to_name(ret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Scan failed: %s", esp_err_to_name(ret));
        // بازگشت به حالت اصلی حتی در صورت خطا
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ret;
    }

    // دریافت تعداد شبکه‌ها
    ret = esp_wifi_scan_get_ap_num(&g_scan_count);
    if (ret != ESP_OK || g_scan_count == 0) {
        ESP_LOGW(TAG, "📡 No networks found");
        g_scan_count = 0;
        // بازگشت به حالت اصلی
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    g_scan_count = (g_scan_count > 16) ? 16 : g_scan_count;
    g_scan_results = calloc(g_scan_count, sizeof(wifi_ap_record_t));
    
    if (!g_scan_results) {
        ESP_LOGE(TAG, "❌ Memory allocation failed");
        g_scan_count = 0;
        // بازگشت به حالت اصلی
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_ERR_NO_MEM;
    }
    
    ret = esp_wifi_scan_get_ap_records(&g_scan_count, g_scan_results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to get scan records");
        free(g_scan_results);
        g_scan_results = NULL;
        g_scan_count = 0;
        // بازگشت به حالت اصلی
        if (original_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ret;
    }
    
    // بازگشت به حالت اصلی
    if (original_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    g_last_scan_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "✅ Scan completed: %d networks", g_scan_count);
    
    return ESP_OK;
}

uint16_t wifi_driver_get_scan_count(void) {
    return g_scan_count;
}

esp_err_t wifi_driver_get_scan_results(wifi_network_info_t* results, uint16_t max_count) {
    if (!g_scan_results || g_scan_count == 0 || !results) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint16_t count = (g_scan_count < max_count) ? g_scan_count : max_count;
    
    for (int i = 0; i < count; i++) {
        strncpy(results[i].ssid, (char*)g_scan_results[i].ssid, sizeof(results[i].ssid) - 1);
        results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
        results[i].rssi = g_scan_results[i].rssi;
        results[i].channel = g_scan_results[i].primary;
        results[i].authmode = g_scan_results[i].authmode;
    }
    
    return ESP_OK;
}

const char* wifi_driver_get_auth_mode_string(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "UNKNOWN";
    }
}

// ==================== توابع کنترل پیشرفته ====================

esp_err_t wifi_driver_auto_connect(void) {
    if (!wifi_driver_is_ready() || !g_auto_reconnect) {
        return ESP_FAIL;
    }
    
    if (sta_connected) {
        return ESP_OK;
    }
    
    uint32_t now = xTaskGetTickCount();
    
    // جلوگیری از تلاش‌های مکرر
    if (now - g_last_connect_attempt < pdMS_TO_TICKS(10000)) {
        return ESP_ERR_TIMEOUT;
    }
    
    g_last_connect_attempt = now;
    
    // اسکن اگر داده قدیمی است
    if (now - g_last_scan_time > pdMS_TO_TICKS(30000)) {
        wifi_driver_scan_networks();
    }
    
    // اتصال اگر SSID تنظیم شده
    if (strlen(current_wifi_config.sta_ssid) > 0) {
        ESP_LOGI(TAG, "🔌 Auto-connecting to: %s", current_wifi_config.sta_ssid);
        return wifi_driver_configure_sta(current_wifi_config.sta_ssid, current_wifi_config.sta_password);
    }
    
    return ESP_ERR_NOT_FOUND;
}

void wifi_driver_maintain_connection(void) {
    static uint32_t last_maintenance = 0;
    uint32_t now = xTaskGetTickCount();
    
    // هر ۱۰ ثانیه یکبار چک کن
    if (now - last_maintenance < pdMS_TO_TICKS(10000)) {
        return;
    }
    
    last_maintenance = now;
    
    // تلاش برای اتصال مجدد
    if (!sta_connected && g_auto_reconnect) {
        ESP_LOGI(TAG, "🔄 STA disconnected, attempting auto-reconnect...");
        wifi_driver_auto_connect();
    }
    
    // اسکن دوره‌ای هر ۲ دقیقه
    static uint32_t last_auto_scan = 0;
    if (now - last_auto_scan > pdMS_TO_TICKS(120000)) {
        ESP_LOGI(TAG, "🔍 Periodic network scan...");
        wifi_driver_scan_networks();
        last_auto_scan = now;
    }
}

esp_err_t wifi_driver_set_auto_reconnect(bool enable) {
    g_auto_reconnect = enable;
    ESP_LOGI(TAG, "🔄 Auto-reconnect %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t wifi_driver_set_mode(wifi_mode_t mode) {
    if (!wifi_driver_is_ready()) {
        return ESP_FAIL;
    }
    return esp_wifi_set_mode(mode);
}

esp_err_t wifi_driver_set_ap_config(const char* ssid, const char* password, uint8_t channel) {
    if (!wifi_driver_is_ready()) {
        return ESP_FAIL;
    }
    
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, password, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = channel;
    ap_config.ap.authmode = strlen(password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret == ESP_OK) {
        strncpy(current_wifi_config.ap_ssid, ssid, sizeof(current_wifi_config.ap_ssid) - 1);
        strncpy(current_wifi_config.ap_password, password, sizeof(current_wifi_config.ap_password) - 1);
        current_wifi_config.wifi_channel = channel;
        ESP_LOGI(TAG, "✅ AP configured: %s", ssid);
    }
    
    return ret;
}

esp_err_t wifi_driver_set_power(int8_t power) {
    if (!wifi_driver_is_ready()) {
        return ESP_FAIL;
    }
    return esp_wifi_set_max_tx_power(power);
}

// ==================== توابع اطلاعات ====================

const wifi_app_config_t* wifi_driver_get_config(void) {
    return &current_wifi_config;
}

uint32_t wifi_driver_get_scan_age(void) {
    return xTaskGetTickCount() - g_last_scan_time;
}

bool wifi_driver_is_sta_connected(void) {
    return sta_connected;
}

// 🔥 اضافه کردن توابع مفقوده
bool wifi_driver_is_wifi_connected(void) {
    return wifi_driver_is_connected();
}


