idf_component_register(SRCS "systemLowLevel.c" "system.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos fileSys esp_littlefs vfs esp_partition driver nvs_flash blePeripheralServer)

#littlefs_create_partition_image(fileSys fileIMAGE FLASH_IN_PROJECT)