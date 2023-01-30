#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>  
#include "esp_littlefs.h"
#include "esp_vfs.h"
#include "esp_err.h"

#define AUTO_CLOSE_PREV_FILE_ON_FILE_OPEN 1
#define AUTO_CLOSE_PREV_FILE_ON_UNMOUNT   1

#define BASE_PATH               "/littlefs"
#define PARTITION_LABEL         "fileSys"
#define MAX_FILESYS_RETRIES     3

typedef struct 
{
    esp_vfs_littlefs_conf_t conf;
    bool isMounted;
    uint32_t partitionTotalBytes;
    uint32_t partitionUsedBytes;
    char localFilenames[MAX_NUM_FILES][MAX_FILENAME_CHARS];
    char openFilePath[MAX_FILEPATH_CHARS];
    uint32_t openFileNumBytes;
    FILE * fileHandle;
    uint8_t numFiles;
} sFileSys;