#ifndef LCD101_H
#define LCD101_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// توابع عمومی
void InitLcd(uint16_t mod);
void lcd101_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
void Delay_ms(uint16_t ms) ;
// توابع داخلی (برای استفاده داخلی)
esp_err_t spi_master_init(void);

// 🔴 اضافه کردن توابعی که shared_hardware نیاز دارد

void LCDClearScreen(uint16_t color) ;

#ifdef __cplusplus
}
#endif

#endif