#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		printk("FAIL DISPLAY device_not_ready\n");
		return 0;
	}

	lv_obj_t *label = lv_label_create(lv_screen_active());
	lv_label_set_text(label, "TESA AIoT E84\nWaveshare 4.3 RPi DSI\nLVGL / Zephyr");
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_center(label);

	printk("OK DISPLAY LVGL_TEXT width=800 height=480\n");

	while (true) {
		lv_timer_handler();
		k_msleep(10);
	}
}
