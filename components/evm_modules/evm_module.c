#include "esp_log.h"
#include "evm_module.h"
#include "mujs.h"

static const char *TAG = "evm_module";

// تابع ثبت ماژول در MuJS
static void js_evm_init(js_State *J) {
    ESP_LOGI(TAG, "EVM Module initialized in JavaScript");
    js_pushundefined(J);
}

esp_err_t evm_module_init(void) {
    ESP_LOGI(TAG, "Initializing EVM Base Module");
    ESP_LOGI(TAG, "EVM Base Module initialized");
    return ESP_OK;
}

// تابع برای ثبت ماژول در موتور JavaScript
esp_err_t evm_module_register_js(js_State *J) {
    ESP_LOGI(TAG, "Registering EVM module in JavaScript engine");
    
    // ثبت توابع EVM در global scope
    js_newcfunction(J, js_evm_init, "evm_init", 0);
    js_setglobal(J, "evm_init");
    
    // ایجاد object evm
    js_newobject(J);
    
    // اضافه کردن properties
    js_pushstring(J, "1.0.0");
    js_setproperty(J, -2, "version");
    
    js_pushstring(J, "ESP32");
    js_setproperty(J, -2, "platform");
    
    js_setglobal(J, "evm");
    
    ESP_LOGI(TAG, "EVM module registered in JavaScript");
    return ESP_OK;
}