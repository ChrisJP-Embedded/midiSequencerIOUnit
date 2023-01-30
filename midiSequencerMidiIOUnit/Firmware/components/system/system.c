#include <stdio.h>
#include <sys/unistd.h>
#include <memory.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "blePeripheralServer.h"
#include "fileSys.h"
#include "systemLowLevel.h"


#define LOG_TAG "SystemComponent"
#define PLACYBACK_DATA_ALLOCATION_SIZE 1024*1024
#define HAS_MORE_DELTA_TIME_BYTES(X) ((0x80 & X) && (1 << 8))

typedef struct
{
    uint8_t * playbackPtr;
    bool isRunningStatus;
    uint32_t currentDeltaTime;
    uint32_t totalDataLength;
    const uint8_t * const playbackDataBASE;
    uint8_t statusByte;
    uint8_t previousStatusForRunningStatus;
} midiPlaybackRuntimeData_t;

static void playbackMidiData(midiPlaybackRuntimeData_t *playbackDataPtr);
static uint32_t getMidiDeltaTime(uint8_t **deltaTimeBase);
static uint32_t getMicroSecondsPerQuaterNote(uint8_t *const setTempoBase);
static void processMetaMessage(uint8_t **playbackPtr);


static bool isPlayingBack = false;
static bool waitingForDeltaTimer = false;




typedef enum 
{
    metaEvent_sequenceNum = 0x00,
    metaEvent_textField = 0x01,
    metaEvent_copyright = 0x02,
    metaEvent_trackName = 0x03,
    metaEvent_instrumentName = 0x04,
    metaEvent_lyrics = 0x05,
    metaEvent_marker = 0x06,
    metaEvent_cuePoint = 0x07,
    metaEvent_deviceName = 0x09, //new
    metaEvent_channelPrefix = 0x20,
    metaEvent_midiPort = 0x21, //new
    metaEvent_endOfTrack = 0x2F,
    metaEvent_setTempo = 0x51,
    metaEvent_smpteOffset = 0x54,
    metaEvent_setTimeSig = 0x58,
    metaEvent_keySignature = 0x59,
    metaEvent_sequencerSpecific = 0x7F,
} midiMetaEventType_t;

//*************************
//***** SYSTEM LOOP *******
//*************************
void systemEntryPoint(void)
{
    bleToAppQueueItem_t rxBleItem;
    midiPlaybackRuntimeData_t playbackDataStore = 
    {
        .playbackPtr = heap_caps_malloc(PLACYBACK_DATA_ALLOCATION_SIZE, MALLOC_CAP_SPIRAM),
        .playbackDataBASE = playbackDataStore.playbackPtr,
        .isRunningStatus = false
    };

    playbackBufferBASE = playbackDataStore.playbackDataBASE;

    //Allocates from external-on-module PSRAM
    if(playbackDataStore.playbackPtr == NULL)
    {
        while(1)
        {
            ESP_LOGE(LOG_TAG, "fault allocating system memory from psram");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }


    initSystemLowLevel();


    ESP_LOGI(LOG_TAG, "********* SYSTEM STARTUP SUCCESSFUL *******");
    while(1)
    {
        if(xQueueReceive(blePeriph_bleToAppQueue, &rxBleItem, pdMS_TO_TICKS(1)) == pdTRUE)
        {
            switch(rxBleItem.opcode)
            {

                case 1: //initial playback payload received
                    ESP_LOGI(LOG_TAG, "Playback stream initiated by the client");
                    playbackDataStore.totalDataLength = rxBleItem.dataLength;
                    isPlayingBack = true;
                    break;

                case 2: //additional playback payload recieved
                    ESP_LOGI(LOG_TAG, "New playback streaming packet received");
                    playbackDataStore.totalDataLength += rxBleItem.dataLength;
                    break;

                case 3: //stop playback
                    ESP_LOGI(LOG_TAG, "Stop playback command received from client");
                    isPlayingBack = false;
                    waitingForDeltaTimer = false;
                    break;

                case 4:
                    break;

                case 5:
                    break;

                case 0xFF:
                    break;
            }
        }

        if (isPlayingBack) // tidy this up later
        {
            if (waitingForDeltaTimer)
            {
                if (deltaTimerFired)
                {
                    deltaTimerFired = false;
                    waitingForDeltaTimer = false;
                    playbackMidiData(&playbackDataStore);
                }
            }
            else
            {
                playbackMidiData(&playbackDataStore);
            }
        }

        vTaskDelay(1);
    }
}




static void playbackMidiData(midiPlaybackRuntimeData_t *playbackDataPtr)
{
    uint8_t bytesToSend = 0;
    uint8_t messageSubType = 0;

    if (playbackDataPtr->isRunningStatus == false)
    {
        ESP_LOGI(LOG_TAG, "Fetching next midi event delta-time");
        playbackDataPtr->currentDeltaTime = getMidiDeltaTime(&playbackDataPtr->playbackPtr);
    }

    playbackDataPtr->statusByte = *playbackDataPtr->playbackPtr;

    if ((playbackDataPtr->previousStatusForRunningStatus != 0) && playbackDataPtr->isRunningStatus) // runs from second iteration
    {
        if (playbackDataPtr->previousStatusForRunningStatus < 0xF8) // Doesnt apply to System Real-Time Message type
        {
            playbackDataPtr->statusByte = playbackDataPtr->previousStatusForRunningStatus;
        }
    }


    if (playbackDataPtr->statusByte == 0xFF) 
    {
        if (playbackDataPtr->isRunningStatus == false) playbackDataPtr->playbackPtr += 1; // now points to base of message data
        else playbackDataPtr->isRunningStatus = false;
        
        processMetaMessage(&playbackDataPtr->playbackPtr);

        if(isPlayingBack == false) //Was the last meta message end of file?
        {
            playbackDataPtr->playbackPtr = playbackDataPtr->playbackDataBASE;
            return;
        }

    }
    else if ((playbackDataPtr->statusByte >= 0x80) && (playbackDataPtr->statusByte <= 0xEF)) //--- Voice Message Type ---//
    {
        // All voice message status bytes have the following format:
        // StatusByte[4-7] = voice message opcode (voice message sub-type)
        // StatusByte[0-3] = channel being addressed (0-15)

        // The MIDI spec features 'running status' capability,
        // if the current event type is the event immediately
        // before it, then the new event does not need a status byte
        if (playbackDataPtr->isRunningStatus == false)
        {
            messageSubType = (playbackDataPtr->statusByte >> 4);
        }
        else
        {
            // isRunningStatus is set false later
            messageSubType = (playbackDataPtr->previousStatusForRunningStatus >> 4);
        }

        playbackDataPtr->previousStatusForRunningStatus = playbackDataPtr->statusByte;

        switch (messageSubType)
        {
        case 0x08: //---Note Off---//
            // Format (n = channel number)
            // byte[0] = 0x8n <-- currently pointing at (if NOT running status)
            // Byte[1] = Note Number <-- pointing here if this is 'running status'
            // Byte[2] = Velocity
            ESP_LOGI(LOG_TAG, "note_off channel=%0x, note=%d, velocity=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, *(playbackDataPtr->playbackPtr + 1), playbackDataPtr->currentDeltaTime);
            bytesToSend = 3;
            break;

        case 0x09: //---Note On---//
            // Format (n = channel number)
            // byte[0] = 0x9n <-- currently pointing at (if NOT running status)
            // Byte[1] = Note Number <-- pointing here if this is 'running status'
            // Byte[2] = Velocity
            ESP_LOGI(LOG_TAG, "note_on channel=%0x, note=%d, velocity=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, *(playbackDataPtr->playbackPtr + 1), playbackDataPtr->currentDeltaTime);
            bytesToSend = 3;
            break;

        case 0x0A: //---Aftertouch---//
            // Format (n = channel number)
            // Byte[0] = 0xAn <-- currently pointing at (if NOT running status)
            // Byte[1] = Note Number <-- pointing here if this is 'running status'
            // Byte[2] = Pressure Value
            ESP_LOGI(LOG_TAG, "aftertouch channel=%0x, note=%d, pressure=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, *(playbackDataPtr->playbackPtr + 1), playbackDataPtr->currentDeltaTime);
            bytesToSend = 3;
            break;

        case 0x0B: //---Control Change---//
            // Format (n = channel number)
            // Byte[0] =  0xBn <-- currently pointing at (if NOT running status)
            // Byte[1] = Control opcode <-- pointing here if this is 'running status'
            // Byte[2] = Value
            ESP_LOGI(LOG_TAG, "control_change channel=%0x, control=%d, value=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, *(playbackDataPtr->playbackPtr + 1), playbackDataPtr->currentDeltaTime);
            bytesToSend = 3;
            break;

        case 0x0C: //---Program Change---//
            // Format (n = channel num)
            // Byte[0] = 0xCn <-- currently pointing at (if NOT running status)
            // Byte[1] = Program value (selects instrument) <-- pointing here if this is 'running status'
            ESP_LOGI(LOG_TAG, "program_change channel=%0x, program=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, playbackDataPtr->currentDeltaTime);
            bytesToSend = 2;
            break;

        case 0x0D: //---Channel Pressure---//
            // Format (n = channel num)
            // Byte[0] = 0xDn <-- currently pointing at (if NOT running status)
            // Byte[1] = Pressure value <-- pointing here if this is 'running status'
            ESP_LOGI(LOG_TAG, "channel_pressure channel=%0x, value=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, playbackDataPtr->currentDeltaTime);
            bytesToSend = 2;
            break;

        case 0x0E: //---Pitch Wheel---//
            // Format (n = channel num)
            // Byte[0] = 0xEn <-- currently pointing at (if NOT running status)
            // Byte[1] = Pitch Value MSB (these two bytes must each have bit 8 removed, concatenate result) <-- pointing here if this is 'running status'
            // Byte[2] = Pitch Value LSB
            ESP_LOGI(LOG_TAG, "pitch_wheel channel=%0x, msb=%d, lsb=%d, time=%ld",
                     (0x0F & playbackDataPtr->statusByte), *playbackDataPtr->playbackPtr, *(playbackDataPtr->playbackPtr + 1), playbackDataPtr->currentDeltaTime);
            bytesToSend = 3;
            break;

        default: //--- ERROR ---//
            ESP_LOGE(LOG_TAG, "VOICE MESSAGE: ERROR Unrecognised Message!");
            // HANDLE ERROR!
            break;
        }

        if ((playbackDataPtr->currentDeltaTime != 0) && (playbackDataPtr->isRunningStatus == false))
        {
            ESP_LOGI(LOG_TAG, "Just set delta timer running");
            deltaTimerFired = false;
            waitingForDeltaTimer = true;
            startDeltaTimer(playbackDataPtr->currentDeltaTime);
            //while(!deltaTimerFired){};
        }

        if (playbackDataPtr->isRunningStatus)
        {
            playbackDataPtr->isRunningStatus = false;
            --bytesToSend; // no status byte to send
            ESP_LOGI(LOG_TAG, "Sending following midi message (this is a running status):");
            for (uint8_t a = 0; a < bytesToSend; ++a)
            {
                ESP_LOGI(LOG_TAG, "0x%0x", *(playbackDataPtr->playbackPtr + a));
            }
            uart_write_bytes(MIDI_UART_NUM, playbackDataPtr->playbackPtr, bytesToSend);
            playbackDataPtr->playbackPtr += bytesToSend;
        }
        else
        {
            uart_write_bytes(MIDI_UART_NUM, playbackDataPtr->playbackPtr, bytesToSend);
            ESP_LOGI(LOG_TAG, "Sending following midi message:");
            for (uint8_t a = 0; a < bytesToSend; ++a)
            {
                ESP_LOGI(LOG_TAG, "0x%0x", *(playbackDataPtr->playbackPtr + a));
            }
            playbackDataPtr->playbackPtr += bytesToSend;
        }

        ESP_LOGI(LOG_TAG, "Event processed");
        ESP_LOGI(LOG_TAG, "\n");
    }
    else
    {
        // In order to reach here the playback pointer has
        // reached a status byte that it doesn't recognise.
        // This must be a 'running status', where subsequent
        // events of the same type may ommit the status byte
        ESP_LOGI(LOG_TAG, "Running status detected");
        playbackDataPtr->isRunningStatus = true; // Must be running status?
    }
}



static void processMetaMessage(uint8_t **playbackPtr)
{
    // This function deals with midi meta event messages.
    // A meta message is NEVER sent over midi, it is information
    // about the file being played, intended for the playback device.

    // Meta messages ONLY exist in midi FILES, they are NEVER sent or revieved over midi.

    uint8_t numBytes;

    switch (**playbackPtr)
    {
    case metaEvent_deviceName:
        ESP_LOGI(LOG_TAG, "Device Name meta event detected, ignoring");
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        break;

    case metaEvent_midiPort:
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "Midi Port meta event detected, ignoring");
        break;

    case metaEvent_sequenceNum:
        // Two bytes
        *playbackPtr += 3;
        ESP_LOGI(LOG_TAG, "Sequence Number meta event detected, ignoring");
        break;

    case metaEvent_cuePoint:
    case metaEvent_marker:
    case metaEvent_lyrics:
    case metaEvent_instrumentName:
    case metaEvent_trackName:
    case metaEvent_copyright:
    case metaEvent_textField:
        ESP_LOGI(LOG_TAG, "HERE!");
        // All mevta events of variable length
        // Not bothered about any of these so just increment
        // file pointer onto the delta-time base of the next event
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "Ignored variable-length meta message");
        break;

    case metaEvent_channelPrefix:
        // Single byte
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "Channel prefix meta event detected, ignoring");
        break;

    case metaEvent_endOfTrack:
        // Single byte
        ESP_LOGI(LOG_TAG, "End of midi track detected!");
        isPlayingBack = false;
        break;

    case metaEvent_setTempo:
        // Three bytes
        //ESP_LOGI(LOG_TAG, "Set tempo to %ld microseconds per quater-note", getMicroSecondsPerQuaterNote(*playbackPtr));
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        break;

    case metaEvent_smpteOffset:
        // Five bytes
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "SmpteOffset meta event detected, ignoring");
        break;

    case metaEvent_setTimeSig:
        // Four bytes
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "Time signature meta event detected, ignoring");
        break;

    case metaEvent_keySignature:
        // Two bytes
        *playbackPtr += 1;            // Will be pointing at data length after this
        numBytes = **playbackPtr;     // Get number of data bytes assosiated with thing meta event
        *playbackPtr += numBytes + 1; // Now pointing to delta-time of next event
        ESP_LOGI(LOG_TAG, "Key signature meta event detected, ignoring");
        break;

    case metaEvent_sequencerSpecific:
        // for device specific use
        ESP_LOGE(LOG_TAG, "Device specific meta event detected");
        break;

    default:
        ESP_LOGE(LOG_TAG, "Unrecognised meta event opcode");
        // HANDLE ERROR!
        break;
    }
}



static uint32_t getMicroSecondsPerQuaterNote(uint8_t *const setTempoBase)
{
    // This function processes the SET TEMPO midi meta event
    // it expects a pointer to the base of the event

    // The SET TEMPO meta event has the following layout:
    // 0x51          //Set-Tempo opcode
    // 0x03          //states number of remaining bytes
    // byte byte 0   //MSB of 24-bit temp value
    // data byte 1
    // data byte 2
    //
    // The tempo value is ALWAYS in microseconds per quater-note

    if (*setTempoBase != 0x51)
    {
        ESP_LOGE(LOG_TAG, "Incorrect base address passed to 'getMicroSecondsPerQuaterNote' - operation aborted");
        return 0;
    }

    uint32_t result = 0;
    uint8_t buffer[4] = {0};
    uint8_t iterator = 0;

    if (*(setTempoBase + 1) > sizeof(uint32_t))
    {
        ESP_LOGE(LOG_TAG, "Error, number of data bytes in set tempo exceeds expected size - operation aborted");
        return 0;
    }

    memcpy(buffer, setTempoBase + 2, *(setTempoBase + 1));

    iterator = 0;
    for (int8_t a = (*(setTempoBase + 1) - 1); a >= 0; --a)
    {
        result |= buffer[iterator] << (a * 8);
        iterator++;
    }

    ESP_LOGI(LOG_TAG, "Tempo in uS per quater-note: %ld", result);

    return result;
}



static uint32_t getMidiDeltaTime(uint8_t **deltaTimeBase)
{
    // This function processes the delta-time bytes of a midi event
    // it exptects a pointer to the base of a delta-time.

    // Delta times are ALWAYS four bytes max, MSB FIRST.
    // Bit 8 of a delta-time byte is a flag - indicating more bytes to follow.
    // The final delta-time value is created by removing the flag bit from each
    // byte and concatenating the result.

    uint32_t tempStore = 0;
    uint32_t result = 0;
    uint8_t numBytes = 0;

loopDeltaTime: //--- TIGHT LOOP ---//

    // Always have at least one byte
    tempStore |= **deltaTimeBase;

    if (HAS_MORE_DELTA_TIME_BYTES(**deltaTimeBase)) // IF BIT 8 SET THEN TRUE
    {
        if (numBytes < 3) // A midi delta-time has 4 bytes MAX
        {
            tempStore <<= 8;
            ++numBytes;
            *deltaTimeBase += 1;
            goto loopDeltaTime; //--- TIGHT LOOP ---//
        }
    }

    for (uint8_t a = 0; a <= numBytes; ++a)
    {
        // For each delta-time byte we need to
        // remove bit 8 and concatenate the result
        result |= ((tempStore & (0x0000007F << (a * 8))) >> ((a ? 1 : 0) * a));
    }

    *deltaTimeBase += 1; // Set to point at delta time of next midi event

    return result;
}