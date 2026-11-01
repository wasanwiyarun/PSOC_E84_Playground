#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <cy_sysclk.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ft5406_touch.h"
#include "fluid.h"
#include "motion.h"

#define INTERVAL_MS 33
static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *imu = DEVICE_DT_GET(DT_ALIAS(accel0));
static lv_obj_t *mode_label, *water_image;
static uint16_t pixels[FLUID_WIDTH * FLUID_HEIGHT] __aligned(8);
/* Pre-expand the small fluid raster: LVGL's per-pixel software image scaling
 * costs more than the entire physics solver on this board. */
static uint16_t screen_pixels[FLUID_WIDTH * 4 * FLUID_HEIGHT * 4]
	__attribute__((section("GFX_MEM"), aligned(128)));
static const lv_image_dsc_t water_bitmap = {
	.header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
		.w = FLUID_WIDTH * 4, .h = FLUID_HEIGHT * 4, .stride = FLUID_WIDTH * 8 },
	.data_size = sizeof(screen_pixels), .data = (const uint8_t *)screen_pixels,
};
static uint32_t updates, motion_reads, step_us, frame_ms, pixel_crc;
static float gravity_x, gravity_y = 120;
static int32_t ax_mg, ay_mg, az_mg;
static int last_imu_error;
static bool imu_ready;
/* Test gravity is visibly labelled and automatically expires. */
static const char *mode = "imu";
static int64_t override_until;

static int display_clock_metadata_init(void)
{
	Cy_SysClk_EcoSetFrequency(17203200U);
	return 0;
}
SYS_INIT(display_clock_metadata_init, PRE_KERNEL_1, 0);

static int32_t acceleration_mg(const struct sensor_value *v)
{ return (int32_t)(((int64_t)v->val1 * 1000000 + v->val2) / 9807); }

static int sample_motion(void)
{
	struct sensor_value accel[3];
	int ret = sensor_sample_fetch(imu);
	if (ret == 0) ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	last_imu_error = ret;
	if (ret != 0) return ret;
	ax_mg = acceleration_mg(&accel[0]);
	ay_mg = acceleration_mg(&accel[1]);
	az_mg = acceleration_mg(&accel[2]);
	float gx, gy;
	motion_screen_gravity(ax_mg, ay_mg, &gx, &gy);
	gravity_x += (gx - gravity_x) * 0.65f;
	gravity_y += (gy - gravity_y) * 0.65f;
	motion_reads++;
	return 0;
}

static void reset_water(lv_event_t *event)
{
	ARG_UNUSED(event);
	fluid_reset(); mode = "imu"; override_until = 0;
	printk("OK WATER RESET particles=%u\n", FLUID_PARTICLES);
}

static void report_status(const char *token)
{
	struct ft5406_status touch;
	ft5406_get_status(&touch);
	printk("OK WATER STATUS token=%s imu_ready=%u motion_reads=%u tilt=%d updates=%u "
	       "mode=%s particles=%u valid=%d center=%d,%d pixels=%u crc=%08x "
	       "accel=%d,%d,%d gravity=%d,%d step_us=%u frame_ms=%u "
	       "touch_ready=%u polls=%u reads=%u touches=%u ret=%d\n",
	       token, imu_ready, motion_reads, (int)(gravity_x / 2), updates, mode,
	       FLUID_PARTICLES, fluid_valid(), fluid_center_x(), fluid_center_y(),
	       fluid_pixels(), pixel_crc, ax_mg, ay_mg, az_mg, (int)gravity_x,
	       (int)gravity_y, step_us, frame_ms, touch.ready, touch.polls,
	       touch.reads, touch.touches, last_imu_error ? last_imu_error : touch.last_error);
}

static void handle_command(char *line)
{
	char command[16], token[40], extra;
	if (sscanf(line, "water %15s %39s %c", command, token, &extra) != 2) {
		printk("FAIL WATER COMMAND expected='water COMMAND TOKEN'\n"); return;
	}
	if (!strcmp(command, "reset") || !strcmp(command, "level")) reset_water(NULL);
	else if (!strcmp(command, "auto")) { mode = "imu"; override_until = 0; }
	else if (!strcmp(command, "left")) mode = "left";
	else if (!strcmp(command, "right")) mode = "right";
	else if (!strcmp(command, "up")) mode = "up";
	else if (!strcmp(command, "down")) mode = "down";
	else if (strcmp(command, "info")) {
		printk("FAIL WATER COMMAND token=%s\n", token); return;
	}
	if (strcmp(command, "info") && strcmp(mode, "imu"))
		override_until = k_uptime_get() + 15000;
	report_status(token);
}

static void render_water(void)
{
	uint32_t begin = k_cycle_get_32();
	float gx = gravity_x, gy = gravity_y;
	if (override_until && k_uptime_get() >= override_until) { mode = "imu"; override_until = 0; }
	if (!strcmp(mode, "left")) { gx = -160; gy = 30; }
	if (!strcmp(mode, "right")) { gx = 160; gy = 30; }
	if (!strcmp(mode, "up")) { gx = 0; gy = -160; }
	if (!strcmp(mode, "down")) { gx = 0; gy = 160; }
	fluid_advance(gx, gy, frame_ms ? frame_ms : INTERVAL_MS);
	fluid_render(pixels);
	for (int y = 0; y < FLUID_HEIGHT; ++y) {
		uint16_t *row = &screen_pixels[y * 4 * FLUID_WIDTH * 4];
		for (int x = 0; x < FLUID_WIDTH; ++x) {
			uint16_t color = pixels[y * FLUID_WIDTH + x];
			row[x * 4] = color; row[x * 4 + 1] = color;
			row[x * 4 + 2] = color; row[x * 4 + 3] = color;
		}
		for (int repeat = 1; repeat < 4; ++repeat)
			memcpy(row + repeat * FLUID_WIDTH * 4, row, FLUID_WIDTH * 8);
	}
	pixel_crc = crc32_ieee((const uint8_t *)pixels, sizeof(pixels));
	step_us = k_cyc_to_us_floor32(k_cycle_get_32() - begin);
	lv_obj_invalidate(water_image);
	if (updates % 6 == 0) {
		if (!strcmp(mode, "imu")) lv_label_set_text(mode_label, "TILT TO POUR  /  SHAKE TO SPLASH");
		else lv_label_set_text_fmt(mode_label, "TEST GRAVITY: %s", mode);
	}
	updates++;
}

static int create_ui(void)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x02070D), 0);
	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "TESA AIoT E84  /  FLUID SIMULATION");
	lv_obj_set_style_text_color(title, lv_color_hex(0xBDEBFF), 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, 36, 18);
	mode_label = lv_label_create(screen);
	lv_obj_set_style_text_color(mode_label, lv_color_hex(0x28D8FF), 0);
	lv_obj_align(mode_label, LV_ALIGN_TOP_LEFT, 36, 48);
	lv_obj_t *tank = lv_obj_create(screen);
	lv_obj_set_size(tank, FLUID_WIDTH * 4 + 6, FLUID_HEIGHT * 4 + 6);
	lv_obj_align(tank, LV_ALIGN_TOP_MID, 0, 78);
	lv_obj_set_style_bg_color(tank, lv_color_hex(0x000408), 0);
	lv_obj_set_style_border_color(tank, lv_color_hex(0x65D8FF), 0);
	lv_obj_set_style_border_width(tank, 3, 0);
	lv_obj_set_style_radius(tank, 0, 0);
	lv_obj_set_style_pad_all(tank, 0, 0);
	lv_obj_remove_flag(tank, LV_OBJ_FLAG_SCROLLABLE);
	water_image = lv_image_create(tank);
	if (!water_image) return -ENOMEM;
	lv_image_set_src(water_image, &water_bitmap);
	lv_obj_set_pos(water_image, 0, 0);
	fluid_reset(); render_water();
	lv_obj_t *button = lv_button_create(screen);
	lv_obj_set_size(button, 260, 48);
	lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -14);
	lv_obj_set_style_bg_color(button, lv_color_hex(0x075F89), 0);
	lv_obj_add_event_cb(button, reset_water, LV_EVENT_CLICKED, NULL);
	lv_obj_t *label = lv_label_create(button);
	lv_label_set_text(label, "RESET WATER");
	lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
	lv_obj_center(label);
	return 0;
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(display)) { printk("FAIL WATER DISPLAY_NOT_READY\n"); return 0; }
	int ret = lvgl_init();
	if (ret || !lv_display_get_default()) { printk("FAIL WATER LVGL_INIT ret=%d\n", ret); return 0; }
	if (ft5406_init()) { printk("FAIL WATER FT5406_INIT\n"); return 0; }
	ft5406_lvgl_register();
	const struct sensor_value hz = { .val1 = 100, .val2 = 0 };
	last_imu_error = device_is_ready(imu) ? sensor_attr_set(imu,
		SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &hz) : -ENODEV;
	k_msleep(100);
	if (!last_imu_error) sample_motion();
	imu_ready = last_imu_error == 0;
	if (!imu_ready) { printk("FAIL WATER BMI270_INIT ret=%d\n", last_imu_error); return 0; }
	if (create_ui()) { printk("FAIL WATER UI_CREATE\n"); return 0; }
	lv_refr_now(NULL);
	ret = display_blanking_off(display);
	if (ret) { printk("FAIL WATER BACKLIGHT ret=%d\n", ret); return 0; }
	printk("OK WATER INITIALIZATION_SUCCESS engine=particles bmi270=1 particles=%u interval_ms=%u\n",
		FLUID_PARTICLES, INTERVAL_MS);
	printk("INFO WATER COMMANDS water info|reset|auto|left|right|up|down TOKEN\n");
	int64_t next_update = k_uptime_get(), last_frame = next_update;
	char line[96]; size_t used = 0; bool overflow = false;
	while (true) {
		ft5406_poll();
		int64_t now = k_uptime_get();
		if (now >= next_update) {
			frame_ms = (uint32_t)(now - last_frame); last_frame = now;
			sample_motion(); render_water();
			/* Avoid accumulating catch-up work after slow display refresh. */
			next_update = now + INTERVAL_MS;
		}
		unsigned char ch;
		while (uart_poll_in(uart, &ch) == 0) {
			if (ch == '\r' || ch == '\n') {
				if (overflow) printk("FAIL WATER COMMAND too_long\n");
				else if (used) { line[used] = '\0'; handle_command(line); }
				used = 0; overflow = false;
			} else if (used < sizeof(line) - 1) line[used++] = ch;
			else overflow = true;
		}
		lv_timer_handler(); k_msleep(2);
	}
}
