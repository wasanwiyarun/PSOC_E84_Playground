/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#define LED_NODE DT_ALIAS(led0)
#define DEFAULT_BLINK_PERIOD_MS 500
#define MIN_BLINK_PERIOD_MS 50
#define MAX_BLINK_PERIOD_MS 10000
#define COMMAND_MAX_LENGTH 32
#define APP_VERSION "0.1.0"

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static atomic_t blink_enabled = ATOMIC_INIT(1);
static atomic_t blink_period_ms = ATOMIC_INIT(DEFAULT_BLINK_PERIOD_MS);

K_MUTEX_DEFINE(led_lock);
K_SEM_DEFINE(application_ready, 0, 2);

static void print_status(void)
{
	printk("OK LED0 BLINK=%s PERIOD_MS=%ld\n",
	       atomic_get(&blink_enabled) ? "ON" : "OFF",
	       (long)atomic_get(&blink_period_ms));
}

static void print_info(void)
{
	printk("OK BOARD=kit_pse84_ai APP=led_blink_serial_control VERSION=%s\n",
	       APP_VERSION);
}

static void set_blink_enabled(bool enabled)
{
	atomic_set(&blink_enabled, enabled);

	if (!enabled) {
		k_mutex_lock(&led_lock, K_FOREVER);
		gpio_pin_set_dt(&led, 0);
		k_mutex_unlock(&led_lock);
	}

	print_status();
}

static void set_blink_period(const char *period_text)
{
	char *end;
	unsigned long period = strtoul(period_text, &end, 10);

	if (*period_text == '\0' || *end != '\0' || period < MIN_BLINK_PERIOD_MS ||
	    period > MAX_BLINK_PERIOD_MS) {
		printk("ERR INVALID PERIOD_MS=%s RANGE=%d..%d\n", period_text,
		       MIN_BLINK_PERIOD_MS, MAX_BLINK_PERIOD_MS);
		return;
	}

	atomic_set(&blink_period_ms, (atomic_val_t)period);
	print_status();
}

static void blink_thread(void)
{
	k_sem_take(&application_ready, K_FOREVER);

	while (true) {
		k_mutex_lock(&led_lock, K_FOREVER);
		if (atomic_get(&blink_enabled)) {
			gpio_pin_toggle_dt(&led);
		} else {
			gpio_pin_set_dt(&led, 0);
		}
		k_mutex_unlock(&led_lock);

		k_sleep(K_MSEC(atomic_get(&blink_period_ms)));
	}
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		print_info();
	} else if (strcmp(command, "led blink start") == 0) {
		set_blink_enabled(true);
	} else if (strcmp(command, "led blink stop") == 0) {
		set_blink_enabled(false);
	} else if (strcmp(command, "led blink status") == 0) {
		print_status();
	} else if (strncmp(command, "led blink period ",
			   strlen("led blink period ")) == 0) {
		set_blink_period(command + strlen("led blink period "));
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;led blink start;led blink stop;"
		       "led blink status;led blink period <ms>;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

static void serial_thread(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	k_sem_take(&application_ready, K_FOREVER);
	console_init();

	while (true) {
		uint8_t character = console_getchar();

		if (character == '\r' || character == '\n') {
			if (length > 0) {
				command[length] = '\0';
				handle_command(command);
				length = 0;
			}
			continue;
		}

		if (character == '\b' || character == 0x7f) {
			if (length > 0) {
				length--;
			}
			continue;
		}

		if (length < sizeof(command) - 1U) {
			command[length++] = character;
		} else {
			length = 0;
			printk("Command too long; discarded.\n");
		}
	}
}

K_THREAD_DEFINE(blink_thread_id, 1024, blink_thread, NULL, NULL, NULL,
		5, 0, 0);
K_THREAD_DEFINE(serial_thread_id, 1536, serial_thread, NULL, NULL, NULL,
		5, 0, 0);

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device is not ready.\n");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("Could not configure LED: %d\n", ret);
		return 0;
	}

	printk("PSOC Edge84 LED serial-control example\n");
	print_info();
	print_status();
	printk("INFO COMMANDS=info;led blink start;led blink stop;"
	       "led blink status;led blink period <ms>;help\n");

	/* Start both workers only after the LED GPIO is configured. */
	k_sem_give(&application_ready);
	k_sem_give(&application_ready);

	return 0;
}
