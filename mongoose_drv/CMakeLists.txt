idf_component_register(
                    SRC_DIRS "." "mongoose"
                    INCLUDE_DIRS "." "mongoose" "mongoose/src"
                    EMBED_TXTFILES "certs/ca_cert.pem"
                    REQUIRES backend esp_timer app_update mbedtls
                    PRIV_REQUIRES nvs_flash drv main)


set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LINK_INTERFACE_MULTIPLICITY 6)
