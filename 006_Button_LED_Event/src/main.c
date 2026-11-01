#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#define APP_VERSION "0.1.0"
#define COMMAND_MAX_LENGTH 32

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_callback;
static atomic_t button_presses = ATOMIC_INIT(0);
static atomic_t led_on = ATOMIC_INIT(0);
K_SEM_DEFINE(button_event, 0, 16);

static void print_status(void)
{
	printk("OK BUTTON PRESSES=%ld LED0=%s\n", (long)atomic_get(&button_presses),
	       atomic_get(&led_on) ? "ON" : "OFF");
}

static void on_button(const struct device *port, struct gpio_callback *callback,
		      uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);
	atomic_inc(&button_presses);
	k_sem_give(&button_event);
}

static void button_event_thread(void)
{
	while (true) {
		k_sem_take(&button_event, K_FOREVER);
		atomic_val_t next_led_state = !atomic_get(&led_on);

		gpio_pin_set_dt(&led, next_led_state);
		atomic_set(&led_on, next_led_state);
		print_status();
	}
}

K_THREAD_DEFINE(button_event_thread_id, 1024, button_event_thread, NULL, NULL, NULL,
		15, 0, 0);

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=button_led_event VERSION=%s\n", APP_VERSION);
	} else if (strcmp(command, "button status") == 0) {
		print_status();
	} else if (strcmp(command, "led on") == 0 || strcmp(command, "led off") == 0) {
		bool next_led_state = command[4] == 'o' && command[5] == 'n';

		gpio_pin_set_dt(&led, next_led_state);
		atomic_set(&led_on, next_led_state);
		print_status();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;button status;led on;led off;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button) ||
	    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT) < 0 ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
		printk("ERR GPIO_SETUP\n");
		return 0;
	}

	gpio_init_callback(&button_callback, on_button, BIT(button.pin));
	if (gpio_add_callback(button.port, &button_callback) < 0) {
		printk("ERR BUTTON_CALLBACK\n");
		return 0;
	}

	console_init();
	printk("OK BOARD=kit_pse84_ai APP=button_led_event VERSION=%s\n", APP_VERSION);
	printk("INFO COMMANDS=info;button status;led on;led off;help\n");
	while (true) {
		uint8_t character = console_getchar();

		if (character == '\r' || character == '\n') {
			if (length > 0) {
				command[length] = '\0';
				handle_command(command);
				length = 0;
			}
		} else if (length < sizeof(command) - 1U) {
			command[length++] = character;
		} else {
			length = 0;
			printk("ERR COMMAND_TOO_LONG\n");
		}
	}
}
