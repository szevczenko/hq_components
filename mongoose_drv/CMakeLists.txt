idf_component_register(
                    SRC_DIRS "." "mongoose"
                    INCLUDE_DIRS "." "mongoose" "mongoose/src"
                    REQUIRES backend esp_timer
                    PRIV_REQUIRES nvs_flash drv main)

component_compile_options(-DMG_ENABLE_LWIP=1)
component_compile_options(-DMG_ENABLE_LINES)
component_compile_options(-DHTTP_URL="http://0.0.0.0:80")
component_compile_options(-DHTTPS_URL="https://0.0.0.0:443")
component_compile_options(-DMG_TLS=MG_TLS_NONE) # change to 'MG_TLS_MBED' to enable TLS

set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LINK_INTERFACE_MULTIPLICITY 6)
