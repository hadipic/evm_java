# EVM Loader component
idf_component_register(SRCS "evm_loader.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos mujs driver evm_modules lvgl hardware_manager app_manager lv-fs)