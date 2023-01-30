

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void blePeriphAPI_task(void * param);

extern uint8_t * playbackBufferPtr;
extern uint8_t * playbackBufferBASE;

//Use this for ALL queue items sent from bt to app
typedef struct {
    uint8_t opcode;
    uint16_t dataLength;
    uint8_t data[20];
} bleToAppQueueItem_t;

extern QueueHandle_t blePeriph_appToBleQueue;
extern QueueHandle_t blePeriph_bleToAppQueue;

