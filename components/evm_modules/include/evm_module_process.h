#ifndef EVM_MODULE_PROCESS_H
#define EVM_MODULE_PROCESS_H

#include "esp_err.h"
#include "mujs.h"
#include "esp_timer.h"

esp_err_t evm_process_init(void);
esp_err_t evm_process_register_js(js_State *J);

#endif