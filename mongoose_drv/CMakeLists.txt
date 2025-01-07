idf_component_register(
                    SRC_DIRS "." "mongoose"
                    INCLUDE_DIRS "." "mongoose" "mongoose/src"
                    REQUIRES backend esp_timer app_update
                    PRIV_REQUIRES nvs_flash drv main)


set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LINK_INTERFACE_MULTIPLICITY 6)
