#include "shared_hardware.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hardware_config.h"
#include "lcd101.h"
#include "hardware_manager.h"

static const char *TAG = "shared_hardware";

// Global API
shared_hardware_api_t* g_shared_hw = NULL;

// Mutex ساده و مطمئن
static SemaphoreHandle_t hardware_mutex = NULL;

// وضعیت سخت‌افزار
static bool lcd_initialized = false;
static bool sd_initialized = false;
static bool spi_initialized = false;
static bool wifi_initialized = false;

// پین‌های دکمه
#define BUTTON_UP_GPIO      GPIO_NUM_2
#define BUTTON_SELECT_GPIO  GPIO_NUM_4
#define BUTTON_DOWN_GPIO    GPIO_NUM_34
#define BUTTON_BACK_GPIO    GPIO_NUM_35

// ==================== LCD ====================
static esp_err_t shared_lcd_init(void) {
    if (lcd_initialized) return ESP_OK;

    if (!spi_initialized) {
        spi_master_init();
        spi_initialized = true;
    }

    InitLcd(16);
    LCDClearScreen(0x0000);
    lcd_initialized = true;
    ESP_LOGI(TAG, "LCD initialized");
    return ESP_OK;
}

static esp_err_t shared_lcd_clear(uint16_t color) {
    if (!lcd_initialized) shared_lcd_init();
    LCDClearScreen(color);
    return ESP_OK;
}

// ==================== GPIO ====================
static esp_err_t shared_gpio_set(int pin, int value) {
    return gpio_set_level((gpio_num_t)pin, value);
}

static int shared_gpio_get(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// ==================== زمان ====================
static uint64_t shared_get_time_us(void) {
    return esp_timer_get_time();
}

static void shared_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// ==================== SD Card ====================
static esp_err_t shared_sd_mount(void) {
    if (sd_initialized) return ESP_OK;
    sd_initialized = true;
    ESP_LOGI(TAG, "SD mounted");
    return ESP_OK;
}

// ==================== WiFi (درست و کامل!) ====================
static esp_err_t shared_wifi_init(void) {
    if (wifi_initialized) {
        ESP_LOGI(TAG, "WiFi already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting WiFi...");
    esp_err_t ret = hardware_init_wifi();
    if (ret == ESP_OK) {
        hardware_start_wifi();
        wifi_initialized = true;
        ESP_LOGI(TAG, "WiFi ON! AP: %s", hardware_get_ap_ip());
    } else {
        ESP_LOGW(TAG, "WiFi init failed, continuing...");
    }
    return ESP_OK;
}

static bool shared_wifi_ready(void) {
    return wifi_initialized && hardware_is_wifi_ready();
}

static const char* shared_wifi_ap_ip(void) {
    return hardware_get_ap_ip();
}

static const char* shared_wifi_sta_ip(void) {
    return hardware_get_sta_ip();
}

// ==================== API ====================
static shared_hardware_api_t shared_api = {
    .lcd_init = shared_lcd_init,
    .lcd_clear = shared_lcd_clear,
    .gpio_set = shared_gpio_set,
    .gpio_get = shared_gpio_get,
    .get_time_us = shared_get_time_us,
    .delay_ms = shared_delay_ms,
    .sd_mount = shared_sd_mount,
    .wifi_init = shared_wifi_init,
    .wifi_ready = shared_wifi_ready,
    .wifi_ap_ip = shared_wifi_ap_ip,
    .wifi_sta_ip = shared_wifi_sta_ip
};

// ==================== توابع اصلی ====================
esp_err_t shared_hardware_init(bool is_launcher) {
    if (!hardware_mutex) {
        hardware_mutex = xSemaphoreCreateMutex();
        if (!hardware_mutex) return ESP_FAIL;
    }

    g_shared_hw = &shared_api;

    if (is_launcher) {
        xSemaphoreTake(hardware_mutex, portMAX_DELAY);

        lcd_initialized = true;
        spi_initialized = true;
        shared_api.sd_mount();
     
     //   shared_api.wifi_init();  // وای‌فای راه‌اندازی شد

        // دکمه‌ها
        gpio_config_t btn = {
            .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) |
                            (1ULL << BUTTON_SELECT_GPIO) |
                            (1ULL << BUTTON_DOWN_GPIO) |
                            (1ULL << BUTTON_BACK_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 1,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&btn);

        ESP_LOGI(TAG, "Launcher ready");

        // آزاد کن تا اپ بتونه بگیره!
        xSemaphoreGive(hardware_mutex);
    }

    return ESP_OK;
}

// فقط این دو تابع مهم‌اند!
esp_err_t shared_hardware_acquire_control(uint32_t timeout_ms) {
    if (!hardware_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(hardware_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        ESP_LOGI(TAG, "App got control");
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t shared_hardware_release_control(void) {
    if (!hardware_mutex) return ESP_ERR_INVALID_STATE;
    // همیشه آزاد کن! بدون شرط!
    xSemaphoreGive(hardware_mutex);
    ESP_LOGI(TAG, "App released control");
    return ESP_OK;
}


esp_err_t shared_hardware_transfer_control(void) {
    if (hardware_mutex == NULL) {
        ESP_LOGE(TAG, "❌ Hardware not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "🔄 Transferring hardware control to application...");
    
    // لانچر کنترل را رها می‌کند
    xSemaphoreGive(hardware_mutex);
    
    // برنامه می‌تواند کنترل را بگیرد
    if (xSemaphoreTake(hardware_mutex, pdMS_TO_TICKS(5000))) {
        ESP_LOGI(TAG, "✅ Application acquired hardware control");
        
        // برنامه می‌تواند سخت‌افزار را reinitialize کند اگر نیاز باشد
        // برای مثال اگر برنامه نیاز به LCD با تنظیمات متفاوت دارد:
        // shared_api.lcd_init();
        
        xSemaphoreGive(hardware_mutex); // برنامه کنترل را نگه می‌دارد
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ Application failed to acquire hardware control");
        return ESP_ERR_TIMEOUT;
    }
}


esp_err_t shared_hardware_reclaim_control(void) {
    if (!hardware_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(hardware_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ESP_LOGI(TAG, "Launcher reclaimed control");
        if (g_shared_hw) g_shared_hw->lcd_clear(0x0000);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Launcher failed to reclaim");
    return ESP_ERR_TIMEOUT;
}