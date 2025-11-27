#include "sd_card_driver.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// برای SD Card
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "sd_card_driver";

// تعاریف پین‌ها
#define PIN_NUM_MISO SD_PIN_MISO
#define PIN_NUM_MOSI SD_PIN_MOSI
#define PIN_NUM_CLK  SD_PIN_CLK
#define PIN_NUM_CS   SD_PIN_CS

// متغیرهای Global
static sdmmc_card_t* g_sd_card = NULL;
static bool g_sd_mounted = false;
static const char* mount_point = "/sdcard";

esp_err_t sd_card_driver_init(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SPI peripheral (SPI3_HOST)");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };
    
    // استفاده از SPI3_HOST به جای VSPI_HOST در IDF 5.x
    host.slot = SPI3_HOST;
    host.max_freq_khz = 10000;
    
    // راه‌اندازی SPI bus برای SD Card (SPI3_HOST)
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize bus: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.gpio_cd = GPIO_NUM_NC;
    slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.host_id = host.slot;
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &g_sd_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        }
        return ret;
    }
    
    g_sd_mounted = true;
    ESP_LOGI(TAG, "SD Card mounted successfully to %s", mount_point);
    ESP_LOGI(TAG, "SD Card is ready for use");
    
    return ESP_OK;
}

// سایر توابع بدون تغییر...

bool sd_card_driver_is_mounted(void) {
    return g_sd_mounted;
}

sdmmc_card_t* sd_card_driver_get_card(void) {
    return g_sd_card;
}

esp_err_t sd_card_driver_unmount(void) {
    if (!g_sd_mounted) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, g_sd_card);
    if (ret == ESP_OK) {
        g_sd_mounted = false;
        g_sd_card = NULL;
        ESP_LOGI(TAG, "SD Card unmounted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

const char* sd_card_driver_get_mount_point(void) {
    return mount_point;
}

// تابع utility برای ایجاد دایرکتوری برنامه‌ها
esp_err_t sd_card_create_apps_directory(void) {
    if (!g_sd_mounted) {
        return ESP_FAIL;
    }
    
    struct stat st;
    char apps_path[64];
    snprintf(apps_path, sizeof(apps_path), "%s/apps", mount_point);
    
    // چک کن آیا دایرکتوری /sdcard/apps وجود دارد
    if (stat(apps_path, &st) != 0) {
        ESP_LOGI(TAG, "Creating %s directory...", apps_path);
        
        // ایجاد دایرکتوری
        if (mkdir(apps_path, 0755) == 0) {
            ESP_LOGI(TAG, "Created %s directory successfully", apps_path);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to create %s directory", apps_path);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGI(TAG, "%s directory already exists", apps_path);
        return ESP_OK;
    }
}

