#ifndef EVM_MODULE_MONGOOSE_H
#define EVM_MODULE_MONGOOSE_H

#include "esp_err.h"
#include "mujs.h"

esp_err_t evm_mongoose_init(void);
esp_err_t evm_mongoose_register_js(js_State *J);

#endif