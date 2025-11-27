#include "mqtt_broker.h"
#include "mongoose.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"
#include "cJSON.h"
#include <inttypes.h>  // اضافه کردن این include

static const char *TAG = "MQTTBroker";

// ساختار برای مدیریت subscription
struct sub {
    struct mg_connection *c;
    struct mg_str topic;
    uint8_t qos;
    char client_id[64];
    struct sub *next;
};

// ساختار برای مدیریت کلاینت‌ها
struct client {
    struct mg_connection *c;
    char client_id[64];
    bool connected;
    struct client *next;
};

// متغیرهای global
static struct mg_mgr s_mgr;
static mqtt_broker_config_t s_config;
static bool s_running = false;
static TaskHandle_t s_broker_task = NULL;

// لیست‌ها
static struct sub *s_subs = NULL;
static struct client *s_clients = NULL;

// Callbackها
static mqtt_broker_message_callback_t s_message_callback = NULL;
static mqtt_broker_client_callback_t s_client_callback = NULL;



// تابع برای نمایش وضعیت حافظه با تمرکز بر PSRAM
static void log_memory_status(void) {
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI("MEMORY", "💾 Memory Status:");
    ESP_LOGI("MEMORY", "  🚀 PSRAM Free: %d bytes (%.1f KB)", free_spiram, free_spiram / 1024.0);
    ESP_LOGI("MEMORY", "  📦 Largest PSRAM Block: %d bytes", largest_free_block);
    ESP_LOGI("MEMORY", "  🏠 Internal RAM: %d bytes", free_internal);
    ESP_LOGI("MEMORY", "  🔗 Total 8-bit: %d bytes", free_8bit);
}

// تابع برای تخصیص حافظه در PSRAM
static void* psram_malloc(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) {
        ESP_LOGD("PSRAM", "✅ Allocated %d bytes in PSRAM", size);
    } else {
        ESP_LOGE("PSRAM", "❌ Failed to allocate %d bytes in PSRAM", size);
        // Fallback to internal RAM
        ptr = malloc(size);
    }
    return ptr;
}

// تابع برای تخصیص حافظه با alignment در PSRAM
static void* psram_calloc(size_t num, size_t size) {
    void* ptr = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM);
    if (ptr) {
        ESP_LOGD("PSRAM", "✅ Calloc %d x %d bytes in PSRAM", num, size);
    } else {
        ESP_LOGE("PSRAM", "❌ Failed to calloc in PSRAM");
        ptr = calloc(num, size);
    }
    return ptr;
}

// تابع برای پیدا کردن کلاینت
static struct client* find_client(struct mg_connection *c) {
    struct client *client = s_clients;
    while (client != NULL) {
        if (client->c == c) {
            return client;
        }
        client = client->next;
    }
    return NULL;
}

// تابع برای اضافه کردن کلاینت
static struct client* add_client(struct mg_connection *c, const char *client_id) {
    struct client *client = calloc(1, sizeof(struct client));
  //  struct client *client = psram_calloc(1, sizeof(struct client));
    
    if (!client) return NULL;
    
    client->c = c;
    client->connected = true;
    strncpy(client->client_id, client_id, sizeof(client->client_id) - 1);
    client->next = s_clients;
    s_clients = client;
    
    ESP_LOGI(TAG, "➕ Client connected: %s", client->client_id);
    
    if (s_client_callback != NULL) {
        s_client_callback(client->client_id, true);
    }
    
    return client;
}

// تابع برای حذف کلاینت
static void remove_client(struct mg_connection *c) {
    struct client **prev = &s_clients;
    struct client *curr = s_clients;
    
    while (curr != NULL) {
        if (curr->c == c) {
            *prev = curr->next;
            ESP_LOGI(TAG, "➖ Client disconnected: %s", curr->client_id);
            
            if (s_client_callback != NULL) {
                s_client_callback(curr->client_id, false);
            }
            
            free(curr);
            return;
        }
        prev = &curr->next;
        curr = curr->next;
    }
}

// تابع برای حذف subscriptionهای یک کلاینت
static void remove_client_subscriptions(struct mg_connection *c) {
    struct sub **prev = &s_subs;
    struct sub *curr = s_subs;
    
    while (curr != NULL) {
        if (curr->c == c) {
            *prev = curr->next;
            free((void*)curr->topic.ptr);
            free(curr);
            curr = *prev;
        } else {
            prev = &curr->next;
            curr = curr->next;
        }
    }
}

// تابع برای ارسال پیام به مشترکین یک topic
// تابع برای تطبیق MQTT topics با wildcards
static bool topic_matches(const char *subscription, const char *topic) {
    const char *sub = subscription;
    const char *top = topic;
    
    while (*sub && *top) {
        if (*sub == '#') {
            // # باید آخرین کاراکتر باشد
            return true;
        }
        else if (*sub == '+') {
            // + یک level را match می‌کند
            // برو به بعدی / در subscription
            while (*sub && *sub != '/') sub++;
            // برو به بعدی / در topic  
            while (*top && *top != '/') top++;
        }
        else if (*sub == *top) {
            // کاراکترهای عادی
            sub++;
            top++;
        }
        else {
            return false;
        }
        
        // رد شدن از /
        if (*sub == '/') sub++;
        if (*top == '/') top++;
    }
    
    return (*sub == '\0' && *top == '\0');
}

// تابع برای ارسال پیام به مشترکین یک topic - نسخه اصلاح شده
static void publish_to_subscribers(struct mg_str topic, struct mg_str message, int qos, struct mg_connection *exclude) {
    char topic_str[128] = {0};
    snprintf(topic_str, sizeof(topic_str), "%.*s", (int)topic.len, topic.ptr);
    
    // ✅ حذف فضای خالی از ابتدا و انتهای topic
    char clean_topic[128] = {0};
    strncpy(clean_topic, topic_str, sizeof(clean_topic) - 1);
    
    // حذف فضای خالی
    char *start = clean_topic;
    while (*start == ' ') start++;  // حذف فضای ابتدا
    char *end = start + strlen(start) - 1;
    while (end > start && *end == ' ') end--;  // حذف فضای انتها
    *(end + 1) = '\0';
    
    int sent_count = 0;
    struct sub *sub = s_subs;
    
    ESP_LOGI(TAG, "🔍 Looking for subscribers for: '%s'", start);
    
    while (sub != NULL) {
        char sub_topic[128] = {0};
        snprintf(sub_topic, sizeof(sub_topic), "%.*s", (int)sub->topic.len, sub->topic.ptr);
        
        // ✅ تطبیق دقیق یا با wildcard
        bool matches = false;
        
        if (strcmp(sub_topic, "#") == 0) {
            matches = true;  // همه topics
        }
        else if (strcmp(sub_topic, start) == 0) {
            matches = true;  // تطبیق دقیق
        }
        else if (strstr(sub_topic, "/#") != NULL) {
            // بررسی prefix match برای patternهایی مثل test/#
            char prefix[128];
            strncpy(prefix, sub_topic, strlen(sub_topic) - 2);
            prefix[strlen(sub_topic) - 2] = '\0';
            
            if (strncmp(start, prefix, strlen(prefix)) == 0) {
                matches = true;
            }
        }
        
        if (matches) {
            if (sub->c != exclude && sub->c != NULL) {
                mg_mqtt_pub(sub->c, topic, message, qos, false);
                sent_count++;
                ESP_LOGI(TAG, "✅ Matched: '%s' -> '%s'", sub_topic, start);
            }
        }
        sub = sub->next;
    }
    
    if (sent_count > 0) {
        ESP_LOGI(TAG, "📤 Published to %d subscribers: %s", sent_count, start);
    } else {
        ESP_LOGW(TAG, "📭 No subscribers for topic: %s", start);
    }
}


// ✅ اصلاح: تعریف تابع قبل از استفاده
static int count_subscriptions(void) {
    int count = 0;
    struct sub *sub = s_subs;
    while (sub != NULL) {
        count++;
        sub = sub->next;
    }
    return count;
}

// تابع event handler برای MQTT Broker
static void mqtt_broker_event_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    if (ev == MG_EV_MQTT_CMD) {
        struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
        
        switch (mm->cmd) {
            
            case MQTT_CMD_CONNECT: {
                ESP_LOGI(TAG, "🔌 MQTT Client CONNECT request");
                
                // ✅ روش ساده‌تر برای استخراج Client ID
                char client_id[64] = "unknown";
                
                // اگر client_id در CONNECT packet موجود است
                if (mm->data.len > 10) {
                    // سعی کن client_id را پیدا کن
                    const char *data = mm->data.ptr;
                    
                    // روش ساده: اگر "client_id" در داده‌ها وجود دارد
                    const char *client_id_ptr = strstr(data, "client_id");
                    if (client_id_ptr) {
                        // استخراج ساده
                        const char *value_start = strchr(client_id_ptr, ':');
                        if (value_start) {
                            value_start++; // بعد از :
                            const char *value_end = strchr(value_start, ',');
                            if (!value_end) value_end = value_start + strlen(value_start);
                            
                            int len = value_end - value_start;
                            if (len > 0 && len < sizeof(client_id) - 1) {
                                strncpy(client_id, value_start, len);
                                client_id[len] = '\0';
                            }
                        }
                    }
                }
                
                // اگر پیدا نکردیم، یک ID تصادفی ایجاد کن
                if (strcmp(client_id, "unknown") == 0) {
                    snprintf(client_id, sizeof(client_id), "client_%d", (int)time(NULL) % 1000);
                }
                
                // اضافه کردن کلاینت
                add_client(c, client_id);
                
                // ارسال CONNACK
                uint8_t ack[] = {0x20, 0x02, 0x00, 0x00};
                mg_send(c, ack, sizeof(ack));
                break;
            }
                            
            case MQTT_CMD_SUBSCRIBE: {
                struct client *client = find_client(c);
                char client_id[64] = "unknown";
                if (client) {
                    strncpy(client_id, client->client_id, sizeof(client_id));
                }
                
                ESP_LOGI(TAG, "📝 SUBSCRIBE from %s", client_id);
                
                size_t pos = 4;
                uint8_t qos, resp[256];
                struct mg_str topic;
                int num_topics = 0;
                
                while ((pos = mg_mqtt_next_sub(mm, &topic, &qos, pos)) > 0) {
                    struct sub *sub = calloc(1, sizeof(*sub));
                    if (!sub) continue;
                    
                    sub->c = c;
                    sub->topic = mg_strdup(topic);
                    sub->qos = qos;
                    strncpy(sub->client_id, client_id, sizeof(sub->client_id));
                    sub->next = s_subs;
                    s_subs = sub;
                    resp[num_topics++] = qos;
                    
                    ESP_LOGI(TAG, "✅ %s subscribed to: %.*s (QoS: %d)", 
                            client_id, (int)topic.len, topic.ptr, qos);
                }
                
                mg_mqtt_send_header(c, MQTT_CMD_SUBACK, 0, num_topics + 2);
                uint16_t id = mg_htons(mm->id);
                mg_send(c, &id, 2);
                mg_send(c, resp, num_topics);
                break;
            }
                
            case MQTT_CMD_PUBLISH: {
                struct client *client = find_client(c);
                char client_id[64] = "unknown";
                if (client) {
                    strncpy(client_id, client->client_id, sizeof(client_id));
                }
                
                char topic[128] = {0};
                char message[512] = {0};
                
                snprintf(topic, sizeof(topic), "%.*s", (int)mm->topic.len, mm->topic.ptr);
                snprintf(message, sizeof(message), "%.*s", (int)mm->data.len, mm->data.ptr);  // ✅ اصلاح: mm->data
                
                ESP_LOGI(TAG, "📨 PUBLISH from %s: %s -> %s", client_id, topic, message);
                
                // ارسال به مشترکین
                publish_to_subscribers(mm->topic, mm->data, mm->qos, c);
                
                // فراخوانی callback اگر تنظیم شده
                if (s_message_callback != NULL) {
                    s_message_callback(client_id, topic, message, mm->qos);
                }
                break;
            }
                
            case MQTT_CMD_PINGREQ:
                ESP_LOGI(TAG, "🏓 PING from client");
                mg_mqtt_send_header(c, MQTT_CMD_PINGRESP, 0, 0);
                break;
                
            case MQTT_CMD_DISCONNECT:
                ESP_LOGI(TAG, "🔌 DISCONNECT from client");
                break;
                
            default:
                break;
        }
    }
    else if (ev == MG_EV_CLOSE) {
        ESP_LOGI(TAG, "🔌 Connection closed");
        remove_client_subscriptions(c);
        remove_client(c);
    }
}

// تسک اصلی MQTT Broker
static void mqtt_broker_task(void *pvParameters) {
    ESP_LOGI(TAG, "🚀 MQTT Broker Task Started on port %d", s_config.port);
    
    mg_mgr_init(&s_mgr);
    
    char url[32];
    snprintf(url, sizeof(url), "mqtt://0.0.0.0:%d", s_config.port);
    
    struct mg_connection *c = mg_mqtt_listen(&s_mgr, url, mqtt_broker_event_handler, NULL);
    if (c == NULL) {
        ESP_LOGE(TAG, "❌ Failed to start MQTT broker on port %d", s_config.port);
        vTaskDelete(NULL);
        return;
    }
    
    s_running = true;
    ESP_LOGI(TAG, "✅ MQTT Broker started successfully");
    ESP_LOGI(TAG, "📡 Broker URL: %s", url);
    ESP_LOGI(TAG, "👥 Max clients: %d", s_config.max_clients);
    
    // لاگ دوره‌ای وضعیت
    int status_counter = 0;
    
    while (s_running) {
        mg_mgr_poll(&s_mgr, 100);
        
        // لاگ وضعیت هر 30 ثانیه
        if (++status_counter >= 300) {
            status_counter = 0;
            int client_count = mqtt_broker_get_client_count();
            ESP_LOGI(TAG, "📊 Broker Status - Clients: %d, Subscriptions: %d", 
                    client_count, count_subscriptions());  // ✅ حالا تابع تعریف شده
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Cleanup
    ESP_LOGI(TAG, "🧹 Cleaning up MQTT Broker...");
    
    // پاکسازی subscriptions
    struct sub *sub = s_subs;
    while (sub != NULL) {
        struct sub *next = sub->next;
        free((void*)sub->topic.ptr);
        free(sub);
        sub = next;
    }
    s_subs = NULL;
    
    // پاکسازی کلاینت‌ها
    struct client *client = s_clients;
    while (client != NULL) {
        struct client *next = client->next;
        free(client);
        client = next;
    }
    s_clients = NULL;
    
    mg_mgr_free(&s_mgr);
    
    ESP_LOGI(TAG, "🛑 MQTT Broker Task Ended");
    vTaskDelete(NULL);
}

// ==================== توابع عمومی ====================

esp_err_t mqtt_broker_init(const mqtt_broker_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return ESP_FAIL;
    }
    
    // کپی کردن تنظیمات
    memcpy(&s_config, config, sizeof(mqtt_broker_config_t));
    
    ESP_LOGI(TAG, "MQTT Broker initialized");
    ESP_LOGI(TAG, "  Port: %d", s_config.port);
    ESP_LOGI(TAG, "  Max clients: %d", s_config.max_clients);
    ESP_LOGI(TAG, "  Authentication: %s", s_config.enable_authentication ? "enabled" : "disabled");
    
    if (s_config.enable_authentication) {
        ESP_LOGI(TAG, "  Username: %s", s_config.username);
    }
    
    return ESP_OK;
}

esp_err_t mqtt_broker_start(void) {
    if (s_running) {
        ESP_LOGW(TAG, "MQTT Broker is already running");
        return ESP_OK;
    }
    
    if (xTaskCreate(mqtt_broker_task, "mqtt_broker", 8192, NULL, 6, &s_broker_task) != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create MQTT broker task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ MQTT Broker start command sent");
    return ESP_OK;
}

esp_err_t mqtt_broker_stop(void) {
    if (!s_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🛑 Stopping MQTT Broker...");
    s_running = false;
    
    // صبر کن تا تسک تمام شود (حداکثر 3 ثانیه)
    for (int i = 0; i < 30; i++) {
        if (s_broker_task == NULL) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "✅ MQTT Broker stopped");
    return ESP_OK;
}

bool mqtt_broker_is_running(void) {
    return s_running;
}

// ==================== توابع مدیریت پیام‌ها ====================

esp_err_t mqtt_broker_publish(const char *topic, const char *message, int qos, bool retain) {
    if (!s_running) {
        ESP_LOGE(TAG, "Broker is not running");
        return ESP_FAIL;
    }
    
    if (topic == NULL || message == NULL) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "📤 Broker publishing: %s -> %s", topic, message);
    
    // ارسال به همه مشترکین
    publish_to_subscribers(mg_str(topic), mg_str(message), qos, NULL);
    
    return ESP_OK;
}

esp_err_t mqtt_broker_broadcast(const char *topic, const char *message, int qos, bool retain) {
    return mqtt_broker_publish(topic, message, qos, retain);
}

// ==================== توابع مدیریت کلاینت‌ها ====================

int mqtt_broker_get_client_count(void) {
    int count = 0;
    struct client *client = s_clients;
    
    while (client != NULL) {
        if (client->connected) {
            count++;
        }
        client = client->next;
    }
    
    return count;
}

esp_err_t mqtt_broker_disconnect_client(const char *client_id) {
    if (client_id == NULL) {
        return ESP_FAIL;
    }
    
    struct client *client = s_clients;
    
    while (client != NULL) {
        if (strcmp(client->client_id, client_id) == 0 && client->connected) {
            // ✅ اصلاح: پارامتر دوم حذف شد
            mg_mqtt_disconnect(client->c);
            ESP_LOGI(TAG, "🔌 Disconnected client: %s", client_id);
            return ESP_OK;
        }
        client = client->next;
    }
    
    ESP_LOGW(TAG, "Client not found: %s", client_id);
    return ESP_FAIL;
}

esp_err_t mqtt_broker_get_client_list(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 64) {
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_CreateArray();
    struct client *client = s_clients;
    
    while (client != NULL) {
        if (client->connected) {
            cJSON *client_json = cJSON_CreateObject();
            cJSON_AddStringToObject(client_json, "client_id", client->client_id);
            cJSON_AddItemToArray(json, client_json);
        }
        client = client->next;
    }
    
    const char *json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        strncpy(buffer, json_str, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        free((void*)json_str);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// ==================== توابع Callback ====================

void mqtt_broker_set_message_callback(mqtt_broker_message_callback_t callback) {
    s_message_callback = callback;
}

void mqtt_broker_set_client_callback(mqtt_broker_client_callback_t callback) {
    s_client_callback = callback;
}

// ==================== توابع اطلاعات ====================

int mqtt_broker_get_port(void) {
    return s_config.port;
}

const char* mqtt_broker_get_status(void) {
    if (!s_running) return "stopped";
    
    int clients = mqtt_broker_get_client_count();
    static char status[32];
    snprintf(status, sizeof(status), "running (%d clients)", clients);
    return status;
}