cmake_minimum_required(VERSION 3.5)

set(LVGL_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})

# فقط فایل‌های مورد نیاز LVGL
file(GLOB_RECURSE SOURCES 
    ${LVGL_ROOT_DIR}/src/*.c
)

# حذف فایل‌های مشکل‌ساز filesystem


idf_component_register(SRCS ${SOURCES} 
                    INCLUDE_DIRS 
                    ${LVGL_ROOT_DIR}
                    ${LVGL_ROOT_DIR}/src
                    REQUIRES fatfs)  # اگر به fatfs نیاز دارید