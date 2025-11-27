#ifndef EVM_MODULE_WIFI_H
#define EVM_MODULE_WIFI_H

#include "esp_err.h"
#include "mujs.h"

// توابع عمومی برای استفاده در لودر
esp_err_t system_wifi_init(void);
esp_err_t evm_module_wifi_register(js_State *J);

// توابع کمکی برای دسترسی از خارج
const char* evm_wifi_get_sta_ip(void);
const char* evm_wifi_get_ap_ip(void);


#endif

