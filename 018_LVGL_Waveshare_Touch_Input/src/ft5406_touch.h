#ifndef FT5406_TOUCH_H
#define FT5406_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

struct ft5406_status {
	uint32_t polls;
	uint32_t reads;
	uint32_t touches;
	uint16_t raw_x;
	uint16_t raw_y;
	uint16_t x;
	uint16_t y;
	bool ready;
	bool pressed;
	int last_error;
};

int ft5406_init(void);
int ft5406_poll(void);
void ft5406_lvgl_register(void);
void ft5406_get_status(struct ft5406_status *status);

#endif
