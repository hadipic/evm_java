#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include <stdbool.h>
#include "mongoose_common.h"  // اضافه کردن این خط

#ifdef __cplusplus
extern "C" {
#endif





// انواع callbackها
typedef void (*http_request_callback_t)(const char *method, const char *uri, 
                                       const char *body, char *response, size_t response_size);
typedef void (*http_websocket_callback_t)(const char *data, size_t len);
typedef void (*http_file_callback_t)(const char *filename, const char *operation);

// تنظیمات HTTP Server
typedef struct {
    int port;
    int max_connections;
    bool enable_websocket;
    char web_root[128];      // مسیر ریشه وب
    char mount_point[16];    // نقطه mount برای فایل سیستم
    fs_type_t fs_type;       // نوع فایل سیستم
    bool enable_file_upload; // آپلود فایل
    bool enable_directory_listing; // لیست دایرکتوری
} http_server_config_t;

// توابع عمومی
esp_err_t http_server_init(const http_server_config_t *config);
esp_err_t http_server_start(void);
esp_err_t http_server_stop(void);
bool http_server_is_running(void);

// توابع مدیریت فایل سیستم
esp_err_t http_server_set_filesystem(fs_type_t fs_type, const char *mount_point);
esp_err_t http_server_set_web_root(const char *path);

// توابع مدیریت routeها
esp_err_t http_server_add_route(const char *method, const char *uri_pattern, 
                               http_request_callback_t callback);
esp_err_t http_server_remove_route(const char *method, const char *uri_pattern);

// توابع WebSocket
esp_err_t http_server_websocket_broadcast(const char *data, size_t len);
esp_err_t http_server_websocket_send(int connection_id, const char *data, size_t len);

// توابع فایل و دایرکتوری
esp_err_t http_server_list_directory(const char *path, char *buffer, size_t buffer_size);
esp_err_t http_server_get_file_info(const char *path, char *buffer, size_t buffer_size);
esp_err_t http_server_delete_file(const char *path);
esp_err_t http_server_rename_file(const char *old_path, const char *new_path);

// توابع اطلاعات
int http_server_get_port(void);
int http_server_get_connection_count(void);
fs_type_t http_server_get_filesystem_type(void);
const char* http_server_get_web_root(void);
esp_err_t http_server_get_storage_info(char *buffer, size_t buffer_size);

// تنظیم callbackها
void http_server_set_websocket_callback(http_websocket_callback_t callback);
void http_server_set_file_callback(http_file_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif