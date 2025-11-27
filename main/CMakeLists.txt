# Main component CMakeLists.txt
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES  evm_modules app_manager hardware_manager  shared_hardware lvgl mujs esp_psram)
                    