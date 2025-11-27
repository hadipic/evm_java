#include "evm_module_process.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "sdkconfig.h"

static const char *TAG = "evm_process";

// ساختار برای ذخیره وضعیت CPU
typedef struct {
    uint64_t last_time;
    uint32_t last_idle_ticks;
    float user_usage;
    float system_usage;
} cpu_usage_t;

static cpu_usage_t cpu_stats = {0};

// محاسبه استفاده از CPU
static void calculate_cpu_usage(void) {
    uint64_t current_time = esp_timer_get_time();
    uint32_t current_idle_ticks = xTaskGetTickCount();
    
    if (cpu_stats.last_time > 0 && current_time > cpu_stats.last_time) {
        uint64_t time_diff = current_time - cpu_stats.last_time;
        uint32_t idle_diff = current_idle_ticks - cpu_stats.last_idle_ticks;
        
        // محاسبه درصد idle (ساده شده)
        float idle_percent = (float)idle_diff * portTICK_PERIOD_MS * 1000.0 / time_diff;
        idle_percent = idle_percent > 1.0 ? 1.0 : idle_percent;
        idle_percent = idle_percent < 0.0 ? 0.0 : idle_percent;
        
        float used_percent = (1.0 - idle_percent) * 100.0;
        
        // تقسیم استفاده بین user و system
        cpu_stats.user_usage = used_percent * 0.7;
        cpu_stats.system_usage = used_percent * 0.3;
    }
    
    cpu_stats.last_time = current_time;
    cpu_stats.last_idle_ticks = current_idle_ticks;
}

// تابع برای process.memoryUsage() - اطلاعات حافظه
static void js_process_memoryUsage(js_State *J) {
    js_newobject(J);
    
    // حافظه داخلی
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t used_internal = total_internal - free_internal;
    
    js_pushnumber(J, used_internal);
    js_setproperty(J, -2, "rss");
    
    js_pushnumber(J, free_internal);
    js_setproperty(J, -2, "free");
    
    js_pushnumber(J, total_internal);
    js_setproperty(J, -2, "total");
    
    js_pushnumber(J, total_internal > 0 ? (double)used_internal / total_internal * 100.0 : 0);
    js_setproperty(J, -2, "usage");
    
    // حافظه external/PSRAM
#if CONFIG_SPIRAM_USE || CONFIG_SPIRAM
    size_t free_external = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_external = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t used_external = total_external > free_external ? total_external - free_external : 0;
    
    js_pushnumber(J, used_external);
    js_setproperty(J, -2, "externalUsed");
    
    js_pushnumber(J, free_external);
    js_setproperty(J, -2, "externalFree");
    
    js_pushnumber(J, total_external);
    js_setproperty(J, -2, "externalTotal");
    
    js_pushnumber(J, total_external > 0 ? (double)used_external / total_external * 100.0 : 0);
    js_setproperty(J, -2, "externalUsage");
#else
    js_pushnumber(J, 0);
    js_setproperty(J, -2, "externalUsed");
    
    js_pushnumber(J, 0);
    js_setproperty(J, -2, "externalFree");
    
    js_pushnumber(J, 0);
    js_setproperty(J, -2, "externalTotal");
    
    js_pushnumber(J, 0);
    js_setproperty(J, -2, "externalUsage");
#endif
    
    // حداقل حافظه آزاد از زمان بوت
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    js_pushnumber(J, min_free);
    js_setproperty(J, -2, "minFree");
}

// تابع برای process.uptime() - زمان فعالیت سیستم
static void js_process_uptime(js_State *J) {
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    js_pushnumber(J, uptime_ms / 1000.0); // بازگشت به ثانیه
}

// تابع برای process.cpuUsage() - استفاده از CPU
static void js_process_cpuUsage(js_State *J) {
    calculate_cpu_usage();
    
    js_newobject(J);
    
    js_pushnumber(J, cpu_stats.user_usage);
    js_setproperty(J, -2, "user");
    
    js_pushnumber(J, cpu_stats.system_usage);
    js_setproperty(J, -2, "system");
    
    js_pushnumber(J, cpu_stats.user_usage + cpu_stats.system_usage);
    js_setproperty(J, -2, "total");
    
    js_pushnumber(J, 100.0 - (cpu_stats.user_usage + cpu_stats.system_usage));
    js_setproperty(J, -2, "idle");
}

// تابع برای process.exit() - خروج
static void js_process_exit(js_State *J) {
    int code = 0;
    if (js_gettop(J) > 1) {
        code = js_toint32(J, 1);
    }
    
    ESP_LOGI(TAG, "🚪 Process exit with code: %d", code);
    
    if (code != 0) {
        ESP_LOGE(TAG, "❌ Process exited with error code: %d", code);
    }
    
    js_pushundefined(J);
}

// تابع برای process.kill() - پایان پردازش
static void js_process_kill(js_State *J) {
    int signal = 0;
    if (js_gettop(J) > 1) {
        signal = js_toint32(J, 1);
    }
    
    ESP_LOGI(TAG, "🛑 Process kill signal: %d", signal);
    js_pushundefined(J);
}

// تابع برای process.cwd() - دایرکتوری جاری
static void js_process_cwd(js_State *J) {
    js_pushstring(J, "/sdcard/apps");
}

// تابع برای process.chdir() - تغییر دایرکتوری
static void js_process_chdir(js_State *J) {
    if (js_gettop(J) > 1) {
        const char *path = js_tostring(J, 1);
        ESP_LOGI(TAG, "📁 Changing directory to: %s", path);
    }
    js_pushundefined(J);
}

// تابع برای process.heapStats() - آمار heap
static void js_process_heapStats(js_State *J) {
    js_newobject(J);
    
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    
    js_pushnumber(J, info.total_free_bytes);
    js_setproperty(J, -2, "free");
    
    js_pushnumber(J, info.total_allocated_bytes);
    js_setproperty(J, -2, "used");
    
    js_pushnumber(J, info.largest_free_block);
    js_setproperty(J, -2, "largestFreeBlock");
    
    js_pushnumber(J, info.minimum_free_bytes);
    js_setproperty(J, -2, "minEverFree");
}

// تابع برای process.restart() - راه‌اندازی مجدد
static void js_process_restart(js_State *J) {
    ESP_LOGI(TAG, "🔄 System restart requested");
    js_pushundefined(J);
}

// Property getter برای process.arch
static void js_process_get_arch(js_State *J) {
#ifdef CONFIG_IDF_TARGET_ESP32
    js_pushstring(J, "esp32");
#elif CONFIG_IDF_TARGET_ESP32S2
    js_pushstring(J, "esp32s2");
#elif CONFIG_IDF_TARGET_ESP32S3
    js_pushstring(J, "esp32s3");
#elif CONFIG_IDF_TARGET_ESP32C3
    js_pushstring(J, "esp32c3");
#elif CONFIG_IDF_TARGET_ESP32C6
    js_pushstring(J, "esp32c6");
#elif CONFIG_IDF_TARGET_ESP32H2
    js_pushstring(J, "esp32h2");
#else
    js_pushstring(J, "esp32");
#endif
}

// Property getter برای process.platform
static void js_process_get_platform(js_State *J) {
    js_pushstring(J, "esp32");
}

// Property getter برای process.pid
static void js_process_get_pid(js_State *J) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    js_pushnumber(J, (int)((uint32_t)task & 0xFFFF));
}

// Property getter برای process.versions
static void js_process_get_versions(js_State *J) {
    js_newobject(J);
    
    // استفاده از نسخه ثابت اگر EVM_VERSION تعریف نشده
#ifdef EVM_VERSION
    js_pushstring(J, EVM_VERSION);
#else
    js_pushstring(J, "1.0.0");
#endif
    js_setproperty(J, -2, "evm");
    
    js_pushstring(J, IDF_VER);
    js_setproperty(J, -2, "esp-idf");
    
    js_pushstring(J, "mujs");
    js_setproperty(J, -2, "javascript");
    
#ifdef __VERSION__
    js_pushstring(J, __VERSION__);
#else
    js_pushstring(J, "unknown");
#endif
    js_setproperty(J, -2, "compiler");
}

// Property getter برای process.cwd
static void js_process_get_cwd(js_State *J) {
    js_pushstring(J, "/sdcard/apps");
}

esp_err_t evm_process_init(void) {
    ESP_LOGI(TAG, "⚙️ Initializing EVM Process Module");
    
    // مقداردهی اولیه آمار CPU
    cpu_stats.last_time = 0;
    cpu_stats.last_idle_ticks = 0;
    cpu_stats.user_usage = 0;
    cpu_stats.system_usage = 0;
    
    return ESP_OK;
}

esp_err_t evm_process_register_js(js_State *J) {
    ESP_LOGI(TAG, "📝 Registering Process module in JavaScript");
    
    // ایجاد object process
    js_newobject(J);
    
    // ثبت توابع (با js_newcfunction)
    js_newcfunction(J, js_process_memoryUsage, "memoryUsage", 0);
    js_setproperty(J, -2, "memoryUsage");
    
    js_newcfunction(J, js_process_uptime, "uptime", 0);
    js_setproperty(J, -2, "uptime");
    
    js_newcfunction(J, js_process_cpuUsage, "cpuUsage", 0);
    js_setproperty(J, -2, "cpuUsage");
    
    js_newcfunction(J, js_process_exit, "exit", 1);
    js_setproperty(J, -2, "exit");
    
    js_newcfunction(J, js_process_kill, "kill", 1);
    js_setproperty(J, -2, "kill");
    
    js_newcfunction(J, js_process_cwd, "cwd", 0);
    js_setproperty(J, -2, "cwd");
    
    js_newcfunction(J, js_process_chdir, "chdir", 1);
    js_setproperty(J, -2, "chdir");
    
    js_newcfunction(J, js_process_heapStats, "heapStats", 0);
    js_setproperty(J, -2, "heapStats");
    
    js_newcfunction(J, js_process_restart, "restart", 0);
    js_setproperty(J, -2, "restart");
    
    // ثبت properties با استفاده از js_defproperty برای getter-only
    // ابتدا مقدار را push کنید، سپس با JS_READONLY تعریف کنید
    js_process_get_arch(J);
    js_defproperty(J, -2, "arch", JS_READONLY);
    
    js_process_get_platform(J);
    js_defproperty(J, -2, "platform", JS_READONLY);
    
    js_process_get_pid(J);
    js_defproperty(J, -2, "pid", JS_READONLY);
    
    js_process_get_cwd(J);
    js_defproperty(J, -2, "cwd", JS_READONLY);
    
    // versions یک object است، باید متفاوت تعریف شود
    js_process_get_versions(J);
    js_defproperty(J, -2, "versions", JS_READONLY);
    
    // اضافه کردن ثابت‌ها با js_setproperty
    js_pushstring(J, "production");
    js_setproperty(J, -2, "env");
    
    js_pushstring(J, "EVM Runtime");
    js_setproperty(J, -2, "title");
    
    // استفاده از مقدار ثابت اگر کانفیگ موجود نیست
#ifdef CONFIG_EVM_MEMORY_POOL_SIZE
    js_pushnumber(J, CONFIG_EVM_MEMORY_POOL_SIZE);
#else
    js_pushnumber(J, 8192); // مقدار پیش‌فرض
#endif
    js_setproperty(J, -2, "memoryLimit");
    
    // آرگومان‌های خط فرمان (شبیه‌سازی شده)
    js_newarray(J);
    js_pushstring(J, "evm");
    js_setindex(J, -2, 0);
    js_pushstring(J, "--app");
    js_setindex(J, -2, 1);
    js_pushstring(J, "/sdcard/apps/main.js");
    js_setindex(J, -2, 2);
    js_setproperty(J, -2, "argv");
    
    js_setglobal(J, "process");
    
    ESP_LOGI(TAG, "✅ Process module registered in JavaScript");
    return ESP_OK;
}