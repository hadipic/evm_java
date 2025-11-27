// evm_module_ftp.h
#ifndef EVM_MODULE_FTP_H
#define EVM_MODULE_FTP_H

#include "esp_err.h"
#include "mujs.h"

esp_err_t evm_ftp_init(void);
esp_err_t evm_ftp_register_js_mujs(js_State *J);
void evm_ftp_cleanup(void);
void evm_ftp_stop(void);

#endif