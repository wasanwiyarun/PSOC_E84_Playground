#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <cy_sysclk.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <stdio.h>
#include <string.h>

#include "ft5406_touch.h"
#include "waveshare_4p3_display.h"

static int display_clock_metadata_init(void)
{
	/* CM33 enables the external-clock PLL.  The M55 PDL copy also needs
	 * the board crystal frequency before GFXSS/DSI queries it. */
	Cy_SysClk_EcoSetFrequency(17203200U);
	return 0;
}
SYS_INIT(display_clock_metadata_init, PRE_KERNEL_1, 0);

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static lv_obj_t *status_label;
static uint32_t click_count;
static char armed_token[40];
static uint32_t reported_touches;

static void update_status_label(const char *prefix)
{
	struct ft5406_status status;
	ft5406_get_status(&status);
	lv_label_set_text_fmt(status_label, "%s\nTouch count: %u\nLast: %u, %u",
			      prefix, status.touches, status.x, status.y);
}

static void button_event(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
		return;
	}
	click_count++;
	update_status_label("Button clicked!");
	printk("OK TOUCH LVGL_CLICK count=%u\n", click_count);
}

static void report_touch_status(const char *token)
{
	struct ft5406_status status;
	ft5406_get_status(&status);
	int ret = (!status.ready || status.last_error != 0) ? -EIO : 0;
	printk("%s TOUCH STATUS token=%s ready=%u polls=%u reads=%u touches=%u "
	       "pressed=%u raw=%u,%u mapped=%u,%u ret=%d\n",
	       ret == 0 ? "OK" : "FAIL", token, status.ready, status.polls, status.reads,
	       status.touches, status.pressed, status.raw_x, status.raw_y, status.x, status.y, ret);
}

static void handle_command(char *line)
{
	char command[16];
	char token[40];
	char extra;
	if (sscanf(line, "touch %15s %39s %c", command, token, &extra) != 2) {
		printk("FAIL TOUCH COMMAND expected='touch info|arm TOKEN'\n");
		return;
	}
	if (strcmp(command, "info") == 0) {
		report_touch_status(token);
	} else if (strcmp(command, "arm") == 0) {
		strcpy(armed_token, token);
		reported_touches = 0;
		printk("OK TOUCH ARMED token=%s\n", armed_token);
	} else {
		printk("FAIL TOUCH COMMAND token=%s\n", token);
	}
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(display)) {
		printk("FAIL TOUCH display_not_ready\n");
		return 0;
	}
	int ret = lvgl_init();
	if (ret != 0 || lv_display_get_default() == NULL) {
		printk("FAIL TOUCH LVGL_INIT ret=%d\n", ret);
		return 0;
	}
	if (ft5406_init() != 0) {
		printk("FAIL TOUCH FT5406_INIT\n");
		return 0;
	}
	ft5406_lvgl_register();

	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x102238), 0);
	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "TESA AIoT E84\nFT5406 Touch / LVGL");
	lv_obj_set_width(title, 800);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

	lv_obj_t *button = lv_button_create(screen);
	lv_obj_set_size(button, 400, 125);
	lv_obj_align(button, LV_ALIGN_CENTER, 0, 15);
	lv_obj_set_style_bg_color(button, lv_color_hex(0x1565C0), 0);
	lv_obj_add_event_cb(button, button_event, LV_EVENT_CLICKED, NULL);
	lv_obj_t *button_label = lv_label_create(button);
	lv_label_set_text(button_label, "TOUCH ME");
	lv_obj_set_style_text_font(button_label, &lv_font_montserrat_28, 0);
	lv_obj_center(button_label);

	status_label = lv_label_create(screen);
	lv_obj_set_width(status_label, 800);
	lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(status_label, lv_color_hex(0xB7D7F5), 0);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -35);
	update_status_label("Touch the blue button");
	lv_refr_now(NULL);
	ret = display_blanking_off(display);
	if (ret != 0) {
		printk("FAIL TOUCH BACKLIGHT ret=%d\n", ret);
		return 0;
	}
	printk("OK TOUCH INITIALIZATION_SUCCESS controller=FT5406\n");
	printk("INFO TOUCH COMMANDS touch info|arm TOKEN\n");

	char line[96];
	size_t used = 0;
	bool overflow = false;
	while (true) {
		(void)ft5406_poll();
		struct ft5406_status status;
		ft5406_get_status(&status);
		if (armed_token[0] != '\0' && status.touches != reported_touches) {
			reported_touches = status.touches;
			printk("OK TOUCH TAP token=%s mapped=%u,%u raw=%u,%u\n", armed_token,
			       status.x, status.y, status.raw_x, status.raw_y);
			armed_token[0] = '\0';
		}
		unsigned char ch;
		while (uart_poll_in(uart, &ch) == 0) {
			if (ch == '\r' || ch == '\n') {
				if (overflow) {
					printk("FAIL TOUCH COMMAND too_long\n");
				} else if (used > 0) {
					line[used] = '\0';
					handle_command(line);
				}
				used = 0;
				overflow = false;
			} else if (used < sizeof(line) - 1) {
				line[used++] = ch;
			} else {
				overflow = true;
			}
		}
		lv_timer_handler();
		k_msleep(20);
	}
}
