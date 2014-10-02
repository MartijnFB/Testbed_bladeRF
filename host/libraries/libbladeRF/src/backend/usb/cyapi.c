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

extern "C" {
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

struct stream_data {
	HANDLE *handles;
	OVERLAPPED  *ov;
	PUCHAR *token;
    PUCHAR  *curr_buf;
	CCyBulkEndPoint *ep;
    size_t num_transfers;
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

static inline struct stream_data * get_stream_data(struct bladerf_stream *stream)
{
    assert(stream && stream->backend_data);
    return (struct stream_data *) stream->backend_data;
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


static int cyapi_init_stream(void *driver, struct bladerf_stream *stream,
                            size_t num_transfers)
{
    int status = BLADERF_ERR_MEM;
    CCyUSBDevice *dev = get_device(driver);
    struct stream_data *data;

    data = (struct stream_data *) calloc(1, sizeof(data[0]));
    if (data == NULL) {
        return status;
    }

    data->handles = (HANDLE*) calloc(1,num_transfers * sizeof(HANDLE*));
    if (data->handles == NULL) {
        goto out;
    }

    data->ov = (OVERLAPPED*) calloc(1,num_transfers * sizeof(OVERLAPPED));
    if (data->ov == NULL) {
        goto out;
    }

    data->token = (PUCHAR*) calloc(1,num_transfers * sizeof(PUCHAR*));
    if (data->token == NULL) {
        goto out;
    }

    data->curr_buf = (PUCHAR*) calloc(1,num_transfers * sizeof(void*));
    if (data->curr_buf == NULL) {
        goto out;
    }

    // FIXME move this to stream execution
    data->ep = GetEndPoint(dev, SAMPLE_EP_IN);
    data->ep->XferMode = XFER_MODE_TYPE::XMODE_DIRECT;

    data->num_transfers = num_transfers;
    for (unsigned int i=0; i<num_transfers; i++) {
        data->handles[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (data->handles[i] == NULL) {
            log_debug("%s: Failed to create EventObject %u\n", i);
            goto out;
        }

        data->ov[i].hEvent = data->handles[i];
    }

    status = 0;

out:
    if (status == 0) {
        stream->backend_data = data;
    } else {
        // TODO - Deinit/deallocation as a result of CreateEvent() needed?
        free(data->handles);
        free(data->ov);
        free(data->token);
        free(data->curr_buf);
    }

    return status;
}


#ifdef LOGGING_ENABLED
static inline int find_buf(void* ptr, struct bladerf_stream *stream)
{
    for (unsigned int i=0; i<stream->num_buffers; i++) {
        if (stream->buffers[i] == ptr) {
            return i;
        }
	}

    log_debug("Unabled to find buffer %p\n:", ptr);
    return -1;
}
#endif

#ifndef ENABLE_LIBBLADERF_ASYNC_LOG_VERBOSE
#undef log_verbose
#define log_verbose(...)
#endif

static int cyapi_stream(void *driver, struct bladerf_stream *stream,
                       bladerf_module module)
{
    int idx = 0;
    long len;
    void *next_buffer;
    DWORD wait_status;
    DWORD timeout_ms;
    bool success;
	struct stream_data *data = get_stream_data(stream);
	LONG buffer_size = (LONG) sc16q11_to_bytes(stream->samples_per_buffer);

    assert(stream->dev->transfer_timeout[stream->module] <= MAXDWORD);
    if (stream->dev->transfer_timeout[stream->module] == 0) {
        timeout_ms = INFINITE;
    } else {
        timeout_ms = (DWORD) stream->dev->transfer_timeout[stream->module];
    }


    data->ep->Abort();
    data->ep->Reset();

#ifdef ENABLE_VERBOSE_CYAPI
    for (unsigned int i = 0; i < stream->num_buffers; i++) {
        log_verbose("Buffer %5d: %p\n",i,stream->buffers[i]);
	}
#endif

    if (module == BLADERF_MODULE_RX) {
        for (unsigned int i=0; i < data->num_transfers; i++) {
            data->curr_buf[i] = (PUCHAR) stream->buffers[i];
		    data->token[i] = data->ep->BeginDataXfer(data->curr_buf[i],
                                                     buffer_size, &data->ov[i]);

            log_verbose("Submitting transfer[%d] with buffer[%d]=%p\n",
                        i, i, data->curr_buf[i]);
	    }
    } else {
        assert(module == BLADERF_MODULE_TX);
        assert(!"TO DO");
    }
	log_verbose( "Stream Setup\n");

	while (true)
	{
        wait_status = WaitForSingleObjectEx(data->handles[idx],
                                            INFINITE, false);

        if (wait_status != WAIT_OBJECT_0) {
            if (wait_status == WAIT_TIMEOUT) {
                stream->error_code = BLADERF_ERR_TIMEOUT;
                log_debug("Steam timed out.\n");
            } else {
                stream->error_code = BLADERF_ERR_UNEXPECTED;
                log_debug("Failed to wait for stream event: 0x%lx\n",
                          (long unsigned int) wait_status);
            }

            break;
        }
		

		len = 0;
		next_buffer = NULL;

        log_verbose("Got event for transfer %d buffer[%d]=%p\n",
                    idx, find_buf(data->curr_buf[idx], stream),
                    data->curr_buf[idx]);

        success = data->ep->FinishDataXfer(data->curr_buf[idx], len,
                                           &data->ov[idx], data->token[idx]);

        if (success) {
			data->token[idx] = NULL;
			next_buffer = stream->cb(stream->dev, stream, NULL, data->curr_buf[idx], len/4, stream->user_data);
		} else {
            stream->error_code = BLADERF_ERR_IO;
			log_debug("Transfer idx=%d, buf=%p failed.\n", idx, &data->curr_buf[idx]);
			break;
		}

        if (next_buffer == BLADERF_STREAM_SHUTDOWN) {
            break;
        } else {
            log_verbose("Next buffer=%p\n", next_buffer);
            data->curr_buf[idx] = (PUCHAR) next_buffer;
            data->token[idx] = data->ep->BeginDataXfer(data->curr_buf[idx], buffer_size, &data->ov[idx]);
        }

        idx = (idx + 1) % data->num_transfers;
	}

	stream->state = STREAM_SHUTTING_DOWN;
	log_verbose( "Teardown \n");
    data->ep->Abort();
    for (unsigned int i = 0; i < data->num_transfers; i++) {
		LONG len=0;
		if (data->token[i] != NULL) {
			data->ep->FinishDataXfer(data->curr_buf[i] , len, &data->ov[i], data->token[i]);
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

extern "C" {
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