# components/evm_modules/CMakeLists.txt

idf_component_register(
    SRCS
        "evm_module_gpio.c"
        "evm_module_timer.c"
        "evm_module.c"
        "evm_module_fs.c"
        "evm_module_process.c"
        "evm_module_console.c"
        "evm_module_lvgl.c"
        "evm_module_json.c"
        "evm_module_mongoose.c"
        "evm_module_wifi.c"
        "evm_module_ftp.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        hardware_manager
        driver
        sdmmc
        fatfs
        lvgl
        mongoose
        json
        mujs
        shared_hardware
        esp_wifi 
        esp_netif 
        esp_event 
        nvs_flash 
        esp_psram
        lv-fs
)