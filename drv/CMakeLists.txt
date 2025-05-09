idf_component_register(SRCS "battery.c" "but.c" "buzzer.c" "fast_add.c" 
                            "keepalive.c" "pcf8574.c" "ringBuff.c" "sleep.c"
                            "ultrasonar.c" "power_on.c" "led.c"
                            "pwm_drv.c" "water_flow_sensor.c" "dev_config.c" "ota_drv.c" "error_code.c"
                            "xkc-kl200-uart.c" "e108-gn03.c"
                    INCLUDE_DIRS "." 
                    REQUIRES drv main)
