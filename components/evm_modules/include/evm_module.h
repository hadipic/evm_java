#ifndef EVM_MODULE_H
#define EVM_MODULE_H

#include "esp_err.h"
#include "mujs.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t evm_module_init(void);
esp_err_t evm_module_register_js(js_State *J);  // اصلاح: void* → js_State*

#ifdef __cplusplus
}
#endif

#endif