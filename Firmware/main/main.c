#include <stdio.h>
#include <sys/unistd.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "blePeripheralServer.h"
#include "system.h"

#define LOG_TAG "main"
#define BLE_CLIENT_TASK_STACK_SIZE 8192 //Still need to tune this stack size

static uint8_t initRTOSTasks(void);

TaskHandle_t bluetoothGattServer_task = NULL;
StaticTask_t xTaskBuffer_bleServer;
StackType_t bleServerTaskStack[BLE_CLIENT_TASK_STACK_SIZE];




//---------------------------
//---- MAIN ENTRY POINT -----
//---------------------------
void app_main(void)
{

    if(initRTOSTasks()) 
    {
        while(1)
        {
            ESP_LOGE(LOG_TAG, "RTOS TASK INIT FAILED");
            vTaskDelay(5000);
        }
    }

    ESP_LOGI(LOG_TAG, "Startup sucessful, entering system mode");

    while(1) //---main loop---
    {
        systemEntryPoint();
        vTaskDelay(1);
    }

    esp_restart();
}





static uint8_t initRTOSTasks(void)
{
    uint8_t btQueueItem[20];

    //------------------------------------------------------------
    //---- BLE PERIPERAL / GATT SERVER TASK SETUP & INITIALIZATION 
    //------------------------------------------------------------
    blePeriph_bleToAppQueue = xQueueCreate(10, sizeof(bleToAppQueueItem_t));
    blePeriph_appToBleQueue = xQueueCreate(10, sizeof(uint8_t));

    if(blePeriph_bleToAppQueue == 0 || blePeriph_appToBleQueue == 0)
    {
        ESP_LOGE(LOG_TAG, "Bluetooth queue creation failure");
        return 1;
    }

    //Pin the BLE task to CPU CORE1 - BLE has core1 all to itself throughout
    bluetoothGattServer_task = xTaskCreateStaticPinnedToCore( blePeriphAPI_task, "blePeriph", BLE_CLIENT_TASK_STACK_SIZE,
                                                              NULL, 1, bleServerTaskStack, &xTaskBuffer_bleServer, 1);

    if(bluetoothGattServer_task == NULL)
    {
        ESP_LOGE(LOG_TAG, "Bluetooth client task creation failed");
        return 1; 
    }

    //See the ble component for more info on system ble usage
    if(xQueueReceive(blePeriph_bleToAppQueue, btQueueItem, pdMS_TO_TICKS(5000)) == pdFALSE) 
    {
        ESP_LOGE(LOG_TAG, "Bluetooth client task failed to respond after creation");
        return 1; 
    }

    return 0;
}


