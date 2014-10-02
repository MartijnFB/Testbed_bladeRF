/*
 * Example snippet for RX synchronous interface documentation.
 *
 * This file is part of the bladeRF project:
 *   http://www.github.com/nuand/bladeRF
 *
 * Copyright (C) 2014 Nuand LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <libbladeRF.h>
#include "example_common.h"

/* Just retransmit the received samples */
bool do_work(int16_t *rx, unsigned int rx_len,
             int16_t *tx, unsigned int tx_len)
{
    static unsigned int call_no = 0;

    assert(tx_len == rx_len);
    memcpy(tx, rx, rx_len * 2 * sizeof(int16_t));

    return (++call_no >= 5000);
}

/** [example_snippet] */
int sync_rx_example(struct bladerf *dev)
{
    int status, ret;
    bool done = false;

    /* "User" samples buffers and their associated sizes, in units of samples.
     * Recall that one sample = two int16_t values. */
    int16_t *rx_samples = NULL;
    int16_t *tx_samples = NULL;
    const unsigned int samples_len = 10000; /* May be any (reasonable) size */

    /* These items configure the underlying asynch stream used by the sync
     * interface. The "buffer" here refers to those used internally by worker
     * threads, not the `samples` buffer above. */
    const unsigned int num_buffers = 16;
    const unsigned int buffer_size = 8192;  /* Must be a multiple of 1024 */
    const unsigned int num_transfers = 8;
    const unsigned int timeout_ms  = 3500;


    /* Allocate a buffer to store received samples in */
    rx_samples = malloc(samples_len * 2 * sizeof(int16_t));
    if (rx_samples == NULL) {
        perror("malloc");
        return BLADERF_ERR_MEM;
    }

    /* Allocate a buffer to prepare transmit data in */
    tx_samples = malloc(samples_len * 2 * sizeof(int16_t));
    if (tx_samples == NULL) {
        perror("malloc");
        free(rx_samples);
        return BLADERF_ERR_MEM;
    }

    /* Configure both the device's RX and TX modules for use with the synchronous
     * interface. SC16 Q11 samples *without* metadata are used. */
    status = bladerf_sync_config(dev,
            BLADERF_MODULE_RX,
            BLADERF_FORMAT_SC16_Q11,
            num_buffers,
            buffer_size,
            num_transfers,
            timeout_ms);

    if (status != 0) {
        fprintf(stderr, "Failed to configure RX sync interface: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    status = bladerf_sync_config(dev,
                                 BLADERF_MODULE_TX,
                                 BLADERF_FORMAT_SC16_Q11,
                                 num_buffers,
                                 buffer_size,
                                 num_transfers,
                                 timeout_ms);

    if (status != 0) {
        fprintf(stderr, "Failed to configure TX sync interface: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    /* We must always enable the modules *after* calling bladerf_sync_config(),
     * and *before* attempting to RX or TX samples. */
    status = bladerf_enable_module(dev, BLADERF_MODULE_RX, true);
    if (status != 0) {
        fprintf(stderr, "Failed to enable RX module: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    status = bladerf_enable_module(dev, BLADERF_MODULE_TX, true);
    if (status != 0) {
        fprintf(stderr, "Failed to enable RX module: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    /* Receive samples and do work on them and then transmit a response.
     *
     * Note we transmit more than `buffer_size` samples to ensure that our
     * samples are written to the FPGA. (The samples are sent when the
     * synchronous interface's internal buffer of `buffer_size` samples is
     * filled.) This is generally not nececssary if you are continuously
     * streaming TX data. Otherwise, you may need to zero-pad your TX data to
     * achieve this.
     */
    while (status == 0 && !done) {
        status = bladerf_sync_rx(dev, rx_samples, samples_len, NULL, 5000);
        if (status == 0) {
            done = do_work(rx_samples, samples_len,
                           tx_samples, samples_len);

            if (!done) {
                status = bladerf_sync_tx(dev, tx_samples, samples_len,
                                         NULL, 5000);

                if (status != 0) {
                    fprintf(stderr, "Failed to TX samples: %s\n",
                            bladerf_strerror(status));
                }
            }
        } else {
            fprintf(stderr, "Failed to RX samples: %s\n",
                    bladerf_strerror(status));
        }
    }

    if (status == 0) {
        /* Wait a few seconds for any remaining TX samples to finish */
        usleep(2000000);
    }


out:
    ret = status;

    /* Disable RX module, shutting down our underlying RX stream */
    status = bladerf_enable_module(dev, BLADERF_MODULE_RX, false);
    if (status != 0) {
        fprintf(stderr, "Failed to disable RX module: %s\n",
                bladerf_strerror(status));
    }

    /* Disable TX module, shutting down our underlying TX stream */
    status = bladerf_enable_module(dev, BLADERF_MODULE_TX, false);
    if (status != 0) {
        fprintf(stderr, "Failed to disable TX module: %s\n",
                bladerf_strerror(status));
    }

    /* Free up our resources */
    free(rx_samples);
    free(tx_samples);

    return ret;
}
/** [example_snippet] */

static void usage(const char *argv0) {
    printf("Usage: %s [device specifier]\n\n", argv0);
}


int main(int argc, char *argv[])
{
    int status;
    struct bladerf *dev = NULL;
    const char *devstr = NULL;

    if (argc == 2) {
        if (!strcasecmp("-h", argv[1]) || !strcasecmp("--help", argv[1])) {
            usage(argv[0]);
            return 0;
        } else {
            devstr = argv[1];
        }
    } else if (argc > 1) {
        usage(argv[0]);
        return -1;
    }

    dev = example_init(devstr);
    if (dev) {
        printf("Running...\n");
        status = sync_rx_example(dev);
        printf("Closing the device...\n");
        bladerf_close(dev);
    }

    return status;
}
