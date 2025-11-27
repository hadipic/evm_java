#include "evm_loader.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "shared_hardware.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>


#include <ctype.h>
#include <inttypes.h>

// ماژول‌های EVM
#include "evm_module.h"
#include "evm_module_gpio.h"
#include "evm_module_timer.h"
#include "evm_module_fs.h"
#include "evm_module_console.h"
#include "evm_module_process.h"
#include "evm_module_lvgl.h"
#include "evm_module_wifi.h"
#include "evm_module_mongoose.h"
#include "evm_module_ftp.h"

// MuJS
#include "mujs.h"
#include "lvgl.h"

static const char *TAG = "evm_loader";

// ==================== متغیرهای global ====================

static TaskHandle_t app_core_task = NULL;
static evm_execution_context_t current_evm_context;

// وضعیت MuJS
static bool mujs_running = false;
static js_State* mujs_state = NULL;

// وضعیت برنامه
volatile bool app_core_running = false;
volatile bool app_stop_requested = false;
volatile bool app_has_active_loop = false;
volatile uint32_t app_loop_counter = 0;

// ==================== مدیریت حافظه PSRAM ====================

// تابع allocator مخصوص MuJS با PSRAM (با signature صحیح)
static void* mujs_alloc(void *ctx, void *ptr, size_t size) {
    if (size == 0) {
        if (ptr) {
            heap_caps_free(ptr);
        }
        return NULL;
    }
    
    if (ptr == NULL) {
        // تخصیص جدید از PSRAM
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    
    // تغییر اندازه از PSRAM
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// تابع کمکی برای تخصیص بلاک داده از PSRAM
static void* psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// تابع کمکی برای آزادسازی از PSRAM
static void psram_free(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}

// ==================== توابع JavaScript پایه ====================

// تابع print ساده
static void js_print(js_State *J) {
    int argc = js_gettop(J);
   
    for (int i = 1; i <= argc; i++) {
        const char *str = js_tostring(J, i);
        if (str) {
            if (i > 1) printf(" ");
            printf("%s", str);
        }
    }
    printf("\n");
    fflush(stdout);
   
    js_pushundefined(J);
}

// تابع delay
static void js_delay(js_State *J) {
    int ms = js_toint32(J, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    esp_task_wdt_reset();
    js_pushundefined(J);
}

// تابع debug
static void js_debug(js_State *J) {
    char buffer[512];
    int pos = 0;
   
    int64_t time_us = esp_timer_get_time();
    pos += snprintf(buffer, sizeof(buffer), "[%"PRId64"ms] ", time_us/1000);
   
    int top = js_gettop(J);
    for (int i = 1; i <= top; i++) {
        const char *str = js_tostring(J, i);
        if (str) {
            int len = strlen(str);
            if (pos + len + 1 < sizeof(buffer)) {
                if (i > 1 && pos > 0) buffer[pos++] = ' ';
                strcpy(buffer + pos, str);
                pos += len;
            }
        }
    }
    buffer[pos] = '\0';
   
    ESP_LOGI("JS_DEBUG", "%s", buffer);
    js_pushundefined(J);
}

// تابع memory_info برای گزارش وضعیت حافظه (اصلاح شده)
static void js_memory_info(js_State *J) {
    size_t system_free = esp_get_free_heap_size();
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
   
    js_newobject(J);
   
    js_pushstring(J, "system_free");
    js_pushnumber(J, system_free);
    js_setproperty(J, -3, "system_free");
   
    js_pushstring(J, "psram_free");
    js_pushnumber(J, psram_free);
    js_setproperty(J, -3, "psram_free");
   
    js_pushstring(J, "psram_total");
    js_pushnumber(J, psram_total);
    js_setproperty(J, -3, "psram_total");
   
    js_pushstring(J, "internal_free");
    js_pushnumber(J, internal_free);
    js_setproperty(J, -3, "internal_free");
   
    js_pushstring(J, "internal_total");
    js_pushnumber(J, internal_total);
    js_setproperty(J, -3, "internal_total");

    js_pushstring(J, "memory_type");
    js_pushstring(J, "PSRAM");
    js_setproperty(J, -3, "memory_type");
}

// ==================== مدیریت اجرای EVM ====================

// تابع ایمن برای راه‌اندازی ماژول‌های EVM
static esp_err_t safe_evm_modules_init(void) {
    ESP_LOGI(TAG, "🔧 Initializing EVM modules");
   
    ESP_ERROR_CHECK(evm_module_init());
    ESP_ERROR_CHECK(evm_gpio_init());
    ESP_ERROR_CHECK(evm_timer_init());
    ESP_ERROR_CHECK(evm_fs_init());
    ESP_ERROR_CHECK(evm_console_init());
    ESP_ERROR_CHECK(evm_process_init());
    ESP_ERROR_CHECK(evm_lvgl_init());
    
    // 🔥 اصلاح: استفاده از تابع صحیح WiFi
    // اگر تابع system_wifi_init وجود ندارد، از evm_module_wifi_init استفاده کنید
    // یا کامنت کنید اگر نیازی نیست
    #ifdef CONFIG_EVM_MODULE_WIFI
    // ESP_ERROR_CHECK(evm_module_wifi_init()); // اگر این تابع وجود دارد
    #endif
   
    ESP_LOGI(TAG, "✅ All EVM modules initialized");
    return ESP_OK;
}

// بررسی سلامت state
static bool evm_check_state_health(js_State *J) {
    if (!J) return false;
    
    // تست با یک دستور ساده
    if (js_try(J)) {
        // خطا - state خرابه
        js_pop(J, 1);
        return false;
    }
    
    js_loadstring(J, "[health_check]", "1+1;");
    js_pushundefined(J);
    js_call(J, 0);
    js_pop(J, 1); // پاپ نتیجه
    
    js_endtry(J);
    return true;
}

// پاک‌سازی state
static void evm_cleanup_state(js_State *J) {
    if (!J) return;
    
    // اجرای GC
    js_gc(J, 0);
    
    // پاک‌سازی stack در صورت نیاز
    int top = js_gettop(J);
    if (top > 0) {
        ESP_LOGW(TAG, "Cleaning up stack, top was: %d", top);
        // استفاده از js_pop به جای js_settop
        js_pop(J, top);
    }
}

// ==================== اجرای برنامه‌های JavaScript ====================

// اجرای مستقیم کد JavaScript
esp_err_t evm_execute_js(const char* js_code) {
    if (js_code == NULL || mujs_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
   
    ESP_LOGI(TAG, "📜 Executing JavaScript code");
   
    mujs_running = true;

    // استفاده از try-catch داخلی MuJS
    if (js_try(mujs_state)) {
        const char* error_msg = js_trystring(mujs_state, -1, "Unknown error");
        ESP_LOGE(TAG, "❌ JavaScript execution failed: %s", error_msg);
        
        js_pop(mujs_state, 1);
        mujs_running = false;
        return ESP_FAIL;
    }

    // اجرای کد در بلوک try
    js_dostring(mujs_state, js_code);
    
    // پایان بلوک try
    js_endtry(mujs_state);
   
    mujs_running = false;
    ESP_LOGI(TAG, "✅ JavaScript code executed successfully");
    return ESP_OK;
}

// اجرای فایل JavaScript
esp_err_t evm_load_and_execute(const char* file_path) {
    if (file_path == NULL || mujs_state == NULL) {
        ESP_LOGE(TAG, "❌ Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🚀 Loading JavaScript file: %s", file_path);

    FILE* file = fopen(file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ Cannot open file: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "❌ Empty file");
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    // تخصیص حافظه از PSRAM برای محتوای فایل
    char* file_content = psram_malloc(file_size + 1);
    if (!file_content) {
        ESP_LOGE(TAG, "❌ Failed to allocate PSRAM memory");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(file_content, 1, file_size, file);
    file_content[bytes_read] = '\0';
    fclose(file);

    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "❌ Read error");
        psram_free(file_content);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "📜 Executing JavaScript (%ld bytes)", file_size);

    mujs_running = true;

    // استفاده از try-catch داخلی MuJS
    if (js_try(mujs_state)) {
        const char* error_msg = js_trystring(mujs_state, -1, "Unknown error");
        ESP_LOGE(TAG, "❌ JavaScript execution failed: %s", error_msg);
        
        js_pop(mujs_state, 1);
        mujs_running = false;
        psram_free(file_content);
        return ESP_FAIL;
    }

    // اجرای کد در بلوک try
    js_loadstring(mujs_state, file_path, file_content);
    js_pushundefined(mujs_state); // this
    js_call(mujs_state, 0); // اجرای تابع
    
    // پایان بلوک try
    js_endtry(mujs_state);

    mujs_running = false;
    psram_free(file_content);

    ESP_LOGI(TAG, "✅ JavaScript executed successfully");
    return ESP_OK;
}

// ==================== مدیریت MuJS ====================

// راه‌اندازی موتور JavaScript
esp_err_t evm_loader_init(void) {
    ESP_LOGI(TAG, "Initializing EVM Loader with MuJS");

    // بررسی وجود PSRAM
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0) {
        ESP_LOGI(TAG, "✅ PSRAM available: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "⚠️ PSRAM not available, using internal RAM");
    }

    // ایجاد state MuJS با allocator سفارشی PSRAM
    mujs_state = js_newstate(mujs_alloc, NULL, JS_STRICT);
    if (!mujs_state) {
        ESP_LOGE(TAG, "Failed to create MuJS state");
        return ESP_FAIL;
    }

    // توابع پایه
    js_newcfunction(mujs_state, js_print, "print", 0);
    js_setglobal(mujs_state, "print");

    js_newcfunction(mujs_state, js_delay, "delay", 1);
    js_setglobal(mujs_state, "delay");

    js_newcfunction(mujs_state, js_debug, "debug", 0);
    js_setglobal(mujs_state, "debug");

    js_newcfunction(mujs_state, js_memory_info, "memory_info", 0);
    js_setglobal(mujs_state, "memory_info");

    // شیء system
    js_newobject(mujs_state);
    js_pushstring(mujs_state, "ESP32");
    js_setproperty(mujs_state, -2, "platform");
    
    js_pushnumber(mujs_state, esp_get_free_heap_size());
    js_setproperty(mujs_state, -2, "freeMemory");
    
    js_pushboolean(mujs_state, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0);
    js_setproperty(mujs_state, -2, "hasPSRAM");
    
    js_setglobal(mujs_state, "system");

    // ثبت ماژول‌ها
    ESP_LOGI(TAG, "Registering EVM Modules...");

    evm_gpio_register_js(mujs_state);
    evm_timer_register_js(mujs_state);
    evm_fs_register_js(mujs_state);
    evm_process_register_js(mujs_state);
    evm_console_register_js(mujs_state);
    evm_lvgl_register_js_mujs(mujs_state);
    //evm_mongoose_register_js(mujs_state);
    evm_module_wifi_register(mujs_state);  // این تابع register است نه init
    evm_ftp_register_js_mujs(mujs_state);

    mujs_running = false;
    memset(&current_evm_context, 0, sizeof(current_evm_context));

    // تست اولیه
    const char* test_js =
        "print('=== EVM Loader Ready ===');\n"
        "print('Platform: ' + system.platform);\n"
        "print('PSRAM: ' + (system.hasPSRAM ? 'Available' : 'Not Available'));\n"
        "print('Free RAM: ' + system.freeMemory + ' B');\n"
        "memory_info();";

    evm_execute_js(test_js);
    
    ESP_LOGI(TAG, "✅ EVM Loader initialized");
    return ESP_OK;
}

// توقف اجرای EVM
esp_err_t evm_stop_execution(void) {
    ESP_LOGI(TAG, "🛑 Stopping EVM execution");
   
    if (mujs_running) {
        mujs_running = false;
        ESP_LOGI(TAG, "✅ EVM execution stopped");
    } else {
        ESP_LOGW(TAG, "⚠️ EVM is not running");
    }
   
    return ESP_OK;
}

esp_err_t evm_loader_deinit(void) {
    ESP_LOGI(TAG, "🧹 Deinitializing EVM Loader");
    
    // توقف اجرا
    evm_stop_execution();
    
    // آزادسازی MuJS
    if (mujs_state) {
        js_freestate(mujs_state);
        mujs_state = NULL;
    }
    
    // ریست کردن متغیرها
    mujs_running = false;
    app_core_running = false;
    app_core_task = NULL;
    memset(&current_evm_context, 0, sizeof(current_evm_context));
    
    ESP_LOGI(TAG, "✅ EVM Loader deinitialized");
    return ESP_OK;
}

// ==================== مدیریت Dual-Core ====================

static void evm_app_task_func(void *params) {
    const char* app_path = (const char*)params;
    
    bool hardware_acquired = false;
    bool wdt_added = false;
    app_core_running = true;
    
    app_stop_requested = false;
    app_has_active_loop = false;
    app_loop_counter = 0;
    
    ESP_LOGI(TAG, "🎬 APP CPU: Starting EVM application: %s", app_path);
    app_core_task = xTaskGetCurrentTaskHandle();

    // 1. راه‌اندازی WDT
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        wdt_added = true;
    }

    // 2. گرفتن کنترل سخت‌افزار
    if (shared_hardware_acquire_control(1000) == ESP_OK) {
        hardware_acquired = true;
        ESP_LOGI(TAG, "🔧 Hardware control acquired");
    }

    // 🔥🔥🔥 راه‌اندازی LVFS در APP CPU 🔥🔥🔥
   //   lv_fs_fatfs_init();
    ESP_LOGI(TAG, "✅ LVFS initialized on APP CPU");

    // 3. بررسی سلامت state قبل از اجرا
    if (!evm_check_state_health(mujs_state)) {
        ESP_LOGW(TAG, "⚠️ State is unhealthy, recreating...");
        if (mujs_state) {
            js_freestate(mujs_state);
        }
        mujs_state = js_newstate(mujs_alloc, NULL, JS_STRICT);
        // دوباره ثبت ماژول‌ها
        safe_evm_modules_init();
        
        // 🔥 ثبت مجدد توابع JavaScript
        js_newcfunction(mujs_state, js_print, "print", 0);
        js_setglobal(mujs_state, "print");
        js_newcfunction(mujs_state, js_delay, "delay", 1);
        js_setglobal(mujs_state, "delay");
        js_newcfunction(mujs_state, js_debug, "debug", 0);
        js_setglobal(mujs_state, "debug");
        js_newcfunction(mujs_state, js_memory_info, "memory_info", 0);
        js_setglobal(mujs_state, "memory_info");
    }

    // 4. اجرای برنامه اصلی
    const char *filename = strrchr(app_path, '/');
    if (filename) filename++;
    else filename = app_path;
    
    ESP_LOGI(TAG, "🚀 Executing application: %s", filename);
    
    esp_err_t result = evm_load_and_execute(app_path);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "✅ Application executed successfully");
    } else {
        ESP_LOGE(TAG, "❌ Application execution failed");
        // پاک‌سازی state بعد از خطا
        evm_cleanup_state(mujs_state);
    }

    // 5. مدیریت لوپ
    if (app_core_running && !app_stop_requested) {
        ESP_LOGI(TAG, "🔄 Application has active loop, monitoring for stop...");
        app_has_active_loop = true;
        
        while (evm_should_continue_loop()) {
            if (wdt_added) {
                esp_task_wdt_reset();
            }
            
            app_loop_counter++;
            if (app_loop_counter % 100 == 0) {
                ESP_LOGD(TAG, "🔄 App loop running... (%"PRIu32")", app_loop_counter);
                js_gc(mujs_state, 0); // GC دوره‌ای
            }
            
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // 6. پاک‌سازی نهایی
    ESP_LOGI(TAG, "🧹 Cleaning up after application...");

    // پاک‌سازی state (بدون حذف کامل)
    evm_cleanup_state(mujs_state);

    // رهاسازی سخت‌افزار
    if (hardware_acquired) {
        vTaskDelay(pdMS_TO_TICKS(50));
        shared_hardware_release_control();
    }

    // حذف از WDT
    if (wdt_added) {
        esp_task_wdt_delete(NULL);
    }

    // ریست stateها
    app_core_running = false;
    app_has_active_loop = false;
    app_stop_requested = false;
    app_loop_counter = 0;
    app_core_task = NULL;
    
    ESP_LOGI(TAG, "🏁 APP CPU: EVM application task finished");
    vTaskDelete(NULL);
}

// راه‌اندازی مدیر EVM
esp_err_t evm_loader_core_init(void) {
    ESP_LOGI(TAG, "🔧 Initializing EVM Loader Core");
   
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0) {
        ESP_LOGI(TAG, "✅ PSRAM detected: %d bytes",
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
   
    //esp_task_wdt_init(30, false);
    memset(&current_evm_context, 0, sizeof(current_evm_context));
    app_core_running = false;
   
    ESP_LOGI(TAG, "✅ EVM Loader Core initialized");
    return ESP_OK;
}

// اجرای برنامه روی APP CPU
esp_err_t evm_launch_app(const char *app_path) {
    if (app_core_running) {
        ESP_LOGE(TAG, "❌ APP CPU is already running an application");
        return ESP_ERR_INVALID_STATE;
    }
   
    if (app_path == NULL || strlen(app_path) == 0) {
        ESP_LOGE(TAG, "❌ Invalid app path");
        return ESP_ERR_INVALID_ARG;
    }
   
    ESP_LOGI(TAG, "🚀 PRO CPU: Launching app on APP CPU: %s", app_path);
   
    BaseType_t result = xTaskCreatePinnedToCore(
        evm_app_task_func,
        "evm_app_task",
        1024*8,           // stack size
        (void*)app_path,
        2,               // priority
        &app_core_task,  // task handle
        1                // APP CPU
    );
   
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create EVM task on APP CPU");
        return ESP_FAIL;
    }
   
    app_core_running = true;
    ESP_LOGI(TAG, "✅ App task created on APP CPU");
    return ESP_OK;
}

// توقف برنامه
esp_err_t evm_stop_app(void) {
    if (!app_core_running) {
        ESP_LOGW(TAG, "⚠️ No app running to stop");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "🛑 PRO CPU: Requesting EVM application stop");
    
    evm_request_app_stop();
    
    ESP_LOGI(TAG, "✅ Stop request sent to EVM application");
    return ESP_OK;
}

// ==================== توابع اطلاعات وضعیت ====================

bool evm_is_app_running(void) {
    return app_core_running;
}

bool evm_is_js_running(void) {
    return mujs_running;
}

const char* evm_get_running_app_name(void) {
    if (!app_core_running) return "None";
    return current_evm_context.app_name;
}

const char* evm_get_running_app_type(void) {
    if (!app_core_running) return "None";
    return current_evm_context.app_type;
}

size_t evm_get_memory_usage(void) {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - 
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

const char* evm_get_version(void) {
    return "EVM Loader 2.0 with MuJS & PSRAM";
}

void evm_request_app_stop(void) {
    app_stop_requested = true;
    ESP_LOGI(TAG, "🛑 Stop requested for running application");
}

bool evm_is_app_looping(void) {
    return app_has_active_loop;
}

bool evm_should_continue_loop(void) {
    return app_core_running && !app_stop_requested;
}

void evm_print_status(void) {
    ESP_LOGI(TAG, "=== EVM LOADER STATUS ===");
    ESP_LOGI(TAG, "PRO CPU: Active (Launcher)");
    ESP_LOGI(TAG, "APP CPU: %s", app_core_running ? "Running EVM App" : "Idle");
   
    if (app_core_running) {
        ESP_LOGI(TAG, "Running App: %s [%s]",
                 current_evm_context.app_name,
                 current_evm_context.app_type);
    }
   
    
    ESP_LOGI(TAG, "MuJS Engine: %s", mujs_running ? "Running" : "Stopped");
 //   ESP_LOGI(TAG, "PSRAM Used:  %"PRIu32" bytes", 
 //            heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - 
 //            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
   // ESP_LOGI(TAG, "PSRAM Free:  %" PRIu32 " bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Internal Free: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================");
}


// ==================== توابع بررسی و اعتبارسنجی ====================

// 🔥 تابع جدید: بررسی syntax کد JavaScript بدون اجرا
static esp_err_t evm_validate_syntax(const char* js_code, const char* context) {
    if (!js_code || !mujs_state) {
        ESP_LOGE(TAG, "❌ Invalid parameters for syntax validation");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🔍 Validating JavaScript syntax: %s", context);

    // ایجاد state موقت برای بررسی syntax
    js_State* temp_state = js_newstate(mujs_alloc, NULL, JS_STRICT);
    if (!temp_state) {
        ESP_LOGE(TAG, "❌ Failed to create temporary state for syntax check");
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;

    // بررسی syntax با try-catch
    if (js_try(temp_state)) {
        const char* error_msg = js_trystring(temp_state, -1, "Unknown syntax error");
        ESP_LOGE(TAG, "❌ Syntax error in %s: %s", context, error_msg);
        js_pop(temp_state, 1);
        result = ESP_FAIL;
    } else {
        // تلاش برای parse کردن کد (بدون اجرا)
        js_loadstring(temp_state, "[syntax_check]", js_code);
        js_endtry(temp_state);
        ESP_LOGI(TAG, "✅ Syntax OK: %s", context);
    }

    // پاک‌سازی state موقت
    js_freestate(temp_state);
    return result;
}

// 🔥 تابع جدید: بررسی وجود ماژول‌های مورد نیاز در کد
static esp_err_t evm_check_required_modules(const char* js_code) {
    if (!js_code) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🔍 Checking required modules in JavaScript code");

    // لیست ماژول‌های اصلی برای بررسی
    const char* modules[] = {
        "GPIO", "Pin", "gpio", "pin",           // ماژول GPIO
        "Timer", "timer", "setTimeout", "setInterval", // ماژول تایمر
        "FS", "File", "fs", "file",             // ماژول فایل سیستم
        "Console", "console",                   // ماژول کنسول
        "Process", "process",                   // ماژول پردازش
        "LVGL", "lv", "lv_",                    // ماژول LVGL
        "WiFi", "wifi", "WIFI",                 // ماژول WiFi
        "Net", "net", "mongoose",               // ماژول شبکه
        "FTP", "ftp", "Ftp"                    // ماژول FTP
    };

    int found_modules = 0;
    size_t modules_count = sizeof(modules) / sizeof(modules[0]);

    for (size_t i = 0; i < modules_count; i++) {
        if (strstr(js_code, modules[i]) != NULL) {
            ESP_LOGI(TAG, "📦 Found module reference: %s", modules[i]);
            found_modules++;
        }
    }

    ESP_LOGI(TAG, "✅ Module check completed. Found %d module references", found_modules);
    return ESP_OK;
}

// 🔥 تابع جدید: بررسی امنیت کد (جلوگیری از دستورات خطرناک)
static esp_err_t evm_check_code_safety(const char* js_code) {
    if (!js_code) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🛡️ Checking code safety");

    // لیست الگوهای خطرناک
    const char* dangerous_patterns[] = {
        "while(true)", "while(1)",             // حلقه بی‌نهایت
        "for(;;)",                            // حلقه بی‌نهایت
        "eval(", "Function(",                 // eval خطرناک
        "import(", "require(",                // import/require
        "process.exit", "process.kill",       // خاتمه پردازش
        "os.system", "child_process"          // دستورات سیستم
    };

    size_t patterns_count = sizeof(dangerous_patterns) / sizeof(dangerous_patterns[0]);
    
    for (size_t i = 0; i < patterns_count; i++) {
        if (strstr(js_code, dangerous_patterns[i]) != NULL) {
            ESP_LOGW(TAG, "⚠️ Potentially dangerous pattern found: %s", dangerous_patterns[i]);
            // در اینجا می‌توانید تصمیم بگیرید که آیا اجرا شود یا نه
            // فعلاً فقط warning می‌دهیم
        }
    }

    ESP_LOGI(TAG, "✅ Code safety check completed");
    return ESP_OK;
}

// 🔥 تابع جدید: اعتبارسنجی کامل فایل قبل از اجرا
static esp_err_t evm_validate_app_file(const char* file_path) {
    if (!file_path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🔍 Validating app file: %s", file_path);

    // 1. بررسی وجود فایل
    FILE* file = fopen(file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ File not found: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // 2. بررسی سایز فایل
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "❌ Empty file: %s", file_path);
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    if (file_size > 1024 * 200) { // بیش از 200KB
        ESP_LOGW(TAG, "⚠️ Large file: %s (%ld bytes)", file_path, file_size);
    }

    // 3. خواندن محتوای فایل
    char* file_content = psram_malloc(file_size + 1);
    if (!file_content) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for validation");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(file_content, 1, file_size, file);
    file_content[bytes_read] = '\0';
    fclose(file);

    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "❌ Read error during validation");
        psram_free(file_content);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;

    // 4. بررسی syntax
    if (evm_validate_syntax(file_content, file_path) != ESP_OK) {
        ESP_LOGE(TAG, "❌ Syntax validation failed for: %s", file_path);
        result = ESP_FAIL;
    }

    // 5. بررسی ماژول‌ها
    if (result == ESP_OK) {
        evm_check_required_modules(file_content);
    }

    // 6. بررسی امنیت
    if (result == ESP_OK) {
        evm_check_code_safety(file_content);
    }

    psram_free(file_content);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "✅ App validation passed: %s", file_path);
    } else {
        ESP_LOGE(TAG, "❌ App validation failed: %s", file_path);
    }

    return result;
}