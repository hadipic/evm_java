#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int port;
    char root_dir[128];
    char username[32];
    char password[32];
} ftp_server_config_t;

// Public API
esp_err_t ftp_server_init(const ftp_server_config_t *config);
esp_err_t ftp_server_start(void);
esp_err_t ftp_server_stop(void);
bool ftp_server_is_running(void);
int ftp_server_get_client_count(void);

// Task management
void ftp_server_start_task(void);
void ftp_server_stop_task(void);
bool ftp_server_task_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // FTP_SERVER_H