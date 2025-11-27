#ifndef EVM_MODULE_LVGL_H
#define EVM_MODULE_LVGL_H

#include "hardware_manager.h"
#include "mujs.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *permanent_launcher_screen;

// توابع عمومی مدیریت حافظه
void evm_lvgl_register_app_object(void* obj);
void evm_lvgl_cleanup_app_objects(void);

// توابع جدید برای مدیریت صفحه مجزا
void evm_lvgl_set_app_screen(void);
void evm_lvgl_set_launcher_screen(void);
void evm_lvgl_cleanup_app_screen(void);

// توابع اصلی ماژول
esp_err_t evm_lvgl_init(void);
esp_err_t evm_lvgl_register_js_mujs(js_State *J);
void evm_lvgl_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // EVM_MODULE_LVGL_H