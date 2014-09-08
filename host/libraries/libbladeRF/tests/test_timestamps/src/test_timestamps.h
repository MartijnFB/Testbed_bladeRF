/*
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

#ifndef TEST_TIMESTAMPS_H_
#define TEST_TIMESTAMPS_H_

#include <stdint.h>
#include "test_common.h"

#define DEFAULT_SAMPLERATE  1000000

struct app_params {
    char *device_str;
    unsigned int samplerate;

    char *test_name;

    uint64_t prng_seed;
    uint64_t prng_state;

    unsigned int num_buffers;
    unsigned int num_xfers;
    unsigned int buf_size; // In samples
    unsigned int timeout_ms;
};

/**
 * Initialize and the specified module for sync operation, with the settings
 * specified in the app_params structure.
 *
 * @param   dev             Device handle
 * @param   module          Module to initialize
 * @param   buf_size        Size of buffer to use. If 0, this value will
 *                          be extracted from the application parameters.
 * @param   p               Application parameters
 *
 * @return 0 on success, non-zero on failure
 */
int perform_sync_init(struct bladerf *dev, bladerf_module module,
                      unsigned int buf_size, struct app_params *p);

/**
 * Enable/disable the FPGA's counter mode
 *
 * @param   dev             Device handle
 * @param   enable          Set true to enable, false to disable
 *
 * @return 0 on success, non-zero on failure
 */
int enable_counter_mode(struct bladerf *dev, bool enable);

/**
 * Validate the contents of a sample buffer generated by the FPGA in counter
 * mode
 *
 * @param[in]       samples     Sample buffer
 * @param[in]       n_samples   Size of `samples`, in units of samples
 * @param[in]       ctr         Expected counter value for the start of
 *                              the provided sample block
 *
 * @return true if valid, false otherwise
 */
bool counter_data_is_valid(int16_t *samples, size_t n_samples, uint32_t ctr);

/**
 * Get the earlier of two error codes
 *
 * @return
 */
static inline int first_error(int earlier_status, int later_status)
{
    return earlier_status == 0 ? later_status : earlier_status;
}

static inline uint32_t extract_counter_val(int16_t *samples)
{
#if BLADERF_BIG_ENDIAN
    const uint32_t val = (LE16_TO_HOST(samples[1]) << 16) |
                         LE16_TO_HOST(samples[0]);
#else
    //const uint32_t val = (samples[1] << 16) | samples[0];
    const uint32_t val = ((uint32_t *)samples)[0];
#endif

    return val;
}

#endif
