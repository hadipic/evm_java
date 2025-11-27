
#include "evm_module_mongoose.h"
#include "evm_module.h"
#include "mongoose.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EVM_MONGOOSE";

static struct mg_mgr s_mgr;
static bool s_mgr_initialized = false;

// تابع callback عمومی برای mongoose events
static void mg_event_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    switch (ev) {
        case MG_EV_ERROR:
            ESP_LOGE(TAG, "Error: %s", (char*)ev_data);
            break;
        case MG_EV_HTTP_MSG:
            ESP_LOGI(TAG, "HTTP message received");
            break;
        case MG_EV_WS_MSG:
            ESP_LOGI(TAG, "WebSocket message received");
            break;
        case MG_EV_WS_OPEN:
            ESP_LOGI(TAG, "WebSocket connection opened");
            break;
        case MG_EV_CLOSE:
            ESP_LOGI(TAG, "Connection closed");
            break;
        default:
            ESP_LOGD(TAG, "Event: %d", ev);
            break;
    }
}

// تابع ایجاد HTTP سرور
static void js_mg_http_listen(js_State *J) {
    const char *url = js_tostring(J, 1);
    
    if (!s_mgr_initialized) {
        mg_mgr_init(&s_mgr);
        s_mgr_initialized = true;
        ESP_LOGI(TAG, "Mongoose manager initialized");
    }
    
    struct mg_connection *c = mg_http_listen(&s_mgr, url, mg_event_handler, NULL);
    if (c == NULL) {
        js_error(J, "Failed to create HTTP listener");
        return;
    }
    
    ESP_LOGI(TAG, "HTTP server listening on: %s", url);
    js_pushboolean(J, 1);
}

// تابع ایجاد HTTP client
static void js_mg_http_connect(js_State *J) {
    const char *url = js_tostring(J, 1);
    
    if (!s_mgr_initialized) {
        mg_mgr_init(&s_mgr);
        s_mgr_initialized = true;
        ESP_LOGI(TAG, "Mongoose manager initialized");
    }
    
    struct mg_connection *c = mg_http_connect(&s_mgr, url, mg_event_handler, NULL);
    if (c == NULL) {
        js_error(J, "Failed to create HTTP connection");
        return;
    }
    
    ESP_LOGI(TAG, "HTTP client connected to: %s", url);
    js_pushboolean(J, 1);
}

// تابع polling برای mongoose
static void js_mg_poll(js_State *J) {
    int timeout = js_toint32(J, 1);
    
    if (s_mgr_initialized) {
        mg_mgr_poll(&s_mgr, timeout);
    }
    
    js_pushundefined(J);
}

// تابع ارسال درخواست HTTP
static void js_mg_http_request(js_State *J) {
    const char *url = js_tostring(J, 1);
    const char *method = js_tostring(J, 2);
    const char *body = js_tostring(J, 3);
    
    if (!s_mgr_initialized) {
        js_error(J, "Mongoose not initialized");
        return;
    }
    
    // این یک پیاده‌سازی ساده است
    ESP_LOGI(TAG, "HTTP Request: %s %s", method, url);
    if (body) {
        ESP_LOGI(TAG, "Body: %s", body);
    }
    
    js_pushboolean(J, 1);
}

// تابع parse کردن HTTP message
static void js_mg_http_parse(js_State *J) {
    const char *data = js_tostring(J, 1);
    
    // پیاده‌سازی ساده پارسر
    if (strstr(data, "HTTP")) {
        js_newobject(J);
        js_pushstring(J, "GET");
        js_setproperty(J, -2, "method");
        js_pushstring(J, "/");
        js_setproperty(J, -2, "uri");
        js_pushstring(J, "");
        js_setproperty(J, -2, "body");
    } else {
        js_pushnull(J);
    }
}

// تابع ایجاد WebSocket connection
static void js_mg_ws_connect(js_State *J) {
    const char *url = js_tostring(J, 1);
    
    if (!s_mgr_initialized) {
        mg_mgr_init(&s_mgr);
        s_mgr_initialized = true;
        ESP_LOGI(TAG, "Mongoose manager initialized");
    }
    
    struct mg_connection *c = mg_ws_connect(&s_mgr, url, mg_event_handler, NULL, NULL);
    if (c == NULL) {
        js_error(J, "Failed to create WebSocket connection");
        return;
    }
    
    ESP_LOGI(TAG, "WebSocket connected to: %s", url);
    js_pushboolean(J, 1);
}

// تابع ارسال داده از طریق WebSocket
static void js_mg_ws_send(js_State *J) {
    const char *data = js_tostring(J, 1);
    int op = js_toint32(J, 2);
    
    ESP_LOGI(TAG, "WebSocket send: %s (op: %d)", data, op);
    js_pushboolean(J, 1);
}

// تابع بستن اتصال
static void js_mg_close(js_State *J) {
    if (s_mgr_initialized) {
        // در واقعیت باید connection خاصی بسته شود
        ESP_LOGI(TAG, "Connection closed");
    }
    
    js_pushboolean(J, 1);
}

// تابع مقداردهی اولیه ماژول
esp_err_t evm_mongoose_init(void) {
    ESP_LOGI(TAG, "Initializing Mongoose module");
    return ESP_OK;
}

// تابع ثبت ماژول در MuJS
esp_err_t evm_mongoose_register_js(js_State *J) {
    if (!J) return ESP_FAIL;
    
    ESP_LOGI(TAG, "Registering Mongoose module in JavaScript");
    
    // ایجاد object Net
    js_newobject(J);
    
    // HTTP functions
    js_newcfunction(J, js_mg_http_listen, "httpListen", 1);
    js_setproperty(J, -2, "httpListen");
    
    js_newcfunction(J, js_mg_http_connect, "httpConnect", 1);
    js_setproperty(J, -2, "httpConnect");
    
    js_newcfunction(J, js_mg_http_request, "httpRequest", 3);
    js_setproperty(J, -2, "httpRequest");
    
    js_newcfunction(J, js_mg_http_parse, "httpParse", 1);
    js_setproperty(J, -2, "httpParse");
    
    // WebSocket functions
    js_newcfunction(J, js_mg_ws_connect, "wsConnect", 1);
    js_setproperty(J, -2, "wsConnect");
    
    js_newcfunction(J, js_mg_ws_send, "wsSend", 2);
    js_setproperty(J, -2, "wsSend");
    
    // Utility functions
    js_newcfunction(J, js_mg_poll, "poll", 1);
    js_setproperty(J, -2, "poll");
    
    js_newcfunction(J, js_mg_close, "close", 0);
    js_setproperty(J, -2, "close");
    
    // Constants for event types
    js_pushnumber(J, MG_EV_ERROR);
    js_setproperty(J, -2, "EV_ERROR");
    
    js_pushnumber(J, MG_EV_HTTP_MSG);
    js_setproperty(J, -2, "EV_HTTP_MSG");
    
    js_pushnumber(J, MG_EV_WS_MSG);
    js_setproperty(J, -2, "EV_WS_MSG");
    
    js_pushnumber(J, MG_EV_WS_OPEN);
    js_setproperty(J, -2, "EV_WS_OPEN");
    
    js_pushnumber(J, MG_EV_CLOSE);
    js_setproperty(J, -2, "EV_CLOSE");
    
    // WebSocket opcodes
    js_pushnumber(J, WEBSOCKET_OP_TEXT);
    js_setproperty(J, -2, "WS_OP_TEXT");
    
    js_pushnumber(J, WEBSOCKET_OP_BINARY);
    js_setproperty(J, -2, "WS_OP_BINARY");
    
    // تنظیم به عنوان global
    js_setglobal(J, "Net");
    
    ESP_LOGI(TAG, "✅ Mongoose module registered");
    return ESP_OK;
}