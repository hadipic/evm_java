#include "http_server.h"
#include "mongoose.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"
#include "sys/stat.h"

static const char *TAG = "HTTPServer";

// ساختار برای مدیریت routeها
typedef struct http_route {
    char method[16];
    char uri_pattern[128];
    http_request_callback_t callback;
    struct http_route *next;
} http_route_t;

// ساختار برای مدیریت اتصال‌های WebSocket
typedef struct websocket_connection {
    struct mg_connection *conn;
    int id;
    struct websocket_connection *next;
} websocket_connection_t;

// متغیرهای global
static struct mg_mgr s_mgr;
static http_server_config_t s_config;
static bool s_running = false;
static http_route_t *s_routes = NULL;
static websocket_connection_t *s_ws_connections = NULL;
static int s_next_ws_id = 1;

// Callbackها
static http_websocket_callback_t s_websocket_callback = NULL;

// تابع برای پیدا کردن route منطبق
static http_route_t* find_matching_route(const char *method, const char *uri) {
    http_route_t *route = s_routes;
    while (route != NULL) {
        if (strcmp(route->method, method) == 0) {
            // تطبیق ساده URI (می‌توانید از regex استفاده کنید)
            if (strcmp(route->uri_pattern, uri) == 0 || 
                strcmp(route->uri_pattern, "*") == 0) {
                return route;
            }
        }
        route = route->next;
    }
    return NULL;
}

// تابع برای اضافه کردن connection WebSocket
static void add_websocket_connection(struct mg_connection *c) {
    websocket_connection_t *conn = malloc(sizeof(websocket_connection_t));
    if (!conn) return;
    
    conn->conn = c;
    conn->id = s_next_ws_id++;
    conn->next = s_ws_connections;
    s_ws_connections = conn;
    
    ESP_LOGI(TAG, "🔗 WebSocket connection added: %d", conn->id);
}

// تابع برای حذف connection WebSocket
static void remove_websocket_connection(struct mg_connection *c) {
    websocket_connection_t **prev = &s_ws_connections;
    websocket_connection_t *curr = s_ws_connections;
    
    while (curr != NULL) {
        if (curr->conn == c) {
            *prev = curr->next;
            ESP_LOGI(TAG, "🔌 WebSocket connection removed: %d", curr->id);
            free(curr);
            return;
        }
        prev = &curr->next;
        curr = curr->next;
    }
}

// تابع event handler برای HTTP Server
static void http_event_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        
        char method[16] = {0};
        char uri[256] = {0};
        char body[1024] = {0};
        
        snprintf(method, sizeof(method), "%.*s", (int)hm->method.len, hm->method.ptr);
        snprintf(uri, sizeof(uri), "%.*s", (int)hm->uri.len, hm->uri.ptr);
        
        if (hm->body.len > 0) {
            snprintf(body, sizeof(body) - 1, "%.*s", (int)hm->body.len, hm->body.ptr);
        }
        
        ESP_LOGI(TAG, "🌐 HTTP Request: %s %s", method, uri);
        
        // چک کردن upgrade به WebSocket
        if (s_config.enable_websocket && mg_http_match_uri(hm, "/ws")) {
            mg_ws_upgrade(c, hm, NULL);
            ESP_LOGI(TAG, "🔄 Upgraded to WebSocket: %s", uri);
            return;
        }
        
        // پیدا کردن route منطبق
        http_route_t *route = find_matching_route(method, uri);
        
        if (route != NULL && route->callback != NULL) {
            char response[4096] = {0};
            route->callback(method, uri, body, response, sizeof(response));
            
            if (strlen(response) > 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", response);
            } else {
                mg_http_reply(c, 200, "", "OK");
            }
        } else {
            // سرو کردن فایل‌های استاتیک
            char file_path[512] = {0};  // تعریف متغیر file_path
            
            if (strlen(s_config.web_root) + strlen(uri) < sizeof(file_path)) {
                snprintf(file_path, sizeof(file_path), "%s%s", s_config.web_root, uri);
            } else {
                // مدیریت خطا
                strncpy(file_path, s_config.web_root, sizeof(file_path) - 1);
                file_path[sizeof(file_path) - 1] = '\0';
            }
            
            struct mg_http_serve_opts opts = {.root_dir = s_config.web_root};
            mg_http_serve_dir(c, hm, &opts);
        }
    }
    else if (ev == MG_EV_WS_OPEN) {
        ESP_LOGI(TAG, "🔗 WebSocket connection opened");
        add_websocket_connection(c);
    }
    else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        char message[1024] = {0};
        
        snprintf(message, sizeof(message) - 1, "%.*s", (int)wm->data.len, wm->data.ptr);
        ESP_LOGI(TAG, "📨 WebSocket message: %s", message);
        
        if (s_websocket_callback != NULL) {
            s_websocket_callback(message, wm->data.len);
        }
        
        // پاسخ echo (اختیاری)
        mg_ws_send(c, message, strlen(message), WEBSOCKET_OP_TEXT);
    }
    else if (ev == MG_EV_CLOSE) {
        remove_websocket_connection(c);
    }
}

// تسک اصلی HTTP Server
static void http_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "🚀 HTTP Server Task Started on port %d", s_config.port);
    
    mg_mgr_init(&s_mgr);
    
    char url[32];
    snprintf(url, sizeof(url), "http://0.0.0.0:%d", s_config.port);
    
    struct mg_connection *c = mg_http_listen(&s_mgr, url, http_event_handler, NULL);
    if (c == NULL) {
        ESP_LOGE(TAG, "Failed to start HTTP server on port %d", s_config.port);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "✅ HTTP Server started successfully");
    ESP_LOGI(TAG, "📡 Server URL: %s", url);
    if (s_config.enable_websocket) {
        ESP_LOGI(TAG, "🔗 WebSocket enabled at: %s/ws", url);
    }
    if (s_config.web_root) {
        ESP_LOGI(TAG, "📁 Static files from: %s", s_config.web_root);
    }
    
    while (s_running) {
        mg_mgr_poll(&s_mgr, 100);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Cleanup
    mg_mgr_free(&s_mgr);
    
    // پاکسازی routeها
    http_route_t *route = s_routes;
    while (route != NULL) {
        http_route_t *next = route->next;
        free(route);
        route = next;
    }
    s_routes = NULL;
    
    // پاکسازی WebSocket connections
    websocket_connection_t *ws_conn = s_ws_connections;
    while (ws_conn != NULL) {
        websocket_connection_t *next = ws_conn->next;
        free(ws_conn);
        ws_conn = next;
    }
    s_ws_connections = NULL;
    
    ESP_LOGI(TAG, "🛑 HTTP Server Task Ended");
    vTaskDelete(NULL);
}

// ==================== توابع عمومی ====================

esp_err_t http_server_init(const http_server_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return ESP_FAIL;
    }
    
    // کپی کردن تنظیمات
    memcpy(&s_config, config, sizeof(http_server_config_t));
    
    ESP_LOGI(TAG, "HTTP Server initialized");
    ESP_LOGI(TAG, "  Port: %d", s_config.port);
    ESP_LOGI(TAG, "  Max connections: %d", s_config.max_connections);
    ESP_LOGI(TAG, "  WebSocket: %s", s_config.enable_websocket ? "enabled" : "disabled");
    ESP_LOGI(TAG, "  Web root: %s", s_config.web_root ? s_config.web_root : "none");
    
    return ESP_OK;
}

esp_err_t http_server_start(void) {
    if (s_running) {
        ESP_LOGW(TAG, "HTTP Server is already running");
        return ESP_OK;
    }
    
    s_running = true;
    
    if (xTaskCreate(http_server_task, "http_server", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create HTTP server task");
        s_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ HTTP Server started");
    return ESP_OK;
}

esp_err_t http_server_stop(void) {
    if (!s_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🛑 Stopping HTTP Server");
    s_running = false;
    
    // صبر کن تا تسک تمام شود
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    return ESP_OK;
}

bool http_server_is_running(void) {
    return s_running;
}

// ==================== توابع مدیریت Routeها ====================

esp_err_t http_server_add_route(const char *method, const char *uri_pattern, 
                               http_request_callback_t callback) {
    if (method == NULL || uri_pattern == NULL || callback == NULL) {
        return ESP_FAIL;
    }
    
    http_route_t *new_route = malloc(sizeof(http_route_t));
    if (!new_route) {
        ESP_LOGE(TAG, "Failed to allocate memory for route");
        return ESP_FAIL;
    }
    
    strncpy(new_route->method, method, sizeof(new_route->method) - 1);
    strncpy(new_route->uri_pattern, uri_pattern, sizeof(new_route->uri_pattern) - 1);
    new_route->callback = callback;
    new_route->next = s_routes;
    s_routes = new_route;
    
    ESP_LOGI(TAG, "➕ Added route: %s %s", method, uri_pattern);
    return ESP_OK;
}

esp_err_t http_server_remove_route(const char *method, const char *uri_pattern) {
    if (method == NULL || uri_pattern == NULL) {
        return ESP_FAIL;
    }
    
    http_route_t **prev = &s_routes;
    http_route_t *curr = s_routes;
    
    while (curr != NULL) {
        if (strcmp(curr->method, method) == 0 && 
            strcmp(curr->uri_pattern, uri_pattern) == 0) {
            *prev = curr->next;
            free(curr);
            ESP_LOGI(TAG, "➖ Removed route: %s %s", method, uri_pattern);
            return ESP_OK;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    
    ESP_LOGW(TAG, "Route not found: %s %s", method, uri_pattern);
    return ESP_FAIL;
}

// ==================== توابع WebSocket ====================

esp_err_t http_server_websocket_broadcast(const char *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_FAIL;
    }
    
    websocket_connection_t *conn = s_ws_connections;
    int sent_count = 0;
    
    while (conn != NULL) {
        if (conn->conn != NULL) {
            mg_ws_send(conn->conn, data, len, WEBSOCKET_OP_TEXT);
            sent_count++;
        }
        conn = conn->next;
    }
    
    if (sent_count > 0) {
        ESP_LOGI(TAG, "📤 WebSocket broadcast to %d clients", sent_count);
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "No WebSocket clients connected");
    return ESP_FAIL;
}

esp_err_t http_server_websocket_send(int connection_id, const char *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_FAIL;
    }
    
    websocket_connection_t *conn = s_ws_connections;
    
    while (conn != NULL) {
        if (conn->id == connection_id && conn->conn != NULL) {
            mg_ws_send(conn->conn, data, len, WEBSOCKET_OP_TEXT);
            ESP_LOGI(TAG, "📤 WebSocket sent to client %d", connection_id);
            return ESP_OK;
        }
        conn = conn->next;
    }
    
    ESP_LOGE(TAG, "WebSocket client %d not found", connection_id);
    return ESP_FAIL;
}

void http_server_set_websocket_callback(http_websocket_callback_t callback) {
    s_websocket_callback = callback;
}

// ==================== توابع فایل استاتیک ====================

esp_err_t http_server_serve_file(const char *path, const char *mime_type, 
                                char *buffer, size_t buffer_size) {
    // این تابع می‌تواند برای سرو فایل‌های استاتیک استفاده شود
    // در این پیاده‌سازی ساده، از mongoose built-in serving استفاده می‌کنیم
    return ESP_OK;
}

// ==================== توابع اطلاعات ====================

int http_server_get_port(void) {
    return s_config.port;
}

int http_server_get_connection_count(void) {
    int count = 0;
    websocket_connection_t *conn = s_ws_connections;
    
    while (conn != NULL) {
        count++;
        conn = conn->next;
    }
    
    return count;
}