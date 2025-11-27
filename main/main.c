#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include <inttypes.h>  // اضافه کردن این include برای PRIu32

// هدرهای صحیح برای IDF 5.x
#include "esp_private/esp_clk.h"
#include "esp_psram.h"  // استفاده از esp_psram به جای esp_spiram
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_netif.h"

// کامپوننت‌های اصلی
#include "app_manager.h"
#include "hardware_manager.h"
#include "shared_hardware.h"
#include "evm_loader.h"

// ماژول‌های جدید EVM
#include "evm_module.h"
#include "evm_module_gpio.h"
#include "evm_module_timer.h"
#include "evm_module_fs.h"
#include "evm_module_console.h"
#include "evm_module_process.h"
#include "evm_module_lvgl.h"
#include "evm_module_wifi.h"

#include <sys/stat.h>
#include <dirent.h>

// سرویس‌های شبکه - اضافه شده
#include "mqtt_broker.h"
#include "mqtt_client.h" 
#include "ftp_server.h"
#include "http_server.h"

static const char *TAG = "evm_launcher";

// توابع callback و مدیریت MQTT - قبل از app_main
static void mqtt_message_callback(const char *client_id, const char *topic, 
                                const char *message, int qos) {
    ESP_LOGI("MQTT_TEST", "📨 Message from %s: %s -> %s", client_id, topic, message);
}

static void mqtt_client_callback(const char *client_id, bool connected) {
    if (connected) {
        ESP_LOGI("MQTT_TEST", "🎉 Client connected: %s", client_id);
    } else {
        ESP_LOGI("MQTT_TEST", "🔌 Client disconnected: %s", client_id);
    }
}

static int mqtt_publish_counter = 0;

static void publish_mqtt_status(void) {
    if (mqtt_broker_is_running()) {
        char message[128];
        // 🔥 اصلاح فرمت برای uint32_t
        snprintf(message, sizeof(message), 
                "{\"counter\":%d,\"free_heap\":%" PRIu32 ",\"clients\":%d,\"evm_running\":%s}", 
                mqtt_publish_counter, 
                esp_get_free_heap_size(), 
                mqtt_broker_get_client_count(),
                evm_is_app_running() ? "true" : "false");
        
        mqtt_broker_publish("evm/status", message, 0, false);
        ESP_LOGI("MQTT_TEST", "📤 MQTT Published: %s", message);
    }
}

static esp_err_t initialize_mqtt_broker(void) {
    ESP_LOGI("MQTT_TEST", "🌐 Initializing MQTT Broker...");
    
    mqtt_broker_config_t broker_config = {
        .port = 1883,
        .max_clients = 10,
        .enable_authentication = false,
        .username = "",
        .password = ""
    };
    
    // مقداردهی اولیه MQTT Broker
    if (mqtt_broker_init(&broker_config) != ESP_OK) {
        ESP_LOGE("MQTT_TEST", "❌ Failed to initialize MQTT broker");
        return ESP_FAIL;
    }
    
    // تنظیم callbackها
    mqtt_broker_set_message_callback(mqtt_message_callback);
    mqtt_broker_set_client_callback(mqtt_client_callback);
    
    // شروع MQTT Broker
    if (mqtt_broker_start() != ESP_OK) {
        ESP_LOGE("MQTT_TEST", "❌ Failed to start MQTT broker");
        return ESP_FAIL;
    }
    
    ESP_LOGI("MQTT_TEST", "✅ MQTT Broker started on port %d", mqtt_broker_get_port());
    ESP_LOGI("MQTT_TEST", "📡 Connect using: mosquitto_sub -h YOUR_ESP_IP -t evm/status");
    
    return ESP_OK;
}

// تابع ساده تست MQTT - همه چیز در یک تابع
static void simple_mqtt_test(void) {
    ESP_LOGI("MQTT_TEST", "🔧 Starting Simple MQTT Test...");
    
    // ۱. راه‌اندازی MQTT Broker
    ESP_LOGI("MQTT_TEST", "1. Starting MQTT Broker...");
    mqtt_broker_config_t broker_config = {
        .port = 1883,
        .max_clients = 5,
        .enable_authentication = false
    };
    
    if (mqtt_broker_init(&broker_config) == ESP_OK && 
        mqtt_broker_start() == ESP_OK) {
        ESP_LOGI("MQTT_TEST", "✅ Broker started on port %d", mqtt_broker_get_port());
    } else {
        ESP_LOGE("MQTT_TEST", "❌ Broker failed");
        return;
    }
    
    // ۲. راه‌اندازی MQTT Client
    ESP_LOGI("MQTT_TEST", "2. Starting MQTT Client...");
    mqtt_client_config_t client_config = {
        .broker_url = "mqtt://192.168.1.61:1883",
        .client_id = "test_client",
        .clean_session = true
    };
    
    if (mqtt_client_init(&client_config) == ESP_OK && 
        mqtt_client_start() == ESP_OK) {
        ESP_LOGI("MQTT_TEST", "✅ Client started");
    } else {
        ESP_LOGE("MQTT_TEST", "❌ Client failed");
        return;
    }
    
    // ۳. صبر برای اتصال
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // ۴. Subscribe به topics
    ESP_LOGI("MQTT_TEST", "3. Subscribing to topics...");
    mqtt_client_subscribe("test", 0);
    mqtt_client_subscribe("test/#", 0);
    mqtt_client_subscribe("#", 0);
    
    // ۵. تست انتشار پیام
    ESP_LOGI("MQTT_TEST", "4. Starting message test...");
    int counter = 0;
    
    while (1) {
        if (mqtt_client_is_connected() && mqtt_broker_is_running()) {
            char message[64];
            
            // انتشار از Client به Broker
            snprintf(message, sizeof(message), "Hello from client %d", counter);
            mqtt_client_publish("test", message, 0, false);
            ESP_LOGI("MQTT_TEST", "📤 Client -> Broker: %s", message);
            
            // انتشار از Broker به همه
            snprintf(message, sizeof(message), "Hello from broker %d", counter);
            mqtt_broker_publish("broker/test", message, 0, false);
            ESP_LOGI("MQTT_TEST", "📤 Broker -> All: %s", message);
            
            counter++;
        } else {
            ESP_LOGW("MQTT_TEST", "⚠️ Connection issue");
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // هر ۵ ثانیه
    }
}

// تابع برای گرفتن IP دستگاه
static void get_ip_address(char *ip_buffer, size_t buffer_size) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(ip_buffer, "192.168.4.1", buffer_size);
    }
}

void start_ftp_service(void) {
    ftp_server_config_t ftp_config = {
        .port = 21,
        .root_dir = "/sdcard",
        .username = "esp32",
        .password = "123456"
    };
    
    ESP_LOGI("MAIN", "Starting FTP server...");
    
    if (ftp_server_init(&ftp_config) == ESP_OK) {
        ftp_server_start_task();
        ESP_LOGI("MAIN", "FTP server started successfully");
        
        // نمایش اطلاعات اتصال
        char local_ip[16];
        get_ip_address(local_ip, sizeof(local_ip));
        ESP_LOGI("MAIN", "FTP Server Ready! Connect to: ftp://%s:%d", local_ip, ftp_config.port);
        ESP_LOGI("MAIN", "Username: %s, Password: %s", ftp_config.username, ftp_config.password);
    } else {
        ESP_LOGE("MAIN", "Failed to initialize FTP server");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== 🚀 ESP32 EVM Launcher ===");
    ESP_LOGI(TAG, "🏗️  Architecture: PRO CPU (Launcher) + APP CPU (EVM Apps)");
    ESP_LOGI(TAG, "📚 Supported: JavaScript, QML, EVM Bytecode");

    // 1. راه‌اندازی NVS
    ESP_LOGI(TAG, "0. Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS initialized");

    // 2. راه‌اندازی PSRAM - استفاده از API جدید IDF 5.x
    ESP_LOGI(TAG, "1. Initializing PSRAM...");
    
    // 🔥 استفاده از esp_psram به جای esp_spiram
    if (esp_psram_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ PSRAM initialization failed!");
    } else {
        ESP_LOGI(TAG, "✅ PSRAM initialized");
        
        // تست تخصیص حافظه PSRAM
        void* test_ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
        if (test_ptr) {
            ESP_LOGI(TAG, "✅ PSRAM test allocation successful");
            free(test_ptr);
        } else {
            ESP_LOGE(TAG, "❌ PSRAM test allocation failed!");
        }
    }

   // 2. ابتدا واچداگ را راه‌اندازی و تسک اصلی را اضافه کن
  ESP_LOGI(TAG, "🐕 Initializing Watchdog...");
    
    // غیرفعال کردن اولیه برای اطمینان
    esp_task_wdt_deinit();
    
    // // راه‌اندازی با تنظیمات
    // esp_task_wdt_config_t wdt_config = {
    //     .timeout_ms = 5000,           // تایم‌اوت 5 ثانیه
    //     .idle_core_mask = 0,          // نظارت روی همه هسته‌ها
    //     .trigger_panic = true         // ریست سیستم در صورت تایم‌اوت
    // };
    
    // esp_err_t wdt_ret = esp_task_wdt_init(&wdt_config);
    // if (wdt_ret != ESP_OK) {
    //     ESP_LOGW(TAG, "⚠️ Watchdog init failed: %s", esp_err_to_name(wdt_ret));
    // } else {
    //     ESP_LOGI(TAG, "✅ Watchdog initialized");
    // }

    // // اضافه کردن تسک اصلی به واچداگ
    // wdt_ret = esp_task_wdt_add(NULL);
    // if (wdt_ret != ESP_OK && wdt_ret != ESP_ERR_INVALID_STATE) {
    //     ESP_LOGW(TAG, "⚠️ Failed to add main task to WDT: %s", esp_err_to_name(wdt_ret));
    // } else {
    //     ESP_LOGI(TAG, "✅ Main task added to watchdog");
    // }


    // 4. راه‌اندازی WiFi System
    ESP_LOGI(TAG, "۵. Initializing WiFi...");
    ret = hardware_init_wifi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi initialization failed!");
        // ادامه بده حتی اگر WiFi fail شد
    } else {
        ret = hardware_start_wifi();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ WiFi start failed - continuing...");
        }
    }

    // ==================== مرحله ۱: راه‌اندازی سخت‌افزار (بدون WiFi) ====================
    ESP_LOGI(TAG, "🔧 Phase 1: Hardware Initialization (WiFi DISABLED for testing)...");
    
    // راه‌اندازی سخت‌افزار بدون WiFi
    if (hardware_init_lcd() != ESP_OK) {
        ESP_LOGE(TAG, "❌ LCD initialization failed!");
        return;
    }

    ESP_LOGI(TAG, "✅ LCD initialized");
    
    if (hardware_init_sd_card() != ESP_OK) {
        ESP_LOGE(TAG, "❌ SD Card initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "✅ SD Card initialized");
    
    if (hardware_init_buttons() != ESP_OK) {
        ESP_LOGE(TAG, "❌ Buttons initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "✅ Buttons initialized");
    
    // ==================== مرحله ۲: راه‌اندازی Shared Hardware ====================
    ESP_LOGI(TAG, "🔗 Phase 2: Shared Hardware Initialization...");
    if (shared_hardware_init(false) != ESP_OK) {  // false = بدون WiFi
        ESP_LOGE(TAG, "❌ Shared hardware initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "✅ Shared Hardware initialized");

    // ==================== مرحله ۳: راه‌اندازی EVM Loader ====================
    ESP_LOGI(TAG, "🔄 Phase 3: EVM Loader Initialization...");
    if (evm_loader_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ EVM Loader initialization failed!");
        return;
    }
    
    if (evm_loader_core_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ EVM Loader Core initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "✅ EVM Loader initialized");

    // ==================== مرحله ۴: سیستم اصلی ====================
    ESP_LOGI(TAG, "🎯 Phase 4: Core System Initialization...");
    
    if (app_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ Application Manager initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "✅ Application Manager initialized");

    // ==================== مرحله ۵: اسکن برنامه‌ها ====================
    ESP_LOGI(TAG, "🔍 Phase 5: Application Scanning...");
    int app_count = app_manager_scan("/sdcard/apps");
    ESP_LOGI(TAG, "📦 Found %d EVM applications", app_count);

    // ==================== مرحله ۶: نمایش وضعیت سیستم ====================
    ESP_LOGI(TAG, "📊 System Status:");
    
    // 🔥 اصلاح تمام فرمت‌های printf برای uint32_t
    ESP_LOGI(TAG, "  💾 Free RAM: %" PRIu32 " bytes", esp_get_free_heap_size());
    
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0) {
        ESP_LOGI(TAG, "  🚀 Free PSRAM: %" PRIu32 " bytes", (uint32_t)psram_size);
    }
    
    ESP_LOGI(TAG, "  ⚡ PRO CPU Frequency: %" PRIu32 " MHz", 
             (uint32_t)(esp_clk_cpu_freq() / 1000000));

    // ==================== مرحله ۷: رابط کاربری ====================
    ESP_LOGI(TAG, "🎮 Phase 6: Starting User Interface...");
    if (app_manager_start_ui() != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Continuing without UI...");
    } else {
        ESP_LOGI(TAG, "✅ EVM Launcher UI started");
    }

    // ==================== مرحله ۸: راه‌اندازی کامل ====================
    ESP_LOGI(TAG, "🎉 === EVM LAUNCHER READY ===");
    ESP_LOGI(TAG, "🎯 PRO CPU: Launcher Core");
    ESP_LOGI(TAG, "🚀 APP CPU: EVM Applications");

    // ==================== مرحله ۹: تست MQTT ====================
    // ESP_LOGI(TAG, "🌐 Phase 6: Starting MQTT Broker...");
    // if (initialize_mqtt_broker() != ESP_OK) {
    //     ESP_LOGW(TAG, "⚠️ MQTT Broker failed - continuing without it...");
    // }

    //xTaskCreate(simple_mqtt_test, "mqtt_test", 4096, NULL, 4, NULL);

    // نمایش IP
    char ip[16];
    get_ip_address(ip, sizeof(ip));
    ESP_LOGI(TAG, "🌐 Device IP: %s", ip);
    
    // تست‌های مختلف - می‌توانید یکی را انتخاب کنید
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "🚀 Starting FTP Server Tests...");
    ESP_LOGI(TAG, "==========================================");
    
    // تست سریع (پیشنهادی)
    start_ftp_service();
    
    // ==================== حلقه اصلی PRO CPU ====================
    ESP_LOGI(TAG, "🔄 Starting PRO CPU main loop...");
    
    int system_monitor_counter = 0;

    while (1) {
        if (++system_monitor_counter >= 600) {
            system_monitor_counter = 0;
            
            ESP_LOGI(TAG, "📈 System Monitor:");
            
            // 🔥 اصلاح فرمت برای uint32_t
            ESP_LOGI(TAG, "  💾 Free RAM: %" PRIu32 " bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "  🚀 EVM Running: %s", evm_is_app_running() ? "Yes" : "No");
            
            if (evm_is_app_running()) {
                ESP_LOGI(TAG, "  📱 Running: %s", evm_get_running_app_name());
            }
        }
        
        // ریست watchdog
      //  esp_task_wdt_reset();
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}