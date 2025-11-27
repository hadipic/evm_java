# Hardware Manager component
idf_component_register(SRCS "hardware_manager.c" "lcd101.c" "sd_card_driver.c" "wifi_driver.c"
                    INCLUDE_DIRS "include"
                    REQUIRES 
                    driver 
                    fatfs  
                    sdmmc
                    lvgl
                    mongoose
                    json
                    mujs
                    nvs_flash
                    esp_wifi
                    esp_event
                    esp_netif
                    )
                    