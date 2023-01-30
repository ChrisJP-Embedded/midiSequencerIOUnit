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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "malloc.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bleprph.h"
#include "include/blePeripheralServer.h"

#define LOG_TAG "gattServer"

/* 59462f12-9543-9999-12c8-58b459a2712d */
static const ble_uuid128_t gatt_svr_service_uuid = BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12, 0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);
/* 5c3a659e-897e-45e1-b016-007107c96df6 */
static const ble_uuid128_t gatt_svr_characteristic_eventBuffer = BLE_UUID128_INIT(0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0, 0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);
/* 5c3a659e-897e-45e1-b016-007107c96df7 */
static const ble_uuid128_t gatt_svr_characteristic_fileBuffer = BLE_UUID128_INIT(0xf7, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0, 0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c);

#define CHAR_EVENT_BUFFER_BYTES 512
#define CHAR_FILE_BUFFER_BYTES sizeof(uint16_t)

static uint8_t characteristic_eventBuffer[CHAR_EVENT_BUFFER_BYTES]; // Used to receive inividual events and commands

uint8_t * playbackBufferBASE;
static uint8_t * playbackWritePtr = NULL;


static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// Array of services this GATT server hosts
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service: Security test. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){{
                                                           // Characteristic: event buffer
                                                           .uuid = &gatt_svr_characteristic_eventBuffer.u,
                                                           .access_cb = gatt_svr_chr_access,
                                                           .flags = BLE_GATT_CHR_F_WRITE, BLE_GATT_CHR_F_WRITE_ENC
                                                       },
                                                       {
                                                           // Characteristic: file buffer
                                                           .uuid = &gatt_svr_characteristic_fileBuffer.u,
                                                           .access_cb = gatt_svr_chr_access,
                                                           .flags = BLE_GATT_CHR_F_WRITE, BLE_GATT_CHR_F_WRITE_ENC
                                                           
                                                       },
                                                       {
                                                           0, /* No more characteristics in this service. */
                                                       }},
    },

    {
        0, /* No more services. */
    },
};

static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    bleToAppQueueItem_t queueItem;
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    uint16_t lengthWritten = 0;
    static uint32_t playbackPayloadsReceived;
    uint8_t flags = 0;
    int rc;


    ESP_LOGI(LOG_TAG, "Connected central/client attempted to access a charcteristic %ld", playbackPayloadsReceived);

    //This system uses this 

    if (ble_uuid_cmp(uuid, &gatt_svr_characteristic_eventBuffer.u) == 0)
    {
        switch (ctxt->op)
        {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            // This characteristic is used for receiving commands,
            // as well as individual midi events - both of which
            // are just a few bytes in size (see blePeripheralServer.h)
            ESP_LOGI(LOG_TAG, "Event/command buffer write operation executed");

            rc = gatt_svr_chr_write(ctxt->om, 1, CHAR_EVENT_BUFFER_BYTES, characteristic_eventBuffer, &lengthWritten);

            if(lengthWritten < 4)
            {
                ESP_LOGE(LOG_TAG, "Error recieved ble data format incorrect - aborting characteristic write");
                return 0;
            }

            flags = characteristic_eventBuffer[0];
            ESP_LOGI("DEBU8G", "flags=%0x", flags);

            if(flags == 0x20) //first lot of multiple playback data
            {
                ESP_LOGI(LOG_TAG, "Received start of multi-payload playback stream");
                playbackWritePtr = playbackBufferBASE;
                memcpy(playbackWritePtr, (characteristic_eventBuffer + 2), 510);
                queueItem.dataLength = 510;
                playbackPayloadsReceived = 1;
            }
            else if(flags == 0x10) //part of ongoing multiple payload transaction
            {

                memcpy(playbackWritePtr + (playbackPayloadsReceived * 510), (characteristic_eventBuffer + 2), lengthWritten - 2);
                queueItem.dataLength = lengthWritten - 2;
                playbackPayloadsReceived++;
                ESP_LOGI(LOG_TAG, "playback payload %ld received", playbackPayloadsReceived);
            }
            
            queueItem.opcode = *(characteristic_eventBuffer + 1);

            xQueueSendFromISR(blePeriph_bleToAppQueue, &queueItem, pdFALSE); // needs updating
            return rc;

        default:
            assert(0);
            return BLE_ATT_ERR_UNLIKELY;
        }
    }


    assert(0);
    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                           "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}
