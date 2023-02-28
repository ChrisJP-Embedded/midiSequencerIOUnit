idf_component_register(SRCS "blePeripheralServer.c" "gatt_svr.c" "misc.c"
                    INCLUDE_DIRS "include"
                    REQUIRES bt freertos nvs_flash)
