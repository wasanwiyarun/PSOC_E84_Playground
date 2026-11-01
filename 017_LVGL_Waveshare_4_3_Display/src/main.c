#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <cy_sysclk.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <stdio.h>
#include <string.h>
#include "waveshare_4p3_display.h"

static int display_clock_metadata_init(void)
{
	/* PDL keeps this frequency in each core's own RAM. Set the board's
	 * crystal frequency before peripheral drivers query an ECO-fed PLL. */
	Cy_SysClk_EcoSetFrequency(17203200U);
	return 0;
}
SYS_INIT(display_clock_metadata_init, PRE_KERNEL_1, 0);

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const char demo_text[] = "TESA AIoT E84\nWaveshare 4.3 RPi DSI\nLVGL / Zephyr";

static int report_status(const char *token)
{
	struct waveshare_status status;
	int ret = waveshare_get_status(display, &status);
	printk("%s DISPLAY STATUS token=%s panel=0x%02x clock=%u frames=%u flushes=%u "
	       "fb=0x%08x crc=0x%08x phy=0x%x dc=0x%x dsi0=0x%x dsi1=0x%x ret=%d\n",
	       ret == 0 ? "OK" : "FAIL", token, status.panel_id, status.clock_hz,
	       status.frames, status.flushes, status.framebuffer, status.crc,
	       status.phy, status.dc_errors, status.dsi_errors0, status.dsi_errors1, ret);
	return ret;
}

static void handle_command(char *line, lv_obj_t *label)
{
	char command[16], token[40], extra;
	if (sscanf(line, "display %15s %39s %c", command, token, &extra) != 2) {
		printk("FAIL DISPLAY COMMAND expected='display info|test|demo TOKEN'\n");
		return;
	}
	if (strcmp(command, "test") == 0) {
		lv_label_set_text_fmt(label, "TESA AIoT E84\nDisplay test\n%s", token);
	} else if (strcmp(command, "demo") == 0) {
		lv_label_set_text(label, demo_text);
	} else if (strcmp(command, "info") == 0) {
		(void)report_status(token);
		return;
	} else {
		printk("FAIL DISPLAY COMMAND token=%s\n", token);
		return;
	}
	lv_obj_invalidate(lv_screen_active());
	lv_refr_now(NULL);
	(void)report_status(token);
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(display)) {
		printk("FAIL DISPLAY device_not_ready\n");
		return 0;
	}
	int ret = lvgl_init();
	if (ret != 0 || lv_display_get_default() == NULL || lv_screen_active() == NULL) {
		printk("FAIL DISPLAY LVGL_INIT ret=%d\n", ret);
		return 0;
	}
	printk("OK DISPLAY LVGL_INIT heap=%u\n", CONFIG_LV_Z_MEM_POOL_SIZE);

	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x102238), 0);
	lv_obj_t *label = lv_label_create(screen);
	if (label == NULL) {
		printk("FAIL DISPLAY LVGL_LABEL\n");
		return 0;
	}
	lv_label_set_text(label, demo_text);
	lv_obj_set_width(label, 800);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(label, lv_color_white(), 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
	/* The rightmost 32 pixels of the 832-pixel timing are not visible. */
	lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

	/* Submit the first rendered LVGL frame immediately. */
	lv_refr_now(NULL);
	ret = display_blanking_off(display);
	if (ret != 0) {
		printk("FAIL DISPLAY BACKLIGHT ret=%d\n", ret);
		return 0;
	}
	if (report_status("boot") == 0) {
		printk("OK DISPLAY INITIALIZATION_SUCCESS lvgl_frame_presented=1\n");
	}
	/* Keep diagnostics available even if the display health check failed. */
	printk("INFO DISPLAY COMMANDS display info|test|demo TOKEN\n");
	char line[96];
	size_t used = 0;
	bool overflow = false;
	while (true) {
		unsigned char ch;
		while (uart_poll_in(uart, &ch) == 0) {
			if (ch == '\r' || ch == '\n') {
				if (overflow) {
					printk("FAIL DISPLAY COMMAND too_long\n");
				} else if (used > 0) {
					line[used] = '\0';
					handle_command(line, label);
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
		k_msleep(10);
	}
}
