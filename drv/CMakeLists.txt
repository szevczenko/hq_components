idf_component_register(SRCS "battery.c" "but.c" "buzzer.c" "fast_add.c" 
                            "pcf8574.c" "ringBuff.c"
                            "ultrasonar.c" "power_on.c" "led.c"
                            "pwm_drv.c" "water_flow_sensor.c" "dev_config.c" "ota_drv.c" "error_code.c"
                    INCLUDE_DIRS "." 
                    PRIV_REQUIRES config backend esp_adc oled_ui esp_https_ota mbedtls efuse app_update esp-tls nvs_flash)
