#ifndef WAVESHARE_4P3_DISPLAY_H
#define WAVESHARE_4P3_DISPLAY_H

#include <zephyr/device.h>
#include <stdint.h>

struct waveshare_status {
	uint32_t frames;
	uint32_t flushes;
	uint32_t framebuffer;
	uint32_t crc;
	uint32_t dc_errors;
	uint32_t dsi_errors0;
	uint32_t dsi_errors1;
	uint32_t phy;
	uint32_t clock_hz;
	uint8_t panel_id;
	int last_error;
};

int waveshare_get_status(const struct device *dev, struct waveshare_status *status);

#endif
