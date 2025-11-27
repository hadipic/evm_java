// components/mongoose/include/mongoose_common.h
#ifndef MONGOOSE_COMMON_H
#define MONGOOSE_COMMON_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// انواع فایل سیستم مشترک
typedef enum {
    FS_TYPE_SPIFFS,     // SPIFFS داخلی
    FS_TYPE_SD_CARD,    // کارت SD
    FS_TYPE_AUTO        // تشخیص خودکار
} fs_type_t;

#ifdef __cplusplus
}
#endif

#endif