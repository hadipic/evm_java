#include "evm_module_console.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define RETURN_UNDEFINED() js_pushundefined(J)

static const char *TAG = "evm_console";

// تابع کمکی برای تبدیل ایمن به رشته
static const char* safe_tostring(js_State *J, int idx) {
    if (js_isundefined(J, idx)) {
        return "undefined";
    } else if (js_isnull(J, idx)) {
        return "null";
    } else if (js_isboolean(J, idx)) {
        return js_toboolean(J, idx) ? "true" : "false";
    } else if (js_isnumber(J, idx)) {
        // استفاده از buffer استاتیک برای اعداد
        static char num_buffer[32];
        double val = js_tonumber(J, idx);
        snprintf(num_buffer, sizeof(num_buffer), "%.6g", val);
        return num_buffer;
    } else {
        const char *str = js_tostring(J, idx);
        return str ? str : "[conversion failed]";
    }
}

// در evm_module_console.c - تابع console_print:

static void console_print(js_State *J, const char *prefix){
    int argc = js_gettop(J);
    
    // چاپ prefix اگر وجود دارد
    if (prefix && prefix[0]) {
        printf("%s", prefix);
    }
    
    // چاپ تمام آرگومان‌ها
    for (int i = 1; i <= argc -1 ; i++) {
        const char *str = js_tostring(J, i);
        if (str) {
            if (i > 1) printf(" ");
            printf("%s", str);
        }
    }
   // printf("\n");
   // fflush(stdout);
    
    // حتماً undefined برگردانید - اما این در خروجی چاپ نمی‌شود
   // js_pushundefined(J);
}
// تابع برای console.log
static void js_console_log(js_State *J) {
    console_print(J,  "");
    printf("\n");  // فقط اینجا خط جدید بزن!
  
}

// تابع برای console.write - چاپ بدون newline
static void js_console_write(js_State *J) {
    char buffer[512];
    int pos = 0;
    int argc = js_gettop(J);
    
    for (int i = 1; i <= argc; i++) {
        const char *str = safe_tostring(J, i);
        if (str) {
            int len = strlen(str);
            
            // اضافه کردن فاصله بین آرگومان‌ها (به جز اولین)
            if (i > 1 && pos > 0) {
                if (pos + 1 < (int)sizeof(buffer)) {
                    buffer[pos++] = ' ';
                }
            }
            
            // کپی کردن رشته به بافر
            if (pos + len < (int)sizeof(buffer)) {
                strcpy(buffer + pos, str);
                pos += len;
            } else {
                break;
            }
        }
    }
    buffer[pos] = '\0';
    
    ESP_LOGI("JS_CONSOLE", "✍️ %s", buffer);
    printf("%s", buffer);
    fflush(stdout);
    
   // js_pushundefined(J);
}

// تابع برای console.error
static void js_console_error(js_State *J) {
    console_print(J, "ERROR ❌ :");
    printf("\n");

}

// تابع برای console.info
static void js_console_info(js_State *J) {
    console_print(J, "INFO ℹ️ :");
     printf("\n");
}

// تابع برای console.warn
static void js_console_warn(js_State *J) {
    console_print(J, "WARN ⚠️ : ");
     printf("\n");
}

// تابع برای console.debug
static void js_console_debug(js_State *J) {
#ifdef CONFIG_EVM_DEBUG
    console_print(J, "DEBUG 🐛 : ");
     printf("\n");
#else
  //  js_pushundefined(J);
#endif
}

// تابع برای console.clear
static void js_console_clear(js_State *J) {
    printf("\033[2J\033[H"); // پاک کردن ترمینال
    ESP_LOGI("JS_CONSOLE", "🧹 Console cleared");
   // js_pushundefined(J);
}

// تابع برای console.trace - نمایش stack trace
static void js_console_trace(js_State *J) {
    ESP_LOGI("JS_CONSOLE", "🔍 Stack trace:");
    printf("Stack trace:\n");
    printf("    at <anonymous>\n");
    fflush(stdout);
    
   // js_pushundefined(J);
}

// تابع برای console.time و console.timeEnd (ساده شده)
static void js_console_time(js_State *J) {
    const char *label = "default";
    if (js_gettop(J) > 1) {
        label = safe_tostring(J, 1);
    }
    
    ESP_LOGI("JS_CONSOLE", "⏱️  Timer '%s' started", label);
    printf("Timer '%s' started\n", label);
    fflush(stdout);
    
   // js_pushundefined(J);
}

static void js_console_timeEnd(js_State *J) {
    const char *label = "default";
    if (js_gettop(J) > 1) {
        label = safe_tostring(J, 1);
    }
    
    ESP_LOGI("JS_CONSOLE", "⏱️  Timer '%s' ended", label);
    printf("Timer '%s' ended\n", label);
    fflush(stdout);
    
   // js_pushundefined(J);
}

esp_err_t evm_console_init(void) {
    ESP_LOGI(TAG, "📟 Initializing EVM Console Module");
    return ESP_OK;
}

esp_err_t evm_console_register_js(js_State *J) {
    ESP_LOGI(TAG, "📝 Registering Console module in JavaScript");
    
    // ایجاد object console
    js_newobject(J);
    
    // ثبت توابع اصلی
    js_newcfunction(J, js_console_log, "log", 0);
    js_setproperty(J, -2, "log");
    
    js_newcfunction(J, js_console_write, "write", 0);
    js_setproperty(J, -2, "write");
    
    js_newcfunction(J, js_console_error, "error", 0);
    js_setproperty(J, -2, "error");
    
    js_newcfunction(J, js_console_info, "info", 0);
    js_setproperty(J, -2, "info");
    
    js_newcfunction(J, js_console_warn, "warn", 0);
    js_setproperty(J, -2, "warn");
    
    js_newcfunction(J, js_console_debug, "debug", 0);
    js_setproperty(J, -2, "debug");
    
    js_newcfunction(J, js_console_clear, "clear", 0);
    js_setproperty(J, -2, "clear");
    
    js_newcfunction(J, js_console_trace, "trace", 0);
    js_setproperty(J, -2, "trace");
    
    js_newcfunction(J, js_console_time, "time", 1);
    js_setproperty(J, -2, "time");
    
    js_newcfunction(J, js_console_timeEnd, "timeEnd", 1);
    js_setproperty(J, -2, "timeEnd");
    
    // اضافه کردن برخی ثابت‌ها
    js_pushboolean(J, 1);
    js_setproperty(J, -2, "isTTY");
    
    js_setglobal(J, "console");
    
    ESP_LOGI(TAG, "✅ Console module registered in JavaScript");
    return ESP_OK;
}