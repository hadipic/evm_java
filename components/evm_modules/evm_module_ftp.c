// evm_module_ftp.c - فقط کد FTP
#include "esp_log.h"
#include "evm_module_ftp.h"
#include "mujs.h"
#include "mongoose.h"
#include <string.h>

static const char *TAG = "evm_ftp";

static struct mg_mgr mgr;
static struct mg_connection *ftp_listener = NULL;
static bool ftp_running = false;
static int ftp_port = 2121;
static char ftp_root[128] = "/sdcard";

// هندلر رویدادهای Mongoose
static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    if (ev == MG_EV_ACCEPT) {
        ESP_LOGI(TAG, "FTP client connected from %s", mg_straddr(&c->rem, NULL, 0));
    } else if (ev == MG_EV_READ) {
        struct mg_str *msg = (struct mg_str *)ev_data;
        ESP_LOGI(TAG, "FTP received: %.*s", (int)msg->len, msg->ptr);
        
        // پاسخ ساده به کلاینت
        if (mg_strstr(*msg, mg_str("USER"))) {
            mg_printf(c, "331 User name okay, need password.\r\n");
        } else if (mg_strstr(*msg, mg_str("PASS"))) {
            mg_printf(c, "230 User logged in, proceed.\r\n");
        } else if (mg_strstr(*msg, mg_str("SYST"))) {
            mg_printf(c, "215 UNIX Type: L8\r\n");
        } else if (mg_strstr(*msg, mg_str("FEAT"))) {
            mg_printf(c, "211-Features:\r\n211 End\r\n");
        } else if (mg_strstr(*msg, mg_str("PWD"))) {
            mg_printf(c, "257 \"%s\" is current directory.\r\n", ftp_root);
        } else if (mg_strstr(*msg, mg_str("TYPE"))) {
            mg_printf(c, "200 Type set to I\r\n");
        } else if (mg_strstr(*msg, mg_str("PASV"))) {
            mg_printf(c, "227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n", 
                     (ftp_port >> 8) & 0xFF, ftp_port & 0xFF);
        } else if (mg_strstr(*msg, mg_str("LIST"))) {
            mg_printf(c, "150 Here comes the directory listing.\r\n");
            mg_printf(c, "226 Directory send OK.\r\n");
        } else if (mg_strstr(*msg, mg_str("QUIT"))) {
            mg_printf(c, "221 Goodbye.\r\n");
        } else {
            mg_printf(c, "502 Command not implemented.\r\n");
        }
    } else if (ev == MG_EV_CLOSE) {
        ESP_LOGI(TAG, "FTP client disconnected");
    }
}

// شروع FTP Server
static void start_ftp_server(void) {
    if (ftp_running) return;
    
    mg_mgr_init(&mgr);
    
    char url[64];
    snprintf(url, sizeof(url), "ftp://0.0.0.0:%d", ftp_port);
    
    ftp_listener = mg_listen(&mgr, url, fn, NULL);
    if (ftp_listener == NULL) {
        ESP_LOGE(TAG, "Failed to start FTP server on port %d", ftp_port);
        return;
    }
    
    ftp_running = true;
    ESP_LOGI(TAG, "FTP server started on port %d, root: %s", ftp_port, ftp_root);
    
    // اجرای سرور در یک تسک جداگانه
    while (ftp_running) {
        mg_mgr_poll(&mgr, 1000);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    mg_mgr_free(&mgr);
    ESP_LOGI(TAG, "FTP server stopped");
}

// تابع برای شروع FTP از JavaScript
static void js_ftp_start(js_State *J) {
    int port = 2121;
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        port = js_toint32(J, 1);
    }
    
    if (port < 1 || port > 65535) {
        js_error(J, "Invalid port number");
        return;
    }
    
    ftp_port = port;
    
    if (!ftp_running) {
        xTaskCreate((TaskFunction_t)start_ftp_server, "ftp_server", 8192, NULL, 5, NULL);
        js_pushboolean(J, true);
    } else {
        js_pushboolean(J, false);
    }
}

// تابع برای توقف FTP
static void js_ftp_stop(js_State *J) {
    if (ftp_running) {
        ftp_running = false;
        js_pushboolean(J, true);
    } else {
        js_pushboolean(J, false);
    }
}

// تابع برای تنظیم مسیر ریشه
static void js_ftp_set_root(js_State *J) {
    if (js_gettop(J) < 1 || !js_isstring(J, 1)) {
        js_error(J, "ftp.setRoot requires a path string");
        return;
    }
    
    const char *path = js_tostring(J, 1);
    strncpy(ftp_root, path, sizeof(ftp_root) - 1);
    ftp_root[sizeof(ftp_root) - 1] = '\0';
    
    ESP_LOGI(TAG, "FTP root set to: %s", ftp_root);
    js_pushundefined(J);
}

// تابع برای گرفتن وضعیت FTP
static void js_ftp_status(js_State *J) {
    js_newobject(J);
    
    js_pushboolean(J, ftp_running);
    js_setproperty(J, -2, "running");
    
    js_pushnumber(J, ftp_port);
    js_setproperty(J, -2, "port");
    
    js_pushstring(J, ftp_root);
    js_setproperty(J, -2, "root");
}

// ثبت ماژول در JavaScript
esp_err_t evm_ftp_init(void) {
    ESP_LOGI(TAG, "Initializing FTP module");
    ftp_running = false;
    strcpy(ftp_root, "/sdcard");
    return ESP_OK;
}

esp_err_t evm_ftp_register_js_mujs(js_State *J) {
    ESP_LOGI(TAG, "Registering FTP module in JavaScript");
    
    js_newobject(J);
    
    js_newcfunction(J, js_ftp_start, "start", 1);
    js_setproperty(J, -2, "start");
    
    js_newcfunction(J, js_ftp_stop, "stop", 0);
    js_setproperty(J, -2, "stop");
    
    js_newcfunction(J, js_ftp_set_root, "setRoot", 1);
    js_setproperty(J, -2, "setRoot");
    
    js_newcfunction(J, js_ftp_status, "status", 0);
    js_setproperty(J, -2, "status");
    
    js_setglobal(J, "FTP");
    
    ESP_LOGI(TAG, "✅ FTP module registered");
    return ESP_OK;
}

void evm_ftp_cleanup(void) {
    evm_ftp_stop();
}

void evm_ftp_stop(void) {
    ftp_running = false;
}