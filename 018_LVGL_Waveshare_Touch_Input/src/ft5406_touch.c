#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include "ft5406_touch.h"

#define FT5406_REG_MODE 0x00U
#define FT5406_REG_TOUCH_DATA 0x01U
#define FT5406_NORMAL_MODE 0x00U
#define FT5406_EVENT_DOWN 0U
#define FT5406_EVENT_CONTACT 2U
#define DISPLAY_WIDTH 800U
#define DISPLAY_HEIGHT 480U

static const struct i2c_dt_spec ft5406 = I2C_DT_SPEC_GET(DT_NODELABEL(ft5406));
static struct ft5406_status touch;

static uint16_t clamp_coordinate(int value, uint16_t maximum)
{
	return (uint16_t)CLAMP(value, 0, (int)maximum - 1);
}

static void ft5406_lvgl_read(lv_indev_t *indev, lv_indev_data_t *data)
{
	ARG_UNUSED(indev);
	data->state = touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	data->point.x = touch.x;
	data->point.y = touch.y;
}

int ft5406_init(void)
{
	uint8_t normal_mode[] = { FT5406_REG_MODE, FT5406_NORMAL_MODE };

	if (!i2c_is_ready_dt(&ft5406)) {
		touch.last_error = -ENODEV;
		return touch.last_error;
	}
	touch.last_error = i2c_write_dt(&ft5406, normal_mode, sizeof(normal_mode));
	if (touch.last_error == 0) {
		touch.ready = true;
		printk("OK TOUCH FT5406_READY address=0x%02x\n", ft5406.addr);
	}
	return touch.last_error;
}

int ft5406_poll(void)
{
	uint8_t reg = FT5406_REG_TOUCH_DATA;
	uint8_t raw[6];
	int ret;
	bool was_pressed = touch.pressed;

	if (!touch.ready) {
		return -ENODEV;
	}
	touch.polls++;
	ret = i2c_write_read_dt(&ft5406, &reg, sizeof(reg), raw, sizeof(raw));
	if (ret != 0) {
		touch.last_error = ret;
		touch.pressed = false;
		return ret;
	}
	touch.reads++;
	touch.last_error = 0;
	touch.pressed = false;
	uint8_t count = raw[1] & 0x0fU;
	uint8_t event = raw[2] >> 6U;
	if (count != 0U && (event == FT5406_EVENT_DOWN || event == FT5406_EVENT_CONTACT)) {
		touch.raw_x = ((uint16_t)(raw[2] & 0x0fU) << 8U) | raw[3];
		touch.raw_y = ((uint16_t)(raw[4] & 0x0fU) << 8U) | raw[5];
		/* The 4.3-inch panel is physically rotated relative to the DSI scan-out. */
		touch.x = clamp_coordinate((int)DISPLAY_WIDTH - 1 - touch.raw_x, DISPLAY_WIDTH);
		touch.y = clamp_coordinate((int)DISPLAY_HEIGHT - 1 - touch.raw_y, DISPLAY_HEIGHT);
		touch.pressed = true;
		if (!was_pressed) {
			touch.touches++;
			printk("OK TOUCH EVENT raw=%u,%u mapped=%u,%u count=%u\n",
			       touch.raw_x, touch.raw_y, touch.x, touch.y, count);
		}
	}
	return 0;
}

void ft5406_lvgl_register(void)
{
	lv_indev_t *indev = lv_indev_create();
	__ASSERT(indev != NULL, "LVGL touch input allocation failed");
	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, ft5406_lvgl_read);
}

void ft5406_get_status(struct ft5406_status *status)
{
	*status = touch;
}
