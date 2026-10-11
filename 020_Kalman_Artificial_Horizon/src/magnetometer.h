/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct magnetometer_status {
    float xyz[3], temperature;
    uint32_t samples, errors, transfers, clock_hz, bus_status;
    int64_t sample_ms;
    int ret;
    uint8_t chip_id, address;
    bool ready, field_high;
};
/* Single-owner API: call init/read only from the sensor worker. */
int magnetometer_init(void);
int magnetometer_read(void);
void magnetometer_status(struct magnetometer_status *out);
