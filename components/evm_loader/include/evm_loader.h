#ifndef EVM_LOADER_H
#define EVM_LOADER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ساختار زمینه اجرای EVM
typedef struct {
    char app_name[64];
    char app_type[32];
    uint32_t start_time;
    uint32_t memory_usage;
} evm_execution_context_t;

// توابع عمومی
esp_err_t evm_loader_init(void);
esp_err_t evm_loader_deinit(void);
esp_err_t evm_loader_core_init(void);

// مدیریت برنامه‌ها
esp_err_t evm_launch_app(const char *app_path);
esp_err_t evm_stop_app(void);
esp_err_t evm_stop_execution(void);

// اجرای کد JavaScript
esp_err_t evm_execute_js(const char* js_code);
esp_err_t evm_load_and_execute(const char* file_path);

// وضعیت‌سنجی
bool evm_is_app_running(void);
bool evm_is_js_running(void);
bool evm_is_app_looping(void);
bool evm_should_continue_loop(void);
const char* evm_get_running_app_name(void);
const char* evm_get_running_app_type(void);
size_t evm_get_memory_usage(void);
const char* evm_get_version(void);

// کنترل برنامه
void evm_request_app_stop(void);
void evm_print_status(void);



// متغیرهای خارجی
extern volatile bool app_core_running;
extern volatile bool app_stop_requested;
extern volatile bool app_has_active_loop;
extern volatile uint32_t app_loop_counter;

#ifdef __cplusplus
}
#endif

#endif // EVM_LOADER_H