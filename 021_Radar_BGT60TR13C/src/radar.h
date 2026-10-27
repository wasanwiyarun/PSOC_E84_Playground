#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RADAR_SAMPLES 128U
#define RADAR_MAX_FRAMES 64U
struct radar_result {
    uint32_t frames, samples, elapsed_us, mismatches, irq_observed, fstat;
    int ret;
};
int radar_init(void);
int radar_id(uint32_t *id);
int radar_capture(bool test, unsigned frames, uint16_t *samples, struct radar_result *result);
uint32_t radar_spi_hz(void);
