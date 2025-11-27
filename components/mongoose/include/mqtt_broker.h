#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include "esp_err.h"
#include <stdbool.h>
#include "mongoose_common.h"  // اضافه کردن این خط

#ifdef __cplusplus
extern "C" {
#endif

// انواع callbackها
typedef void (*mqtt_broker_message_callback_t)(const char *client_id, const char *topic, 
                                              const char *message, int qos);
typedef void (*mqtt_broker_client_callback_t)(const char *client_id, bool connected);

// تنظیمات MQTT Broker
typedef struct {
    int port;
    int max_clients;
    bool enable_authentication;
    char username[32];
    char password[32];
} mqtt_broker_config_t;

// توابع عمومی
esp_err_t mqtt_broker_init(const mqtt_broker_config_t *config);
esp_err_t mqtt_broker_start(void);
esp_err_t mqtt_broker_stop(void);
bool mqtt_broker_is_running(void);

// توابع مدیریت پیام‌ها
esp_err_t mqtt_broker_publish(const char *topic, const char *message, int qos, bool retain);
esp_err_t mqtt_broker_broadcast(const char *topic, const char *message, int qos, bool retain);

// توابع مدیریت کلاینت‌ها
int mqtt_broker_get_client_count(void);
esp_err_t mqtt_broker_disconnect_client(const char *client_id);
esp_err_t mqtt_broker_get_client_list(char *buffer, size_t buffer_size);

// تنظیم callbackها
void mqtt_broker_set_message_callback(mqtt_broker_message_callback_t callback);
void mqtt_broker_set_client_callback(mqtt_broker_client_callback_t callback);

// اطلاعات بروکر
int mqtt_broker_get_port(void);
const char* mqtt_broker_get_status(void);

#ifdef __cplusplus
}
#endif

#endif