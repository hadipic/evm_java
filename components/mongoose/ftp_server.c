#include "ftp_server.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"




#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "sys/stat.h"
#include "dirent.h"
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>



#pragma GCC diagnostic ignored "-Wformat-truncation"

static const char *TAG = "FTPServer";

#define FTP_CWD_SIZE 256
#define MIN_DATA_PORT 60000
#define MAX_DATA_PORT 61000
static uint16_t current_data_port = MIN_DATA_PORT;

// ساختار برای مدیریت وضعیت rename
typedef struct {
    char from_path[512];
    bool pending;
} rename_state_t;

static rename_state_t s_rename_state = {0};

typedef struct ftp_client {
    struct tcp_pcb *ctrl_pcb;
    struct tcp_pcb *data_listen_pcb;
    struct tcp_pcb *data_pcb;
    char current_dir[FTP_CWD_SIZE];
    char client_ip[16];
    bool authenticated;
    bool passive_mode;
    bool data_connected;
    bool waiting_for_data;
    bool marked_for_close;
    char pending_command[32];
    char pending_param[128];
    TickType_t data_timeout;
    long restart_offset;
    struct ftp_client *next;
} ftp_client_t;

static ftp_server_config_t s_config;
static bool s_running = false;
static ftp_client_t *s_clients = NULL;
static struct tcp_pcb *s_listen_pcb = NULL;
static TaskHandle_t s_ftp_task = NULL;

// ==================== توابع اصلی ====================

static err_t data_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) ;
static err_t data_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) ;


static void close_data_connection(ftp_client_t *client) {
    if (!client) {
        ESP_LOGW(TAG, "⚠️ close_data_connection called with NULL client");
        return;
    }
    
    // اگر قبلاً بسته شده، return
    if (!client->data_pcb && !client->data_listen_pcb) {
        return;
    }
    
    bool had_data_pcb = (client->data_pcb != NULL);
    bool had_listen_pcb = (client->data_listen_pcb != NULL);
    
    if (had_data_pcb || had_listen_pcb) {
        ESP_LOGI(TAG, "🔌 Closing data connection for: %s", client->client_ip);
    }
    
    // مدیریت اتصال داده فعال
    if (client->data_pcb) {
        // ذخیره PCB موقت
        struct tcp_pcb *data_pcb = client->data_pcb;
        client->data_pcb = NULL;
        
        // غیرفعال کردن callbackها
        tcp_arg(data_pcb, NULL);
        tcp_recv(data_pcb, NULL);
        tcp_sent(data_pcb, NULL);
        tcp_err(data_pcb, NULL);
        
        // بستن ایمن
        if (tcp_close(data_pcb) != ERR_OK) {
            tcp_abort(data_pcb);
        }
    }
    
    // مدیریت listening socket
    if (client->data_listen_pcb) {
        // ذخیره PCB موقت
        struct tcp_pcb *listen_pcb = client->data_listen_pcb;
        client->data_listen_pcb = NULL;
        
        tcp_arg(listen_pcb, NULL);
        tcp_accept(listen_pcb, NULL);
        tcp_close(listen_pcb);
    }
    
    client->data_connected = false;
    client->waiting_for_data = false;
    
    if (had_data_pcb || had_listen_pcb) {
        ESP_LOGI(TAG, "✅ Data connection fully closed");
    }
}


static void send_response_direct(struct tcp_pcb *pcb, int code, const char *message) {
    if (!pcb) return;
    
    // بررسی وضعیت اتصال به روش صحیح
    if (pcb->state != ESTABLISHED) {
        ESP_LOGW(TAG, "Cannot send response - connection not established");
        return;
    }
    
    char response[512];
    int n = snprintf(response, sizeof(response), "%d %s\r\n", code, message);
    if (n > 0 && n < (int)sizeof(response)) {
        err_t err = tcp_write(pcb, response, n, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            tcp_output(pcb);
            ESP_LOGI(TAG, "RESP: %d %s", code, message);
        } else {
            ESP_LOGE(TAG, "Failed to send response: %d", err);
        }
    }
}


static ftp_client_t* find_client(struct tcp_pcb *pcb) {
    ftp_client_t *client = s_clients;
    while (client) {
        if (client->ctrl_pcb == pcb) return client;
        client = client->next;
    }
    return NULL;
}

static ftp_client_t* add_client(struct tcp_pcb *pcb, const char *client_ip) {
    ftp_client_t *client = calloc(1, sizeof(ftp_client_t));
    if (!client) return NULL;
    
    client->ctrl_pcb = pcb;
    if (client_ip) strncpy(client->client_ip, client_ip, 15);
    strcpy(client->current_dir, "/");
    client->marked_for_close = false;
    client->restart_offset = 0;
    client->next = s_clients;
    s_clients = client;
    
    ESP_LOGI(TAG, "➕ Client connected: %s", client->client_ip);
    return client;
}

// 🔴 اضافه کردن تابع ctrl_error_callback که گم شده بود
static void ctrl_error_callback(void *arg, err_t err) {
    ftp_client_t *client = (ftp_client_t*)arg;
    if (client) {
        ESP_LOGE(TAG, "Control connection error: %d", err);
        client->marked_for_close = true;
    }
}

static void remove_client(struct tcp_pcb *pcb) {
    if (!pcb) return;
    
    ftp_client_t **prev = &s_clients;
    ftp_client_t *curr = s_clients;
    
    while (curr) {
        if (curr->ctrl_pcb == pcb) {
            *prev = curr->next;
            ESP_LOGI(TAG, "➖ Client disconnected: %s", curr->client_ip);
            
            // FIRST: علامت گذاری
            curr->marked_for_close = true;
            
            // SECOND: بستن اتصالات داده
            close_data_connection(curr);
            
            // THIRD: غیرفعال کردن callbackهای کنترل
            if (curr->ctrl_pcb) {
                tcp_arg(curr->ctrl_pcb, NULL);
                tcp_recv(curr->ctrl_pcb, NULL);
                tcp_err(curr->ctrl_pcb, NULL);
            }
            
            // 🔥 FOURTH: تاخیر بیشتر برای اطمینان از پایان انتقال
            vTaskDelay(200);
            
            // FIFTH: حالا free کنید
            free(curr);
            return;
        }
        prev = &curr->next;
        curr = curr->next;
    }
}


// ==================== توابع کمکی ====================

static void get_local_ip(char *ip_buffer, size_t buffer_size) {
    esp_netif_ip_info_t ip_info;
    
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(ip_buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
        return;
    } 
    
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        snprintf(ip_buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
        return;
    }
    
    strncpy(ip_buffer, "192.168.4.1", buffer_size);
}

static bool check_authentication(ftp_client_t *client, struct tcp_pcb *pcb) {
    if (!client || !client->authenticated) {
        if (pcb) send_response_direct(pcb, 530, "Not logged in");
        return false;
    }
    return true;
}

static bool is_directory_empty(const char *path) {
    ESP_LOGI(TAG, "Checking if directory is empty: %s", path);
    
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open directory: %s", path);
        return true;
    }
    
    bool empty = true;
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL && count < 10) {
        count++;
        
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        empty = false;
        break;
    }
    
    closedir(dir);
    
    ESP_LOGI(TAG, "Directory %s is %s (checked %d entries)", 
             path, empty ? "EMPTY" : "NOT EMPTY", count);
    return empty;
}

static void normalize_path(char *path) {
    if (!path || strlen(path) == 0) return;
    
    char *src = path;
    char *dst = path;
    
    while (*src) {
        if (*src == '/' && *(src + 1) == '/') {
            src++;
        } else if (*src == '/' && *(src + 1) == '.' && *(src + 2) == '.' && 
                   (*(src + 3) == '/' || *(src + 3) == '\0')) {
            if (dst > path) {
                dst--;
                while (dst > path && *dst != '/') dst--;
            }
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    if (path[0] != '/') {
        memmove(path + 1, path, strlen(path) + 1);
        path[0] = '/';
    }
    
    if (strlen(path) == 0) {
        strcpy(path, "/");
    }
}

static bool build_full_path(char *full_path, size_t full_path_size, ftp_client_t *client, const char *filename) {
    if (!full_path || !client || !filename) {
        ESP_LOGE(TAG, "Invalid parameters for build_full_path");
        return false;
    }
    
    memset(full_path, 0, full_path_size);
    
    if (filename[0] == '/') {
        snprintf(full_path, full_path_size, "%s%s", s_config.root_dir, filename);
    } else {
        if (strcmp(client->current_dir, "/") == 0) {
            snprintf(full_path, full_path_size, "%s/%s", s_config.root_dir, filename);
        } else {
            snprintf(full_path, full_path_size, "%s%s/%s", s_config.root_dir, client->current_dir, filename);
        }
    }
    
    normalize_path(full_path);
    
    if (strncmp(full_path, s_config.root_dir, strlen(s_config.root_dir)) != 0) {
        ESP_LOGE(TAG, "Invalid path: %s", full_path);
        return false;
    }
    
    ESP_LOGI(TAG, "Built full path: %s", full_path);
    return true;
}

// ==================== توابع مدیریت فایل سیستم ====================

static void handle_list_command(ftp_client_t *client) {
    ESP_LOGI(TAG, "🚀 START handle_list_command =====================");
    
    if (!client || !client->data_connected || !client->data_pcb) {
        ESP_LOGE(TAG, "❌ No data connection");
        send_response_direct(client->ctrl_pcb, 425, "No data connection");
        return;
    }

    char base_path[256];
    if (strcmp(client->current_dir, "/") == 0) {
        strncpy(base_path, s_config.root_dir, sizeof(base_path) - 1);
    } else {
        snprintf(base_path, sizeof(base_path), "%s%s", s_config.root_dir, client->current_dir);
    }

    DIR *dir = opendir(base_path);
    if (!dir) {
        ESP_LOGE(TAG, "❌ Cannot open directory: %s", base_path);
        send_response_direct(client->ctrl_pcb, 550, "Failed to open directory");
        close_data_connection(client);
        return;
    }

    struct dirent *entry;
    char line[128];
    char full_path[384];
    int file_count = 0;
    bool transfer_failed = false;

    const char *dot_entries = 
        "drwxr-xr-x 1 ftp ftp 4096 Jan 1 2020 .\r\n"
        "drwxr-xr-x 1 ftp ftp 4096 Jan 1 2020 ..\r\n";
    
    if (tcp_write(client->data_pcb, dot_entries, strlen(dot_entries), TCP_WRITE_FLAG_COPY) != ERR_OK) {
        ESP_LOGE(TAG, "❌ Failed to send . and ..");
        transfer_failed = true;
    } else {
        tcp_output(client->data_pcb);
       // vTaskDelay(15);
    }

    while (!transfer_failed && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char file_type = '?';
        long file_size = 4096;
        const char *time_str = "Jan 1 2020";

        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);
        
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                file_type = 'd';
                file_size = 4096;
            } else if (S_ISREG(file_stat.st_mode)) {
                file_type = '-';
                file_size = file_stat.st_size;
            } else {
                file_type = '-';
                file_size = 1024;
            }
            
            struct tm *timeinfo = localtime(&file_stat.st_mtime);
            if (timeinfo != NULL) {
                char time_buffer[32];
                if (strftime(time_buffer, sizeof(time_buffer), "%b %d %H:%M", timeinfo) > 0) {
                    time_str = time_buffer;
                }
            }
        } else {
            if (entry->d_type == DT_DIR) {
                file_type = 'd';
                file_size = 4096;
            } else if (entry->d_type == DT_REG) {
                file_type = '-';
                file_size = 1024;
            } else {
                file_type = '-';
                file_size = 1024;
            }
        }

        int line_len = snprintf(line, sizeof(line), 
                               "%crwxr-xr-x 1 ftp ftp %8ld %s %s\r\n",
                               file_type, file_size, time_str, entry->d_name);
        
        if (line_len > 0 && line_len < (int)sizeof(line)) {
            if (tcp_sndbuf(client->data_pcb) < line_len) {
                tcp_output(client->data_pcb);
              //  vTaskDelay(20);
            }
            
            if (tcp_write(client->data_pcb, line, line_len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
                ESP_LOGE(TAG, "❌ TCP write failed");
                transfer_failed = true;
                break;
            }
            file_count++;

            if (file_count % 8 == 0) {
                tcp_output(client->data_pcb);
                //vTaskDelay(15);
            }
        }

      //  vTaskDelay(2);
    }

    closedir(dir);

    if (!transfer_failed) {
        tcp_output(client->data_pcb);
        vTaskDelay(40);
        
        ESP_LOGI(TAG, "✅ LIST completed: %d files", file_count);
        send_response_direct(client->ctrl_pcb, 226, "Directory send OK");
    } else {
        ESP_LOGE(TAG, "❌ LIST failed after %d files", file_count);
        send_response_direct(client->ctrl_pcb, 426, "Transfer failed");
    }

   // vTaskDelay(80);
    close_data_connection(client);
    ESP_LOGI(TAG, "🏁 END handle_list_command =====================\n");
}

// 🔴 تابع handle_size_command که استفاده می‌شود
static void handle_size_command(ftp_client_t *client, const char *filename) {
    if (!check_authentication(client, client->ctrl_pcb)) {
        return;
    }
    
    ESP_LOGI(TAG, "SIZE command: %s", filename);
    
    char full_path[384];
    if (!build_full_path(full_path, sizeof(full_path), client, filename)) {
        ESP_LOGE(TAG, "❌ Invalid path for SIZE: %s", filename);
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        return;
    }
    
    struct stat file_stat;
    if (stat(full_path, &file_stat) == 0) {
        if (S_ISREG(file_stat.st_mode)) {
            char response[32];
            snprintf(response, sizeof(response), "%lld", (long long)file_stat.st_size);
            send_response_direct(client->ctrl_pcb, 213, response);
            ESP_LOGI(TAG, "✅ SIZE response: %s = %lld bytes", filename, (long long)file_stat.st_size);
        } else {
            ESP_LOGE(TAG, "❌ Not a regular file: %s", filename);
            send_response_direct(client->ctrl_pcb, 550, "Not a regular file");
        }
    } else {
        ESP_LOGE(TAG, "❌ File not found for SIZE: %s", filename);
        send_response_direct(client->ctrl_pcb, 550, "File not found");
    }
}


// 🔧 این ساختار جدید را اضافه کنید
typedef struct {
    FILE *file;
    ftp_client_t *client;
    size_t total_sent;
    bool transfer_complete;
    bool transfer_failed;
    uint8_t buffer[1460]; // MTU size
} transfer_context_t;

static void continue_file_transfer(transfer_context_t *ctx);

// 🔧 تابع callback برای ارسال موفق
static err_t data_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    ftp_client_t *client = (ftp_client_t*)arg;
    
    // 🔥 بررسی سلامت client
    if (!client || client->marked_for_close) {
        return ERR_OK;
    }
       tcp_output(client->data_pcb);
    
    ESP_LOGD(TAG, "📨 Data sent: %u bytes for %s", len, client->client_ip);
    return ERR_OK;
}



// 🔧 تابع ادامه ارسال
static void continue_file_transfer(transfer_context_t *ctx) {
    if (!ctx || !ctx->file || !ctx->client || !ctx->client->data_pcb) {
        return;
    }

    size_t bytes_read = fread(ctx->buffer, 1, sizeof(ctx->buffer), ctx->file);
    
    if (bytes_read > 0) {
        int available = tcp_sndbuf(ctx->client->data_pcb);
        
        if (available >= bytes_read) {
            err_t result = tcp_write(ctx->client->data_pcb, ctx->buffer, bytes_read, TCP_WRITE_FLAG_COPY);
            
            if (result == ERR_OK) {
                ctx->total_sent += bytes_read;
                
                // گزارش پیشرفت
                if (ctx->total_sent % (50 * 1024) == 0) {
                    ESP_LOGI(TAG, "📤 Progress: %.1f KB", ctx->total_sent / 1024.0);
                }
            } else {
                ESP_LOGE(TAG, "❌ Write failed: %d", result);
                ctx->transfer_failed = true;
            }
        } else {
            // بافر پر است - منتظر callback بعدی می‌مانیم
            ESP_LOGW(TAG, "⚠️ Buffer full (%d < %d), waiting for sent callback", available, bytes_read);
            // فایل موقعیت را عقب می‌بریم
            fseek(ctx->file, -bytes_read, SEEK_CUR);
        }
    } else {
        // پایان فایل
        ctx->transfer_complete = true;
        ESP_LOGI(TAG, "✅ File transfer completed: %u bytes", ctx->total_sent);
        
        // ارسال پاسخ نهایی
        send_response_direct(ctx->client->ctrl_pcb, 226, "Transfer complete");
        close_data_connection(ctx->client);
        
        // پاکسازی
        fclose(ctx->file);
        free(ctx);
    }
}


static err_t data_sent_callback_enhanced(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    transfer_context_t *ctx = (transfer_context_t*)arg;
    if (!ctx || !ctx->client) return ERR_OK;
    
    ESP_LOGD(TAG, "📨 Data sent: %u bytes, total: %u", len, ctx->total_sent);
    
    // ادامه ارسال داده‌های بعدی
    if (!ctx->transfer_complete && !ctx->transfer_failed) {
        continue_file_transfer(ctx);
    }
    
    return ERR_OK;
}

// 🔧 تابع اصلی انتقال با callback
static void handle_retr_command_callback_based(ftp_client_t *client, const char *filename) {
    ESP_LOGI(TAG, "📥 RETR file (CALLBACK): %s", filename);
    
    char full_path[256];
    if (!build_full_path(full_path, sizeof(full_path), client, filename)) {
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        close_data_connection(client);
        return;
    }
    
    FILE *file = fopen(full_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "❌ Cannot open file: %s", full_path);
        send_response_direct(client->ctrl_pcb, 550, "File not found");
        close_data_connection(client);
        return;
    }
    
    // مدیریت REST
    if (client->restart_offset > 0) {
        if (fseek(file, client->restart_offset, SEEK_SET) != 0) {
            ESP_LOGW(TAG, "⚠️ Seek failed, starting from beginning");
        }
        client->restart_offset = 0;
    }
    
    // ایجاد context انتقال
    transfer_context_t *ctx = calloc(1, sizeof(transfer_context_t));
    if (!ctx) {
        fclose(file);
        send_response_direct(client->ctrl_pcb, 451, "Local resource error");
        close_data_connection(client);
        return;
    }
    
    ctx->file = file;
    ctx->client = client;
    ctx->total_sent = 0;
    ctx->transfer_complete = false;
    ctx->transfer_failed = false;
    
    // تنظیم callback
    tcp_sent(client->data_pcb, data_sent_callback_enhanced);
    tcp_arg(client->data_pcb, ctx);
    
    send_response_direct(client->ctrl_pcb, 150, "Starting binary data transfer");
    
    // شروع انتقال
    continue_file_transfer(ctx);
}



static void handle_retr_command(ftp_client_t *client, const char *filename) {
    // به جای تابع قدیمی، از تابع جدید استفاده کنید
    handle_retr_command_callback_based(client, filename);
   // handle_retr_command_smart_chunks(client, filename);
}

// 🔧 ساختار برای دریافت فایل (STOR)
typedef struct {
    FILE *file;
    ftp_client_t *client;
    size_t total_received;
    bool transfer_complete;
    bool transfer_failed;
    uint8_t buffer[1460];
    bool context_active;
    volatile bool in_use;
} transfer_context_receive_t;


static err_t stor_data_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    transfer_context_receive_t *ctx = (transfer_context_receive_t*)arg;
    
    if (!ctx || !ctx->context_active || ctx->in_use) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    
    ctx->in_use = true;
    
    // بررسی سلامت
    if (!ctx->client || ctx->client->marked_for_close || !ctx->file) {
        // پاکسازی مستقیم
        if (ctx->file) fclose(ctx->file);
        if (ctx->client && ctx->client->data_pcb) {
            tcp_recv(ctx->client->data_pcb, NULL);
            tcp_arg(ctx->client->data_pcb, NULL);
        }
        free(ctx);
        if (p) pbuf_free(p);
        ctx->in_use = false;
        return ERR_OK;
    }
    
    if (p == NULL) {
        // اتصال بسته شده - انتقال کامل
        ESP_LOGI(TAG, "✅ STOR transfer completed: %.2f KB", ctx->total_received / 1024.0);
        
        // پاکسازی مستقیم
        if (ctx->file) fclose(ctx->file);
        send_response_direct(ctx->client->ctrl_pcb, 226, "Transfer complete");
        close_data_connection(ctx->client);
        free(ctx);
        
        ctx->in_use = false;
        return ERR_OK;
    }
    
    if (err == ERR_OK && p != NULL) {
        // نوشتن داده در فایل
        size_t bytes_written = fwrite(p->payload, 1, p->len, ctx->file);
        if (bytes_written == p->len) {
            ctx->total_received += p->len;
            
            // گزارش پیشرفت
            if (ctx->total_received % (100 * 1024) == 0) {
                ESP_LOGI(TAG, "📥 STOR Progress: %.2f KB", ctx->total_received / 1024.0);
            }
            
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            
            // فورس کردن نوشتن روی دیسک
            fflush(ctx->file);
        } else {
            ESP_LOGE(TAG, "❌ File write error");
            
            // پاکسازی مستقیم در صورت خطا
            if (ctx->file) fclose(ctx->file);
            send_response_direct(ctx->client->ctrl_pcb, 451, "Local error in processing");
            close_data_connection(ctx->client);
            pbuf_free(p);
            free(ctx);
        }
    } else {
        ESP_LOGE(TAG, "❌ STOR receive error: %d", err);
        
        // پاکسازی مستقیم در صورت خطا
        if (ctx->file) fclose(ctx->file);
        send_response_direct(ctx->client->ctrl_pcb, 426, "Transfer failed");
        close_data_connection(ctx->client);
        if (p) pbuf_free(p);
        free(ctx);
    }
    
    ctx->in_use = false;
    return ERR_OK;
}


static void handle_stor_command(ftp_client_t *client, const char *filename) {
    if (!client) {
        ESP_LOGE(TAG, "❌ No client for STOR");
        return;
    }
    
    if (!client->data_connected || !client->data_pcb) {
        ESP_LOGE(TAG, "❌ No data connection for STOR");
        send_response_direct(client->ctrl_pcb, 425, "No data connection");
        return;
    }
    
    ESP_LOGI(TAG, "📤 STOR file: %s", filename);
    
    char full_path[512];
    if (!build_full_path(full_path, sizeof(full_path), client, filename)) {
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        close_data_connection(client);
        return;
    }
    
    // ایجاد پوشه والد اگر وجود ندارد
    char *last_slash = strrchr(full_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        struct stat st;
        if (stat(full_path, &st) != 0) {
            if (mkdir(full_path, 0755) != 0) {
                ESP_LOGE(TAG, "❌ Failed to create parent directory: %s", full_path);
                send_response_direct(client->ctrl_pcb, 550, "Cannot create parent directory");
                close_data_connection(client);
                return;
            }
        }
        *last_slash = '/';
    }
    
    ESP_LOGI(TAG, "Creating file: %s", full_path);
    
    // باز کردن فایل برای نوشتن
    FILE *file = fopen(full_path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "❌ Cannot create file: %s", full_path);
        send_response_direct(client->ctrl_pcb, 550, "Cannot create file");
        close_data_connection(client);
        return;
    }
    
    // مدیریت REST offset
    if (client->restart_offset > 0) {
        if (fseek(file, client->restart_offset, SEEK_SET) != 0) {
            ESP_LOGW(TAG, "⚠️ Seek failed, starting from beginning");
            client->restart_offset = 0;
        }
    }
    
    // ایجاد context دریافت مستقیم
    transfer_context_receive_t *ctx = calloc(1, sizeof(transfer_context_receive_t));
    if (!ctx) {
        fclose(file);
        send_response_direct(client->ctrl_pcb, 451, "Local resource error");
        close_data_connection(client);
        return;
    }
    
    ctx->file = file;
    ctx->client = client;
    ctx->total_received = 0;
    ctx->transfer_complete = false;
    ctx->transfer_failed = false;
    ctx->context_active = true;
    ctx->in_use = false;
    
    // تنظیم callback برای دریافت داده
    tcp_recv(client->data_pcb, stor_data_recv_callback);
    tcp_arg(client->data_pcb, ctx);
    
    ESP_LOGI(TAG, "✅ Ready to receive data for: %s", full_path);
}



static void handle_mkd_command(ftp_client_t *client, const char *dirname) {
    if (!check_authentication(client, client->ctrl_pcb)) {
        return;
    }
    
    ESP_LOGI(TAG, "MKD command: %s", dirname);
    
    char full_path[512];
    if (!build_full_path(full_path, sizeof(full_path), client, dirname)) {
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        return;
    }
    
    if (mkdir(full_path, 0755) == 0) {
        char response[128];
        snprintf(response, sizeof(response), "\"%s\" created", dirname);
        send_response_direct(client->ctrl_pcb, 257, response);
        ESP_LOGI(TAG, "✅ Directory created: %s", full_path);
    } else {
        ESP_LOGE(TAG, "❌ Failed to create directory: %s (errno: %d)", full_path, errno);
        send_response_direct(client->ctrl_pcb, 550, "Create directory operation failed");
    }
}



static void handle_dele_command(ftp_client_t *client, const char *filename) {
    if (!check_authentication(client, client->ctrl_pcb)) {
        return;
    }
    
    ESP_LOGI(TAG, "DELE command: %s", filename);
    
    char full_path[512];
    if (!build_full_path(full_path, sizeof(full_path), client, filename)) {
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        return;
    }
    
    if (remove(full_path) == 0) {
        ESP_LOGI(TAG, "✅ File deleted: %s", full_path);
        send_response_direct(client->ctrl_pcb, 250, "Delete operation successful");
    } else {
        ESP_LOGE(TAG, "❌ Failed to delete file: %s (errno: %d)", full_path, errno);
        send_response_direct(client->ctrl_pcb, 550, "Delete operation failed");
    }
}

static void handle_rmd_command(ftp_client_t *client, const char *dirname) {
    if (!check_authentication(client, client->ctrl_pcb)) {
        return;
    }
    
    ESP_LOGI(TAG, "RMD command: %s", dirname);
    
    char full_path[512];
    if (!build_full_path(full_path, sizeof(full_path), client, dirname)) {
        send_response_direct(client->ctrl_pcb, 550, "Invalid path");
        return;
    }
    
    ESP_LOGI(TAG, "Attempting to remove directory: %s", full_path);
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "Directory not found: %s", full_path);
        send_response_direct(client->ctrl_pcb, 550, "Directory not found");
        return;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "Not a directory: %s", full_path);
        send_response_direct(client->ctrl_pcb, 550, "Not a directory");
        return;
    }
    
    ESP_LOGI(TAG, "Checking if directory is empty...");
    
    if (!is_directory_empty(full_path)) {
        ESP_LOGE(TAG, "Directory is not empty: %s", full_path);
        send_response_direct(client->ctrl_pcb, 550, "Directory not empty");
        return;
    }
    
    ESP_LOGI(TAG, "Directory is empty, attempting to remove...");
    
    if (rmdir(full_path) == 0) {
        ESP_LOGI(TAG, "✅ Directory removed successfully: %s", full_path);
        send_response_direct(client->ctrl_pcb, 250, "Remove directory operation successful");
    } else {
        ESP_LOGE(TAG, "❌ Failed to remove directory: %s (errno: %d)", full_path, errno);
        send_response_direct(client->ctrl_pcb, 550, "Remove directory operation failed");
    }
}




// ==================== CALLBACKS ====================



static err_t data_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    ftp_client_t *client = (ftp_client_t*)arg;
    
    // 🔥 FIRST: بررسی سلامت client
    if (!client || client->marked_for_close) {
        ESP_LOGI(TAG, "📪 Data connection closed (invalid or marked client)");
        if (p) pbuf_free(p);
        if (tpcb) tcp_abort(tpcb);
        return ERR_OK;
    }
    
    if (p == NULL) {
        ESP_LOGI(TAG, "📪 Data connection closed by client: %s", client->client_ip);
        close_data_connection(client);
        return ERR_OK;
    }
    
    if (err == ERR_OK && p != NULL) {
        ESP_LOGW(TAG, "⚠️ Received %u bytes on data connection", p->tot_len);
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
    }
    
    return ERR_OK;
}


static void data_error_callback(void *arg, err_t err) {
    ftp_client_t *client = (ftp_client_t*)arg;
    
    // 🔥 بررسی سلامت client
    if (!client || client->marked_for_close) {
        return;
    }
    
    ESP_LOGE(TAG, "❌ Data connection error for %s: %d", client->client_ip, err);
    close_data_connection(client);
}



// 🔧 این تابع data_accept_callback بهبود یافته را جایگزین کن
static err_t data_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) {
        ESP_LOGE(TAG, "Data accept failed: %d", err);
        return ERR_VAL;
    }

    ftp_client_t *client = (ftp_client_t*)arg;
    if (!client) {
        ESP_LOGE(TAG, "No client context for data connection");
        tcp_abort(newpcb);
        return ERR_VAL;
    }
    
    // در ftp_server.c خطوط 990-994 و 1426-1430 را اصلاح کنید:

  char ip_str[16];
ip4addr_ntoa_r(ip_2_ip4(&newpcb->remote_ip), ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "✅ Data connection accepted from %s:%d", ip_str, newpcb->remote_port);

    // بستن اتصال قبلی اگر وجود دارد
    if (client->data_pcb) {
        close_data_connection(client);
    }
    
    client->data_pcb = newpcb;
    client->data_connected = true;
    client->passive_mode = true;
    client->waiting_for_data = false;
    client->data_timeout = xTaskGetTickCount();
    
    // تنظیمات TCP - بسیار مهم!
    tcp_arg(newpcb, client);
    tcp_recv(newpcb, data_recv_callback);
    tcp_sent(newpcb, data_sent_callback);
    tcp_err(newpcb, data_error_callback);
    
    // بهینه‌سازی‌ها
    tcp_nagle_disable(newpcb);
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    
    // این خط حیاتی است!
    tcp_accepted(client->data_listen_pcb);
    
    ESP_LOGI(TAG, "🔧 Data connection fully configured");
    
    // اجرای دستور معلق
    if (strlen(client->pending_command) > 0) {
        ESP_LOGI(TAG, "🚀 Executing pending command: %s %s", 
                client->pending_command, client->pending_param);
        
        if (strcmp(client->pending_command, "LIST") == 0 || strcmp(client->pending_command, "NLST") == 0) {
            handle_list_command(client);
        }
        else if (strcmp(client->pending_command, "RETR") == 0) {
            handle_retr_command(client, client->pending_param);
        }
        else if (strcmp(client->pending_command, "STOR") == 0) {
            handle_stor_command(client, client->pending_param);
        }
        
        // پاکسازی دستور معلق
        memset(client->pending_command, 0, sizeof(client->pending_command));
        memset(client->pending_param, 0, sizeof(client->pending_param));
    }
    
    return ERR_OK;
}



static err_t ctrl_recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (err != ERR_OK || p == NULL) {
        ftp_client_t *client = find_client(pcb);
        if (client) client->marked_for_close = true;
        return ERR_OK;
    }

    ftp_client_t *client = (ftp_client_t*)arg;
    if (!client) {
        pbuf_free(p);
        return ERR_OK;
    }
    
    char buffer[256];
    size_t len = p->tot_len < sizeof(buffer) - 1 ? p->tot_len : sizeof(buffer) - 1;
    pbuf_copy_partial(p, buffer, len, 0);
    buffer[len] = '\0';
    
    // پردازش دستور
    char *cmd = buffer;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    
    char *newline = strchr(cmd, '\r');
    if (newline) *newline = '\0';
    newline = strchr(cmd, '\n');
    if (newline) *newline = '\0';
    
    if (strlen(cmd) > 0) {
        char command[32] = {0};
        char param[128] = {0};
        sscanf(cmd, "%31s %127[^\n]", command, param);
        
        // تبدیل به uppercase
        for (char *c = command; *c; c++) *c = toupper((unsigned char)*c);
        
        ESP_LOGI(TAG, "CMD: %s %s", command, param);
        
        // دستوراتی که نیاز به احراز هویت ندارند
        if (strcmp(command, "USER") == 0) {
            send_response_direct(pcb, 331, "User name okay, need password");
        }
        else if (strcmp(command, "PASS") == 0) {
            client->authenticated = true;
            send_response_direct(pcb, 230, "User logged in, proceed");
        }
        else if (strcmp(command, "QUIT") == 0) {
            send_response_direct(pcb, 221, "Goodbye");
            client->marked_for_close = true;
        }
        else if (strcmp(command, "SYST") == 0) {
            send_response_direct(pcb, 215, "UNIX Type: L8");
        }
        else if (strcmp(command, "NOOP") == 0) {
            send_response_direct(pcb, 200, "NOOP ok");
        }
        else if (strcmp(command, "FEAT") == 0) {
            // فقط featureهایی که واقعاً پیاده‌سازی کرده‌اید
            const char *feat_response =
                "211-Extensions supported:\r\n"
                " SIZE\r\n"
                " REST STREAM\r\n"
                " UTF8\r\n"
                "211 End\r\n";
            
            tcp_write(pcb, feat_response, strlen(feat_response), TCP_WRITE_FLAG_COPY);
            tcp_output(pcb);
            ESP_LOGI(TAG, "FEAT: Sent actual supported features");
        }
        // دستوراتی که نیاز به احراز هویت دارند
        else if (!check_authentication(client, pcb)) {
            // اگر احراز هویت نشده، از ادامه اجرا جلوگیری می‌شود
        }
        else if (strcmp(command, "PWD") == 0 || strcmp(command, "XPWD") == 0) {
            char response[128];
            snprintf(response, sizeof(response), "\"%s\" is current directory", client->current_dir);
            send_response_direct(pcb, 257, response);
        }
        else if (strcmp(command, "TYPE") == 0) {
            send_response_direct(pcb, 200, "Type set to I");
        }
        else if (strcmp(command, "CDUP") == 0 || strcmp(command, "XCUP") == 0) {
            char *last_slash = strrchr(client->current_dir, '/');
            if (last_slash != client->current_dir) {
                *last_slash = '\0';
            }
            send_response_direct(pcb, 250, "Directory changed to parent");
        }
        else if (strcmp(command, "CWD") == 0) {
            if (strlen(param) > 0) {
                if (strcmp(param, "..") == 0) {
                    char *last_slash = strrchr(client->current_dir, '/');
                    if (last_slash != client->current_dir) {
                        *last_slash = '\0';
                    }
                } else if (strcmp(param, "/") == 0) {
                    strcpy(client->current_dir, "/");
                } else {
                    if (param[0] == '/') {
                        strncpy(client->current_dir, param, sizeof(client->current_dir) - 1);
                    } else {
                        if (strcmp(client->current_dir, "/") != 0) {
                            strncat(client->current_dir, "/", sizeof(client->current_dir) - strlen(client->current_dir) - 1);
                        }
                        strncat(client->current_dir, param, sizeof(client->current_dir) - strlen(client->current_dir) - 1);
                    }
                }
                
                normalize_path(client->current_dir);
                send_response_direct(pcb, 250, "Directory changed successfully");
            } else {
                send_response_direct(pcb, 550, "Invalid directory");
            }
        }
        
        else if (strcmp(command, "PASV") == 0) {
            ESP_LOGI(TAG, "🔌 Processing PASV command");
            close_data_connection(client);
            
            client->data_listen_pcb = tcp_new();
            if (!client->data_listen_pcb) {
                ESP_LOGE(TAG, "❌ Failed to create data listen PCB");
                send_response_direct(pcb, 425, "Can't create data connection");
            } else {
                err_t bind_err = tcp_bind(client->data_listen_pcb, IP_ADDR_ANY, current_data_port);
                if (bind_err != ERR_OK) {
                    ESP_LOGE(TAG, "❌ Failed to bind data port %d: %d", current_data_port, bind_err);
                    tcp_close(client->data_listen_pcb);
                    client->data_listen_pcb = NULL;
                    send_response_direct(pcb, 425, "Can't bind data port");
                } else {
                    struct tcp_pcb* listen_pcb = tcp_listen(client->data_listen_pcb);
                    if (!listen_pcb) {
                        ESP_LOGE(TAG, "❌ Failed to listen on data port");
                        tcp_close(client->data_listen_pcb);
                        client->data_listen_pcb = NULL;
                        send_response_direct(pcb, 425, "Can't listen on data port");
                    } else {
                        client->data_listen_pcb = listen_pcb;
                        
                        tcp_accept(client->data_listen_pcb, data_accept_callback);
                        tcp_arg(client->data_listen_pcb, client);

                        client->passive_mode = true;
                        client->waiting_for_data = true;
                        client->data_timeout = xTaskGetTickCount();
                        
                        char local_ip[16];
                        get_local_ip(local_ip, sizeof(local_ip));
                        
                        int ip_parts[4];
                        sscanf(local_ip, "%d.%d.%d.%d", &ip_parts[0], &ip_parts[1], &ip_parts[2], &ip_parts[3]);
                        
                        char message[128];
                        snprintf(message, sizeof(message), 
                                "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", 
                                ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3],
                                current_data_port >> 8, current_data_port & 0xFF);
                        
                        ESP_LOGI(TAG, "📡 Listening on data port: %d", current_data_port);
                        send_response_direct(pcb, 227, message);
                        
                        current_data_port++;
                        if (current_data_port > MAX_DATA_PORT) current_data_port = MIN_DATA_PORT;
                    }
                }
            }
        }

                // 🔧 این قسمت را به ctrl_recv_callback اضافه کن
        else if (strcmp(command, "EPSV") == 0) {
            ESP_LOGI(TAG, "🔌 Processing EPSV command");
            close_data_connection(client);
            
            client->data_listen_pcb = tcp_new();
            if (!client->data_listen_pcb) {
                ESP_LOGE(TAG, "❌ Failed to create data listen PCB");
                send_response_direct(pcb, 425, "Can't create data connection");
            } else {
                err_t bind_err = tcp_bind(client->data_listen_pcb, IP_ADDR_ANY, current_data_port);
                if (bind_err != ERR_OK) {
                    ESP_LOGE(TAG, "❌ Failed to bind data port %d: %d", current_data_port, bind_err);
                    tcp_close(client->data_listen_pcb);
                    client->data_listen_pcb = NULL;
                    send_response_direct(pcb, 425, "Can't bind data port");
                } else {
                    struct tcp_pcb* listen_pcb = tcp_listen(client->data_listen_pcb);
                    if (!listen_pcb) {
                        ESP_LOGE(TAG, "❌ Failed to listen on data port");
                        tcp_close(client->data_listen_pcb);
                        client->data_listen_pcb = NULL;
                        send_response_direct(pcb, 425, "Can't listen on data port");
                    } else {
                        client->data_listen_pcb = listen_pcb;
                        tcp_accept(client->data_listen_pcb, data_accept_callback);
                        tcp_arg(client->data_listen_pcb, client);

                        client->passive_mode = true;
                        client->waiting_for_data = true;
                        client->data_timeout = xTaskGetTickCount();
                        
                        // پاسخ EPSV - فرمت صحیح برای curl
                        char message[64];
                        snprintf(message, sizeof(message), "Entering Extended Passive Mode (|||%d|)", current_data_port);
                        
                        ESP_LOGI(TAG, "📡 EPSV Listening on port: %d", current_data_port);
                        send_response_direct(pcb, 229, message);
                        
                        current_data_port++;
                        if (current_data_port > MAX_DATA_PORT) current_data_port = MIN_DATA_PORT;
                    }
                }
            }
        }
            
          
        else if (strcmp(command, "OPTS") == 0) {
            if (strcmp(param, "UTF8 ON") == 0) {
                send_response_direct(pcb, 200, "UTF8 set to on");
                ESP_LOGI(TAG, "UTF8 enabled by client");
            } else {
                send_response_direct(pcb, 502, "Option not supported");
            }
        }
            else if (strcmp(command, "PORT") == 0) {
        send_response_direct(pcb, 502, "PORT not supported, use PASV instead");
    }
        else if (strcmp(command, "MLSD") == 0) {
                send_response_direct(pcb, 502, "MLSD not supported, use LIST instead");
            }
                else if (strcmp(command, "LIST") == 0 || strcmp(command, "NLST") == 0) {
            if (client->data_connected && client->data_pcb) {
                send_response_direct(pcb, 150, "Starting directory listing");
                handle_list_command(client);
            }
            else if (client->waiting_for_data) {
                strncpy(client->pending_command, command, sizeof(client->pending_command) - 1);
                strncpy(client->pending_param, param, sizeof(client->pending_param) - 1);
                send_response_direct(pcb, 150, "Waiting for data connection");
            }
            else {
                send_response_direct(pcb, 425, "Use PASV or PORT first");
            }
        }
        else if (strcmp(command, "RETR") == 0) {
            if (client->data_connected && client->data_pcb) {
              //  send_response_direct(pcb, 150, "Opening data connection");
                handle_retr_command(client, param);
            }
            else if (client->waiting_for_data) {
                strncpy(client->pending_command, command, sizeof(client->pending_command) - 1);
                strncpy(client->pending_param, param, sizeof(client->pending_param) - 1);
                send_response_direct(pcb, 150, "Waiting for data connection");
            }
            else {
                send_response_direct(pcb, 425, "Use PASV or PORT first");
            }
        }
        else if (strcmp(command, "STOR") == 0) {
            if (!client->authenticated) {
                send_response_direct(pcb, 530, "Not logged in");
            }
            else if (client->data_connected && client->data_pcb) {
                send_response_direct(pcb, 150, "Ready to receive file");
                handle_stor_command(client, param);
            }
            else if (client->waiting_for_data) {
                strncpy(client->pending_command, command, sizeof(client->pending_command) - 1);
                strncpy(client->pending_param, param, sizeof(client->pending_param) - 1);
                send_response_direct(pcb, 150, "Waiting for data connection");
            }
            else {
                send_response_direct(pcb, 425, "Use PASV or PORT first");
            }
        }
        else if (strcmp(command, "RNFR") == 0) {
            if (strlen(param) == 0) {
                send_response_direct(pcb, 501, "Invalid file name");
            } else {
                char full_path[512];
                if (param[0] == '/') {
                    snprintf(full_path, sizeof(full_path), "%s%s", s_config.root_dir, param);
                } else {
                    if (strcmp(client->current_dir, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "%s/%s", s_config.root_dir, param);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s%s/%s", s_config.root_dir, client->current_dir, param);
                    }
                }
                
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    strncpy(s_rename_state.from_path, full_path, sizeof(s_rename_state.from_path) - 1);
                    s_rename_state.pending = true;
                    send_response_direct(pcb, 350, "Requested file action pending further information");
                    ESP_LOGI(TAG, "RNFR: %s", full_path);
                } else {
                    send_response_direct(pcb, 550, "File or directory not found");
                }
            }
        }
        else if (strcmp(command, "RNTO") == 0) {
            if (strlen(param) == 0) {
                send_response_direct(pcb, 501, "Invalid file name");
            } else if (!s_rename_state.pending) {
                send_response_direct(pcb, 503, "Bad sequence of commands (use RNFR first)");
            } else {
                char full_path_to[512];
                if (param[0] == '/') {
                    snprintf(full_path_to, sizeof(full_path_to), "%s%s", s_config.root_dir, param);
                } else {
                    if (strcmp(client->current_dir, "/") == 0) {
                        snprintf(full_path_to, sizeof(full_path_to), "%s/%s", s_config.root_dir, param);
                    } else {
                        snprintf(full_path_to, sizeof(full_path_to), "%s%s/%s", s_config.root_dir, client->current_dir, param);
                    }
                }
                
                struct stat st;
                if (stat(full_path_to, &st) == 0) {
                    send_response_direct(pcb, 550, "File already exists");
                } else {
                    if (rename(s_rename_state.from_path, full_path_to) == 0) {
                        ESP_LOGI(TAG, "✅ Renamed: %s -> %s", s_rename_state.from_path, full_path_to);
                        send_response_direct(pcb, 250, "Rename successful");
                    } else {
                        ESP_LOGE(TAG, "❌ Rename failed: %s -> %s (errno: %d)", 
                                s_rename_state.from_path, full_path_to, errno);
                        send_response_direct(pcb, 550, "Rename operation failed");
                    }
                }
                
                memset(&s_rename_state, 0, sizeof(s_rename_state));
            }
        }
        else if (strcmp(command, "DELE") == 0) {
            handle_dele_command(client, param);
        }
        else if (strcmp(command, "MKD") == 0) {
            handle_mkd_command(client, param);
        }
        else if (strcmp(command, "RMD") == 0) {
            handle_rmd_command(client, param);
        }
        else if (strcmp(command, "SIZE") == 0) {
            handle_size_command(client, param); // 🔴 استفاده از تابع handle_size_command
        }
        else if (strcmp(command, "REST") == 0) {
            long restart_offset = atol(param);
            ESP_LOGI(TAG, "🔄 REST command requested: %ld", restart_offset);
            
            if (restart_offset == 0) {
                send_response_direct(pcb, 350, "Restarting at 0");
            } else {
                send_response_direct(pcb, 350, "Restarting at 0 - large file support needed");
            }
            
            // ذخیره offset برای استفاده در RETR
            client->restart_offset = restart_offset;
        }
        else {
            send_response_direct(pcb, 502, "Command not implemented");
        }
    }
    
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t ctrl_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    


char client_ip[16];
ip4addr_ntoa_r(ip_2_ip4(&newpcb->remote_ip), client_ip, sizeof(client_ip));


    ftp_client_t *client = add_client(newpcb, client_ip);
    if (client) {
        tcp_arg(newpcb, client);
        tcp_recv(newpcb, ctrl_recv_callback);
        tcp_err(newpcb, ctrl_error_callback); // 🔴 حالا این تابع تعریف شده است
        send_response_direct(newpcb, 220, "Welcome to ESP32 FTP Server");
    } else {
        tcp_close(newpcb);
    }
    
    return ERR_OK;
}

// ==================== PUBLIC API ====================

esp_err_t ftp_server_init(const ftp_server_config_t *config) {
    if (!config) return ESP_FAIL;
    memcpy(&s_config, config, sizeof(ftp_server_config_t));
    return ESP_OK;
}

esp_err_t ftp_server_start(void) {
    if (s_running) return ESP_OK;
    
    s_listen_pcb = tcp_new();
    if (!s_listen_pcb) return ESP_FAIL;
    
    err_t err = tcp_bind(s_listen_pcb, IP_ADDR_ANY, s_config.port);
    if (err != ERR_OK) {
        tcp_close(s_listen_pcb);
        return ESP_FAIL;
    }
    
    s_listen_pcb = tcp_listen(s_listen_pcb);
    tcp_accept(s_listen_pcb, ctrl_accept_callback);
    
    s_running = true;
    ESP_LOGI(TAG, "FTP Server started on port %d", s_config.port);
    return ESP_OK;
}

esp_err_t ftp_server_stop(void) {
    if (!s_running) return ESP_OK;
    
    if (s_listen_pcb) {
        tcp_close(s_listen_pcb);
        s_listen_pcb = NULL;
    }
    
    ftp_client_t *client = s_clients;
    while (client) {
        ftp_client_t *next = client->next;
        close_data_connection(client);
        if (client->ctrl_pcb) {
            tcp_arg(client->ctrl_pcb, NULL);
            tcp_recv(client->ctrl_pcb, NULL);
            tcp_close(client->ctrl_pcb);
        }
        free(client);
        client = next;
    }
    s_clients = NULL;
    
    s_running = false;
    ESP_LOGI(TAG, "FTP Server stopped");
    return ESP_OK;
}

bool ftp_server_is_running(void) {
    return s_running;
}

int ftp_server_get_client_count(void) {
    int count = 0;
    ftp_client_t *client = s_clients;
    while (client) {
        count++;
        client = client->next;
    }
    return count;
}

bool ftp_server_task_is_running(void) {
    return s_ftp_task != NULL;
}

// ==================== TASK ====================

static void ftp_server_task(void *arg) {
    ESP_LOGI(TAG, "FTP Server task started");
    
    if (ftp_server_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start FTP server");
        vTaskDelete(NULL);
        return;
    }
    
    while (ftp_server_is_running()) {
        // مدیریت کلاینت‌های علامت‌دار برای بستن
        ftp_client_t *client = s_clients;
        while (client) {
            ftp_client_t *next = client->next;
            
            if (client->marked_for_close) {
                ESP_LOGI(TAG, "Closing marked client: %s", client->client_ip);
                
                if (client->ctrl_pcb) {
                    tcp_arg(client->ctrl_pcb, NULL);
                    tcp_recv(client->ctrl_pcb, NULL);
                    tcp_err(client->ctrl_pcb, NULL);
                    tcp_close(client->ctrl_pcb);
                }
                
                close_data_connection(client);
                remove_client(client->ctrl_pcb); // 🔴 حالا این تابع استفاده می‌شود
            }
            
            // مدیریت timeout
            if (client->waiting_for_data && client->data_timeout > 0) {
                TickType_t current_time = xTaskGetTickCount();
                if ((current_time - client->data_timeout) > pdMS_TO_TICKS(1800000)) {
                    ESP_LOGW(TAG, "Data connection timeout for client %s", client->client_ip);
                    close_data_connection(client);
                    if (client->ctrl_pcb) {
                        send_response_direct(client->ctrl_pcb, 421, "Data connection timeout");
                    }
                }
            }
            
            client = next;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}

void ftp_server_start_task(void) {
    if (s_ftp_task == NULL) {
        xTaskCreate(ftp_server_task, "ftp_server", 40960, NULL, 5, &s_ftp_task);
    }
}

void ftp_server_stop_task(void) {
    if (s_ftp_task != NULL) {
        ftp_server_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_ftp_task = NULL;
    }
}