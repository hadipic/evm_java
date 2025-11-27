#ifndef EVM_MODULE_JSON_H
#define EVM_MODULE_JSON_H

#include "esp_err.h"
#include "mujs.h"

esp_err_t evm_json_init(void);
esp_err_t evm_json_register_js(js_State *J);

#endif