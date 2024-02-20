idf_component_register(SRCS "oled.c" "oled_fonts.c" "oled_fonts_mar.c" "menu_drv.c"
                    INCLUDE_DIRS "." 
                    REQUIRES main oled)
