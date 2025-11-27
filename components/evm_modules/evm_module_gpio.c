#include "evm_module_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "shared_hardware.h"

static const char *TAG = "evm_gpio";

// پین‌های ممنوعه (استفاده شده توسط لانچر)
static const int forbidden_pins[] = {2, 4, 5, 13, 14, 15, 18, 19, 23, 34, 35};
static const int forbidden_pins_count = sizeof(forbidden_pins) / sizeof(forbidden_pins[0]);

// بررسی آیا پین مجاز است
static bool is_pin_allowed(int pin) {
    for (int i = 0; i < forbidden_pins_count; i++) {
        if (forbidden_pins[i] == pin) {
            return false;
        }
    }
    return true;
}

// تابع JavaScript برای خواندن وضعیت GPIO
static void js_gpio_read(js_State *J) {
    int pin = js_toint32(J, 1);
    
    // بررسی مجاز بودن پین
    if (!is_pin_allowed(pin)) {
     //   ESP_LOGW(TAG, "🚫 Attempt to read forbidden pin: %d", pin);
        js_pushnumber(J, -1); // مقدار خطا
        return;
    }
    
    // خواندن وضعیت پین
    int level = gpio_get_level(pin);
   // ESP_LOGI(TAG, "📊 Reading GPIO %d: %d", pin, level);
    js_pushnumber(J, level);
}

// تابع JavaScript برای نوشتن روی GPIO
static void js_gpio_write(js_State *J) {
    int pin = js_toint32(J, 1);
    int level = js_toint32(J, 2);
    
    // بررسی مجاز بودن پین
    if (!is_pin_allowed(pin)) {
 //       ESP_LOGW(TAG, "🚫 Attempt to write forbidden pin: %d", pin);
        js_pushundefined(J);
        return;
    }
    
    // تنظیم وضعیت پین
    gpio_set_level(pin, level);
 //   ESP_LOGI(TAG, "💡 Writing GPIO %d: %d", pin, level);
    js_pushundefined(J);
}

// تابع JavaScript برای تنظیم جهت GPIO
static void js_gpio_set_direction(js_State *J) {
    int pin = js_toint32(J, 1);
    int direction = js_toint32(J, 2); // 0 = INPUT, 1 = OUTPUT
    
    // بررسی مجاز بودن پین
    if (!is_pin_allowed(pin)) {
     //   ESP_LOGW(TAG, "🚫 Attempt to configure forbidden pin: %d", pin);
        js_pushundefined(J);
        return;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = (direction == 1) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "⚙️ Configuring GPIO %d as %s", pin, 
             (direction == 1) ? "OUTPUT" : "INPUT");
    js_pushundefined(J);
}

// تابع برای خواندن دکمه‌های لانچر (فقط خواندن)
static void js_gpio_read_button(js_State *J) {
    int button_id = js_toint32(J, 1);
    
    // نگاشت button_id به پین‌های واقعی
    int pin;
    switch(button_id) {
        case 0: pin = 2; break;  // UP
        case 1: pin = 4; break;  // SELECT  
        case 2: pin = 34; break; // DOWN
        case 3: pin = 35; break; // BACK
        default: 
            js_pushnumber(J, -1);
            return;
    }
    
    // خواندن وضعیت دکمه (معکوس چون pull-up است)
    int level = gpio_get_level(pin);
    int pressed = (level == 0) ? 1 : 0; // 0 = فشرده, 1 = رها
    
  //  ESP_LOGI(TAG, "🎮 Reading button %d (GPIO %d): %s", 
   //          button_id, pin, pressed ? "PRESSED" : "RELEASED");
    js_pushnumber(J, pressed);
}

// تابع برای گرفتن لیست پین‌های مجاز
static void js_gpio_get_available_pins(js_State *J) {
    js_newarray(J);
    int index = 0;
    
    // پین‌های مجاز معمولی
    int available_pins[] = {12, 16, 17, 21, 22, 25, 26, 27, 32, 33};
    int available_count = sizeof(available_pins) / sizeof(available_pins[0]);
    
    for (int i = 0; i < available_count; i++) {
        js_pushnumber(J, available_pins[i]);
        js_setindex(J, -2, index++);
    }
}

esp_err_t evm_gpio_init(void) {
    ESP_LOGI(TAG, "🔌 Initializing EVM GPIO Module");
    ESP_LOGI(TAG, "✅ EVM GPIO Module initialized");
    return ESP_OK;
}

esp_err_t evm_gpio_register_js(js_State *J) {
    ESP_LOGI(TAG, "📝 Registering GPIO module in JavaScript");
    
    // ایجاد object gpio
    js_newobject(J);
    
    // ثبت توابع GPIO اصلی
    js_newcfunction(J, js_gpio_read, "read", 1);
    js_setproperty(J, -2, "read");
    
    js_newcfunction(J, js_gpio_write, "write", 2);
    js_setproperty(J, -2, "write");
    
    js_newcfunction(J, js_gpio_set_direction, "setDirection", 2);
    js_setproperty(J, -2, "setDirection");
    
    // تابع برای خواندن دکمه‌ها
    js_newcfunction(J, js_gpio_read_button, "readButton", 1);
    js_setproperty(J, -2, "readButton");
    
    // تابع برای گرفتن پین‌های مجاز
    js_newcfunction(J, js_gpio_get_available_pins, "getAvailablePins", 0);
    js_setproperty(J, -2, "getAvailablePins");
    
    // اضافه کردن ثابت‌ها
    js_pushnumber(J, 0); // INPUT
    js_setproperty(J, -2, "INPUT");
    
    js_pushnumber(J, 1); // OUTPUT
    js_setproperty(J, -2, "OUTPUT");
    
    js_pushnumber(J, 0); // LOW
    js_setproperty(J, -2, "LOW");
    
    js_pushnumber(J, 1); // HIGH
    js_setproperty(J, -2, "HIGH");
    
    // ثابت‌های دکمه‌ها
    js_pushnumber(J, 0); // BUTTON_UP
    js_setproperty(J, -2, "BUTTON_UP");
    
    js_pushnumber(J, 1); // BUTTON_SELECT
    js_setproperty(J, -2, "BUTTON_SELECT");
    
    js_pushnumber(J, 2); // BUTTON_DOWN
    js_setproperty(J, -2, "BUTTON_DOWN");
    
    js_pushnumber(J, 3); // BUTTON_BACK
    js_setproperty(J, -2, "BUTTON_BACK");
    
    js_setglobal(J, "gpio");
    
    ESP_LOGI(TAG, "✅ GPIO module registered in JavaScript");
    return ESP_OK;
}