#include "esp_log.h"
#include "evm_module_timer.h"
#include "mujs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "evm_timer";

// ساختار برای مدیریت تایمرها
typedef struct {
    TimerHandle_t timer_handle;
    js_State *js_state;
    bool is_interval;
    int callback_ref;
    char *name;
} timer_context_t;

static timer_context_t *active_timers[10] = {0};
static int timer_count = 0;

// تابع callback برای تایمرها
static void timer_callback(TimerHandle_t xTimer) {
    timer_context_t *context = (timer_context_t *)pvTimerGetTimerID(xTimer);
    
    if (context && context->js_state && context->callback_ref != 0) {
        ESP_LOGD(TAG, "Timer callback: %s", context->name);
        
        // فراخوانی تابع JavaScript
        js_getregistry(context->js_state, context->callback_ref);
        if (js_iscallable(context->js_state, -1)) {
            js_call(context->js_state, 0);
        }
        js_pop(context->js_state, 1);
        
        // اگر interval نیست، تایمر را حذف کن
        if (!context->is_interval) {
            for (int i = 0; i < timer_count; i++) {
                if (active_timers[i] == context) {
                    active_timers[i] = NULL;
                    break;
                }
            }
            free(context->name);
            free(context);
        }
    }
}

// تابع delay (ایمن!)
static void js_timer_delay(js_State *J) {
    int ms = js_toint32(J, 1);
    if (ms < 1 || ms > 30000) ms = 100;  // محدود کردن به 30 ثانیه
   // ESP_LOGI(TAG, "Timer.delay(%d ms)", ms);
    vTaskDelay(pdMS_TO_TICKS(ms));
    js_pushundefined(J);
}

// تابع getTime
static void js_timer_gettime(js_State *J) {
    int64_t time_us = esp_timer_get_time();
    js_pushnumber(J, time_us / 1000);
}

// تابع setTimeout
static void js_timer_settimeout(js_State *J) {
    if (js_gettop(J) < 2 || !js_iscallable(J, 1)) {
        js_error(J, "setTimeout requires a callback function and delay");
        return;
    }
    
    int delay_ms = js_toint32(J, 2);
    if (delay_ms < 1 || delay_ms > 30000) delay_ms = 1000;
    
    // پیدا کردن جای خالی در آرایه تایمرها
    int index = -1;
    for (int i = 0; i < 10; i++) {
        if (active_timers[i] == NULL) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        js_error(J, "Maximum timers reached");
        return;
    }
    
    // ایجاد context جدید
    timer_context_t *context = malloc(sizeof(timer_context_t));
    if (!context) {
        js_error(J, "Out of memory for timer");
        return;
    }
    
    // 🔥 مقداردهی اولیه همه فیلدها
    memset(context, 0, sizeof(timer_context_t));
    context->js_state = J;
    context->is_interval = false;
    context->name = strdup("setTimeout");
    context->callback_ref = 0;
    context->timer_handle = NULL;
    
    // ذخیره callback در registry
    js_copy(J, 1); // کپی کردن تابع callback
    context->callback_ref = js_ref(J);
    
    // ایجاد تایمر FreeRTOS
    context->timer_handle = xTimerCreate(
        "js_timeout",
        pdMS_TO_TICKS(delay_ms),
        pdFALSE,  // one-shot
        (void *)context,
        timer_callback
    );
    
    if (context->timer_handle == NULL) {
        free(context->name);
        free(context);
        js_error(J, "Failed to create timer");
        return;
    }
    
    // شروع تایمر
    if (xTimerStart(context->timer_handle, 0) != pdPASS) {
        xTimerDelete(context->timer_handle, 0);
        free(context->name);
        free(context);
        js_error(J, "Failed to start timer");
        return;
    }
    
    active_timers[index] = context;
    timer_count++;
    
    js_pushnumber(J, index);  // بازگرداندن ID تایمر
    ESP_LOGI(TAG, "setTimeout created: %d ms, ID: %d", delay_ms, index);
}


// تابع setInterval
static void js_timer_setinterval(js_State *J) {
    if (js_gettop(J) < 2 || !js_iscallable(J, 1)) {
        js_error(J, "setInterval requires a callback function and interval");
        return;
    }
    
    int interval_ms = js_toint32(J, 2);
    if (interval_ms < 10 || interval_ms > 30000) interval_ms = 1000;
    
    // پیدا کردن جای خالی
    int index = -1;
    for (int i = 0; i < 10; i++) {
        if (active_timers[i] == NULL) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        js_error(J, "Maximum timers reached");
        return;
    }
    
    // ایجاد context جدید
    timer_context_t *context = malloc(sizeof(timer_context_t));
    if (!context) {
        js_error(J, "Out of memory for timer");
        return;
    }
    
    context->js_state = J;
    context->is_interval = true;
    context->name = strdup("setInterval");
    
    // ذخیره callback در registry
    context->callback_ref = js_ref(J);
    
    // ایجاد تایمر FreeRTOS
    context->timer_handle = xTimerCreate(
        "js_interval",
        pdMS_TO_TICKS(interval_ms),
        pdTRUE,  // auto-reload
        (void *)context,
        timer_callback
    );
    
    if (context->timer_handle == NULL) {
        free(context->name);
        free(context);
        js_error(J, "Failed to create timer");
        return;
    }
    
    // شروع تایمر
    if (xTimerStart(context->timer_handle, 0) != pdPASS) {
        free(context->name);
        free(context);
        js_error(J, "Failed to start timer");
        return;
    }
    
    active_timers[index] = context;
    timer_count++;
    
    js_pushnumber(J, index);  // بازگرداندن ID تایمر
    ESP_LOGI(TAG, "setInterval created: %d ms, ID: %d", interval_ms, index);
}

// تابع clearInterval/clearTimeout
static void js_timer_clear(js_State *J) {
    int timer_id = js_toint32(J, 1);
    
    if (timer_id < 0 || timer_id >= 10 || active_timers[timer_id] == NULL) {
        js_pushundefined(J);
        return;
    }
    
    timer_context_t *context = active_timers[timer_id];
    
    // توقف تایمر
    xTimerStop(context->timer_handle, 0);
    xTimerDelete(context->timer_handle, 0);
    
    // آزادسازی منابع
    js_unref(context->js_state, context->callback_ref);
    free(context->name);
    free(context);
    
    active_timers[timer_id] = NULL;
    timer_count--;
    
    js_pushundefined(J);
    ESP_LOGI(TAG, "Timer cleared: ID %d", timer_id);
}

// تابع پاک‌سازی همه تایمرها
static void cleanup_all_timers(void) {
    for (int i = 0; i < 10; i++) {
        if (active_timers[i] != NULL) {
            timer_context_t *context = active_timers[i];
            
            xTimerStop(context->timer_handle, 0);
            xTimerDelete(context->timer_handle, 0);
            
            js_unref(context->js_state, context->callback_ref);
            free(context->name);
            free(context);
            
            active_timers[i] = NULL;
        }
    }
    timer_count = 0;
    ESP_LOGI(TAG, "All timers cleaned up");
}

esp_err_t evm_timer_init(void) {
    ESP_LOGI(TAG, "Initializing EVM Timer Module");
    timer_count = 0;
    memset(active_timers, 0, sizeof(active_timers));
    return ESP_OK;
}



// 🔥 متغیرهای全局 برای زمان واقعی
static int64_t time_offset_ms = 0;
static bool real_time_initialized = false;

// 🔥 تابع برای تنظیم زمان واقعی
static void js_timer_setrealtime(js_State *J) {
    if (js_gettop(J) < 4) {
        js_error(J, "setRealTime requires hours, minutes, seconds");
        return;
    }
    
    int hours = js_toint32(J, 1);
    int minutes = js_toint32(J, 2);
    int seconds = js_toint32(J, 3);
    
    // اعتبارسنجی
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        js_error(J, "Invalid time values");
        return;
    }
    
    // محاسبه زمان فعلی سیستم (میلی‌ثانیه از شروع)
    int64_t current_system_ms = esp_timer_get_time() / 1000;
    
    // محاسبه زمان هدف (ثانیه از نیمه‌شب)
    int target_seconds = hours * 3600 + minutes * 60 + seconds;
    
    // محاسبه offset
    // زمان سیستم - زمان هدف = offset
    time_offset_ms = current_system_ms - (target_seconds * 1000);
    real_time_initialized = true;
    
    ESP_LOGI(TAG, "🕒 Real time set: %02d:%02d:%02d | Offset: %lld ms", 
             hours, minutes, seconds, time_offset_ms);
    
    js_pushundefined(J);
}

// 🔥 تابع برای گرفتن زمان واقعی
static void js_timer_getrealtime(js_State *J) {
    if (!real_time_initialized) {
        // اگر زمان تنظیم نشده، از زمان سیستم استفاده کن
        int64_t system_ms = esp_timer_get_time() / 1000;
        int total_seconds = (system_ms / 1000) % 86400; // 24 ساعت
        
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;
        
        js_newobject(J);
        js_pushnumber(J, hours);
        js_setproperty(J, -2, "hours");
        js_pushnumber(J, minutes);
        js_setproperty(J, -2, "minutes");
        js_pushnumber(J, seconds);
        js_setproperty(J, -2, "seconds");
        js_pushnumber(J, total_seconds);
        js_setproperty(J, -2, "totalSeconds");
        return;
    }
    
    // محاسبه زمان واقعی
    int64_t current_system_ms = esp_timer_get_time() / 1000;
    int64_t real_time_ms = current_system_ms - time_offset_ms;
    int total_seconds = (real_time_ms / 1000) % 86400; // محدود به 24 ساعت
    
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;
    
    // ایجاد object با زمان
    js_newobject(J);
    js_pushnumber(J, hours);
    js_setproperty(J, -2, "hours");
    js_pushnumber(J, minutes);
    js_setproperty(J, -2, "minutes");
    js_pushnumber(J, seconds);
    js_setproperty(J, -2, "seconds");
    js_pushnumber(J, total_seconds);
    js_setproperty(J, -2, "totalSeconds");
    js_pushnumber(J, real_time_ms);
    js_setproperty(J, -2, "timestamp");
}

// 🔥 تابع برای تنظیم زمان نسبی (شبیه‌سازی)
static void js_timer_settime(js_State *J) {
    if (js_gettop(J) < 4) {
        js_error(J, "setTime requires hours, minutes, seconds");
        return;
    }
    
    int hours = js_toint32(J, 1);
    int minutes = js_toint32(J, 2);
    int seconds = js_toint32(J, 3);
    
    // اعتبارسنجی
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        js_error(J, "Invalid time values");
        return;
    }
    
    // محاسبه زمان هدف (ثانیه از نیمه‌شب)
    int target_seconds = hours * 3600 + minutes * 60 + seconds;
    
    // تنظیم offset برای شروع از زمان مورد نظر
    int64_t current_system_ms = esp_timer_get_time() / 1000;
    time_offset_ms = current_system_ms - (target_seconds * 1000);
    real_time_initialized = true;
    
    ESP_LOGI(TAG, "⏰ Time set to: %02d:%02d:%02d", hours, minutes, seconds);
    
    js_pushundefined(J);
}

// 🔥 تابع برای گرفتن زمان به صورت رشته
static void js_timer_gettimestring(js_State *J) {
    if (!real_time_initialized) {
        // زمان پیش‌فرض
        js_pushstring(J, "12:00:00");
        return;
    }
    
    int64_t current_system_ms = esp_timer_get_time() / 1000;
    int64_t real_time_ms = current_system_ms - time_offset_ms;
    int total_seconds = (real_time_ms / 1000) % 86400;
    
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;
    
    char time_str[12];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);
    
    js_pushstring(J, time_str);
}

esp_err_t evm_timer_register_js(js_State *J) {
    ESP_LOGI(TAG, "Registering Timer module in JavaScript");

    // ساخت شیء Timer
    js_newobject(J);

    // توابع پایه
    js_newcfunction(J, js_timer_delay, "delay", 1);
    js_setproperty(J, -2, "delay");

    js_newcfunction(J, js_timer_gettime, "getTime", 0);
    js_setproperty(J, -2, "getTime");

    // توابع پیشرفته
    js_newcfunction(J, js_timer_settimeout, "setTimeout", 2);
    js_setproperty(J, -2, "setTimeout");

    js_newcfunction(J, js_timer_setinterval, "setInterval", 2);
    js_setproperty(J, -2, "setInterval");

    js_newcfunction(J, js_timer_clear, "clearInterval", 1);
    js_setproperty(J, -2, "clearInterval");

    js_newcfunction(J, js_timer_clear, "clearTimeout", 1);
    js_setproperty(J, -2, "clearTimeout");

    // 🔥 توابع جدید زمان واقعی
    js_newcfunction(J, js_timer_settime, "setTime", 3);
    js_setproperty(J, -2, "setTime");

    js_newcfunction(J, js_timer_setrealtime, "setRealTime", 3);
    js_setproperty(J, -2, "setRealTime");

    js_newcfunction(J, js_timer_getrealtime, "getRealTime", 0);
    js_setproperty(J, -2, "getRealTime");

    js_newcfunction(J, js_timer_gettimestring, "getTimeString", 0);
    js_setproperty(J, -2, "getTimeString");

    // فقط Timer رو در global قرار بده
    js_setglobal(J, "Timer");

    ESP_LOGI(TAG, "✅ Timer module registered with REAL TIME functions");
    return ESP_OK;
}

// تابع پاک‌سازی هنگام توقف برنامه
void evm_timer_cleanup(void) {
    cleanup_all_timers();
}