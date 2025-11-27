#ifndef SD_CARD_DRIVER_H
#define SD_CARD_DRIVER_H


#include "esp_err.h"
#include "driver/sdmmc_host.h"


#ifdef __cplusplus
extern "C" {
#endif

// تعاریف توابع SD Card
esp_err_t sd_card_driver_init(void);
bool sd_card_driver_is_mounted(void);
sdmmc_card_t* sd_card_driver_get_card(void);
esp_err_t sd_card_driver_unmount(void);
const char* sd_card_driver_get_mount_point(void);

esp_err_t sd_card_create_apps_directory(void)  ;


#ifdef __cplusplus
}
#endif

#endif // SD_CARD_DRIVER_H