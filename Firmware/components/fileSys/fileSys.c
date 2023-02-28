#include "esp_log.h"
#include "include/fileSys.h"
#include "fileSysPrivate.h"

#define LOG_TAG "FileSysComponent"
#define MAX_FILE_SIZE_IN_BYTES 1024*1024

static uint8_t fileSys_refreshLocalData(void);
static uint8_t fileSys_mount(void);
static uint8_t fileSys_unmount(void);

static bool runOnceFlag = true;
static fileSysInterfaceData_t fileSysInterfaceData;
static sFileSys fileSysLocalData;

fileSysInterfaceData_t * initFileSystem(void)
{
    if(runOnceFlag) //Can only run once!
    {
        fileSysLocalData.fileHandle = NULL;
        fileSysLocalData.isMounted  = false;
        fileSysLocalData.openFileNumBytes = 0;
        fileSysLocalData.numFiles = 0;
        fileSysLocalData.partitionTotalBytes = 0;
        fileSysLocalData.partitionUsedBytes = 0;

        memset(fileSysLocalData.openFilePath, 0, MAX_FILEPATH_CHARS);
        memset(fileSysLocalData.localFilenames, 0, sizeof(fileSysLocalData.localFilenames));

        if(fileSys_mount() == 0)
        {
            runOnceFlag = false;
            fileSysInterfaceData.hasMountedSucessfully = true;
            fileSysInterfaceData.numFiles = fileSysLocalData.numFiles;
            fileSysInterfaceData.fileNamesPtr = fileSysLocalData.localFilenames;
        }
        else
        {
            fileSysInterfaceData.hasMountedSucessfully = false;
            fileSysInterfaceData.fileNamesPtr = NULL;
            fileSysInterfaceData.numFiles = 0;
        }

        //Even if the file system mount was a
        //success, no files are open yet so clear
        memset(fileSysInterfaceData.openFilename, 0, MAX_FILENAME_CHARS);
        fileSysInterfaceData.isFileOpen = false;
        fileSysInterfaceData.numBytesInOpenFile = 0;
    }

    return &fileSysInterfaceData;
}


//**** Public
void deInitFileSystem(void)
{
    if(fileSysLocalData.isMounted)
    {
        fileSys_unmount();
        runOnceFlag = false;
    }
}


//**** Public
void fileSys_resetFilePtr(void)
{
    if(fileSysLocalData.fileHandle != NULL) rewind(fileSysLocalData.fileHandle);
}


//**** Public
uint8_t fileSys_openFileRW(char * fileName, bool createNew)
{
    char scratch[MAX_FILEPATH_CHARS] = {0};
    struct stat fileInfo;
    char * fullFilePath = NULL;
    bool fileFound = false;

    //If not file system mounted then
    //abort function, file cant be opened
    if(fileSysLocalData.isMounted == false)
    {
        ESP_LOGE(LOG_TAG, "Attempted to write to file with no file system mounted");
        return 1;
    }

    //If another file is already open, 
    //then close it before continuing 
    if(fileSysLocalData.fileHandle != NULL)
    {
#ifdef AUTO_CLOSE_PREV_FILE_ON_FILE_OPEN
        ESP_LOGI(LOG_TAG, "Attempted to open file whilst another was open, forcing existing file closed");
        fileSys_closeFile();
#else
        ESP_LOGE(LOG_TAG, "Attempted to open file with another currently open - close the file then try again");
        return 1;
#endif
    }

    //If no files exist on the currentltly mounted
    //file system, and we dont have permission
    //to create a new file, then abort function
    if((fileSysLocalData.numFiles == 0) && (createNew == false)) 
    {
        ESP_LOGE(LOG_TAG, "File open error - Zero files exists and not authorized to create new files");
        return 1;
    }
    else
    {   
        //Scan through local record of filenames and confirm
        //that the target file exists before attempting to open
        for(uint8_t i = 0; i < fileSysLocalData.numFiles; ++i)
        {
            if(strcmp(&fileSysLocalData.localFilenames[i][0], fileName) == 0)
            {
                fileFound = true;
                break;
            }
        }
    }

    //If the target file could not be found on 
    //the file system and we dont have permission
    //to create a new file, then abort function
    if((fileFound == false) && (createNew == false))
    {
        ESP_LOGE(LOG_TAG, "Specified file '%s' not found -AND- not authorized to create new files", fileName);
        return 1;
    }
    else if(fileFound == false) //Target file not found, but we are authorized to create new files, so create a new file
    {
        //If creating a new file exceeds the max
        //num files allowed, then abort function
        if(fileSysLocalData.numFiles >= MAX_NUM_FILES) 
        {
            ESP_LOGE(LOG_TAG, "Cannot create new file as max numer of files reached");
            return 1;
        }
    }

    
    //Checks complete, now perform file system operations
    

    //Construct full path of the target file (or file to be created)
    strcpy(scratch, BASE_PATH); //Copy partition root path
    fullFilePath = strcat(scratch, "/"); //Add target filename to path
    fullFilePath = strcat(fullFilePath, fileName); //Add target filename to path

    //ESP_LOGI(LOG_TAG, "Just generated FullFilePath: %s", fullFilePath);

    if(fileFound == true) fileSysLocalData.fileHandle = fopen(fullFilePath, "r+"); //open with read/write access (file must exist)
    else fileSysLocalData.fileHandle = fopen(fullFilePath, "w+"); //create file and open with read/write access

    //If the previous file open operation
    //failed we must abort the function
    if(fileSysLocalData.fileHandle == NULL)
    {
        ESP_LOGE(LOG_TAG, "Call to fopen returned NULL, could not open file");
        ESP_LOGE(LOG_TAG, "errno: %d", errno);
        return 1;
    }
    else    // File opened or created sucessfully
    {
        //Clear local file path storage bytes
        memset(fileSysLocalData.openFilePath, 0, MAX_FILEPATH_CHARS);
        //Update local file path record
        strcpy(fileSysLocalData.openFilePath, fullFilePath);

        if(fileFound == false) //If the requested file didn't exist (meaning we just created it)
        {
            fileSysLocalData.numFiles++; //We just created a file, so increment number of files

            //Manually add this new file name to the local record of file names
            memset(&fileSysLocalData.localFilenames[fileSysLocalData.numFiles - 1][0], 0, MAX_FILEPATH_CHARS);
            strcpy(&fileSysLocalData.localFilenames[fileSysLocalData.numFiles - 1][0], fileName);

            ESP_LOGI(LOG_TAG, "Created new file: %s, with file path: %s", &fileSysLocalData.localFilenames[fileSysLocalData.numFiles - 1][0], fileSysLocalData.openFilePath);
        }
    }

    //Need to keep a record of the number of bytes in the file
    if (stat(fileSysLocalData.openFilePath, &fileInfo) == 0)
    {
	    fileSysLocalData.openFileNumBytes = (uint32_t)fileInfo.st_size;
        ESP_LOGI(LOG_TAG, "fileSize = %ld bytes", fileSysLocalData.openFileNumBytes);
    }
    else
    {   
        ESP_LOGE(LOG_TAG, "Error - littleFs didnt populate stat struct for file info - errno: %d", errno);
        return 1;
    }

    //Update external interface regarding this newly opened file
    memset(fileSysInterfaceData.openFilename, 0, MAX_FILENAME_CHARS);
    strcpy(fileSysInterfaceData.openFilename, fileName);
    fileSysInterfaceData.numBytesInOpenFile = fileSysLocalData.openFileNumBytes;
    fileSysInterfaceData.isFileOpen = true;

    ESP_LOGI(LOG_TAG, "Successfully opened file: %s", fileSysLocalData.openFilePath);
    
    return 0;   //** SUCCESS **//
}


//**** Public
uint8_t fileSys_readFile(uint8_t * dataBuffer, uint16_t numBytes)
{
    //This function performs a read on the currently open file,
    //proceeding from the current file pointer position in file.
    //numBytes of data is written to the address pointed at by
    //'dataBuffer' (which should have been allocated from PSRAM)

    size_t numBytesRead;

    //Abort if file system not mounted or file not currently open
    if((fileSysLocalData.fileHandle == NULL) || (fileSysLocalData.isMounted == false)) 
    {
        ESP_LOGE(LOG_TAG, "Attempted to read from file when no file open");
        return 1;
    }
    else //Execute read operation
    {
        if(dataBuffer == NULL)
        {
            ESP_LOGE(LOG_TAG, "Buffer pointer has no memory allocated!");
            return 1;
        }

        numBytesRead = fread(dataBuffer, sizeof(uint8_t), numBytes, fileSysLocalData.fileHandle);
        if(numBytes != numBytesRead)
        {
            if(feof(fileSysLocalData.fileHandle))
            {
                ESP_LOGI(LOG_TAG, "Reached end of currently open file while reading");
                return 2;
            }
        }

        return 0; //** SUCCESS **//
    }
}


//**** Public
uint8_t fileSys_writeFile(uint8_t * data, uint32_t numBytes, bool closeOnExit)
{
    int ret;

    if(fileSysLocalData.fileHandle == NULL) //Abort save if no file open
    {
        ESP_LOGE(LOG_TAG, "Cannot write data, no file currently open");
        return 1;
    }
    else
    {
        //ESP_LOGI(LOG_TAG, "partitionUsedBytes = %d , partitionTotalBytes = %d , numBytesToWrite = %d",
        //fileSys.partitionUsedBytes, fileSys.partitionTotalBytes, numBytes);

        //Make sure the partition has enough free space available
        if((fileSysLocalData.partitionUsedBytes + numBytes) >= fileSysLocalData.partitionTotalBytes)
        {
            ESP_LOGE(LOG_TAG, "Cannot write data, parition size would be exceeded");
            return 1;
        }
        else //If parition DOES have enough free space
        {
            //Ensure the max file size is not exceeded before saving
            if((fileSysLocalData.openFileNumBytes + numBytes) >= MAX_FILE_SIZE_IN_BYTES)
            {
                ESP_LOGE(LOG_TAG, "Requested write operation would exceed system max file size");
                return 1;
            }
            else //Checks complete, perform file save operation
            {
                ESP_LOGI(LOG_TAG, "Attempting to write %ld num bytes", numBytes);
                if((ret = fwrite(data, sizeof(uint8_t), numBytes, fileSysLocalData.fileHandle)) != numBytes)
                {
                    ESP_LOGE(LOG_TAG, "fileWrite operation failed, fopen returned: %d, errno: %d", ret, errno);
                    return 1;
                }   
                fileSysLocalData.openFileNumBytes += numBytes;
                fflush(fileSysLocalData.fileHandle);
            }
        }
    }

    //If requested, close file.
    if(closeOnExit) fileSys_closeFile();    

    return 0;   //** SUCCESS **//
}


//**** Public
uint8_t fileSys_deleteFile(char * fileName)
{
    char scratch[MAX_FILEPATH_CHARS] = {0};     //sort this out, messy temp storage
    char * fullFilePath;        //Used to store constructed file path
    bool fileExists = false;    //Used to indicate whether target file exists

    fileSys_refreshLocalData(); //Ensure local record of file data is up to data.

    if(fileSysLocalData.isMounted == false) //Abort if fileSys not mounted
    {
        ESP_LOGE(LOG_TAG, "Could not delete file - no file system mounted");
        return 1;
    }
    
    if(fileSysLocalData.numFiles == 0) //Abort if no files exist on the partition
    {
        ESP_LOGE(LOG_TAG, "Failed to delete file - no files exist on littlefs parition yet");
        return 1;
    }

    //Construct full path of the target file.
    strcpy(scratch, BASE_PATH); //Copy partition root path
    fullFilePath = strcat(scratch, "/"); //Add target filename to path
    fullFilePath = strcat(fullFilePath, fileName); //Add target filename to path

    //Check to see if target file is currently open, 
    //if so then the delete operation must be aborted
    if(strcmp(fileSysLocalData.openFilePath, fullFilePath) == 0)
    {
        //Abort file delete operation and exit.
        ESP_LOGE(LOG_TAG, "Cannot delete file that is currently open");
        return 1;
    }

    //Find the target file for deletion. Iterate through
    //all files looking for target filename - a local record
    //was created when the file system was first mounted
    for (uint8_t i = 0; i < fileSysLocalData.numFiles; ++i)
    {
        //Compare the current file name with target
        if(strcmp(fileName, &fileSysLocalData.localFilenames[i][0]) == 0)
        {
            //Target file found!
            fileExists = true;
            break;
        }
    }

    ESP_LOGI(LOG_TAG, "Attempting to delete file with path: %s", fullFilePath);

    if(fileExists) //If the target file exists.
    {
        if(remove(fullFilePath) != 0) //Delete the target file!
        {
            ESP_LOGE(LOG_TAG, "Call to remove() failed. errno: %d", errno);
            return 1;
        }
    }
    else //If the file does NOT exist, abort the deletion operation
    {   
        ESP_LOGE(LOG_TAG, "Failure to delete file - '%s' does not exist", fullFilePath);
        return 1;
    }

    fileSys_refreshLocalData(); //Ensure local record of file data is up to data.

    return 0;   //** SUCCESS **//
}


//**** Public
uint8_t fileSys_closeFile(void)
{
    int result;
    uint8_t retries = 0;

    //Abort if file system not mounted or file not currently open
    if((fileSysLocalData.fileHandle == NULL) || (fileSysLocalData.isMounted == false))
    {
        ESP_LOGI(LOG_TAG, "Attempted to close a file when no file open");
        return 1;
    }
    else
    {   
        //**** TIGHT RETRY LOOP ****
        retryFileClose:
        if(fclose(fileSysLocalData.fileHandle) != 0) //Close operation failed
        {
            if(retries < MAX_FILESYS_RETRIES)
            {
                ++retries;
                goto retryFileClose; //**** TIGHT RETRY LOOP ****
            }
            ESP_LOGE(LOG_TAG, "Call to fclose() failed. errno: %d", errno);
            return 1;
        } 
        
        //**************************************//
        //****** FILE CLOSED SUCCESSFULLY ******//
        //**************************************//

        //Update local data cache
        fileSysLocalData.fileHandle = NULL;
        memset(fileSysLocalData.openFilePath, 0, MAX_FILEPATH_CHARS);
        fileSysLocalData.openFileNumBytes = 0;

        //Update external interface data
        fileSysInterfaceData.isFileOpen = false;
        fileSysInterfaceData.numBytesInOpenFile = 0;
        memset(fileSysInterfaceData.openFilename, 0, MAX_FILENAME_CHARS);
    }

    ESP_LOGI(LOG_TAG, "Sucessfully closed file");

    return 0;   //** SUCCESS **//
}


//**** Private
static uint8_t fileSys_mount(void)
{
    esp_err_t ret;

    //Check if file system is already mounted
    //if a file system is mounted, abort function
    if(fileSysLocalData.isMounted)
    {
        ESP_LOGE(LOG_TAG, "Attemped to mount file system whilst already mounted");
        return 1;
    }

    fileSysLocalData.conf.base_path = BASE_PATH;
    fileSysLocalData.conf.partition_label = PARTITION_LABEL;
    fileSysLocalData.conf.format_if_mount_failed = true;         //REMOVE LATER, PROVIDE OPTION IN SYSTEM UI MENU
    fileSysLocalData.conf.dont_mount = false;
    
    //MOUNT littlefs file system flash partition
    ret = esp_vfs_littlefs_register(&fileSysLocalData.conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Call to esp_vfs_littlefs_register() failed. Failed to initialize littlefs (%s)", esp_err_to_name(ret));
        return 1;
    }

    //Now fileSys is mounted we can read the parition info..
    ret = esp_littlefs_info(fileSysLocalData.conf.partition_label, &fileSysLocalData.partitionTotalBytes, &fileSysLocalData.partitionUsedBytes);

    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "Call to esp_littlefs_info() failed. Problem updating littlefs info (%s) ", esp_err_to_name(ret));
        ESP_LOGE(LOG_TAG, "Aborting - attempting to unmount..");
        if(fileSys_unmount() != 0)
        {
            ESP_LOGE(LOG_TAG, "Failed to unmount parition *** system fault ***");
            fileSysLocalData.isMounted = true; //Update global flag
        }
        return 1;
    }

    fileSysLocalData.isMounted = true; //Update global flag

    fileSys_refreshLocalData(); //Update local file sys data - filenames, num files

    return 0; //** SUCCESS **//
}


//**** Private
static uint8_t fileSys_unmount(void)
{
    //If no file system is mounted
    //then abort unmount operation
    if(fileSysLocalData.isMounted == false)
    {
        ESP_LOGE(LOG_TAG, "Attemped to unmount file system that has not been mounted");
        return 1;
    }

    //All files must be closed before unmounting. 
    //If any are still open, close them before countinuing.
    if(fileSysLocalData.fileHandle != NULL)
    {
#ifdef AUTO_CLOSE_PREV_FILE_ON_UNMOUNT
        ESP_LOGI(LOG_TAG, "Attempting to unmount while file still open, so forcing file close..");
        if(fileSys_closeFile() != 0)
        {
            ESP_LOGE(LOG_TAG, "Problem closing file while trying to unmouting file system");
            return 1;
        }
        ESP_LOGI(LOG_TAG, "File closed successfully");
#else
        ESP_LOGI(LOG_TAG, "Attempted to unmount while file still open, aborting unmount");
        return 1;
#endif
    }

    //****** UNMOUNT littlefs VFS (virtual file system) *******//
    esp_err_t ret = esp_vfs_littlefs_unregister(fileSysLocalData.conf.partition_label);

    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "System fault: failed to unmount file system (%s)", esp_err_to_name(ret));
        return 1;
    } 

    //**** MUST CLEAR!! ****
    fileSysLocalData.isMounted = false;
    fileSysLocalData.partitionTotalBytes = 0;
    fileSysLocalData.partitionUsedBytes = 0;

    return 0; //** SUCCESS **//
}


//**** Private
static uint8_t fileSys_refreshLocalData(void)
{
    //This function scans for files on a mounted 
    //file system. The file names and number of files
    //found are stored locally in the global object 'fileSys'

    DIR * dirPtr = NULL;                //Directory pointer, required for directory operations
    struct dirent * dirItemInfoPtr;     //Used to store the data relating to an item in a directory 
    uint8_t numFiles = 0;               //Temp store for number of files found in a directory
    uint8_t retries = 0;

    //If no file system is mounted
    //then we must abort the function
    if(fileSysLocalData.isMounted == false)
    {
        ESP_LOGE(LOG_TAG, "Attemped to unmount file system that has not been mounted");
        return 1;
    }

    //Clear out the local copy of directory filenames before updating
    memset(fileSysLocalData.localFilenames, 0, sizeof(fileSysLocalData.localFilenames));

    //Must open directory to get list of files
    dirPtr = opendir(BASE_PATH);

    if (dirPtr != NULL) //Directory open SUCCESS
    {
        //Generate list of file names present
        while ((dirItemInfoPtr = readdir(dirPtr)) != NULL)
        {
            size_t size = strlen(dirItemInfoPtr->d_name);
            strcpy(&fileSysLocalData.localFilenames[numFiles][0], dirItemInfoPtr->d_name);
            strcpy(&fileSysLocalData.localFilenames[numFiles][0], dirItemInfoPtr->d_name);
            ESP_LOGI(LOG_TAG, "found file: %s", &fileSysLocalData.localFilenames[numFiles][0]);
            numFiles++;
        }

        fileSysLocalData.numFiles = numFiles;
        fileSysInterfaceData.numFiles = numFiles;

        retryDirClose:  //**** TIGHT RETRY LOOP ****
        if(closedir(dirPtr) != 0)
        {
            if(retries < MAX_FILESYS_RETRIES)
            {
                ++retries;
                goto retryDirClose;  //**** TIGHT RETRY LOOP ****
            }

            ESP_LOGE(LOG_TAG, "Call to closedir() failed. errno = %d", errno);
            return 1;
        }
    }
    else //Directory open FAILURE
    {
        ESP_LOGE(LOG_TAG, "Call to opendir() failed. errno = %d", errno);
        return 1;
    }

    return 0; //** SUCCESS **//
}


