/*
 * This file is part of the bladeRF project:
 *   http://www.github.com/nuand/bladeRF
 *
 * Copyright (C) 2013-2014 Nuand LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* This is a Windows-specific USB backend using Cypress's CyAPI, which utilizes the
 * CyUSB3.sys driver (with a CyUSB3.inf modified to include the bladeRF VID/PID).
 */

extern "C"
{
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "bladeRF.h"    /* Firmware interface */

#include "backend/backend.h"
#include "backend/usb/usb.h"
#include "async.h"
#include "log.h"
}
#include <CyAPI.h>

/* This GUID must match that in the modified CyUSB3.inf used with the bladeRF */
static const GUID driver_guid = {
    0x35D5D3F1, 0x9D0E, 0x4F62, 0xBC, 0xFB, 0xB0, 0xD4, 0x8E,0xA6, 0x34, 0x16
};

/* "Private data" for the CyAPI backend */
struct bladerf_cyapi {
    CCyUSBDevice *dev;
};

static inline struct bladerf_cyapi * get_backend_data(void *driver)
{
    assert(driver);
    return (struct bladerf_cyapi *) driver;
}

static inline CCyUSBDevice * get_device(void *driver)
{
    struct bladerf_cyapi *backend_data = get_backend_data(driver);
    assert(backend_data->dev);
    return backend_data->dev;
}

static int cyapi_probe(struct bladerf_devinfo_list *info_list)
{
  int status=0;
    CCyUSBDevice *dev = new CCyUSBDevice(NULL, driver_guid);

    for (int i=0; i< dev->DeviceCount(); i++) {
        struct bladerf_devinfo info;
        if (dev->Open(i)) {
            info.instance = i;
            wcstombs(info.serial, dev->SerialNumber, sizeof(info.serial));
            info.usb_addr = dev->USBAddress;
            info.usb_bus = 0;
            info.backend = BLADERF_BACKEND_CYPRESS;
            status = bladerf_devinfo_list_add(info_list, &info);
            if( status ) {
                log_error( "Could not add device to list: %s\n", bladerf_strerror(status) );
            } else {
                log_verbose("Added instance %d to device list\n",
                    info.instance);
            }
            dev->Close();
        }
    }
    delete dev;
    return status;

}
static int cyapi_open(void **driver,
                     struct bladerf_devinfo *info_in,
                     struct bladerf_devinfo *info_out)
{
    int status = BLADERF_ERR_IO;

    CCyUSBDevice *dev = new CCyUSBDevice(NULL, driver_guid);
    struct bladerf_devinfo_list info_list;
    bladerf_devinfo_list_init(&info_list);
    cyapi_probe(&info_list);
    int instance = -1;
    for (unsigned int i=0; i<info_list.num_elt; i++) {
        if (bladerf_devinfo_matches(&info_list.elt[i], info_in)) {
            instance = info_list.elt[i].instance;
            break;
        }
    }

    free(info_list.elt);
    if (dev->Open(instance)) {
        struct bladerf_cyapi *cyapi_data;
        cyapi_data = (struct bladerf_cyapi *)calloc(1,sizeof(struct bladerf_cyapi));

        cyapi_data->dev = dev;
        *driver = cyapi_data;
        dev->SetAltIntfc(1);
        status = 0;
    } else {
        delete dev;
    }

    return status;
}

static int cyapi_change_setting(void *driver, uint8_t setting)
{
    CCyUSBDevice *dev = get_device(driver);

    if (dev->SetAltIntfc(setting)) {
		int eptCount = dev->EndPointCount();
        return 0;
    } else {
        return BLADERF_ERR_IO;
    }
}


static void cyapi_close(void *driver)
{
    CCyUSBDevice *dev = get_device(driver);
    dev->Close();
    delete dev;
    free(driver);

}

static int cyapi_get_speed(void *driver,
                          bladerf_dev_speed *device_speed)
{
    int status = 0;
    CCyUSBDevice *dev = get_device(driver);

    if (dev->bHighSpeed) {
        *device_speed = BLADERF_DEVICE_SPEED_HIGH;
    } else if (dev->bSuperSpeed) {
        *device_speed = BLADERF_DEVICE_SPEED_SUPER;
    } else {
        *device_speed = BLADERF_DEVICE_SPEED_UNKNOWN;
        status = BLADERF_ERR_UNEXPECTED;
        log_debug("%s: Unable to determine device speed.\n", __FUNCTION__);
    }

    return status;
}

static int cyapi_control_transfer(void *driver,
                                 usb_target target_type, usb_request req_type,
                                 usb_direction dir, uint8_t request,
                                 uint16_t wvalue, uint16_t windex,
                                 void *buffer, uint32_t buffer_len,
                                 uint32_t timeout_ms)
{
    CCyUSBDevice *dev = get_device(driver);

    int Result=0;

    switch(dir)
    {
    case USB_DIR_DEVICE_TO_HOST:
            dev->ControlEndPt->Direction = (CTL_XFER_DIR_TYPE::DIR_FROM_DEVICE);
            break;
    case USB_DIR_HOST_TO_DEVICE:
            dev->ControlEndPt->Direction = (CTL_XFER_DIR_TYPE::DIR_TO_DEVICE);
            break;
    }

    switch(req_type)
    {
        case USB_REQUEST_CLASS:
            dev->ControlEndPt->ReqType = CTL_XFER_REQ_TYPE::REQ_CLASS;
            break;
        case USB_REQUEST_STANDARD:
            dev->ControlEndPt->ReqType = CTL_XFER_REQ_TYPE::REQ_STD;
            break;
        case USB_REQUEST_VENDOR:
            dev->ControlEndPt->ReqType = CTL_XFER_REQ_TYPE::REQ_VENDOR;
            break;
    }

    switch (target_type)
    {
        case USB_TARGET_DEVICE:
            dev->ControlEndPt->Target =  CTL_XFER_TGT_TYPE::TGT_DEVICE;
            break;
        case USB_TARGET_ENDPOINT:
            dev->ControlEndPt->Target =  CTL_XFER_TGT_TYPE::TGT_ENDPT;
            break;
        case USB_TARGET_INTERFACE:
            dev->ControlEndPt->Target =  CTL_XFER_TGT_TYPE::TGT_INTFC;
            break;
        case USB_TARGET_OTHER:
            dev->ControlEndPt->Target =  CTL_XFER_TGT_TYPE::TGT_OTHER;
            break;
    }

    dev->ControlEndPt->MaxPktSize = buffer_len;
    dev->ControlEndPt->ReqCode = request;
    dev->ControlEndPt->Index = windex;
    dev->ControlEndPt->Value = wvalue;
    dev->ControlEndPt->TimeOut = timeout_ms ? timeout_ms : 0xffffffff;
    LONG LEN = buffer_len;
    bool res = dev->ControlEndPt->XferData((PUCHAR)buffer,LEN);
    if (!res)
    {
        Result = BLADERF_ERR_IO;
    }

    return Result;

}

static CCyBulkEndPoint *GetEndPoint(CCyUSBDevice *USBDevice,int id)
{

    int eptCount ;
    eptCount = USBDevice->EndPointCount();

    for (int i=1; i<eptCount; i++)
    {
        if (USBDevice->EndPoints[i]->Address == id)
            return (CCyBulkEndPoint *)USBDevice->EndPoints[i];
    }
    return NULL;
}

static int cyapi_bulk_transfer(void *driver, uint8_t endpoint, void *buffer,
                              uint32_t buffer_len, uint32_t timeout_ms)
{
 int Result = 0;
    CCyUSBDevice *dev = get_device(driver);


    CCyBulkEndPoint *EP = GetEndPoint(dev, endpoint);
    if (EP)
    {
        LONG len=buffer_len;
        dev->ControlEndPt->TimeOut = timeout_ms ? timeout_ms : 0xffffffff;
        EP->LastError=0;
        EP->bytesWritten=0;
        if (EP->XferData((PUCHAR)buffer,len))
        {
            if (len != buffer_len)
                Result = BLADERF_ERR_IO;
        }
        else
        {
            Result = BLADERF_ERR_IO;
        }
    }
    else
    {
        Result = BLADERF_ERR_IO;
    }

    return Result;

}

static int cyapi_get_string_descriptor(void *driver, uint8_t index,
                                      void *buffer, uint32_t buffer_len)
{
    int res = cyapi_control_transfer(driver, USB_TARGET_DEVICE, USB_REQUEST_STANDARD, USB_DIR_DEVICE_TO_HOST, 0x06, 0x0300 | index, 0, buffer, buffer_len, BLADE_USB_TIMEOUT_MS);
    char *str = (char*)buffer;
    for (unsigned int i=0; i<(buffer_len/2); i++)
    {
        str[i] = str[2+(i*2)];
    }
    return res;
}

struct cypressStreamData
{
	HANDLE *Handles;
	OVERLAPPED  *ov;
	PUCHAR *Token;
    PUCHAR  *CurrentBuffer;
	CCyBulkEndPoint *EP81;
    size_t num_transfers;
};

static int cyapi_init_stream(void *driver, struct bladerf_stream *stream,
                            size_t num_transfers)
{
    CCyUSBDevice *dev = get_device(driver);

    cypressStreamData *bData = (cypressStreamData *)malloc(sizeof(cypressStreamData));
    stream->backend_data = bData;
    bData->Handles = (HANDLE*) calloc(1,num_transfers * sizeof(HANDLE*));
    bData->ov = (OVERLAPPED*) calloc(1,num_transfers * sizeof(OVERLAPPED));
    bData->Token = (PUCHAR*) calloc(1,num_transfers * sizeof(PUCHAR*));
    bData->CurrentBuffer = (PUCHAR*) calloc(1,num_transfers * sizeof(void*));
    bData->EP81 = GetEndPoint(dev, 0x81);
    bData->EP81->XferMode = XFER_MODE_TYPE::XMODE_DIRECT;
    bData->num_transfers = num_transfers;
    for (unsigned int i=0; i<num_transfers; i++)
    {
        bData->Handles[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
        bData->ov[i].hEvent = bData->Handles[i];
    }

    return 0;

}


static int FindBuffer(void* ptr, struct bladerf_stream *stream)
{
    for (unsigned int i=0; i<stream->num_buffers; i++)
	{
        if (stream->buffers[i] == ptr)
            return i;
	}
    return -1;
}


static int cyapi_stream(void *driver, struct bladerf_stream *stream,
                       bladerf_module module)
{
    log_verbose( "Stream Start\n");
	cypressStreamData *bData = (cypressStreamData *) stream->backend_data;
	LONG buffer_size = (LONG)sc16q11_to_bytes(stream->samples_per_buffer);
    bData->EP81->Abort();
    bData->EP81->Reset();
    int CurrentBuffer =0;
    for (unsigned int i=0; i<stream->num_buffers; i++)
	{
        log_verbose( "Buffer %d Buffer %x\n",i,stream->buffers[i]);
	}
    for (unsigned int i=0; i<bData->num_transfers; i++)
	{
        bData->CurrentBuffer[i] =(PUCHAR) stream->buffers[i];
		bData->Token[i] = bData->EP81->BeginDataXfer(bData->CurrentBuffer[i],buffer_size, &bData->ov[i]);
        log_verbose( "Stream Transfer %d Buffer %x\n",i,bData->CurrentBuffer[i]);
	}
	log_verbose( "Stream Setup\n");
	while(true)
	{
        DWORD res  = WaitForSingleObjectEx(bData->Handles[CurrentBuffer], INFINITE, false); // WaitForMultipleObjects( (DWORD) bData->num_transfers, bData->Handles, false, INFINITE);
        //if ((res >= WAIT_OBJECT_0) && (res <= WAIT_OBJECT_0+bData->num_transfers))
		{
			int idx = CurrentBuffer; //res - WAIT_OBJECT_0;
            CurrentBuffer = (CurrentBuffer+1) % bData->num_transfers;
			long len =0;
			void* NextBuffer=NULL;
            log_verbose( "[CYPRESS] Got transfer %d (%x)->Buffer(%d)\n",idx,bData->CurrentBuffer[idx],FindBuffer(bData->CurrentBuffer[idx],stream));
			if (bData->EP81->FinishDataXfer(bData->CurrentBuffer[idx] , len, &bData->ov[idx], bData->Token[idx]))
			{
				bData->Token[idx] = NULL;
				NextBuffer = stream->cb(stream->dev, stream, NULL, bData->CurrentBuffer[idx], len/4, stream->user_data);
				log_verbose( "[CYPRESS] Next Buffer %x\n",NextBuffer);
			}
			else
			{
				printf("Error res = %d\n",res);
				return 0;
			}
			if (NextBuffer != NULL)
            {
                bData->CurrentBuffer[idx] = (PUCHAR) NextBuffer;
                bData->Token[idx] = bData->EP81->BeginDataXfer(bData->CurrentBuffer[idx],buffer_size, &bData->ov[idx]);
            }
			else
				break;
		}
	}
	stream->state = STREAM_SHUTTING_DOWN;
	log_verbose( "Teardown \n");
    bData->EP81->Abort();
    for (unsigned int i=0; i<bData->num_transfers; i++)
	{
		LONG len=0;
		if (bData->Token[i] != NULL)
		{
			bData->EP81->FinishDataXfer(bData->CurrentBuffer[i] , len, &bData->ov[i], bData->Token[i]);
		}
	}
	stream->state = STREAM_DONE;
	log_verbose("Stream complete \n");
    return 0;
}
/* The top-level code will have aquired the stream->lock for us */
int cyapi_submit_stream_buffer(void *driver, struct bladerf_stream *stream,
                              void *buffer, unsigned int timeout_ms)
{
    return 0;
}

static int cyapi_deinit_stream(void *driver, struct bladerf_stream *stream)
{
    return 0;
}

extern "C"
{
static const struct usb_fns cypress_fns = {
    FIELD_INIT(.probe, cyapi_probe),
    FIELD_INIT(.open, cyapi_open),
    FIELD_INIT(.close, cyapi_close),
    FIELD_INIT(.get_speed, cyapi_get_speed),
    FIELD_INIT(.change_setting, cyapi_change_setting),
    FIELD_INIT(.control_transfer, cyapi_control_transfer),
    FIELD_INIT(.bulk_transfer, cyapi_bulk_transfer),
    FIELD_INIT(.get_string_descriptor, cyapi_get_string_descriptor),
    FIELD_INIT(.init_stream, cyapi_init_stream),
    FIELD_INIT(.stream, cyapi_stream),
    FIELD_INIT(.submit_stream_buffer, cyapi_submit_stream_buffer),
    FIELD_INIT(.deinit_stream, cyapi_deinit_stream)
};

struct usb_driver usb_driver_cypress = {
    FIELD_INIT(.id, BLADERF_BACKEND_CYPRESS),
    FIELD_INIT(.fn, &cypress_fns)
};

}