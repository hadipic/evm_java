# App Manager component
idf_component_register(SRCS "app_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES evm_loader  driver  hardware_manager shared_hardware lvgl mujs mongoose json  esp_psram lv-fs)