#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>
#include "mongoose_common.h"  // اضافه کردن این خط

#ifdef __cplusplus
extern "C" {
#endif

// انواع callbackها
typedef void (*mqtt_message_callback_t)(const char *topic, const char *message, int qos);
typedef void (*mqtt_connect_callback_t)(bool connected);
typedef void (*mqtt_disconnect_callback_t)(void);

// تنظیمات MQTT Client
typedef struct {
    char broker_url[128];
    char client_id[64];
    char username[32];
    char password[32];
    bool clean_session;
    int keepalive;
} mqtt_client_config_t;

// توابع عمومی
esp_err_t mqtt_client_init(const mqtt_client_config_t *config);
esp_err_t mqtt_client_start(void);
esp_err_t mqtt_client_stop(void);
bool mqtt_client_is_connected(void);

// توابع publish/subscribe
esp_err_t mqtt_client_publish(const char *topic, const char *message, int qos, bool retain);
esp_err_t mqtt_client_subscribe(const char *topic, int qos);
esp_err_t mqtt_client_unsubscribe(const char *topic);

// تنظیم callbackها
void mqtt_client_set_message_callback(mqtt_message_callback_t callback);
void mqtt_client_set_connect_callback(mqtt_connect_callback_t callback);
void mqtt_client_set_disconnect_callback(mqtt_disconnect_callback_t callback);

// اطلاعات اتصال
const char* mqtt_client_get_broker_url(void);
const char* mqtt_client_get_client_id(void);

#ifdef __cplusplus
}
#endif

#endif