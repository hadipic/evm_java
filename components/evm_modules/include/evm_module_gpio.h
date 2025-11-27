#ifndef EVM_MODULE_GPIO_H
#define EVM_MODULE_GPIO_H

#include "esp_err.h"
#include "mujs.h"

esp_err_t evm_gpio_init(void);
esp_err_t evm_gpio_register_js(js_State *J);

#endif