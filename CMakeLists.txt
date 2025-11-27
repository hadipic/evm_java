cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# تنظیم مسیر کامپوننت‌های اضافی
set(EXTRA_COMPONENT_DIRS 
    components
)



project(evm_java_launcher)