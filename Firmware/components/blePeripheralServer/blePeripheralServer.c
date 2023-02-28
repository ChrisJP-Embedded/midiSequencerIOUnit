/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "esp_log.h"
#include "nvs_flash.h"
/* BLE */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "bleprph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "blePeripheralServer.h"

#define CONNECTION_ESTABILISHED  0
#define CONNECTION_ATTEMPT_FAILED 1
#define LOG_TAG "midiSeq"


static int bleprph_gap_event(struct ble_gap_event *event, void *arg);
static uint8_t own_addr_type;

static void initNimBle(void);
void ble_store_config_init(void);
static void bleprph_on_sync(void);
static void bleprph_on_reset(int reason);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);
static void bleprph_advertise(void);
static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc);

QueueHandle_t blePeriph_appToBleQueue;
QueueHandle_t blePeriph_bleToAppQueue;
bool isConnectedToCentral = false;

//************************************
//This is the BLE peripheral RTOS task
//************************************
void bleprph_host_task(void *param)
{
    ESP_LOGI(LOG_TAG, "BLE Host Task Started");
    nimble_port_run(); //This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}


//*************************************
//This is the BLE runtime API RTOS task
//used to communicate with BLE Central
//*************************************
void blePeriphAPI_task(void * param)
{
    bool hasNewAppInput = false;
    uint8_t inputBuffer;
    uint8_t appCommand = 0;

    bleToAppQueueItem_t responseForApp;

    initNimBle();

    //First queue item is just a dummy byte
    if(xQueueSendToBack(blePeriph_bleToAppQueue, &responseForApp, pdMS_TO_TICKS(5000)) == pdFALSE)
    {
        ESP_LOGE(LOG_TAG, "Failure adding item to blePeriph_appToBleQueue - ble task startup failed, deleting task");
        vTaskDelete(NULL); //Delete *this* task
    }

    while(1)
    {
        if (uxQueueMessagesWaiting(blePeriph_appToBleQueue))
        {
            //Receieve from queue - dont wait for data to become available
            //if(xQueueReceive(blePeriph_appToBleQueue, &inputBuffer, 0) == 1)
            {
                //ESP_LOGI(LOG_TAG, "New queue item recieved from system level");
                hasNewAppInput = true;
                //***** EXTRACT COMMAND BYTE FROM RX QUEUE DATA ********//
            } 
        }

        if(hasNewAppInput)
        {
            switch(appCommand)
            {

                case 1:
                    goto shutdown_task;
                    break;

                case 3:
                    break;

                case 4:
                    break;

                default:
                    break;

            }
        }

        vTaskDelay(20);
    }

    shutdown_task:
    nimble_port_freertos_deinit();
    nimble_port_deinit();

   // if(xQueueSendToBack(blePeriph_bleToAppQueue, (void*)&responseForApp, pdMS_TO_TICKS(1000)) == pdFALSE)
   // {
   //     ESP_LOGE(LOG_TAG, "Failed to add item to bleToAppQueue, ble task deleted");
    //}


    vTaskDelete(NULL);
}





static void initNimBle(void)
{
    int rc;

    ESP_LOGI(LOG_TAG, "Attempting to initialize NimBLE");

    esp_err_t ret = nvs_flash_init(); //Initialize NVS (used to store PHY calibration data)

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //Prepare the ble controller
    nimble_port_init(); //Init the ESP32S3 port of NimBLE

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_bonding = 1;
#endif
#ifdef CONFIG_EXAMPLE_MITM
    ble_hs_cfg.sm_mitm = 1;
#endif
#ifdef CONFIG_EXAMPLE_USE_SC
    ble_hs_cfg.sm_sc = 1;
#else
    ble_hs_cfg.sm_sc = 0;
#endif
#ifdef CONFIG_EXAMPLE_BONDING
    ble_hs_cfg.sm_our_key_dist = 1;
    ble_hs_cfg.sm_their_key_dist = 1;
#endif

    rc = gatt_svr_init();
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set("nimble-bleprph");
    assert(rc == 0);

    ble_store_config_init();

    nimble_port_freertos_init(bleprph_host_task);
}




//Just prints out a load of info to console
static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}



static void bleprph_advertise(void)
{
    //Configures advertisement and starts advertising process

    struct ble_gap_adv_params adv_params;   //Used to configure discovery and connection modes
    struct ble_hs_adv_fields fields;        //Used to specify data provided within advertisements
    int rc;                                 //Used to store result conditions (error check) 
    const char *name;                       //String placeholder

    //Clear struct before configuration
    memset(&fields, 0, sizeof(fields));

    //**** FILL STRUCT THAT MAKES UP ADVERTISING DATA ****//
    //The first byte is a set of bit flags
    //BLE_HS_ADV_F_DISC_GEN - Indicates that peripheral using General Disovery mode
    //BLE_HS_ADV_F_BREDR_UNSUP - Bluetooth classic unsupported
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    //include the tx pwr level in advertisement
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    //**** EXCEEDS ADVERTISEMENT DATA LENGTH WHEN USING 128UUIDS
    //**** LOOK INTO EXTENDED ADVERTISING PACKETS?
    //Get and assign device name string
    //show complete name in advertisement
    //name = ble_svc_gap_device_name();
    //fields.name = (uint8_t *)name;
    //fields.name_len = strlen(name);
    //fields.name_is_complete = 1;


    //Provide a list of UUIDS in advertisement
    //We can potentially provide all uuids here, that way connection time can
    //be limited to absolulete miniumum (no need to retrive uuids on connection)
    fields.uuids128 = (ble_uuid128_t[]) {
        BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                         0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59)
    };
    fields.num_uuids128 = 1; //Must specify number of UUIDs in provided array
    fields.uuids128_is_complete = 1; //Show complete list of UUIDs in advertisement


    //Pass configured advertising 
    //content struct to the ble API
    rc = ble_gap_adv_set_fields(&fields); 
    if (rc != 0) { //ERROR CHECK
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    //Clear struct before configuration
    memset(&adv_params, 0, sizeof adv_params);

    //p1317 bt Core Spec
    //A device in the undirected-connectable mode shall accept a connection request
    //from a device performing the auto connection establishment procedure or the
    //general connection establishment procedure. (Accept both known and unknown peers)
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; //Set connection mode to Undirected Connectable


    //p1308 bt Core Spec 
    //Devices configured in the general discoverable mode are discoverable for an
    //indefinite period of time by devices performing the general discovery
    //procedure. Devices typically enter general discoverable mode autonomously.
    //Devices in the general discoverable mode will not be discovered by devices
    //performing the limited discovery procedure.
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; //Set discovery mode to General Discovery mode

    //**** START ADVERTISING NOW ****//
    //Using callback 'bleprph_gap_event' as the GAP handler
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    //GAP Callback - manages advertising and connection processes

    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) 
    {
        case BLE_GAP_EVENT_CONNECT:

            if(event->connect.status == CONNECTION_ESTABILISHED)
            {
                MODLOG_DFLT(INFO, "\n");
                MODLOG_DFLT(INFO, "CONNECTION ESTABLISHED, status=%d ", event->connect.status);
                MODLOG_DFLT(INFO, "\n");
                //Lookup the connection via its handle and populate connection descriptor
                rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(rc == 0); //Error handling (update this)
                //Print out the connection descriptor info
                bleprph_print_conn_desc(&desc);
            }
            else if(event->connect.status == CONNECTION_ATTEMPT_FAILED)
            {
                MODLOG_DFLT(INFO, "\n");
                MODLOG_DFLT(INFO, "Connection attempt FAILED, status=%d ", event->connect.status);
                MODLOG_DFLT(INFO, "\n");
                //Resume advertising
                bleprph_advertise();
            }
            break;


        case BLE_GAP_EVENT_DISCONNECT:
            MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
            bleprph_print_conn_desc(&event->disconnect.conn);
            MODLOG_DFLT(INFO, "\n");
            //Resume advertising
            bleprph_advertise();
            break;


        case BLE_GAP_EVENT_CONN_UPDATE:
            MODLOG_DFLT(INFO, "connection updated; status=%d ", event->conn_update.status);
            //The central has updated the connection parameters,
            //all we do here is lookup the connection via its handle
            //in order to populate a connection descriptor.
            //That descriptor is then used to output the updated 
            //connection parameters to the console.
            rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            assert(rc == 0); //Error handling (update this)
            //Print out the connection descriptor info
            bleprph_print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");
            break;


        case BLE_GAP_EVENT_ADV_COMPLETE:
            MODLOG_DFLT(INFO, "advertise complete; reason=%d", event->adv_complete.reason);
            bleprph_advertise(); //Just keep on advertising until a connection is made
            break;


        case BLE_GAP_EVENT_ENC_CHANGE:
            //Encryption has been enabled or disabled for this connection.
            MODLOG_DFLT(INFO, "encryption change event; status=%d ", event->enc_change.status);
            break;


        case BLE_GAP_EVENT_SUBSCRIBE:
            break;


        case BLE_GAP_EVENT_MTU:
            break;


        case BLE_GAP_EVENT_REPEAT_PAIRING:
            //We already have a bond with the peer, but it is attempting to
            //establish a new secure link. This app sacrifices security for
            //convenience: just throw away the old bond and accept the new link

            //Delete the old bond
            //first get conn descriptor via handle lookup
            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(rc == 0); //Error handling (update this)
            //Now delete the previous connection
            ble_store_util_delete_peer(&desc.peer_id_addr);

            //Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that 
            //the host should continue with the pairing operation.
            return BLE_GAP_REPEAT_PAIRING_RETRY;
            break;


        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(LOG_TAG, "PASSKEY_ACTION_EVENT started \n");
            struct ble_sm_io pkey = {0};
            int key = 0;

            break;
    }

    return 0;
}


static void bleprph_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}



static void bleprph_on_sync(void)
{
    int rc; //Return condition

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    //Figure out address to use while advertising (no privacy for now)
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");

    /* Begin advertising. */
    bleprph_advertise();
}
