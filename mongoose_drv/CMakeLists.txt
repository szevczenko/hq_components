idf_component_register(SRCS "mongoose_drv.c" "mongoose/mongoose.c" "net.c" "packed_fs.c"
                    INCLUDE_DIRS "." "mongoose" "mongoose/src"  
                    REQUIRES main nvs_flash oled esp_timer)

component_compile_options(-DMG_ENABLE_LINES)
component_compile_options(-DMG_ENABLE_PACKED_FS)
component_compile_options(-DHTTP_URL="http://0.0.0.0:80")
component_compile_options(-DHTTPS_URL="https://0.0.0.0:443")
component_compile_options(-DMG_TLS=MG_TLS_NONE) # change to 'MG_TLS_MBED' to enable TLS
