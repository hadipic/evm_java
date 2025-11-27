#include "evm_module_lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>

#include <errno.h>           // برای errno
#include <dirent.h>          // برای DIR, struct dirent, opendir, readdir, closedir
#include <sys/stat.h>        // برای stat, mkdir

#include "lvgl.h"

#include "lvgl.h"

#ifndef LV_ALIGN_DEFAULT
#define LV_ALIGN_DEFAULT LV_ALIGN_TOP_LEFT
#endif



// 🔥 اصلاح: تعریف ثابت‌های缺失 برای نسخه‌های مختلف LVGL

 #define LV_GRID_FR 1
// #define LV_GRID_CONTENT 0
// #define LV_GRID_TEMPLATE_LAST 0x7FFF



static const char* TAG = "evm_lvgl_mujs";

// صفحه‌های مجزا برای برنامه‌ها
static lv_obj_t *app_screen = NULL;

//static lv_obj_t *launcher_screen = NULL;

lv_obj_t *permanent_launcher_screen = NULL;

// مدیریت اشیاء برنامه
static lv_obj_t *app_objects[100];
static int app_object_count = 0;

// ==================== مدیریت حافظه پیشرفته ====================

typedef struct {
    lv_obj_t *obj;
    char *type;
    size_t memory_usage;
    bool managed;
    void *user_data;
    uint32_t timestamp;
    const char *creator;
} lvgl_object_t;

typedef struct {
    lvgl_object_t **object_pool;
    size_t pool_size;
    size_t pool_capacity;
    SemaphoreHandle_t mutex;
    size_t total_allocated;
    size_t peak_memory;
    bool thread_safe;
} lvgl_memory_manager_t;

static lvgl_memory_manager_t memory_manager = {0};

// تخمین مصرف حافظه
static size_t estimate_memory_usage(const char *type) {
    if (!type) return 512;
    
    static const struct {
        const char *type;
        size_t size;
    } type_sizes[] = {
        {"label", 256}, {"button", 512}, {"img", 1024}, {"bar", 384},
        {"slider", 448}, {"switch", 384}, {"checkbox", 320}, 
        {"textarea", 768}, {"dropdown", 640}, {"object", 512}
    };
    
    for (size_t i = 0; i < sizeof(type_sizes)/sizeof(type_sizes[0]); i++) {
        if (strcmp(type, type_sizes[i].type) == 0) {
            return type_sizes[i].size;
        }
    }
    return 512;
}

// راه‌اندازی مدیریت حافظه پیشرفته
static esp_err_t lvgl_memory_init(bool thread_safe) {
    ESP_LOGI(TAG, "🔧 Initializing Advanced LVGL Memory Manager");
    
    memset(&memory_manager, 0, sizeof(memory_manager));
    memory_manager.pool_capacity = 32;
    memory_manager.object_pool = malloc(memory_manager.pool_capacity * sizeof(lvgl_object_t*));
    
    if (!memory_manager.object_pool) {
        ESP_LOGE(TAG, "❌ Failed to allocate object pool");
        return ESP_ERR_NO_MEM;
    }
    
    memory_manager.thread_safe = thread_safe;
    if (thread_safe) {
        memory_manager.mutex = xSemaphoreCreateMutex();
        if (!memory_manager.mutex) {
            free(memory_manager.object_pool);
            return ESP_FAIL;
        }
    }
    
    ESP_LOGI(TAG, "✅ Advanced LVGL Memory Manager initialized");
    return ESP_OK;
}

// ثبت آبجکت در سیستم مدیریت حافظه پیشرفته
static bool lvgl_register_object_advanced(lv_obj_t *obj, const char *type, const char *creator) {
    if (!obj || !type) return false;
    
    if (memory_manager.thread_safe) {
        xSemaphoreTake(memory_manager.mutex, portMAX_DELAY);
    }
    
    if (memory_manager.pool_size >= memory_manager.pool_capacity) {
        size_t new_capacity = memory_manager.pool_capacity * 2;
        lvgl_object_t **new_pool = realloc(memory_manager.object_pool, 
                                         new_capacity * sizeof(lvgl_object_t*));
        if (!new_pool) {
            if (memory_manager.thread_safe) xSemaphoreGive(memory_manager.mutex);
            return false;
        }
        memory_manager.object_pool = new_pool;
        memory_manager.pool_capacity = new_capacity;
    }
    
    lvgl_object_t *record = malloc(sizeof(lvgl_object_t));
    if (!record) {
        if (memory_manager.thread_safe) xSemaphoreGive(memory_manager.mutex);
        return false;
    }
    
    record->obj = obj;
    record->type = strdup(type);
    record->memory_usage = estimate_memory_usage(type);
    record->managed = true;
    record->user_data = NULL;
    record->timestamp = esp_timer_get_time() / 1000;
    record->creator = creator ? strdup(creator) : NULL;
    
    memory_manager.object_pool[memory_manager.pool_size++] = record;
    memory_manager.total_allocated += record->memory_usage;
    
    if (memory_manager.total_allocated > memory_manager.peak_memory) {
        memory_manager.peak_memory = memory_manager.total_allocated;
    }
    
    if (memory_manager.thread_safe) {
        xSemaphoreGive(memory_manager.mutex);
    }
    
    ESP_LOGD(TAG, "📝 Advanced Registered %s (mem: %zu bytes, total: %zu)", 
             type, record->memory_usage, memory_manager.pool_size);
    return true;
}

// حذف آبجکت از سیستم مدیریت حافظه پیشرفته
static bool lvgl_unregister_object_advanced(lv_obj_t *obj) {
    if (!obj || memory_manager.pool_size == 0) return false;
    
    if (memory_manager.thread_safe) {
        xSemaphoreTake(memory_manager.mutex, portMAX_DELAY);
    }
    
    bool found = false;
    for (size_t i = 0; i < memory_manager.pool_size; i++) {
        lvgl_object_t *record = memory_manager.object_pool[i];
        if (record && record->obj == obj) {
            memory_manager.total_allocated -= record->memory_usage;
            free(record->type);
            if (record->creator) free((char*)record->creator);
            free(record);
            memory_manager.object_pool[i] = NULL;
            found = true;
            break;
        }
    }
    
    if (found) {
        size_t new_index = 0;
        for (size_t i = 0; i < memory_manager.pool_size; i++) {
            if (memory_manager.object_pool[i]) {
                memory_manager.object_pool[new_index++] = memory_manager.object_pool[i];
            }
        }
        memory_manager.pool_size = new_index;
    }
    
    if (memory_manager.thread_safe) {
        xSemaphoreGive(memory_manager.mutex);
    }
    
    return found;
}

// ==================== توابع جدید برای مدیریت صفحه مجزا ====================

// ایجاد صفحه مخصوص برنامه‌ها
static lv_obj_t* evm_lvgl_create_app_screen(void) {
    if (app_screen == NULL) {
        app_screen = lv_obj_create(NULL); // LVGL v8: فقط یک آرگومان
        lv_obj_set_size(app_screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
        // LVGL v8: استفاده از استایل جدید
        lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x000000), LV_PART_MAIN);
        ESP_LOGI(TAG, "🖥️ Created dedicated app screen");
    }
    return app_screen;
}

void evm_lvgl_set_app_screen(void) {
    if (!app_screen) {
        app_screen = lv_obj_create(NULL);
        lv_obj_set_size(app_screen, 160, 128);
        lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    }
    lv_scr_load(app_screen);
    lv_refr_now(NULL);
    ESP_LOGI(TAG, "Switched to app screen");
}

void evm_lvgl_set_launcher_screen(void) {
    if (permanent_launcher_screen) {
        lv_scr_load(permanent_launcher_screen);
        lv_refr_now(NULL);
        ESP_LOGI(TAG, "Back to launcher screen");
    }
}

void evm_lvgl_cleanup_app_screen(void) {
    if (app_screen && app_screen != permanent_launcher_screen) {
        lv_obj_clean(app_screen);
        ESP_LOGI(TAG, "App screen cleaned");
    }
}

// ==================== توابع پایه LVGL v8 ====================

// تابع برای ایجاد آبجکت LVGL - نسخه v8
static void js_lv_obj_create(js_State *J) {
    lv_obj_t *parent = NULL;
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    } else {
        parent = evm_lvgl_create_app_screen();
    }
    
    lv_obj_t *obj = lv_obj_create(parent); // LVGL v8: فقط parent
    evm_lvgl_register_app_object(obj);
    js_pushnumber(J, (intptr_t)obj);
}

// تابع برای تنظیم سایز
static void js_lv_obj_set_size(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_size requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t width = js_toint32(J, 2);
    lv_coord_t height = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_size(obj, width, height);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم موقعیت
static void js_lv_obj_set_pos(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_pos requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t x = js_toint32(J, 2);
    lv_coord_t y = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_pos(obj, x, y);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم تراز - نسخه v8
static void js_lv_obj_align(js_State *J) {
    if (js_gettop(J) < 4) {
        js_error(J, "lv_obj_align requires 4 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_align_t align = js_toint32(J, 2);
    lv_coord_t x_ofs = js_toint32(J, 3);
    lv_coord_t y_ofs = js_toint32(J, 4);
    
    if (obj) {
        lv_obj_align(obj, align, x_ofs, y_ofs);
    }
    
    js_pushundefined(J);
}

// تابع برای حذف آبجکت
static void js_lv_obj_del(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "lv_obj_del requires 1 argument");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    
    if (obj) {
        for (int i = 0; i < app_object_count; i++) {
            if (app_objects[i] == obj) {
                app_objects[i] = NULL;
                break;
            }
        }
        
        lvgl_unregister_object_advanced(obj);
        lv_obj_del(obj);
    }
    
    js_pushundefined(J);
}

// ==================== ویجت‌های پایه LVGL v8 ====================

// تابع برای ایجاد لیبل - نسخه v8
static void js_lv_label_create(js_State *J) {
    lv_obj_t *parent = evm_lvgl_create_app_screen();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *label = lv_label_create(parent); // LVGL v8: فقط parent
    evm_lvgl_register_app_object(label);
    js_pushnumber(J, (intptr_t)label);
}

// تابع برای تنظیم متن
static void js_lv_label_set_text(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_label_set_text requires 2 arguments");
        return;
    }
    
    lv_obj_t *label = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    const char *text = js_tostring(J, 2);
    
    if (label && text) {
        lv_label_set_text(label, text);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم تراز متن - نسخه v8
static void js_lv_label_set_align(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_label_set_text_align requires 2 arguments");
        return;
    }
    
    lv_obj_t *label = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_text_align_t align = js_toint32(J, 2);
    
    if (label) {
        lv_obj_set_style_text_align(label, align, 0);
    }
    
    js_pushundefined(J);
}

// تابع برای ایجاد دکمه - نسخه v8
static void js_lv_btn_create(js_State *J) {
    lv_obj_t *parent = evm_lvgl_create_app_screen();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *btn = lv_btn_create(parent); // LVGL v8: فقط parent
    evm_lvgl_register_app_object(btn);
    js_pushnumber(J, (intptr_t)btn);
}

// ==================== مدیریت استایل LVGL v8 ====================

// تابع برای تنظیم رنگ پس‌زمینه - نسخه v8
static void js_lv_obj_set_style_bg_color(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_bg_color requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_color_t color;
    color.full = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_bg_color(obj, color, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم شفافیت پس‌زمینه - نسخه v8
static void js_lv_obj_set_style_bg_opa(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_bg_opa requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_opa_t opa = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_bg_opa(obj, opa, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم شعاع گوشه - نسخه v8
static void js_lv_obj_set_style_radius(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_radius requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t radius = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_radius(obj, radius, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم عرض حاشیه - نسخه v8
static void js_lv_obj_set_style_border_width(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_border_width requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t width = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_border_width(obj, width, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم رنگ حاشیه - نسخه v8
static void js_lv_obj_set_style_border_color(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_border_color requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_color_t color;
    color.full = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_border_color(obj, color, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم رنگ متن - نسخه v8
static void js_lv_obj_set_style_text_color(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_text_color requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_color_t color;
    color.full = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_text_color(obj, color, selector);
    }
    
    js_pushundefined(J);
}

// ==================== ویجت‌های پیشرفته LVGL v8 ====================

// تابع برای ایجاد تصویر - نسخه v8
static void js_lv_img_create(js_State *J) {
    lv_obj_t *parent = lv_scr_act();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *img = lv_img_create(parent);
    evm_lvgl_register_app_object(img);
    js_pushnumber(J, (intptr_t)img);
}

// تابع برای تنظیم منبع تصویر
static void js_lv_img_set_src(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_img_set_src requires 2 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    const char *src = js_tostring(J, 2);
    
    if (img && src) {
        lv_img_set_src(img, src);
    }
    
    js_pushundefined(J);
}

// تابع برای ایجاد بار پیشرفت - نسخه v8
static void js_lv_bar_create(js_State *J) {
    lv_obj_t *parent = lv_scr_act();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *bar = lv_bar_create(parent);
    evm_lvgl_register_app_object(bar);
    js_pushnumber(J, (intptr_t)bar);
}

// تابع برای تنظیم مقدار بار
static void js_lv_bar_set_value(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_bar_set_value requires 3 arguments");
        return;
    }
    
    lv_obj_t *bar = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    int32_t value = js_toint32(J, 2);
    lv_anim_enable_t anim = js_toint32(J, 3);
    
    if (bar) {
        lv_bar_set_value(bar, value, anim);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم محدوده بار
static void js_lv_bar_set_range(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_bar_set_range requires 3 arguments");
        return;
    }
    
    lv_obj_t *bar = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    int32_t min = js_toint32(J, 2);
    int32_t max = js_toint32(J, 3);
    
    if (bar) {
        lv_bar_set_range(bar, min, max);
    }
    
    js_pushundefined(J);
}

// ==================== مدیریت رنگ ====================

// تابع برای ایجاد رنگ
static void js_lv_color_make(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_color_make requires 3 arguments");
        return;
    }
    
    uint8_t r = js_toint32(J, 1);
    uint8_t g = js_toint32(J, 2);
    uint8_t b = js_toint32(J, 3);
    
    lv_color_t color = lv_color_make(r, g, b);
    js_pushnumber(J, color.full);
}

// تابع برای ایجاد رنگ از HEX
static void js_lv_color_hex(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "lv_color_hex requires 1 argument");
        return;
    }
    
    uint32_t hex = js_toint32(J, 1);
    lv_color_t color = lv_color_hex(hex);
    js_pushnumber(J, color.full);
}

// ==================== اطلاعات سیستم ====================

// تابع برای گرفتن اندازه افقی صفحه
static void js_lv_disp_get_hor_res(js_State *J) {
    lv_coord_t width = lv_disp_get_hor_res(NULL);
    js_pushnumber(J, width);
}

// تابع برای گرفتن اندازه عمودی صفحه
static void js_lv_disp_get_ver_res(js_State *J) {
    lv_coord_t height = lv_disp_get_ver_res(NULL);
    js_pushnumber(J, height);
}

// تابع برای گرفتن صفحه اصلی
static void js_lv_scr_act(js_State *J) {
    lv_obj_t *screen = lv_scr_act();
    js_pushnumber(J, (intptr_t)screen);
}

// ==================== توابع جدید LVGL v8 ====================

// تابع برای ایجاد سوئیچ - ویجت جدید در v8
static void js_lv_switch_create(js_State *J) {
    lv_obj_t *parent = evm_lvgl_create_app_screen();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *sw = lv_switch_create(parent);
    evm_lvgl_register_app_object(sw);
    js_pushnumber(J, (intptr_t)sw);
}

// تابع برای تنظیم وضعیت سوئیچ
static void js_lv_switch_set_state(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_switch_set_state requires 2 arguments");
        return;
    }
    
    lv_obj_t *sw = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    bool state = js_toboolean(J, 2);
    
    if (sw) {
        if (state) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
    }
    
    js_pushundefined(J);
}

// تابع برای ایجاد اسلایدر - ویجت جدید در v8
static void js_lv_slider_create(js_State *J) {
    lv_obj_t *parent = evm_lvgl_create_app_screen();
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_obj_t *slider = lv_slider_create(parent);
    evm_lvgl_register_app_object(slider);
    js_pushnumber(J, (intptr_t)slider);
}

// تابع برای تنظیم مقدار اسلایدر
static void js_lv_slider_set_value(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_slider_set_value requires 3 arguments");
        return;
    }
    
    lv_obj_t *slider = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    int32_t value = js_toint32(J, 2);
    lv_anim_enable_t anim = js_toint32(J, 3);
    
    if (slider) {
        lv_slider_set_value(slider, value, anim);
    }
    
    js_pushundefined(J);
}

// ==================== توابع مدیریت حافظه ====================

void evm_lvgl_register_app_object(void* obj) {
    lv_obj_t *lv_obj = (lv_obj_t*)obj;
    
    if (app_object_count < 100 && lv_obj != NULL) {
        app_objects[app_object_count++] = lv_obj;
        ESP_LOGD(TAG, "📝 Registered LVGL object %p (total: %d)", lv_obj, app_object_count);
    }
    
    lvgl_register_object_advanced(lv_obj, "object", "evm_lvgl_register_app_object");
}

void evm_lvgl_cleanup_app_objects(void) {
    ESP_LOGI(TAG, "🧹 Cleaning up %d LVGL app objects", app_object_count);
    
    if (app_object_count == 0) {
        ESP_LOGI(TAG, "✅ No objects to clean up");
        return;
    }
    
    ESP_LOGI(TAG, "🔄 Using SCREEN cleanup method");
    evm_lvgl_cleanup_app_screen();
    
    for (int i = 0; i < app_object_count; i++) {
        app_objects[i] = NULL;
    }
    app_object_count = 0;
    
    if (memory_manager.object_pool) {
        for (size_t i = 0; i < memory_manager.pool_size; i++) {
            if (memory_manager.object_pool[i]) {
                free(memory_manager.object_pool[i]->type);
                if (memory_manager.object_pool[i]->creator) {
                    free((char*)memory_manager.object_pool[i]->creator);
                }
                free(memory_manager.object_pool[i]);
                memory_manager.object_pool[i] = NULL;
            }
        }
        memory_manager.pool_size = 0;
        memory_manager.total_allocated = 0;
    }
    
    ESP_LOGI(TAG, "✅ LVGL cleanup completed using screen method");
}

// ==================== تابع اصلی ثبت ماژول ====================

esp_err_t evm_lvgl_init(void) {
    ESP_LOGI(TAG, "🔧 Initializing LVGL Module");
    app_object_count = 0;
    memset(app_objects, 0, sizeof(app_objects));
    
    lvgl_memory_init(true);
    return ESP_OK;
}

// ==================== توابع کمکی ====================

// تابع برای گرفتن تعداد اشیاء ثبت شده
static void js_lvgl_get_object_count(js_State *J) {
    js_pushnumber(J, app_object_count);
}

// تابع cleanup برای JavaScript
static void js_cleanup_app(js_State *J) {
    evm_lvgl_cleanup_app_objects();
    js_pushundefined(J);
}


// ==================== توابع مدیریت تصویر ====================

// ==================== توابع مدیریت تصویر برای ساعت آنالوگ ====================

// تابع برای تنظیم زاویه تصویر (برای عقربه‌ها)
// تابع تنظیم زاویه تصویر - استفاده از API اصلی LVGL
static void js_lv_img_set_angle(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_img_set_angle requires 2 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    int16_t angle = js_toint32(J, 2);
    
    if (img) {
        // استفاده از تابع اصلی LVGL که در کد C شما کار می‌کند
        lv_img_set_angle(img, angle);
        ESP_LOGD(TAG, "🔄 Set image angle to %d", angle);
    }
    
    js_pushundefined(J);
}


// تابع تنظیم pivot - استفاده از API اصلی LVGL
static void js_lv_img_set_pivot(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_img_set_pivot requires 3 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t x = js_toint32(J, 2);
    lv_coord_t y = js_toint32(J, 3);
    
    if (img) {
        // استفاده از تابع اصلی LVGL
        lv_img_set_pivot(img, x, y);
        ESP_LOGD(TAG, "🎯 Set image pivot to (%d, %d)", x, y);
    }
    
    js_pushundefined(J);
}


// تابع برای تنظیم بزرگنمایی تصویر
// تابع تنظیم zoom - استفاده از API اصلی LVGL
static void js_lv_img_set_zoom(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_img_set_zoom requires 2 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    uint16_t zoom = js_toint32(J, 2);
    
    if (img) {
        // استفاده از تابع اصلی LVGL
        lv_img_set_zoom(img, zoom);
        ESP_LOGD(TAG, "🔍 Set image zoom to %d", zoom);
    }
    
    js_pushundefined(J);
}


// تابع برای تنظیم استایل تصویر
static void js_lv_img_set_style(js_State *J) {
    if (js_gettop(J) < 4) {
        js_error(J, "lv_img_set_style requires 4 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_style_prop_t prop = js_toint32(J, 2);
    lv_style_value_t value;
    value.num = js_toint32(J, 3);
    lv_style_selector_t selector = js_toint32(J, 4);
    
    if (img) {
        // در LVGL v8 از lv_obj_set_style استفاده می‌شود
        switch (prop) {
            case LV_STYLE_BG_COLOR:
                lv_obj_set_style_bg_color(img, *((lv_color_t*)&value.num), selector);
                break;
            case LV_STYLE_BG_OPA:
                lv_obj_set_style_bg_opa(img, (lv_opa_t)value.num, selector);
                break;
            case LV_STYLE_BORDER_COLOR:
                lv_obj_set_style_border_color(img, *((lv_color_t*)&value.num), selector);
                break;
            case LV_STYLE_BORDER_WIDTH:
                lv_obj_set_style_border_width(img, (lv_coord_t)value.num, selector);
                break;
            case LV_STYLE_RADIUS:
                lv_obj_set_style_radius(img, (lv_coord_t)value.num, selector);
                break;
            case LV_STYLE_TEXT_COLOR:
                lv_obj_set_style_text_color(img, *((lv_color_t*)&value.num), selector);
                break;
            default:
                ESP_LOGW(TAG, "Unsupported style property: %d", prop);
                break;
        }
    }
    
    js_pushundefined(J);
}
// ==================== توابع بارگذاری تصویر از کارت حافظه ====================
// تابع بررسی وجود فایل با LVFS API
static void js_lv_fs_file_exists(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "file_exists requires 1 argument");
        return;
    }
    
    const char *path = js_tostring(J, 1);
    bool exists = false;
    
    if (path) {
        ESP_LOGI(TAG, "📁 Checking file via LVFS: %s", path);
        
        // استفاده از LVFS API به جای fopen مستقیم
        lv_fs_file_t file;
        lv_fs_res_t res = lv_fs_open(&file, path, LV_FS_MODE_RD);
        
        if (res == LV_FS_RES_OK) {
            exists = true;
            lv_fs_close(&file);
            ESP_LOGI(TAG, "✅ File exists via LVFS: %s", path);
        } else {
            ESP_LOGW(TAG, "❌ File not found via LVFS: %s (error: %d)", path, res);
        }
    }
    
    js_pushboolean(J, exists);
}


// تابع برای بررسی وجود فایل
static void js_file_exists(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "file_exists requires 1 argument");
        return;
    }
    
    const char *path = js_tostring(J, 1);
    bool exists = false;
    
    if (path) {
        FILE *file = fopen(path, "r");
        if (file) {
            exists = true;
            fclose(file);
        }
    }
    
    js_pushboolean(J, exists);
}

// تابع برای بارگذاری تصویر از فایل
static void js_img_create_from_file(js_State *J) {
    lv_obj_t *parent = lv_scr_act();
    const char *path = NULL;
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    if (js_gettop(J) > 1 && js_isstring(J, 2)) {
        path = js_tostring(J, 2);
    }
    
    lv_obj_t *img = lv_img_create(parent);
    evm_lvgl_register_app_object(img);
    
    if (path && strlen(path) > 0) {
        ESP_LOGI(TAG, "📁 Loading image: %s", path);
        lv_img_set_src(img, path);
        
        // بررسی موفقیت بارگذاری
        if (lv_img_get_src(img) == NULL) {
            ESP_LOGW(TAG, "❌ Failed to load image: %s", path);
        } else {
            ESP_LOGI(TAG, "✅ Image loaded successfully");
        }
    }
    
    js_pushnumber(J, (intptr_t)img);
}

// تابع بارگذاری تصویر از فایل - نسخه ایمن
static void js_lv_img_create_from_file(js_State *J) {
    lv_obj_t *parent = lv_scr_act();
    const char *path = NULL;
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        parent = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    if (js_gettop(J) > 1 && js_isstring(J, 2)) {
        path = js_tostring(J, 2);
    }
    
    lv_obj_t *img = lv_img_create(parent);
    if (!img) {
        ESP_LOGE(TAG, "❌ Failed to create image object");
        js_pushnumber(J, 0);
        return;
    }
    
    evm_lvgl_register_app_object(img);
    
    if (path && strlen(path) > 0) {
        ESP_LOGI(TAG, "🖼️ Loading image from: %s", path);
        
        // بررسی وجود فایل
        FILE *test_file = fopen(path, "r");
        if (test_file) {
            fclose(test_file);
            ESP_LOGI(TAG, "✅ File verified, setting source...");
            
            // بارگذاری تصویر
            lv_img_set_src(img, path);
            
            // بررسی موفقیت
            if (lv_img_get_src(img) == NULL) {
                ESP_LOGE(TAG, "❌ lv_img_set_src failed for: %s", path);
            } else {
                ESP_LOGI(TAG, "✅ Image source set successfully");
            }
        } else {
            ESP_LOGE(TAG, "❌ Cannot open file: %s", path);
        }
    } else {
        ESP_LOGW(TAG, "⚠️ No path provided, creating empty image");
    }
    
    js_pushnumber(J, (intptr_t)img);
}

// تابع جدید: لیست فایل‌های یک دایرکتوری
// تابع لیست فایل‌ها با LVFS API - نسخه LVGL v8
static void js_list_files(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "list_files requires 1 argument");
        return;
    }
    
    const char *path = js_tostring(J, 1);
    ESP_LOGI(TAG, "📂 Listing directory via LVFS: %s", path);
    
    js_newarray(J);
    int array_index = 0;
    
    // استفاده از LVFS API برای خواندن دایرکتوری - LVGL v8
    lv_fs_dir_t dir;
    lv_fs_res_t res = lv_fs_dir_open(&dir, path);
    
    if (res == LV_FS_RES_OK) {
        char fn[256];
        
        while (1) {
            // 🔥 LVGL v8: فقط ۲ آرگومان
            res = lv_fs_dir_read(&dir, fn);
            if (res != LV_FS_RES_OK || fn[0] == '\0') {
                break;
            }
            
            // اضافه کردن به آرایه JavaScript (فایل‌های معمولی)
            if (fn[0] != '.' && fn[0] != '/') { // فایل‌های پنهان و دایرکتوری‌های خاص را نادیده بگیر
                js_pushstring(J, fn);
                js_setindex(J, -2, array_index++);
                ESP_LOGD(TAG, "   📄 %s", fn);
            }
        }
        
        lv_fs_dir_close(&dir);
        ESP_LOGI(TAG, "✅ Found %d files via LVFS", array_index);
    } else {
        ESP_LOGE(TAG, "❌ Cannot open directory via LVFS: %s (error: %d)", path, res);
    }
}


// تابع برای بارگذاری تصویر از مسیر فایل - نسخه اصلاح شده
static void js_lv_img_set_src_file(js_State *J) {
    if (js_gettop(J) < 2) {
        js_error(J, "lv_img_set_src_file requires 2 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    const char *path = js_tostring(J, 2);
    
    if (img && path) {
        ESP_LOGI(TAG, "📁 Setting image source: %s", path);
        
        // استفاده مستقیم از lv_img_set_src
        lv_img_set_src(img, path);
        
        // بررسی موفقیت
        if (lv_img_get_src(img) == NULL) {
            ESP_LOGW(TAG, "❌ Failed to set image source: %s", path);
            lv_img_set_src(img, "A:0,0,black_50x50");
        } else {
            ESP_LOGI(TAG, "✅ Image source set successfully");
        }
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم pivot پیش‌فرض برای عقربه‌های ساعت
static void js_lv_img_set_hand_pivot(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_img_set_hand_pivot requires 3 arguments");
        return;
    }
    
    lv_obj_t *img = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t x = js_toint32(J, 2);
    lv_coord_t y = js_toint32(J, 3);
    
    if (img) {
        lv_img_set_pivot(img, x, y);
        ESP_LOGD(TAG, "🎯 Set pivot to (%d, %d)", x, y);
    }
    
    js_pushundefined(J);
}



// ==================== توابع جدید برای مدیریت transform ====================

// تابع برای تنظیم زاویه transform
static void js_lv_obj_set_style_transform_angle(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_transform_angle requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    int32_t angle = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_transform_angle(obj, angle, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم pivot X
static void js_lv_obj_set_style_transform_pivot_x(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_transform_pivot_x requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t pivot_x = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_transform_pivot_x(obj, pivot_x, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای تنظیم pivot Y
static void js_lv_obj_set_style_transform_pivot_y(js_State *J) {
    if (js_gettop(J) < 3) {
        js_error(J, "lv_obj_set_style_transform_pivot_y requires 3 arguments");
        return;
    }
    
    lv_obj_t *obj = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    lv_coord_t pivot_y = js_toint32(J, 2);
    lv_style_selector_t selector = js_toint32(J, 3);
    
    if (obj) {
        lv_obj_set_style_transform_pivot_y(obj, pivot_y, selector);
    }
    
    js_pushundefined(J);
}

// تابع برای بارگذاری صفحه
static void js_lv_scr_load(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "lv_scr_load requires 1 argument");
        return;
    }
    
    lv_obj_t *screen = (lv_obj_t*)(intptr_t)js_tonumber(J, 1);
    
    if (screen) {
        lv_scr_load(screen);
    }
    
    js_pushundefined(J);
}

// تابع برای رندر فوری
static void js_lv_refr_now(js_State *J) {
    lv_disp_t *disp = NULL;
    
    if (js_gettop(J) > 0 && js_isnumber(J, 1)) {
        disp = (lv_disp_t*)(intptr_t)js_tonumber(J, 1);
    }
    
    lv_refr_now(disp);
    js_pushundefined(J);
}

// تابع برای افزایش تیک
static void js_lv_tick_inc(js_State *J) {
    if (js_gettop(J) < 1) {
        js_error(J, "lv_tick_inc requires 1 argument");
        return;
    }
    
    uint32_t tick = js_toint32(J, 1);
    lv_tick_inc(tick);
    js_pushundefined(J);
}

// تابع برای مدیریت task
static void js_lv_task_handler(js_State *J) {
    lv_task_handler();
    js_pushundefined(J);
}

// ==================== تابع اصلی ثبت ماژول LVGL v8 ====================

esp_err_t evm_lvgl_register_js_mujs(js_State *J) {
    if (!J) {
        ESP_LOGE(TAG, "❌ Invalid JS state for LVGL registration");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "📦 Registering LVGL v8 module in MuJS");
    
    // ایجاد object lvgl
    js_newobject(J);
    
    // 🔧 گروه ۱: توابع پایه آبجکت (LVGL v8)
    js_newcfunction(J, js_lv_obj_create, "obj_create", 1);
    js_setproperty(J, -2, "obj_create");
    
    js_newcfunction(J, js_lv_obj_set_size, "obj_set_size", 3);
    js_setproperty(J, -2, "obj_set_size");
    
    js_newcfunction(J, js_lv_obj_set_pos, "obj_set_pos", 3);
    js_setproperty(J, -2, "obj_set_pos");
    
    js_newcfunction(J, js_lv_obj_align, "obj_align", 4);
    js_setproperty(J, -2, "obj_align");
    
    js_newcfunction(J, js_lv_obj_del, "obj_del", 1);
    js_setproperty(J, -2, "obj_del");
    
    js_newcfunction(J, js_lv_scr_act, "scr_act", 0);
    js_setproperty(J, -2, "scr_act");
    
    // 📱 گروه ۲: ویجت‌های پایه (LVGL v8)
    js_newcfunction(J, js_lv_label_create, "label_create", 1);
    js_setproperty(J, -2, "label_create");
    
    js_newcfunction(J, js_lv_label_set_text, "label_set_text", 2);
    js_setproperty(J, -2, "label_set_text");
    
    js_newcfunction(J, js_lv_label_set_align, "label_set_text_align", 2);
    js_setproperty(J, -2, "label_set_text_align");
    
    js_newcfunction(J, js_lv_btn_create, "btn_create", 1);
    js_setproperty(J, -2, "btn_create");
    
    js_newcfunction(J, js_lv_img_create, "img_create", 1);
    js_setproperty(J, -2, "img_create");
    
    js_newcfunction(J, js_lv_img_set_src, "img_set_src", 2);
    js_setproperty(J, -2, "img_set_src");
    
    js_newcfunction(J, js_lv_bar_create, "bar_create", 1);
    js_setproperty(J, -2, "bar_create");
    
    js_newcfunction(J, js_lv_bar_set_value, "bar_set_value", 3);
    js_setproperty(J, -2, "bar_set_value");
    
    js_newcfunction(J, js_lv_bar_set_range, "bar_set_range", 3);
    js_setproperty(J, -2, "bar_set_range");
    
    // 🆕 گروه جدید: ویجت‌های مدرن LVGL v8
    js_newcfunction(J, js_lv_switch_create, "switch_create", 1);
    js_setproperty(J, -2, "switch_create");
    
    js_newcfunction(J, js_lv_switch_set_state, "switch_set_state", 2);
    js_setproperty(J, -2, "switch_set_state");
    
    js_newcfunction(J, js_lv_slider_create, "slider_create", 1);
    js_setproperty(J, -2, "slider_create");
    
    js_newcfunction(J, js_lv_slider_set_value, "slider_set_value", 3);
    js_setproperty(J, -2, "slider_set_value");
    
    // 🎨 گروه ۳: مدیریت استایل v8 (API جدید)
    js_newcfunction(J, js_lv_obj_set_style_bg_color, "obj_set_style_bg_color", 3);
    js_setproperty(J, -2, "obj_set_style_bg_color");
    
    js_newcfunction(J, js_lv_obj_set_style_bg_opa, "obj_set_style_bg_opa", 3);
    js_setproperty(J, -2, "obj_set_style_bg_opa");
    
    js_newcfunction(J, js_lv_obj_set_style_radius, "obj_set_style_radius", 3);
    js_setproperty(J, -2, "obj_set_style_radius");
    
    js_newcfunction(J, js_lv_obj_set_style_border_width, "obj_set_style_border_width", 3);
    js_setproperty(J, -2, "obj_set_style_border_width");
    
    js_newcfunction(J, js_lv_obj_set_style_border_color, "obj_set_style_border_color", 3);
    js_setproperty(J, -2, "obj_set_style_border_color");
    
    js_newcfunction(J, js_lv_obj_set_style_text_color, "obj_set_style_text_color", 3);
    js_setproperty(J, -2, "obj_set_style_text_color");
    
    // 🎨 گروه ۴: مدیریت رنگ
    js_newcfunction(J, js_lv_color_make, "color_make", 3);
    js_setproperty(J, -2, "color_make");
    
    js_newcfunction(J, js_lv_color_hex, "color_hex", 1);
    js_setproperty(J, -2, "color_hex");
    
    // 📊 گروه ۵: اطلاعات سیستم
    js_newcfunction(J, js_lv_disp_get_hor_res, "disp_get_hor_res", 0);
    js_setproperty(J, -2, "disp_get_hor_res");
    
    js_newcfunction(J, js_lv_disp_get_ver_res, "disp_get_ver_res", 0);
    js_setproperty(J, -2, "disp_get_ver_res");
    
    // 🧹 گروه ۶: مدیریت حافظه
    js_newcfunction(J, js_cleanup_app, "cleanup_app", 0);
    js_setproperty(J, -2, "cleanup_app");
    
    js_newcfunction(J, js_lvgl_get_object_count, "get_object_count", 0);
    js_setproperty(J, -2, "get_object_count");
    
    // 📋 گروه ۷: ثابت‌های LVGL v8 (به‌روزشده)
    
    // ترازهای جدید v8
    js_pushnumber(J, LV_ALIGN_CENTER);
    js_setproperty(J, -2, "ALIGN_CENTER");
    
    js_pushnumber(J, LV_ALIGN_TOP_LEFT);
    js_setproperty(J, -2, "ALIGN_TOP_LEFT");
    
    js_pushnumber(J, LV_ALIGN_TOP_MID);
    js_setproperty(J, -2, "ALIGN_TOP_MID");
    
    js_pushnumber(J, LV_ALIGN_TOP_RIGHT);
    js_setproperty(J, -2, "ALIGN_TOP_RIGHT");
    
    js_pushnumber(J, LV_ALIGN_BOTTOM_LEFT);
    js_setproperty(J, -2, "ALIGN_BOTTOM_LEFT");
    
    js_pushnumber(J, LV_ALIGN_BOTTOM_MID);
    js_setproperty(J, -2, "ALIGN_BOTTOM_MID");
    
    js_pushnumber(J, LV_ALIGN_BOTTOM_RIGHT);
    js_setproperty(J, -2, "ALIGN_BOTTOM_RIGHT");
    
    js_pushnumber(J, LV_ALIGN_LEFT_MID);
    js_setproperty(J, -2, "ALIGN_LEFT_MID");
    
    js_pushnumber(J, LV_ALIGN_RIGHT_MID);
    js_setproperty(J, -2, "ALIGN_RIGHT_MID");
    
    js_pushnumber(J, LV_ALIGN_DEFAULT);
    js_setproperty(J, -2, "ALIGN_DEFAULT");
    
    // بخش‌های آبجکت v8
    js_pushnumber(J, LV_PART_MAIN);
    js_setproperty(J, -2, "PART_MAIN");
    
    js_pushnumber(J, LV_PART_SCROLLBAR);
    js_setproperty(J, -2, "PART_SCROLLBAR");
    
    js_pushnumber(J, LV_PART_INDICATOR);
    js_setproperty(J, -2, "PART_INDICATOR");
    
    js_pushnumber(J, LV_PART_KNOB);
    js_setproperty(J, -2, "PART_KNOB");
    
    js_pushnumber(J, LV_PART_SELECTED);
    js_setproperty(J, -2, "PART_SELECTED");
    
    js_pushnumber(J, LV_PART_ITEMS);
    js_setproperty(J, -2, "PART_ITEMS");
    
    js_pushnumber(J, LV_PART_TICKS);
    js_setproperty(J, -2, "PART_TICKS");
    
    js_pushnumber(J, LV_PART_CURSOR);
    js_setproperty(J, -2, "PART_CURSOR");
    
    // وضعیت‌های v8
    js_pushnumber(J, LV_STATE_DEFAULT);
    js_setproperty(J, -2, "STATE_DEFAULT");
    
    js_pushnumber(J, LV_STATE_CHECKED);
    js_setproperty(J, -2, "STATE_CHECKED");
    
    js_pushnumber(J, LV_STATE_FOCUSED);
    js_setproperty(J, -2, "STATE_FOCUSED");
    
    js_pushnumber(J, LV_STATE_FOCUS_KEY);
    js_setproperty(J, -2, "STATE_FOCUS_KEY");
    
    js_pushnumber(J, LV_STATE_EDITED);
    js_setproperty(J, -2, "STATE_EDITED");
    
    js_pushnumber(J, LV_STATE_HOVERED);
    js_setproperty(J, -2, "STATE_HOVERED");
    
    js_pushnumber(J, LV_STATE_PRESSED);
    js_setproperty(J, -2, "STATE_PRESSED");
    
    js_pushnumber(J, LV_STATE_SCROLLED);
    js_setproperty(J, -2, "STATE_SCROLLED");
    
    js_pushnumber(J, LV_STATE_DISABLED);
    js_setproperty(J, -2, "STATE_DISABLED");
    
    js_pushnumber(J, LV_STATE_USER_1);
    js_setproperty(J, -2, "STATE_USER_1");
    
    js_pushnumber(J, LV_STATE_USER_2);
    js_setproperty(J, -2, "STATE_USER_2");
    
    js_pushnumber(J, LV_STATE_USER_3);
    js_setproperty(J, -2, "STATE_USER_3");
    
    js_pushnumber(J, LV_STATE_USER_4);
    js_setproperty(J, -2, "STATE_USER_4");
    
    // ترکیب‌های وضعیت رایج
    js_pushnumber(J, LV_STATE_CHECKED | LV_STATE_PRESSED);
    js_setproperty(J, -2, "STATE_CHECKED_PRESSED");
    
    js_pushnumber(J, LV_STATE_FOCUSED | LV_STATE_PRESSED);
    js_setproperty(J, -2, "STATE_FOCUSED_PRESSED");
    
    // تراز متن v8
    js_pushnumber(J, LV_TEXT_ALIGN_LEFT);
    js_setproperty(J, -2, "TEXT_ALIGN_LEFT");
    
    js_pushnumber(J, LV_TEXT_ALIGN_CENTER);
    js_setproperty(J, -2, "TEXT_ALIGN_CENTER");
    
    js_pushnumber(J, LV_TEXT_ALIGN_RIGHT);
    js_setproperty(J, -2, "TEXT_ALIGN_RIGHT");
    
    js_pushnumber(J, LV_TEXT_ALIGN_AUTO);
    js_setproperty(J, -2, "TEXT_ALIGN_AUTO");
    
    // انیمیشن
    js_pushnumber(J, LV_ANIM_OFF);
    js_setproperty(J, -2, "ANIM_OFF");
    
    js_pushnumber(J, LV_ANIM_ON);
    js_setproperty(J, -2, "ANIM_ON");
    
    // رنگ‌های پایه
    js_pushnumber(J, 0x000000);
    js_setproperty(J, -2, "COLOR_BLACK");
    
    js_pushnumber(J, 0xFFFFFF);
    js_setproperty(J, -2, "COLOR_WHITE");
    
    js_pushnumber(J, 0xFF0000);
    js_setproperty(J, -2, "COLOR_RED");
    
    js_pushnumber(J, 0x00FF00);
    js_setproperty(J, -2, "COLOR_GREEN");
    
    js_pushnumber(J, 0x0000FF);
    js_setproperty(J, -2, "COLOR_BLUE");
    
    js_pushnumber(J, 0xFFFF00);
    js_setproperty(J, -2, "COLOR_YELLOW");
    
    js_pushnumber(J, 0xFFA500);
    js_setproperty(J, -2, "COLOR_ORANGE");
    
    js_pushnumber(J, 0x800080);
    js_setproperty(J, -2, "COLOR_PURPLE");
    
    js_pushnumber(J, 0x808080);
    js_setproperty(J, -2, "COLOR_GRAY");
    
    js_pushnumber(J, 0x00FFFF);
    js_setproperty(J, -2, "COLOR_CYAN");
    
    js_pushnumber(J, 0xFF00FF);
    js_setproperty(J, -2, "COLOR_MAGENTA");
    
    js_pushnumber(J, 0x8B4513);
    js_setproperty(J, -2, "COLOR_BROWN");
    
    // رنگ‌های متریال دیزاین
    js_pushnumber(J, 0xF44336);
    js_setproperty(J, -2, "COLOR_RED_500");
    
    js_pushnumber(J, 0x4CAF50);
    js_setproperty(J, -2, "COLOR_GREEN_500");
    
    js_pushnumber(J, 0x2196F3);
    js_setproperty(J, -2, "COLOR_BLUE_500");
    
    js_pushnumber(J, 0xFFEB3B);
    js_setproperty(J, -2, "COLOR_YELLOW_500");
    
    js_pushnumber(J, 0x9C27B0);
    js_setproperty(J, -2, "COLOR_PURPLE_500");
    
    // شفافیت
    js_pushnumber(J, LV_OPA_TRANSP);
    js_setproperty(J, -2, "OPA_TRANSP");
    
    js_pushnumber(J, LV_OPA_0);
    js_setproperty(J, -2, "OPA_0");
    
    js_pushnumber(J, LV_OPA_10);
    js_setproperty(J, -2, "OPA_10");
    
    js_pushnumber(J, LV_OPA_20);
    js_setproperty(J, -2, "OPA_20");
    
    js_pushnumber(J, LV_OPA_30);
    js_setproperty(J, -2, "OPA_30");
    
    js_pushnumber(J, LV_OPA_40);
    js_setproperty(J, -2, "OPA_40");
    
    js_pushnumber(J, LV_OPA_50);
    js_setproperty(J, -2, "OPA_50");
    
    js_pushnumber(J, LV_OPA_60);
    js_setproperty(J, -2, "OPA_60");
    
    js_pushnumber(J, LV_OPA_70);
    js_setproperty(J, -2, "OPA_70");
    
    js_pushnumber(J, LV_OPA_80);
    js_setproperty(J, -2, "OPA_80");
    
    js_pushnumber(J, LV_OPA_90);
    js_setproperty(J, -2, "OPA_90");
    
    js_pushnumber(J, LV_OPA_100);
    js_setproperty(J, -2, "OPA_100");
    
    js_pushnumber(J, LV_OPA_COVER);
    js_setproperty(J, -2, "OPA_COVER");
    
    // 🆕 ثابت‌های جدید v8
    js_pushnumber(J, LV_SIZE_CONTENT);
    js_setproperty(J, -2, "SIZE_CONTENT");
    
    js_pushnumber(J, LV_GRID_CONTENT);
    js_setproperty(J, -2, "GRID_CONTENT");
    
    js_pushnumber(J, LV_GRID_FR);
    js_setproperty(J, -2, "GRID_FR");
    
    js_pushnumber(J, LV_GRID_TEMPLATE_LAST);
    js_setproperty(J, -2, "GRID_TEMPLATE_LAST");
    


// در تابع evm_lvgl_register_js_mujs، این بخش را جایگزین کنید:

// 📁 گروه مدیریت فایل و تصویر - نسخه یکپارچه
js_newcfunction(J, js_lv_fs_file_exists, "file_exists", 1);
js_setproperty(J, -2, "file_exists");

js_newcfunction(J, js_lv_img_create_from_file, "img_create_from_file", 2);
js_setproperty(J, -2, "img_create_from_file");

js_newcfunction(J, js_lv_img_set_src_file, "img_set_src", 2);
js_setproperty(J, -2, "img_set_src");

js_newcfunction(J, js_list_files, "list_files", 1);
js_setproperty(J, -2, "list_files");

  // در تابع evm_lvgl_register_js_mujs، بعد از گروه مدیریت استایل اضافه کنید:

// 📸 گروه جدید: مدیریت تصویر پیشرفته
js_newcfunction(J, js_lv_img_set_angle, "img_set_angle", 2);
js_setproperty(J, -2, "img_set_angle");

js_newcfunction(J, js_lv_img_set_pivot, "img_set_pivot", 3);
js_setproperty(J, -2, "img_set_pivot");

js_newcfunction(J, js_lv_img_set_zoom, "img_set_zoom", 2);
js_setproperty(J, -2, "img_set_zoom");

js_newcfunction(J, js_lv_img_set_style, "img_set_style", 4);
js_setproperty(J, -2, "img_set_style");

// 🔄 گروه جدید: توابع transform و مدیریت
js_newcfunction(J, js_lv_obj_set_style_transform_angle, "obj_set_style_transform_angle", 3);
js_setproperty(J, -2, "obj_set_style_transform_angle");

js_newcfunction(J, js_lv_obj_set_style_transform_pivot_x, "obj_set_style_transform_pivot_x", 3);
js_setproperty(J, -2, "obj_set_style_transform_pivot_x");

js_newcfunction(J, js_lv_obj_set_style_transform_pivot_y, "obj_set_style_transform_pivot_y", 3);
js_setproperty(J, -2, "obj_set_style_transform_pivot_y");

js_newcfunction(J, js_lv_scr_load, "scr_load", 1);
js_setproperty(J, -2, "scr_load");

js_newcfunction(J, js_lv_refr_now, "refr_now", 1);
js_setproperty(J, -2, "refr_now");

js_newcfunction(J, js_lv_tick_inc, "tick_inc", 1);
js_setproperty(J, -2, "tick_inc");

js_newcfunction(J, js_lv_task_handler, "task_handler", 0);
js_setproperty(J, -2, "task_handler");

    // تنظیم به عنوان global
    js_setglobal(J, "lvgl");
    
    ESP_LOGI(TAG, "✅ LVGL v8 module registered in MuJS with %d functions and constants", 75);
    return ESP_OK;
}

void evm_lvgl_cleanup(void) {
    evm_lvgl_cleanup_app_objects();
    
    if (memory_manager.object_pool) {
        free(memory_manager.object_pool);
        memory_manager.object_pool = NULL;
    }
    
    if (memory_manager.mutex) {
        vSemaphoreDelete(memory_manager.mutex);
        memory_manager.mutex = NULL;
    }
    
    ESP_LOGI(TAG, "🧹 LVGL module cleanup completed");
}