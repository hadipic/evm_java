#pragma once

// ===== تنظیمات LCD =====
#define LCD_PIN_CLK   14
#define LCD_PIN_MOSI  13
#define LCD_PIN_MISO  12
#define LCD_PIN_CS    15


// #define PIN_NUM_MISO 12
// #define PIN_NUM_MOSI 13
// #define PIN_NUM_CLK  14
// #define PIN_NUM_CS  15



#define LCD_WIDTH     160
#define LCD_HEIGHT    128

// ===== تنظیمات SD Card =====
#define SD_PIN_MISO   23
#define SD_PIN_MOSI   19
#define SD_PIN_CLK    18
#define SD_PIN_CS     5

// #define PIN_NUM_MISO 23
// #define PIN_NUM_MOSI 19
// #define PIN_NUM_CLK  18
// #define PIN_NUM_CS   5



// ===== تنظیمات دکمه‌ها =====
#define BUTTON_PREV_GPIO   GPIO_NUM_2
#define BUTTON_PLAY_GPIO   GPIO_NUM_4
#define BUTTON_NEXT_GPIO   GPIO_NUM_34 
#define BUTTON_MODE_GPIO   GPIO_NUM_35

// ===== تنظیمات عمومی =====
#define VOLUME_STEP       2
#define EQUALIZER_STEP    1
#define MAX_PLAY_FILE_NUM 20
#define DISP_BUF_SIZE     (LCD_WIDTH * 5)