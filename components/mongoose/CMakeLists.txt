# components/mongoose/CMakeLists.txt
idf_component_register(SRCS "http_server.c" "mqtt_client.c" "ftp_server.c" "mqtt_broker.c" "mongoose.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lwip 
                    esp_timer 
                    json
                    mujs
                    nvs_flash
                    esp_wifi
                    esp_event
                    esp_netif
                    driver
                    sdmmc
                    fatfs 
                    lvgl)