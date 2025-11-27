#ifndef EVM_MODULE_TIMER_H
#define EVM_MODULE_TIMER_H

#include "esp_err.h"
#include "mujs.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif
// صفحه لانچر دائمی (همه جا در دسترس)

esp_err_t evm_timer_init(void);
esp_err_t evm_timer_register_js(js_State *J);  // اصلاح: void* → js_State*

#ifdef __cplusplus
}
#endif

#endif