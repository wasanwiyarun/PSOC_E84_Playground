#include <stdlib.h>
#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

#define COMMAND_MAX_LENGTH 32
static const struct device *const sht40 = DEVICE_DT_GET(DT_ALIAS(ambient_temp0));
static const struct device *const dps368 = DEVICE_DT_GET(DT_ALIAS(pressure_sensor));

static void print_value(const char *name, const struct sensor_value *value)
{
	int64_t micro = sensor_value_to_micro(value);
	printk("OK %s_U=%lld\n", name, micro);
}

static void env_read(void)
{
	struct sensor_value value;

	if (!device_is_ready(sht40) || !device_is_ready(dps368) ||
	    sensor_sample_fetch(sht40) < 0 || sensor_sample_fetch(dps368) < 0) {
		printk("ERR ENV_READ\n");
		return;
	}
	if (sensor_channel_get(sht40, SENSOR_CHAN_AMBIENT_TEMP, &value) < 0) {
		printk("ERR ENV_READ\n");
		return;
	}
	print_value("SHT40_TEMP_C", &value);
	if (sensor_channel_get(sht40, SENSOR_CHAN_HUMIDITY, &value) < 0) {
		printk("ERR ENV_READ\n");
		return;
	}
	print_value("SHT40_HUMIDITY_PCT", &value);
	if (sensor_channel_get(dps368, SENSOR_CHAN_AMBIENT_TEMP, &value) < 0) {
		printk("ERR ENV_READ\n");
		return;
	}
	print_value("DPS368_TEMP_C", &value);
	if (sensor_channel_get(dps368, SENSOR_CHAN_PRESS, &value) < 0) {
		printk("ERR ENV_READ\n");
		return;
	}
	print_value("DPS368_PRESS_KPA", &value);
	printk("OK ENV_READ=PASS\n");
}

static void command(char *text)
{
	if (strcmp(text, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=environment_monitor VERSION=0.1.0\n");
	} else if (strcmp(text, "env read") == 0) {
		env_read();
	} else if (strcmp(text, "help") == 0) {
		printk("INFO COMMANDS=info;env read;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", text);
	}
}

int main(void)
{
	char text[COMMAND_MAX_LENGTH];
	size_t length = 0;

	console_init();
	printk("OK BOARD=kit_pse84_ai APP=environment_monitor VERSION=0.1.0\n");
	printk("INFO COMMANDS=info;env read;help\n");
	while (true) {
		uint8_t c = console_getchar();
		if (c == '\r' || c == '\n') {
			if (length) {
				text[length] = 0;
				command(text);
				length = 0;
			}
		} else if (length < sizeof(text) - 1U) {
			text[length++] = c;
		} else {
			length = 0;
			printk("ERR COMMAND_TOO_LONG\n");
		}
	}
}
