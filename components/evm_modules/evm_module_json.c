#include "evm_module.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EVM_JSON";

// تابع کمکی برای تبدیل cJSON به JavaScript
static void cjson_to_js(js_State *J, cJSON *item) {
    if (!item) {
        js_pushnull(J);
        return;
    }

    if (cJSON_IsNull(item)) {
        js_pushnull(J);
    } else if (cJSON_IsFalse(item)) {
        js_pushboolean(J, 0);
    } else if (cJSON_IsTrue(item)) {
        js_pushboolean(J, 1);
    } else if (cJSON_IsBool(item)) {
        js_pushboolean(J, cJSON_IsTrue(item));
    } else if (cJSON_IsNumber(item)) {
        js_pushnumber(J, item->valuedouble);
    } else if (cJSON_IsString(item)) {
        js_pushstring(J, item->valuestring);
    } else if (cJSON_IsArray(item)) {
        js_newarray(J);
        cJSON *child = item->child;
        int index = 0;
        while (child) {
            cjson_to_js(J, child);
            js_setindex(J, -2, index++);
            child = child->next;
        }
    } else if (cJSON_IsObject(item)) {
        js_newobject(J);
        cJSON *child = item->child;
        while (child) {
            cjson_to_js(J, child);
            js_setproperty(J, -2, child->string);
            child = child->next;
        }
    } else {
        js_pushundefined(J);
    }
}

// تابع بهبود یافته برای تبدیل JavaScript به cJSON
static cJSON* js_to_cjson(js_State *J, int idx) {
    if (js_isundefined(J, idx)) {
        return NULL;
    } else if (js_isnull(J, idx)) {
        return cJSON_CreateNull();
    } else if (js_isboolean(J, idx)) {
        return cJSON_CreateBool(js_toboolean(J, idx));
    } else if (js_isnumber(J, idx)) {
        return cJSON_CreateNumber(js_tonumber(J, idx));
    } else if (js_isstring(J, idx)) {
        return cJSON_CreateString(js_tostring(J, idx));
    } else if (js_isarray(J, idx)) {
        cJSON *array = cJSON_CreateArray();
        int len = js_getlength(J, idx);
        for (int i = 0; i < len; i++) {
            js_getindex(J, idx, i);
            cJSON *item = js_to_cjson(J, -1);
            if (item) {
                cJSON_AddItemToArray(array, item);
            }
            js_pop(J, 1);
        }
        return array;
    } else if (js_isobject(J, idx)) {
        cJSON *object = cJSON_CreateObject();
        
        // لیست propertyهای رایج برای بررسی
        const char *common_props[] = {
            "name", "value", "type", "data", "config", "settings", 
            "active", "enabled", "version", "author", "app",
            "display", "sound", "brightness", "timeout", "theme",
            "volume", "path", "icon", "category", "id",
            "width", "height", "rotation", "up", "down", "select", "back",
            "users", "metadata", "hobbies", "age",
            "x", "y", "z", "a", "b", "c",
            NULL
        };
        
        for (int i = 0; common_props[i] != NULL; i++) {
            const char *key = common_props[i];
            js_getproperty(J, idx, key);
            if (!js_isundefined(J, -1) && !js_isnull(J, -1)) {
                cJSON *value = js_to_cjson(J, -1);
                if (value) {
                    cJSON_AddItemToObject(object, key, value);
                }
            }
            js_pop(J, 1);
        }
        
        return object;
    }
    
    return cJSON_CreateNull();
}

// تابع JSON.parse
static void js_json_parse(js_State *J) {
    const char *json_str = js_tostring(J, 1);
    if (!json_str) {
        js_error(J, "JSON.parse: argument must be a string");
        return;
    }
    
    ESP_LOGD(TAG, "Parsing JSON: %.50s...", json_str);
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            ESP_LOGE(TAG, "JSON parse error at: %s", error_ptr);
            js_error(J, "JSON.parse: invalid JSON string");
        } else {
            js_error(J, "JSON.parse: invalid JSON string");
        }
        return;
    }
    
    cjson_to_js(J, root);
    cJSON_Delete(root);
}

// تابع JSON.stringify بهبود یافته
static void js_json_stringify(js_State *J) {
    if (js_isundefined(J, 1)) {
        js_pushundefined(J);
        return;
    }
    
    if (js_isnull(J, 1)) {
        js_pushstring(J, "null");
        return;
    }
    
    cJSON *json_item = js_to_cjson(J, 1);
    if (!json_item) {
        js_pushstring(J, "null");
        return;
    }
    
    char *json_str = cJSON_PrintUnformatted(json_item);
    if (!json_str) {
        js_pushstring(J, "null");
    } else {
        js_pushstring(J, json_str);
        free(json_str);
    }
    
    cJSON_Delete(json_item);
}

// تابع JSON.isValid
static void js_json_is_valid(js_State *J) {
    const char *json_str = js_tostring(J, 1);
    if (!json_str) {
        js_pushboolean(J, 0);
        return;
    }
    
    cJSON *test = cJSON_Parse(json_str);
    if (test) {
        cJSON_Delete(test);
        js_pushboolean(J, 1);
    } else {
        js_pushboolean(J, 0);
    }
}

// تابع JSON.stringify با فرمت (زیبا)
static void js_json_stringify_pretty(js_State *J) {
    if (js_isundefined(J, 1)) {
        js_pushundefined(J);
        return;
    }
    
    if (js_isnull(J, 1)) {
        js_pushstring(J, "null");
        return;
    }
    
    cJSON *json_item = js_to_cjson(J, 1);
    if (!json_item) {
        js_pushstring(J, "null");
        return;
    }
    
    char *json_str = cJSON_Print(json_item);
    if (!json_str) {
        js_pushstring(J, "null");
    } else {
        js_pushstring(J, json_str);
        free(json_str);
    }
    
    cJSON_Delete(json_item);
}

// توابع ساده شده برای قابلیت‌های پیشرفته
static void js_json_keys(js_State *J) {
    if (js_isstring(J, 1)) {
        // اگر رشته JSON است، parse کنیم
        const char *json_str = js_tostring(J, 1);
        cJSON *obj = cJSON_Parse(json_str);
        if (obj && cJSON_IsObject(obj)) {
            js_newarray(J);
            int index = 0;
            cJSON *child = obj->child;
            while (child) {
                js_pushstring(J, child->string);
                js_setindex(J, -2, index++);
                child = child->next;
            }
            cJSON_Delete(obj);
            return;
        }
        if (obj) cJSON_Delete(obj);
    }
    js_newarray(J);
}

static void js_json_values(js_State *J) {
    if (js_isstring(J, 1)) {
        const char *json_str = js_tostring(J, 1);
        cJSON *obj = cJSON_Parse(json_str);
        if (obj && cJSON_IsObject(obj)) {
            js_newarray(J);
            int index = 0;
            cJSON *child = obj->child;
            while (child) {
                cjson_to_js(J, child);
                js_setindex(J, -2, index++);
                child = child->next;
            }
            cJSON_Delete(obj);
            return;
        }
        if (obj) cJSON_Delete(obj);
    }
    js_newarray(J);
}

static void js_json_merge(js_State *J) {
    // فقط برای رشته‌های JSON کار می‌کند
    if (js_isstring(J, 1) && js_isstring(J, 2)) {
        const char *json1 = js_tostring(J, 1);
        const char *json2 = js_tostring(J, 2);
        
        cJSON *obj1 = cJSON_Parse(json1);
        cJSON *obj2 = cJSON_Parse(json2);
        
        if (obj1 && obj2 && cJSON_IsObject(obj1) && cJSON_IsObject(obj2)) {
            cJSON *merged = cJSON_Duplicate(obj1, 1);
            cJSON *child = obj2->child;
            while (child) {
                cJSON_DeleteItemFromObject(merged, child->string);
                cJSON_AddItemToObject(merged, child->string, cJSON_Duplicate(child, 1));
                child = child->next;
            }
            cjson_to_js(J, merged);
            cJSON_Delete(merged);
        } else {
            js_newobject(J);
        }
        
        if (obj1) cJSON_Delete(obj1);
        if (obj2) cJSON_Delete(obj2);
    } else {
        js_newobject(J);
    }
}

static void js_json_get(js_State *J) {
    if (js_isstring(J, 1) && js_isstring(J, 2)) {
        const char *json_str = js_tostring(J, 1);
        const char *path = js_tostring(J, 2);
        
        cJSON *root = cJSON_Parse(json_str);
        if (!root) {
            js_pushundefined(J);
            return;
        }
        
        cJSON *current = root;
        char *path_copy = strdup(path);
        char *token = strtok(path_copy, ".");
        
        while (token && current) {
            if (cJSON_IsObject(current)) {
                current = cJSON_GetObjectItemCaseSensitive(current, token);
            } else if (cJSON_IsArray(current)) {
                int index = atoi(token);
                if (index >= 0 && index < cJSON_GetArraySize(current)) {
                    current = cJSON_GetArrayItem(current, index);
                } else {
                    current = NULL;
                }
            } else {
                current = NULL;
            }
            token = strtok(NULL, ".");
        }
        
        if (current) {
            cjson_to_js(J, current);
        } else {
            js_pushundefined(J);
        }
        
        free(path_copy);
        cJSON_Delete(root);
    } else {
        js_pushundefined(J);
    }
}

static void js_json_type(js_State *J) {
    const char *json_str = js_tostring(J, 1);
    if (!json_str) {
        js_pushstring(J, "invalid");
        return;
    }
    
    cJSON *item = cJSON_Parse(json_str);
    if (!item) {
        js_pushstring(J, "invalid");
        return;
    }
    
    const char *type_str;
    if (cJSON_IsNull(item)) {
        type_str = "null";
    } else if (cJSON_IsFalse(item) || cJSON_IsTrue(item)) {
        type_str = "boolean";
    } else if (cJSON_IsNumber(item)) {
        type_str = "number";
    } else if (cJSON_IsString(item)) {
        type_str = "string";
    } else if (cJSON_IsArray(item)) {
        type_str = "array";
    } else if (cJSON_IsObject(item)) {
        type_str = "object";
    } else {
        type_str = "unknown";
    }
    
    js_pushstring(J, type_str);
    cJSON_Delete(item);
}

static void js_json_length(js_State *J) {
    const char *json_str = js_tostring(J, 1);
    if (!json_str) {
        js_pushnumber(J, 0);
        return;
    }
    
    cJSON *item = cJSON_Parse(json_str);
    if (!item) {
        js_pushnumber(J, 0);
        return;
    }
    
    int length = 0;
    if (cJSON_IsArray(item)) {
        length = cJSON_GetArraySize(item);
    } else if (cJSON_IsObject(item)) {
        cJSON *child = item->child;
        while (child) {
            length++;
            child = child->next;
        }
    }
    
    js_pushnumber(J, length);
    cJSON_Delete(item);
}

static void js_json_minify(js_State *J) {
    const char *json_str = js_tostring(J, 1);
    if (!json_str) {
        js_pushstring(J, "");
        return;
    }
    
    char *minified = strdup(json_str);
    if (!minified) {
        js_pushstring(J, json_str);
        return;
    }
    
    cJSON_Minify(minified);
    js_pushstring(J, minified);
    free(minified);
}

static void js_json_escape(js_State *J) {
    const char *str = js_tostring(J, 1);
    if (!str) {
        js_pushstring(J, "");
        return;
    }
    
    cJSON *temp = cJSON_CreateString(str);
    if (!temp) {
        js_pushstring(J, str);
        return;
    }
    
    char *escaped = cJSON_PrintUnformatted(temp);
    if (!escaped) {
        js_pushstring(J, str);
    } else {
        if (escaped[0] == '"' && escaped[strlen(escaped)-1] == '"') {
            escaped[strlen(escaped)-1] = '\0';
            js_pushstring(J, escaped + 1);
        } else {
            js_pushstring(J, escaped);
        }
        free(escaped);
    }
    
    cJSON_Delete(temp);
}

// تابع مقداردهی اولیه ماژول
esp_err_t evm_json_init(void) {
    ESP_LOGI(TAG, "Initializing JSON module");
    return ESP_OK;
}

// تابع ثبت ماژول در MuJS
esp_err_t evm_json_register_js(js_State *J) {
    if (!J) return ESP_FAIL;
    
    ESP_LOGI(TAG, "Registering JSON module in JavaScript");
    
    // ایجاد object JSON
    js_newobject(J);
    
    // توابع اصلی
    js_newcfunction(J, js_json_parse, "parse", 1);
    js_setproperty(J, -2, "parse");
    
    js_newcfunction(J, js_json_stringify, "stringify", 1);
    js_setproperty(J, -2, "stringify");
    
    js_newcfunction(J, js_json_stringify_pretty, "stringifyPretty", 1);
    js_setproperty(J, -2, "stringifyPretty");
    
    js_newcfunction(J, js_json_is_valid, "isValid", 1);
    js_setproperty(J, -2, "isValid");
    
    // توابع پیشرفته
    js_newcfunction(J, js_json_merge, "merge", 2);
    js_setproperty(J, -2, "merge");
    
    js_newcfunction(J, js_json_get, "get", 2);
    js_setproperty(J, -2, "get");
    
    js_newcfunction(J, js_json_type, "type", 1);
    js_setproperty(J, -2, "type");
    
    js_newcfunction(J, js_json_keys, "keys", 1);
    js_setproperty(J, -2, "keys");
    
    js_newcfunction(J, js_json_values, "values", 1);
    js_setproperty(J, -2, "values");
    
    js_newcfunction(J, js_json_length, "length", 1);
    js_setproperty(J, -2, "length");
    
    js_newcfunction(J, js_json_minify, "minify", 1);
    js_setproperty(J, -2, "minify");
    
    js_newcfunction(J, js_json_escape, "escape", 1);
    js_setproperty(J, -2, "escape");
    
    // تنظیم به عنوان global
    js_setglobal(J, "JSON");
    
    ESP_LOGI(TAG, "✅ JSON module registered with %d functions", 12);
    return ESP_OK;
}