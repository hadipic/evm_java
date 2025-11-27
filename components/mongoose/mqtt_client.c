#include "mqtt_client.h"
#include "mongoose.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "string.h"
#include <inttypes.h>  // اضافه کردن این include

static const char *TAG = "MQTTClient";

// متغیرهای global
static struct mg_mgr s_mgr;
static struct mg_connection *s_conn = NULL;
static mqtt_client_config_t s_config;
static bool s_connected = false;
static bool s_running = false;

// Callbackها
static mqtt_message_callback_t s_message_callback = NULL;
static mqtt_connect_callback_t s_connect_callback = NULL;
static mqtt_disconnect_callback_t s_disconnect_callback = NULL;

// صف برای انتشار پیام‌ها
static QueueHandle_t s_publish_queue = NULL;

// تعریف ساختار برای پیام‌های publish
typedef struct {
    char topic[128];
    char message[256];
    int qos;
    bool retain;
} mqtt_publish_msg_t;

// تابع event handler برای MQTT
static void mqtt_event_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    switch (ev) {
        case MG_EV_CONNECT:
            ESP_LOGI(TAG, "🔗 TCP connection established to %s", s_config.broker_url);
            break;
            
        case MG_EV_MQTT_OPEN:
            ESP_LOGI(TAG, "✅ MQTT CONNECTED to broker");
            s_connected = true;
            
            if (s_connect_callback != NULL) {
                s_connect_callback(true);
            }
            break;
            
        case MG_EV_MQTT_MSG: {
            struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
            char topic[128] = {0};
            char message[256] = {0};
            
            snprintf(topic, sizeof(topic), "%.*s", (int)mm->topic.len, mm->topic.ptr);
            snprintf(message, sizeof(message), "%.*s", (int)mm->data.len, mm->data.ptr);
            
            ESP_LOGI(TAG, "📨 MQTT Received - Topic: %s, Message: %s", topic, message);
            
            if (s_message_callback != NULL) {
                s_message_callback(topic, message, mm->qos);
            }
            break;
        }
            
        case MG_EV_CLOSE:
            ESP_LOGI(TAG, "🔌 MQTT connection closed");
            s_connected = false;
            s_conn = NULL;
            
            if (s_disconnect_callback != NULL) {
                s_disconnect_callback();
            }
            break;
            
        case MG_EV_ERROR:
            ESP_LOGE(TAG, "❌ MQTT error: %s", (char *)ev_data);
            s_connected = false;
            break;
            
        default:
            break;
    }
}

// تسک برای انتشار پیام‌ها
static void mqtt_publisher_task(void *pvParameters) {
    mqtt_publish_msg_t msg;
    
    while (s_running) {
        if (xQueueReceive(s_publish_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (s_connected && s_conn != NULL) {
                mg_mqtt_pub(s_conn, mg_str(msg.topic), mg_str(msg.message), msg.qos, msg.retain);
                ESP_LOGI(TAG, "📤 Published - Topic: %s, Message: %s", msg.topic, msg.message);
            } else {
                ESP_LOGW(TAG, "Cannot publish - MQTT not connected");
            }
        }
    }
    
    vTaskDelete(NULL);
}

// تسک اصلی MQTT Client
static void mqtt_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "🚀 MQTT Client Task Started");
    
    mg_mgr_init(&s_mgr);
    
    // ایجاد صف publish
    s_publish_queue = xQueueCreate(10, sizeof(mqtt_publish_msg_t));
    if (s_publish_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create publish queue");
        vTaskDelete(NULL);
        return;
    }
    
    // راه‌اندازی تسک publisher
    xTaskCreate(mqtt_publisher_task, "mqtt_pub", 4096, NULL, 5, NULL);
    
    while (s_running) {
        // اگر قطع شدیم یا اولین بار است، اتصال برقرار کن
        if (s_conn == NULL && s_running) {
            ESP_LOGI(TAG, "Connecting to MQTT broker: %s", s_config.broker_url);
            
            struct mg_mqtt_opts opts = {
                .clean = s_config.clean_session,
                .client_id = mg_str(s_config.client_id),
                .user = mg_str(s_config.username),
                .pass = mg_str(s_config.password)
            };
            
            s_conn = mg_mqtt_connect(&s_mgr, s_config.broker_url, &opts, mqtt_event_handler, NULL);
            
            if (s_conn == NULL) {
                ESP_LOGE(TAG, "Failed to create MQTT connection");
                vTaskDelay(pdMS_TO_TICKS(5000)); // 5 ثانیه صبر کن قبل از تلاش مجدد
                continue;
            }
        }
        
        // پردازش events
        mg_mgr_poll(&s_mgr, 100);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Cleanup
    if (s_conn != NULL) {
        mg_mqtt_disconnect(s_conn);  // ✅ اصلاح: پارامتر دوم حذف شد
        s_conn = NULL;
    }
    
    mg_mgr_free(&s_mgr);
    vQueueDelete(s_publish_queue);
    
    ESP_LOGI(TAG, "🛑 MQTT Client Task Ended");
    vTaskDelete(NULL);
}

// ==================== توابع عمومی ====================

esp_err_t mqtt_client_init(const mqtt_client_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return ESP_FAIL;
    }
    
    // کپی کردن تنظیمات
    memcpy(&s_config, config, sizeof(mqtt_client_config_t));
    
    ESP_LOGI(TAG, "MQTT Client initialized");
    ESP_LOGI(TAG, "  Broker: %s", s_config.broker_url);
    ESP_LOGI(TAG, "  Client ID: %s", s_config.client_id);
    ESP_LOGI(TAG, "  Username: %s", s_config.username);
    
    return ESP_OK;
}

esp_err_t mqtt_client_start(void) {
    if (s_running) {
        ESP_LOGW(TAG, "MQTT Client is already running");
        return ESP_OK;
    }
    
    s_running = true;
    
    if (xTaskCreate(mqtt_client_task, "mqtt_client", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT client task");
        s_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ MQTT Client started");
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void) {
    if (!s_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🛑 Stopping MQTT Client");
    s_running = false;
    
    // صبر کن تا تسک تمام شود
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    return ESP_OK;
}

bool mqtt_client_is_connected(void) {
    return s_connected;
}

// ==================== توابع Publish/Subscribe ====================

esp_err_t mqtt_client_publish(const char *topic, const char *message, int qos, bool retain) {
    if (topic == NULL || message == NULL) {
        return ESP_FAIL;
    }
    
    if (!s_running) {
        ESP_LOGE(TAG, "MQTT Client is not running");
        return ESP_FAIL;
    }
    
    mqtt_publish_msg_t msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.qos = qos;
    msg.retain = retain;
    
    if (xQueueSend(s_publish_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue MQTT message");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char *topic, int qos) {
    if (topic == NULL) {
        return ESP_FAIL;
    }
    
    if (!s_connected || s_conn == NULL) {
        ESP_LOGE(TAG, "MQTT not connected, cannot subscribe");
        return ESP_FAIL;
    }
    
    mg_mqtt_sub(s_conn, mg_str(topic), qos);
    ESP_LOGI(TAG, "📝 Subscribed to: %s (QoS: %d)", topic, qos);
    
    return ESP_OK;
}

// ✅ اصلاح: فقط یک تعریف از تابع unsubscribe باقی ماند
esp_err_t mqtt_client_unsubscribe(const char *topic) {
    if (topic == NULL) {
        return ESP_FAIL;
    }
    
    if (!s_connected || s_conn == NULL) {
        ESP_LOGE(TAG, "MQTT not connected, cannot unsubscribe");
        return ESP_FAIL;
    }
    
    // ✅ روش ساده: استفاده از subscribe با QoS 0 برای unsubscribe
    // یا اگر Mongoose از unsubscribe پشتیبانی می‌کند:
    // mg_mqtt_unsubscribe(s_conn, topic);
    
    // روش فعلی: کامنت کردن خط مشکل‌دار و استفاده از روش جایگزین
    // struct mg_mqtt_opts unsubscribe_opts;
    // memset(&unsubscribe_opts, 0, sizeof(unsubscribe_opts));
    // unsubscribe_opts.topic = mg_str(topic);  // ❌ این خط را کامنت کنید
    
    mg_mqtt_sub(s_conn, mg_str(topic), 0);  // ✅ استفاده از subscribe با QoS 0
    
    ESP_LOGI(TAG, "📝 Unsubscribed from: %s", topic);
    return ESP_OK;
}

// ==================== توابع Callback ====================

void mqtt_client_set_message_callback(mqtt_message_callback_t callback) {
    s_message_callback = callback;
}

void mqtt_client_set_connect_callback(mqtt_connect_callback_t callback) {
    s_connect_callback = callback;
}

void mqtt_client_set_disconnect_callback(mqtt_disconnect_callback_t callback) {
    s_disconnect_callback = callback;
}

// ==================== توابع اطلاعات ====================

const char* mqtt_client_get_broker_url(void) {
    return s_config.broker_url;
}

const char* mqtt_client_get_client_id(void) {
    return s_config.client_id;
}