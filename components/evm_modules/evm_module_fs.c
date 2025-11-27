#include "evm_module_fs.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "hardware_manager.h"

static const char *TAG = "evm_fs";


static bool g_sd_mounted = false;

#define RETURN_UNDEFINED() js_pushundefined(J)



// static bool hardware_is_sd_mounted(void) {
//     if (!g_sd_mounted) {
//         ESP_LOGW(TAG, "SD not mounted by hardware_manager");
//         return false;  // فقط false برگردون!
//     }
//     struct stat st;
//     if (stat("/sdcard", &st) != 0 || !S_ISDIR(st.st_mode)) {
//         ESP_LOGE(TAG, "Cannot access /sdcard");
//         return false;
//     }
//     ESP_LOGI(TAG, "SD Card is accessible");
//     return true;
// }


// 🔥 تابع جداگانه برای بررسی قابلیت نوشتن
static bool is_sd_writable(void) {
    return true; // ما می‌دونیم SD کار می‌کنه!
}

// 🔥 تابع برای ایجاد خودکار دایرکتوری‌ها
static bool ensure_parent_directory(const char *filename) {
    char path[512];
    strncpy(path, filename, sizeof(path)-1);
    path[sizeof(path)-1] = '\0';

    char *last_slash = strrchr(path, '/');
    if (!last_slash || last_slash == path) return true;
    *last_slash = '\0';

    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    ESP_LOGI(TAG, "Creating parent dir: %s", path);
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return true;
    }
    ESP_LOGE(TAG, "mkdir failed: %s", strerror(errno));
    return false;
}

// 🔥 تابع نوشتن فایل - فقط برای نوشتن چک کند
static void js_fs_writeFileSync(js_State *J) {
    const char *filename = js_tostring(J, 1);
    const char *js_content = js_tostring(J, 2);

    if (!filename || !js_content || strncmp(filename, "/sdcard/", 8) != 0) {
        js_pushboolean(J, 0);
        return;
    }

    // کپی امن محتوا
    size_t len = strlen(js_content);
    char *content = malloc(len + 1);
    if (!content) {
        js_pushboolean(J, 0);
        return;
    }
    strcpy(content, js_content);

    // حالا بنویس!
    FILE *f = fopen(filename, "wb");
    if (!f) {
        free(content);
        js_pushboolean(J, 0);
        return;
    }

    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    free(content);

    js_pushboolean(J, written == len);
}


// 🔥 تابع خواندن فایل - فقط mount بودن چک کند
static void js_fs_readFileSync(js_State *J) {
    const char *filename = js_tostring(J, 1);
    
    ESP_LOGI(TAG, "📖 Reading file: '%s'", filename);
    
    // 🔥 فقط mount بودن چک کن - نه writable بودن
    if (!hardware_is_sd_mounted()) {
        js_pushnull(J);
        return;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        ESP_LOGE(TAG, "❌ Cannot open file for reading '%s' (errno: %d - %s)", 
                 filename, errno, strerror(errno));
        js_pushnull(J);
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(file);
        js_pushstring(J, "");
        return;
    }
    
    char *content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        ESP_LOGE(TAG, "❌ Memory allocation failed for %ld bytes", size);
        js_pushnull(J);
        return;
    }
    
    size_t bytes_read = fread(content, 1, size, file);
    fclose(file);
    
    if (bytes_read != (size_t)size) {
        ESP_LOGE(TAG, "❌ Read incomplete: %zu/%ld bytes", bytes_read, size);
        free(content);
        js_pushnull(J);
        return;
    }
    
    content[bytes_read] = '\0';
    ESP_LOGI(TAG, "✅ Read SUCCESS: %zu bytes from %s", bytes_read, filename);
    js_pushstring(J, content);
    free(content);
}

// 🔥 تابع بررسی وجود فایل
static void js_fs_existsSync(js_State *J) {
    const char *filename = js_tostring(J, 1);
    
    if (!filename) {
        js_pushboolean(J, 0);
        return;
    }
    
    // 🔥 فقط mount بودن چک کن
    if (!hardware_is_sd_mounted()) {
        js_pushboolean(J, 0);
        return;
    }
    
    int result = access(filename, F_OK);
    bool exists = (result == 0);
    
    ESP_LOGI(TAG, "🔍 File exists '%s': %s", filename, exists ? "YES" : "NO");
    js_pushboolean(J, exists ? 1 : 0);
}

// 🔥 تابع لیست دایرکتوری - فقط mount بودن چک کند
static void js_fs_readdirSync(js_State *J) {
    const char *path = js_tostring(J, 1);
    
    ESP_LOGI(TAG, "📁 Listing directory: '%s'", path);
    
    // 🔥 فقط mount بودن چک کن
    if (!hardware_is_sd_mounted()) {
        js_pushnull(J);
        return;
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "❌ Cannot open directory '%s' (errno: %d - %s)", 
                 path, errno, strerror(errno));
        js_pushnull(J);
        return;
    }
    
    js_newarray(J);
    int index = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            js_pushstring(J, entry->d_name);
            js_setindex(J, -2, index++);
        }
    }
    
    closedir(dir);
    ESP_LOGI(TAG, "✅ Found %d files in %s", index, path);
}

// 🔥 تابع اطلاعات فایل - فقط mount بودن چک کند
static void js_fs_statSync(js_State *J) {
    const char *filename = js_tostring(J, 1);
    
    // 🔥 فقط mount بودن چک کن
    if (!hardware_is_sd_mounted()) {
        js_pushnull(J);
        return;
    }
    
    struct stat st;
    if (stat(filename, &st) == 0) {
        js_newobject(J);
        
        js_pushnumber(J, st.st_size);
        js_setproperty(J, -2, "size");
        
        js_pushboolean(J, S_ISREG(st.st_mode) ? 1 : 0);
        js_setproperty(J, -2, "isFile");
        
        js_pushboolean(J, S_ISDIR(st.st_mode) ? 1 : 0);
        js_setproperty(J, -2, "isDirectory");
        
        js_pushnumber(J, st.st_mtime);
        js_setproperty(J, -2, "mtime");
        
        ESP_LOGI(TAG, "📊 File stats: %s -> size: %ld, isFile: %d, isDir: %d", 
                 filename, st.st_size, S_ISREG(st.st_mode), S_ISDIR(st.st_mode));
    } else {
        ESP_LOGE(TAG, "❌ Cannot stat file '%s' (errno: %d - %s)", 
                 filename, errno, strerror(errno));
        js_pushnull(J);
    }
}

// 🔥 تابع حذف فایل - فقط برای نوشتن چک کند
static void js_fs_unlink(js_State *J) {
    const char *filename = js_tostring(J, 1);
    
    // 🔥 فقط برای نوشتن چک کن
    if (!is_sd_writable()) {
        js_pushboolean(J, 0);
        return;
    }
    
    int result = remove(filename);
    bool success = (result == 0);
    
    if (success) {
        ESP_LOGI(TAG, "✅ Deleted file: %s", filename);
    } else {
        ESP_LOGE(TAG, "❌ Failed to delete file '%s' (errno: %d - %s)", 
                 filename, errno, strerror(errno));
    }
    
    js_pushboolean(J, success ? 1 : 0);
}

// 🔥 تابع ایجاد دایرکتوری - فقط برای نوشتن چک کند
static void js_fs_mkdir(js_State *J) {
    const char *path = js_tostring(J, 1);
    ESP_LOGI(TAG, "mkdir: '%s'", path);

    if (!path || strncmp(path, "/sdcard/", 8) != 0) {
        js_pushboolean(J, 0);
        
    }

    // خودکار همه پوشه‌ها رو بساز
    char temp[512];
    strncpy(temp, path, sizeof(temp)-1);
    temp[sizeof(temp)-1] = '\0';

    char *p = temp + 8; // رد شو از /sdcard/
    while ((p = strchr(p, '/'))) {
        *p = '\0';
        mkdir(temp, 0755);
        *p = '/';
        p++;
    }

    int ret = mkdir(path, 0755);
    bool ok = (ret == 0 || errno == EEXIST);

    if (ok) {
        struct stat st;
        ok = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    }

    ESP_LOGI(TAG, "mkdir %s: %s", path, ok ? "OK" : "FAILED");
    js_pushboolean(J, ok);
    
}

// 🔥 تابع حذف دایرکتوری - فقط برای نوشتن چک کند
static void js_fs_rmdir(js_State *J) {
    const char *path = js_tostring(J, 1);
    
    // 🔥 فقط برای نوشتن چک کن
    if (!is_sd_writable()) {
        js_pushboolean(J, 0);
        return;
    }
    
    int result = rmdir(path);
    bool success = (result == 0);
    
    if (success) {
        ESP_LOGI(TAG, "✅ Deleted directory: %s", path);
    } else {
        ESP_LOGE(TAG, "❌ Failed to delete directory '%s' (errno: %d - %s)", 
                 path, errno, strerror(errno));
    }
    
    js_pushboolean(J, success ? 1 : 0);
}

// 🔥 تابع جدید برای بررسی وضعیت دقیق
static void js_fs_getStatus(js_State *J) {
    js_newobject(J);
    
    // وضعیت کلی
    js_pushboolean(J, g_sd_mounted ? 1 : 0);
    js_setproperty(J, -2, "mounted");
    
    // بررسی permissions برای مسیرهای مختلف
    const char* test_paths[] = {"/sdcard", "/sdcard/apps"};
    const char* path_names[] = {"root", "apps"};
    
    for (int i = 0; i < 2; i++) {
        // خواندن
        js_pushboolean(J, access(test_paths[i], R_OK) == 0 ? 1 : 0);
        js_setproperty(J, -2, path_names[i]);
        
        // نوشتن  
        js_pushboolean(J, access(test_paths[i], W_OK) == 0 ? 1 : 0);
        char prop_name[32];
        snprintf(prop_name, sizeof(prop_name), "%s_writable", path_names[i]);
        js_setproperty(J, -2, prop_name);
    }
    
    // اطلاعات خطا
    js_pushnumber(J, errno);
    js_setproperty(J, -2, "errno");
    js_pushstring(J, strerror(errno));
    js_setproperty(J, -2, "error");
}




esp_err_t evm_fs_init(void) {
    ESP_LOGI(TAG, "📁 Initializing EVM Filesystem Module");
    
    // بررسی اولیه mount
    if (!g_sd_mounted) {
        ESP_LOGE(TAG, "❌ SD Card not mounted by hardware_manager");
        return ESP_FAIL;
    }
    
   
    ESP_LOGI(TAG, "✅ Filesystem module initialized (Limited functionality)");
    return ESP_OK;
}

esp_err_t evm_fs_register_js(js_State *J) {
    ESP_LOGI(TAG, "📝 Registering Filesystem module in JavaScript");
    
    js_newobject(J);
    
    // توابع اصلی
    js_newcfunction(J, js_fs_readFileSync, "readFileSync", 1);
    js_setproperty(J, -2, "readFileSync");
    
    js_newcfunction(J, js_fs_writeFileSync, "writeFileSync", 2);
    js_setproperty(J, -2, "writeFileSync");
    
    js_newcfunction(J, js_fs_existsSync, "existsSync", 1);
    js_setproperty(J, -2, "existsSync");
    
    js_newcfunction(J, js_fs_readdirSync, "readdirSync", 1);
    js_setproperty(J, -2, "readdirSync");
    
    js_newcfunction(J, js_fs_statSync, "statSync", 1);
    js_setproperty(J, -2, "statSync");
    
    js_newcfunction(J, js_fs_unlink, "unlink", 1);
    js_setproperty(J, -2, "unlink");
    
    js_newcfunction(J, js_fs_mkdir, "mkdir", 1);
    js_setproperty(J, -2, "mkdir");
    
    js_newcfunction(J, js_fs_rmdir, "rmdir", 1);
    js_setproperty(J, -2, "rmdir");
    
    // 🔥 توابع جدید برای دیباگ
    js_newcfunction(J, js_fs_getStatus, "getStatus", 0);
    js_setproperty(J, -2, "getStatus");
    
   
    // ثابت‌ها
    js_pushstring(J, "/sdcard");
    js_setproperty(J, -2, "SDCARD_PATH");
    
    js_pushstring(J, "/sdcard/apps");
    js_setproperty(J, -2, "APPS_PATH");
    
    js_setglobal(J, "fs");
    
    ESP_LOGI(TAG, "✅ Filesystem module registered with %d functions", 11);
    return ESP_OK;
}