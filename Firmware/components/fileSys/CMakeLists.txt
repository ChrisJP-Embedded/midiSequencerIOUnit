idf_component_register(SRCS "fileSys.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_littlefs vfs esp_partition)

#littlefs_create_partition_image(fileSys fileIMAGE FLASH_IN_PROJECT)