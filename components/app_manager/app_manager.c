
#include "evm_loader.h"
#include "shared_hardware.h"
#include "hardware_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_task_wdt.h"

#include "app_manager.h"

#include "lcd101.h"
#include <inttypes.h>
#include <ctype.h>
#include <inttypes.h>
// LVGL
#include "lvgl.h"
#include "evm_module_lvgl.h"
#include "evm_module_wifi.h"

#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "lv_fs_if.h"

#include "../lvgl/src/extra/libs/png/lodepng.h"

#include "../lvgl/src/extra/libs/png/lv_png.h"

static const char *TAG = "app_manager";

// متغیرهای global
static evm_app_t *apps = NULL;
static int app_count = 0;
static int selected_app = 0;

// متغیرهای LVGL
static lv_obj_t *ui_scr = NULL;
static lv_obj_t *ui_app_list = NULL;
static lv_obj_t *ui_selected_label = NULL;
static lv_obj_t *ui_status_label = NULL;
static lv_obj_t *ui_launch_btn = NULL;
static bool lvgl_initialized = false;

// تعریف پین‌های دکمه
#define BUTTON_UP_GPIO    GPIO_NUM_2
#define BUTTON_SELECT_GPIO GPIO_NUM_4  
#define BUTTON_DOWN_GPIO  GPIO_NUM_34
#define BUTTON_BACK_GPIO  GPIO_NUM_35

// تعریف‌های مربوط به LCD و بافر
#define LVGL_DISP_BUF_SIZE (160 * 128)
static SemaphoreHandle_t xGuiSemaphore = NULL;

extern lv_obj_t *permanent_launcher_screen;

// پیش‌تعریف توابع
static void create_lvgl_ui(void);
static void update_lvgl_ui(void);
static void lvgl_button_task_handler(void *arg);
static void guiTask(void *pvParameter);

// ========================= توابع اصلی Application Manager =========================

typedef struct {
    StackType_t *stack;
    StaticTask_t *tcb;
    TaskHandle_t handle;
} safe_task_t;

TaskHandle_t create_safe_task(TaskFunction_t func, const char *name, 
                             uint32_t stack_words, UBaseType_t priority, BaseType_t core)
{
    safe_task_t *task_data = malloc(sizeof(safe_task_t));
    if (!task_data) return NULL;
    
    task_data->stack = heap_caps_malloc(stack_words * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    task_data->tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
    
    if (!task_data->stack || !task_data->tcb) {
        if (task_data->stack) heap_caps_free(task_data->stack);
        if (task_data->tcb) heap_caps_free(task_data->tcb);
        free(task_data);
        return NULL;
    }
    
    task_data->handle = xTaskCreateStaticPinnedToCore(
        func, name, stack_words, task_data, priority, 
        task_data->stack, task_data->tcb, core
    );
    
    if (!task_data->handle) {
        heap_caps_free(task_data->stack);
        heap_caps_free(task_data->tcb);
        free(task_data);
        return NULL;
    }
    
    return task_data->handle;
}

void vPortCleanUpTCB(void *pxTCB) {
    if (pxTCB) {
        // heap_caps_free(pxTCB);
    }
}

TaskHandle_t create_task_with_psram(TaskFunction_t func,
                                   const char *name,
                                   uint32_t stack_size_words,
                                   UBaseType_t priority,
                                   BaseType_t core)
{
    static StackType_t stack_buffer[3072] __attribute__((aligned(4)));
    static StaticTask_t tcb_buffer;

    return xTaskCreateStaticPinnedToCore(
        func,
        name,
        3072,
        NULL,
        priority,
        stack_buffer,
        &tcb_buffer,
        core
    );
}

TaskHandle_t create_task_with_psram1(TaskFunction_t func,
                                   const char *name,
                                   uint32_t stack_size_words,
                                   UBaseType_t priority,
                                   BaseType_t core)
{
    static StackType_t stack_buffer[4096] __attribute__((aligned(8)));
    static StaticTask_t tcb_buffer;
    
    return xTaskCreateStaticPinnedToCore(
        func,
        name,
        4096,
        NULL,
        priority,
        stack_buffer,
        &tcb_buffer,
        core
    );
}

esp_err_t app_manager_init(void) {
    ESP_LOGI(TAG, "🚀 Initializing EVM Application Manager");
    
    // راه‌اندازی shared hardware
    esp_err_t hw_ret = shared_hardware_init(true);
    if (hw_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize shared hardware");
        return hw_ret;
    }
    
    // راه‌اندازی EVM loader core
    esp_err_t dc_ret = evm_loader_core_init();
    if (dc_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize EVM loader core");
        return dc_ret;
    }
    
    // Initialize button GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) | 
                       (1ULL << BUTTON_SELECT_GPIO) | 
                       (1ULL << BUTTON_DOWN_GPIO) | 
                       (1ULL << BUTTON_BACK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t gpio_ret = gpio_config(&io_conf);
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to configure buttons");
        return gpio_ret;
    }
    
    ESP_LOGI(TAG, "✅ EVM Application Manager initialized successfully");
    return ESP_OK;
}

// تابع برای استخراج نام واقعی از فایل
static void get_real_app_name(const char *file_path, char *display_name) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        strcpy(display_name, "Unknown");
        return;
    }
    
    char line[256];
    int lines_checked = 0;
    
    while (fgets(line, sizeof(line), file) && lines_checked < 10) {
        lines_checked++;
        
        if (strstr(line, "//") || strstr(line, "/*")) {
            const char *patterns[] = {
                "Name:", "App:", "Program:", "Title:",
                "name:", "app:", "program:", "title:"
            };
            
            for (int i = 0; i < 8; i++) {
                char *pos = strstr(line, patterns[i]);
                if (pos) {
                    pos += strlen(patterns[i]);
                    while (*pos == ' ' || *pos == '\t' || *pos == ':') pos++;
                    
                    int len = 0;
                    while (pos[len] && pos[len] != '\n' && pos[len] != '\r' && 
                           pos[len] != '/' && pos[len] != '*' && len < 30) {
                        display_name[len] = pos[len];
                        len++;
                    }
                    display_name[len] = '\0';
                    
                    fclose(file);
                    return;
                }
            }
        }
    }
    
    fclose(file);
    
    const char *filename = strrchr(file_path, '/');
    if (!filename) filename = file_path;
    else filename++;
    
    strncpy(display_name, filename, 31);
    display_name[31] = '\0';
    
    char *dot = strrchr(display_name, '.');
    if (dot) *dot = '\0';
    
    char *tilde = strchr(display_name, '~');
    if (tilde) *tilde = '\0';
}

// تابع ساده‌شده برای اسکن - بدون تغییر نام
int app_manager_scan(const char *path) {
    ESP_LOGI(TAG, "🔍 Scanning for EVM applications in: %s", path);
    
    // آزاد کردن حافظه قبلی
    if (apps) {
        free(apps);
        apps = NULL;
    }
    app_count = 0;
    
    // بررسی وجود دایرکتوری
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGI(TAG, "📁 Creating directory: %s", path);
        if (mkdir(path, 0755) != 0) {
            ESP_LOGE(TAG, "❌ Failed to create directory: %s", path);
            return 0;
        }
        ESP_LOGI(TAG, "✅ Created directory: %s", path);
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "❌ Cannot open directory: %s", path);
        return 0;
    }
    
    struct dirent *entry;
    
    // اول تعداد فایل‌های EVM رو بشمار
    int total_evm_files = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".js") == 0 || 
                       strcasecmp(ext, ".qml") == 0 ||
                       strcasecmp(ext, ".evm") == 0)) {
                total_evm_files++;
            }
        }
    }
    
    if (total_evm_files == 0) {
        closedir(dir);
        ESP_LOGI(TAG, "📭 No EVM files found in %s", path);
        return 0;
    }
    
    heap_caps_malloc_extmem_enable(4096);
    
    // آزادسازی
    if (apps) {
        heap_caps_free(apps);
        apps = NULL;
    }

    // اختصاص حافظه از PSRAM
    apps = (evm_app_t *)heap_caps_malloc(total_evm_files * sizeof(evm_app_t), MALLOC_CAP_SPIRAM);
    if (!apps) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for apps from PSRAM");
        closedir(dir);
        return 0;
    }
    
    memset(apps, 0, total_evm_files * sizeof(evm_app_t));
    
    // حالا فایل‌ها رو اضافه کن
    rewinddir(dir);
    int successfully_added = 0;
    
    while ((entry = readdir(dir)) != NULL && successfully_added < total_evm_files) {
        if (entry->d_type == DT_REG) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".js") == 0 || 
                       strcasecmp(ext, ".qml") == 0 ||
                       strcasecmp(ext, ".evm") == 0)) {
                
                // ایجاد مسیر کامل
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
                
                // پر کردن اطلاعات برنامه
                strncpy(apps[successfully_added].path, full_path, 
                        sizeof(apps[successfully_added].path) - 1);
                apps[successfully_added].path[sizeof(apps[successfully_added].path) - 1] = '\0';
                
                // استفاده از نام واقعی فایل (بدون پسوند)
                char display_name[32] = {0};
                strncpy(display_name, entry->d_name, sizeof(display_name) - 1);
                char *dot = strrchr(display_name, '.');
                if (dot) *dot = '\0';
                
                // تبدیل به حروف بزرگ برای نمایش بهتر
                for (char *p = display_name; *p; p++) {
                    *p = toupper((unsigned char)*p);
                }
                
                strncpy(apps[successfully_added].name, display_name, 
                        sizeof(apps[successfully_added].name) - 1);
                apps[successfully_added].name[sizeof(apps[successfully_added].name) - 1] = '\0';
                
                // تشخیص نوع
                if (strcasecmp(ext, ".js") == 0) {
                    strcpy(apps[successfully_added].type, "JS");
                } else if (strcasecmp(ext, ".qml") == 0) {
                    strcpy(apps[successfully_added].type, "QML");
                } else if (strcasecmp(ext, ".evm") == 0) {
                    strcpy(apps[successfully_added].type, "EVM");
                }
                
                // گرفتن سایز فایل
                struct stat file_st;
                if (stat(full_path, &file_st) == 0) {
                    apps[successfully_added].size = file_st.st_size;
                } else {
                    apps[successfully_added].size = 0;
                    ESP_LOGW(TAG, "⚠️ Could not get file size for: %s", full_path);
                }
                
                ESP_LOGI(TAG, "📦 Found %s app: %s (%d bytes)", 
                         apps[successfully_added].type, 
                         apps[successfully_added].name,
                         apps[successfully_added].size);
                
                successfully_added++;
            }
        }
    }
    
    closedir(dir);
    app_count = successfully_added;
    
    ESP_LOGI(TAG, "✅ EVM scan complete: %d applications found", app_count);
    
    // نمایش لیست نهایی برنامه‌ها
    for (int i = 0; i < app_count; i++) {
        ESP_LOGI(TAG, "   %d. [%s] %s -> %s", 
                 i + 1, apps[i].type, apps[i].name, apps[i].path);
    }
    
    return app_count;
}

// تابع اجرای برنامه EVM - نسخه ساده شده
esp_err_t app_manager_launch_evm_app(int index) {
    if (index < 0 || index >= app_count) {
        ESP_LOGE(TAG, "❌ Invalid app index: %d", index);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🚀 Launching EVM app: %s (%s)", 
             apps[index].name, apps[index].type);
    
    // اگر برنامه قبلی در حال اجراست، اول متوقفش کن
    if (evm_is_app_running()) {  
        ESP_LOGI(TAG, "🛑 Stopping previous app before launch...");
        evm_stop_app();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // تغییر صفحه قبل از اجرای برنامه
    evm_lvgl_set_app_screen();
    
    // اجرای برنامه
    esp_err_t result = evm_launch_app(apps[index].path);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "✅ EVM app running on APP CPU");
    } else {
        ESP_LOGE(TAG, "❌ Failed to launch EVM app");
        // در صورت شکست، به لانچر برگرد
        evm_lvgl_set_launcher_screen();
    }
    
    return result;
}

// ========================= LVGL Implementation =========================

static void test_basic_colors_immediately1(void) {
    ESP_LOGI(TAG, "🎨 Testing basic colors immediately...");
    
    // بررسی حافظه
    if (esp_get_free_heap_size() < 20000) {
        ESP_LOGE(TAG, "❌ Not enough memory for color test");
        return;
    }
    
    // ایجاد یک صفحه تست ساده
    lv_obj_t *test_screen = lv_obj_create(NULL);
    if (!test_screen) {
        ESP_LOGE(TAG, "❌ Failed to create test screen");
        return;
    }
    
    // پس‌زمینه سیاه
    lv_obj_set_style_bg_color(test_screen, lv_color_black(), 0);
    lv_obj_set_size(test_screen, 160, 128);
    
    // ایجاد مستطیل‌های رنگی
    struct color_test {
        lv_color_t color;
        const char *name;
        int x, y;
    } tests[] = {
        {lv_color_hex(0xFF0000), "RED",    10, 10},
        {lv_color_hex(0x00FF00), "GREEN",  50, 10},
        {lv_color_hex(0x0000FF), "BLUE",   90, 10},
        {lv_color_hex(0xFFFFFF), "WHITE",  10, 50},
        {lv_color_hex(0x000000), "BLACK",  50, 50},
        {lv_color_hex(0xFFFF00), "YELLOW", 90, 50},
    };
    
    for (int i = 0; i < 6; i++) {
        lv_obj_t *rect = lv_obj_create(test_screen);
        if (!rect) {
            ESP_LOGE(TAG, "❌ Failed to create rectangle %d", i);
            continue;
        }
        lv_obj_set_size(rect, 25, 25);  // کوچک‌تر برای جا شدن
        lv_obj_set_pos(rect, tests[i].x, tests[i].y);
        lv_obj_set_style_bg_color(rect, tests[i].color, 0);
        lv_obj_set_style_border_width(rect, 0, 0);
        lv_obj_set_style_radius(rect, 0, 0);  // بدون rounded corners
        
        lv_obj_t *label = lv_label_create(test_screen);
        if (label) {
            lv_label_set_text(label, tests[i].name);
            lv_obj_set_pos(label, tests[i].x, tests[i].y + 30);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        }
    }
    
    // نمایش صفحه تست
    lv_scr_load(test_screen);
    lv_refr_now(NULL);  // فوراً رفرش کن
    
    ESP_LOGI(TAG, "Color test displayed - check LCD for:");
    ESP_LOGI(TAG, "  Top: RED, GREEN, BLUE");
    ESP_LOGI(TAG, "  Bottom: WHITE, BLACK, YELLOW");
    
    // صبر کن و لاگ کن
    for (int i = 0; i < 30; i++) {  // 3 ثانیه
        vTaskDelay(pdMS_TO_TICKS(100));
        lv_tick_inc(10);
        
        if (xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }
    
    // برگرد به صفحه اصلی (اگر وجود دارد)
    if (permanent_launcher_screen) {
        lv_scr_load(permanent_launcher_screen);
    }
    
    // پاکسازی
    lv_obj_del(test_screen);
    
    ESP_LOGI(TAG, "✅ Color test completed");
}




static void create_analog_clock_with_images(void) {
    ESP_LOGI(TAG, "🕐 Creating Analog Clock with Scaled Images");
    
   lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_size(screen, 160, 128);
    
    // صفحه ساعت (کوچک شده)
    lv_obj_t *watch_face = lv_img_create(screen);
  //  lv_obj_set_size(watch_face, 100, 100);  // م//ناسب برای LCD
    lv_img_set_zoom(watch_face, 80); // 25% scale
   
    lv_obj_align(watch_face, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(watch_face, "S:apps/watch.png");
    

    // عقربه ساعت
    lv_obj_t *hour_hand = lv_img_create(screen);

    lv_img_set_zoom(hour_hand, 80); // 37.5% scale
    lv_img_set_pivot(hour_hand, 8, 25); // نقطه چرخش با توجه به scale
    lv_obj_align(hour_hand, LV_ALIGN_CENTER, 0, 0);
        lv_img_set_src(hour_hand, "S:/apps/hour.png");


    // عقربه دقیقه
    lv_obj_t *minute_hand = lv_img_create(screen);
   
    lv_img_set_zoom(minute_hand, 80); // 37.5% scale
    lv_img_set_pivot(minute_hand, 6, 30);
    lv_obj_align(minute_hand, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(minute_hand, "S:/apps/minute.png");
    
    // عقربه ثانیه
    lv_obj_t *second_hand = lv_img_create(screen);
    
    lv_img_set_zoom(second_hand, 80); // 37.5% scale
    lv_img_set_pivot(second_hand, 4, 35);
    lv_obj_align(second_hand, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(second_hand, "S:/apps/second.png");


    // مرکز ساعت
    lv_obj_t *center_dot = lv_obj_create(screen);
    lv_obj_set_size(center_dot, 6, 6);
    lv_obj_align(center_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_radius(center_dot, 3, 0);
    
    // اضافه کردن اطلاعات
    lv_obj_t *info_label = lv_label_create(screen);
    lv_label_set_text(info_label, "C Clock - Zoom Applied");
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 5);
    
    lv_scr_load(screen);
    
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "✅ Analog clock created with scaled images");
    
    // انیمیشن تست
    time_t now;
    struct tm timeinfo;
    
    for (int i = 0; i < 6 ; i++) { // 2 دقیقه تست
        time(&now);
        localtime_r(&now, &timeinfo);
        
        int hours = (timeinfo.tm_hour % 12) + 4;
        int mins = timeinfo.tm_min;
        int secs = timeinfo.tm_sec;
        
        // محاسبه زوایای واقعی
        int hour_angle = (hours * 30 + mins * 0.5) * 10;
        int min_angle = (mins * 6 + secs * 0.1) * 10;
        int sec_angle = secs * 6 * 10;
        
        // اعمال چرخش
        lv_img_set_angle(hour_hand, hour_angle);
        lv_img_set_angle(minute_hand, min_angle);
        lv_img_set_angle(second_hand, sec_angle);
        
        // رندر فوری
        lv_refr_now(NULL);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        lv_tick_inc(10);
        if (xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }
    
    if (permanent_launcher_screen) {
        lv_scr_load(permanent_launcher_screen);
    }
    lv_obj_del(screen);
}




#include "../lvgl/examples/libs/png/img_wink_png.c"

void lv_example_png_1(void)
{
     lv_obj_t * img;
    LV_IMG_DECLARE(img_wink_png);
      lv_png_init();
   img = lv_img_create(lv_scr_act());
   lv_img_set_src(img, &img_wink_png);
   lv_obj_align(img, LV_ALIGN_LEFT_MID, 20, 0);
   
   
    img = lv_img_create(lv_scr_act());
    /* Assuming a File system is attached to letter 'A'
     * E.g. set LV_USE_FS_STDIO 'A' in lv_conf.h */
    lv_img_set_src(img, "S:apps/watch.png");

    lv_obj_set_size(img, 100, 100); 
    
}


static void test_basic_colors_immediately(void) {


 //   lv_example_png_1();

create_analog_clock_with_images();
//load_and_decode_png_manual("S:/apps/watch.png");
   


}




// // نسخه سایلنت برای عملکرد بهتر
// void* lvgl_custom_malloc_silent(size_t size) {
//     if(size >= 512) { 
//         // اول PSRAM را امتحان کن
//         void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
//         if(ptr) return ptr;
//     }
    
//     // سپس Internal RAM
//     return heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
// }

// void lvgl_custom_free_silent(void* ptr) {
//     heap_caps_free(ptr);
// }

// void* lvgl_custom_realloc_silent(void* ptr, size_t new_size) {
//     return heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
// }


static void disp_driver_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    lcd101_flush(disp_drv, area, color_p);
    lv_disp_flush_ready(disp_drv);
}

// تعریف constants برای سازگاری با LVGL v8
#define LV_ALIGN_IN_TOP_LEFT     LV_ALIGN_TOP_LEFT
#define LV_ALIGN_IN_TOP_MID      LV_ALIGN_TOP_MID
#define LV_ALIGN_IN_BOTTOM_MID   LV_ALIGN_BOTTOM_MID
#define LV_ALIGN_CENTER          LV_ALIGN_CENTER
#define LV_OBJ_PART_MAIN         LV_PART_MAIN
#define LV_LABEL_PART_MAIN       LV_PART_MAIN
#define LV_BTN_PART_MAIN         LV_PART_MAIN
#define LV_COLOR_WHITE           lv_color_white()

// تابع task اصلی GUI - LVGL v8
static void guiTask(void *pvParameter) {
    ESP_LOGI(TAG, "🎨 Starting GUI Task");
    
  //  esp_task_wdt_add(NULL);
    
    xGuiSemaphore = xSemaphoreCreateMutex();
    if (xGuiSemaphore == NULL) {
        ESP_LOGE(TAG, "❌ Failed to create GUI semaphore");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize LVGL
    lv_init();
       
   lv_png_init();
    lv_bmp_init();
   lv_fs_fatfs_init();

    // تنظیم swap اگر نیاز است
    #if LV_COLOR_16_SWAP
        ESP_LOGI(TAG, "  Color swap is ENABLED");
    #else
        ESP_LOGI(TAG, "  Color swap is DISABLED");
    #endif

    // Allocate display buffers - LVGL v8 style
    static lv_color_t* buf1;
    static lv_color_t* buf2;
    static lv_disp_draw_buf_t disp_buf;

    buf1 = (lv_color_t *)heap_caps_malloc(LVGL_DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(LVGL_DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "❌ Failed to allocate display buffers");
        vTaskDelete(NULL);
        return;
    }
    
    // LVGL v8: استفاده از lv_disp_draw_buf_init
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LVGL_DISP_BUF_SIZE);

    // Initialize display driver - LVGL v8 style
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = 161;
    disp_drv.ver_res = 129;
    disp_drv.flush_cb = disp_driver_flush;
    disp_drv.draw_buf = &disp_buf;  // LVGL v8: استفاده از draw_buf به جای buffer
    
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (!disp) {
        ESP_LOGE(TAG, "❌ Failed to register display");
        free(buf1);
        free(buf2);
        vTaskDelete(NULL);
        return;
    }

  


    // بلافاصله تست رنگ‌ها را اجرا کن


    // ایجاد UI با semaphore
    if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
        create_lvgl_ui();
       
        test_basic_colors_immediately();
       
        xSemaphoreGive(xGuiSemaphore);
        ESP_LOGI(TAG, "✅ Main UI created successfully");
    }
    
    // Main GUI loop
    while (1) {
        lv_tick_inc(10);
        
        if (xSemaphoreTake(xGuiSemaphore, (TickType_t)10) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
     //     esp_task_wdt_reset();
    }
}

// راه‌اندازی اولیه LVGL
static esp_err_t lvgl_init(void) {
    if (lvgl_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🎨 Initializing LVGL");
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        guiTask,
        "gui_task",
        1024 * 8,
        NULL,
        2,
        NULL,
        0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create GUI task");
        return ESP_FAIL;
    }

    lvgl_initialized = true;
    ESP_LOGI(TAG, "✅ LVGL initialized successfully");
    return ESP_OK;
}

// ایجاد رابط کاربری LVGL v8
static void create_lvgl_ui(void) {
    ESP_LOGI(TAG, "Creating LVGL UI (LVGL v8 mode)");

    // 1. ایجاد صفحه دائمی لانچر - LVGL v8
    if (!permanent_launcher_screen) {
        permanent_launcher_screen = lv_obj_create(NULL);  // LVGL v8: فقط یک پارامتر
        lv_obj_set_size(permanent_launcher_screen, 161, 129);
        lv_obj_set_style_bg_color(permanent_launcher_screen, lv_color_hex(0x2D3250), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_scr_load(permanent_launcher_screen);
        ESP_LOGI(TAG, "Permanent launcher screen created");
    }

    // پاک کردن UI قبلی
    lv_obj_clean(permanent_launcher_screen);

    // 3. وضعیت سیستم - LVGL v8
    lv_obj_t *status_bg = lv_obj_create(permanent_launcher_screen);  // فقط یک پارامتر
    lv_obj_set_size(status_bg, 155, 25);
    lv_obj_align(status_bg, LV_ALIGN_TOP_LEFT, 3, 5);  // LVGL v8: align ساده‌تر
    lv_obj_set_style_bg_color(status_bg, lv_color_hex(0x424769), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(status_bg, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_status_label = lv_label_create(status_bg);  // فقط یک پارامتر
    lv_label_set_text(ui_status_label, "Ready");
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0x50FF7F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_status_label, LV_ALIGN_CENTER, 0, 0);

    // 4. لیست برنامه‌ها - LVGL v8
    lv_obj_t *list_container = lv_obj_create(permanent_launcher_screen);
    lv_obj_set_size(list_container, 152, 95);
    lv_obj_align(list_container, LV_ALIGN_TOP_MID, 0, 31);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x424769), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(list_container, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *list_title = lv_label_create(list_container);
    lv_label_set_text(list_title, "Apps:");
    lv_obj_set_style_text_color(list_title, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(list_title, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(list_title, LV_ALIGN_TOP_LEFT, -10, -15);

    ui_app_list = lv_label_create(list_container);
    lv_obj_set_width(ui_app_list, 140);
    lv_obj_align(ui_app_list, LV_ALIGN_TOP_LEFT, -8, 0);
    lv_obj_set_style_text_color(ui_app_list, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_app_list, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 5. برنامه انتخاب شده - LVGL v8
    ui_selected_label = lv_label_create(permanent_launcher_screen);
    lv_label_set_text(ui_selected_label, "Select an app");
    lv_obj_set_style_text_color(ui_selected_label, lv_color_hex(0xFFD700), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_selected_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_selected_label, LV_ALIGN_TOP_MID, 0, 125);

    // 6. دکمه اجرا - LVGL v8
    ui_launch_btn = lv_btn_create(permanent_launcher_screen);
    lv_obj_set_size(ui_launch_btn, 40, 20);
    lv_obj_align(ui_launch_btn, LV_ALIGN_BOTTOM_MID, 52, -37);
    lv_obj_set_style_bg_color(ui_launch_btn, lv_color_hex(0x676FA3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_launch_btn, lv_color_hex(0x8B94C7), LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *btn_label = lv_label_create(ui_launch_btn);
    lv_label_set_text(btn_label, "RUN");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(btn_label, LV_ALIGN_CENTER, 0, 0);

    // 8. به‌روزرسانی اولیه
    update_lvgl_ui();

    ESP_LOGI(TAG, "LVGL v8 UI created successfully");
}

// تابع جدید برای بررسی سلامت آبجکت LVGL
// خط 615 را پیدا کن و نام تابع را تغییر بده:

// از این:
// static bool lv_obj_is_valid(lv_obj_t *obj) {

// به این تغییر بده:
static bool app_manager_lv_obj_is_valid(lv_obj_t *obj) {
    if (!obj) return false;
    
    // صفحه اصلی همیشه معتبر است
    if (obj == lv_scr_act()) return true;
    
    // بررسی آدرس حافظه
    uint32_t obj_addr = (uint32_t)obj;
    if (obj_addr < 0x3FF00000 || obj_addr > 0x3FFFFFFF) {
        return false;
    }
    
    // بررسی والد
    lv_obj_t* parent = lv_obj_get_parent(obj);
    if (!parent && obj != lv_scr_act()) {
        return false;
    }
    
    return true;
}

// بازسازی رابط کاربری
void rebuild_lvgl_ui(void) {
    ESP_LOGI(TAG, "🔄 Rebuilding LVGL UI after crash");
    
    // مطمئن شو این خط استفاده می‌کند از: app_manager_lv_obj_is_valid
    if (ui_scr != NULL && app_manager_lv_obj_is_valid(ui_scr) && esp_get_free_heap_size() > 20000) {
        ESP_LOGI(TAG, "✅ UI objects are valid - refreshing instead of rebuild");
        
        if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, (TickType_t)100) == pdTRUE) {
            update_lvgl_ui();
            xSemaphoreGive(xGuiSemaphore);
        }
        return;
    }
    
    if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
        // اینجا هم مطمئن شو از نام جدید استفاده شده
        if (ui_scr && app_manager_lv_obj_is_valid(ui_scr)) {
            lv_obj_clean(ui_scr);
        }

        // ریست کردن اشاره‌گرها
        ui_scr = NULL;
        ui_app_list = NULL;
        ui_selected_label = NULL;
        ui_status_label = NULL;
        ui_launch_btn = NULL;
        
        // ایجاد مجدد رابط کاربری
        create_lvgl_ui();
        
        xSemaphoreGive(xGuiSemaphore);
        ESP_LOGI(TAG, "✅ LVGL UI rebuilt successfully");
    } else {
        ESP_LOGE(TAG, "❌ Failed to acquire semaphore for UI rebuild");
    }
}

// به روزرسانی رابط کاربری LVGL با محافظت بیشتر
static void update_lvgl_ui(void) {
    // بررسی سلامت اولیه - مطمئن شو از نام جدید استفاده شده
    if (!permanent_launcher_screen || 
        !app_manager_lv_obj_is_valid(permanent_launcher_screen) ||
        !ui_app_list || !ui_selected_label || !ui_status_label || !ui_launch_btn) {
        ESP_LOGE(TAG, "UI objects are invalid - cannot update");
        return;
    }

    // بررسی حافظه - استفاده از PRIu32 برای format صحیح
    if (esp_get_free_heap_size() < 15000) {
        ESP_LOGE(TAG, "Very low memory: %" PRIu32 " bytes - skipping UI update", esp_get_free_heap_size());
        return;
    }

    char buf[512] = "";
    int pos = 0;

    if (app_count == 0) {
        lv_label_set_text(ui_app_list, "No EVM applications found\nAdd .js/.qml to /sdcard/apps");
        lv_label_set_text(ui_selected_label, "No apps available");
    } else {
        // نمایش ۵ اپ (قابل اسکرول)
        int start = (selected_app >= 2) ? selected_app - 2 : 0;
        int end = start + 5;
        if (end > app_count) end = app_count;

        for (int i = start; i < end; i++) {
            if (i == selected_app) {
                snprintf(buf + pos, sizeof(buf) - pos, "> [%s] %s\n", apps[i].type, apps[i].name);
            } else {
                snprintf(buf + pos, sizeof(buf) - pos, "  [%s] %s\n", apps[i].type, apps[i].name);
            }
            pos = strlen(buf);
        }
        lv_label_set_text(ui_app_list, buf);

        // اپ انتخابی
        snprintf(buf, sizeof(buf), "Selected: %s [%s]", apps[selected_app].name, apps[selected_app].type);
        lv_label_set_text(ui_selected_label, buf);
    }

    // محاسبه حافظه با format specifier صحیح
    uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    
    snprintf(buf, sizeof(buf), "RAM:%" PRIu32 "K PSRAM:%" PRIu32 "K", internal_free, psram_free);
    lv_label_set_text(ui_status_label, buf);

    // دکمه RUN/STOP - LVGL v8 style
    lv_obj_t *btn_label = lv_obj_get_child(ui_launch_btn, 0);  // LVGL v8: استفاده از index به جای NULL
    if (evm_is_app_running()) {
        lv_obj_set_style_bg_color(ui_launch_btn, lv_color_hex(0xFF5555), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (btn_label) lv_label_set_text(btn_label, "STOP");
    } else {
        lv_obj_set_style_bg_color(ui_launch_btn, lv_color_hex(0x676FA3), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (btn_label) lv_label_set_text(btn_label, "RUN");
    }
}

// ========================= Button Handler =========================

// تابع کمکی برای توقف ایمن برنامه
static esp_err_t safe_stop_app(void) {
    ESP_LOGI(TAG, "🛑 Safe stopping EVM app...");
    
    if (!evm_is_app_running()) {
        ESP_LOGI(TAG, "⚠️ No app is running");
        return ESP_OK;
    }
    
    evm_request_app_stop();
    
    if (evm_is_app_looping()) {
        ESP_LOGI(TAG, "⏳ Waiting for app loop to stop gracefully...");
        
        int timeout = 0;
        while (evm_is_app_running() && timeout < 10) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout++;
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    shared_hardware_reclaim_control();
    
    ESP_LOGI(TAG, "✅ App stop procedure completed");
    return ESP_OK;
}

static void lvgl_button_task_handler(void *arg) {
    ESP_LOGI(TAG, "EVM Launcher button task started");
   // esp_task_wdt_add(NULL);

    bool prev_up = true, prev_down = true, prev_select = true, prev_back = true;
    int display_counter = 0;
    static bool was_app_running = false;

    while (1) {
        // خواندن دکمه‌ها
        bool up = gpio_get_level(BUTTON_UP_GPIO) == 0;
        bool down = gpio_get_level(BUTTON_DOWN_GPIO) == 0;
        bool select = gpio_get_level(BUTTON_SELECT_GPIO) == 0;
        bool back = gpio_get_level(BUTTON_BACK_GPIO) == 0;

        // دکمه UP
        if (up && !prev_up && !evm_is_app_running()) {
            ESP_LOGI(TAG, "UP button pressed");
            if (app_count > 0) {
                selected_app = (selected_app - 1 + app_count) % app_count;
                if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                    update_lvgl_ui();
                    xSemaphoreGive(xGuiSemaphore);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        // دکمه DOWN
        if (down && !prev_down && !evm_is_app_running()) {
            ESP_LOGI(TAG, "DOWN button pressed");
            if (app_count > 0) {
                selected_app = (selected_app + 1) % app_count;
                if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                    update_lvgl_ui();
                    xSemaphoreGive(xGuiSemaphore);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        // دکمه SELECT - اجرا
        if (select && !prev_select) {
            ESP_LOGI(TAG, "SELECT button pressed");
            if (!evm_is_app_running() && app_count > 0) {
                ESP_LOGI(TAG, "Launching: %s", apps[selected_app].name);
                if (app_manager_launch_evm_app(selected_app) == ESP_OK) {
                    if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                        update_lvgl_ui();
                        xSemaphoreGive(xGuiSemaphore);
                    }
                    evm_lvgl_set_app_screen();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        // دکمه BACK - توقف
        if (back && !prev_back) {
            ESP_LOGI(TAG, "BACK button pressed");
            if (evm_is_app_running()) {
                ESP_LOGI(TAG, "Stopping EVM app...");
                safe_stop_app();   
                
                vTaskDelay(pdMS_TO_TICKS(100));
                
                if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                    evm_lvgl_set_launcher_screen();
                    update_lvgl_ui();
                    xSemaphoreGive(xGuiSemaphore);
                }

                ESP_LOGI(TAG, "Returned to launcher");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // تشخیص پایان اپ
        bool is_app_running_now = evm_is_app_running();
        
        if (was_app_running && !is_app_running_now) {
            ESP_LOGI(TAG, "🔄 App finished automatically, returning to launcher");
            
            vTaskDelay(pdMS_TO_TICKS(500));
            
            if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                evm_lvgl_set_launcher_screen();
                update_lvgl_ui();
                xSemaphoreGive(xGuiSemaphore);
            }
            
            ESP_LOGI(TAG, "✅ Returned to launcher after app completion");
        }
        
        was_app_running = is_app_running_now;

        // آپدیت دوره‌ای UI
        if ((++display_counter >= 40) && !evm_is_app_running()) {
            display_counter = 0;
            if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                update_lvgl_ui();
                xSemaphoreGive(xGuiSemaphore);
            }
        }

        // ذخیره وضعیت
        prev_up = up; prev_down = down; prev_select = select; prev_back = back;

     //   esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ========================= تابع start_ui =========================

esp_err_t app_manager_start_ui(void) {
    ESP_LOGI(TAG, "🖥️ Starting EVM Launcher UI");
    
    // راه‌اندازی LVGL
    ESP_ERROR_CHECK(lvgl_init());
    
    // ایجاد تسک دکمه‌ها
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        lvgl_button_task_handler, 
        "lvgl_buttons", 
        1024 * 8, 
        NULL, 
        2, 
        NULL,
        0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create button task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "=== EVM LAUNCHER READY ===");
    ESP_LOGI(TAG, "PRO CPU: Launcher | APP CPU: EVM Applications");
    ESP_LOGI(TAG, "Free RAM: %" PRIu32 " bytes", esp_get_free_heap_size());
    
    return ESP_OK;
}

// ========================= توابع کمکی =========================

int app_manager_get_count(void) {
    return app_count;
}

const char* app_manager_get_name(int index) {
    if (index < 0 || index >= app_count) {
        return NULL;
    }
    return apps[index].name;
}

int app_manager_get_selected_index(void) {
    return selected_app;
}

bool app_manager_is_app_running(void) {
   return evm_is_app_running();
}

// تابع برای مدیریت بهتر منابع
esp_err_t app_manager_cleanup(void) {
    ESP_LOGI(TAG, "🧹 Final cleanup - stopping all EVM modules");
    
    if (evm_is_app_running()) {
        evm_stop_app();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    evm_loader_deinit();
    
    if (apps) {
        heap_caps_free(apps);
        apps = NULL;
    }
    
    app_count = 0;
    selected_app = 0;
    
    return ESP_OK;
}

// تابع برای به‌روزرسانی دستی UI از خارج
esp_err_t app_manager_refresh_ui(void) {
    if (xGuiSemaphore && xSemaphoreTake(xGuiSemaphore, (TickType_t)100) == pdTRUE) {
        update_lvgl_ui();
        xSemaphoreGive(xGuiSemaphore);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}