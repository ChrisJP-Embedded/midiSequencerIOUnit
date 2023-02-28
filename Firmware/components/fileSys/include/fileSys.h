
#include <stdio.h>
#include <stdbool.h>


#define MAX_NUM_FILES           10
#define MAX_FILENAME_CHARS      20
#define MAX_FILEPATH_CHARS      30
#define LOCAL_FILE_BUFFER_SIZE  200

typedef struct 
{
    //File data
    uint32_t numBytesInOpenFile;
    char openFilename[MAX_FILENAME_CHARS];
    bool isFileOpen;
    //Partition data
    bool hasMountedSucessfully;
    uint8_t numFiles;
    char (*fileNamesPtr)[MAX_FILENAME_CHARS];
} fileSysInterfaceData_t;


fileSysInterfaceData_t * initFileSystem(void);
void deInitFileSystem(void);

uint8_t fileSys_closeFile(void);
uint8_t fileSys_openFileRW(char * fileName, bool createNew);
uint8_t fileSys_readFile(uint8_t * dataBuffer, uint16_t numBytes);
uint8_t fileSys_deleteFile(char * fileName);
uint8_t fileSys_writeFile(uint8_t * data, uint32_t numBytes, bool closeOnExit);
void fileSys_resetFilePtr(void);