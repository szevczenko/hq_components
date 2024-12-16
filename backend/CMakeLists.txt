idf_component_register(SRCS "wifidrv.c"
                             "parameters.c" "json.c"
                    INCLUDE_DIRS "." 
                    PRIV_REQUIRES main esp_driver_gpio esp_event esp_netif esp_wifi nvs_flash)
