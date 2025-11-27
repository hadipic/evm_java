#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#ifdef __cplusplus
extern "C" {
#endif

// ساختار برنامه EVM
typedef struct {
    char name[32];
    char path[128];
    char type[8];
    int size;
} evm_app_t;

// توابع اصلی
esp_err_t app_manager_init(void);
int app_manager_scan(const char *path);
esp_err_t app_manager_launch_evm_app(int index);
esp_err_t app_manager_start_ui(void);
esp_err_t app_manager_cleanup(void);
esp_err_t app_manager_refresh_ui(void);

// توابع اطلاعاتی
int app_manager_get_count(void);
const char* app_manager_get_name(int index);
int app_manager_get_selected_index(void);
bool app_manager_is_app_running(void);

// TaskHandle_t create_task_with_psram(TaskFunction_t func,
//                                     const char *name,
//                                     uint32_t stack_size,
//                                     UBaseType_t priority,
//                                     BaseType_t core);

// تابع بازسازی UI - حذف static
void rebuild_lvgl_ui(void);

#ifdef __cplusplus
}
#endif

#endif // APP_MANAGER_H