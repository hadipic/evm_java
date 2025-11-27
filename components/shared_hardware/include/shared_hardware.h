#ifndef SHARED_HARDWARE_H
#define SHARED_HARDWARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Shared hardware API function pointers
 */
typedef struct {
    // توابع LCD
    esp_err_t (*lcd_init)(void);
    esp_err_t (*lcd_deinit)(void);
    esp_err_t (*lcd_clear)(uint16_t color);
    esp_err_t (*lcd_draw)(void* buffer, uint32_t size);
    
    // توابع GPIO
    esp_err_t (*gpio_set)(int pin, int value);
    int (*gpio_get)(int pin);
    
    // توابع زمان
    uint64_t (*get_time_us)(void);
    void (*delay_ms)(uint32_t ms);
    
    // توابع SD Card
    esp_err_t (*sd_mount)(void);
    esp_err_t (*sd_unmount)(void);
    
    // 🔥 توابع WiFi
    esp_err_t (*wifi_init)(void);
    bool (*wifi_ready)(void);
    const char* (*wifi_ap_ip)(void);
    const char* (*wifi_sta_ip)(void);
} shared_hardware_api_t;

// تعریف global shared hardware API
extern shared_hardware_api_t* g_shared_hw;

// توابع اصلی
esp_err_t shared_hardware_init(bool is_launcher);
esp_err_t shared_hardware_acquire_control(uint32_t timeout_ms);
esp_err_t shared_hardware_release_control(void);
esp_err_t shared_hardware_reclaim_control(void);
esp_err_t shared_hardware_transfer_control(void);


#ifdef __cplusplus
}
#endif

#endif // SHARED_HARDWARE_H