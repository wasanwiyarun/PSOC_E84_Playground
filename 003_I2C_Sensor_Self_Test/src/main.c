#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define COMMAND_MAX_LENGTH 32
#define APP_VERSION "0.1.0"

static const struct device *const sht40 = DEVICE_DT_GET(DT_ALIAS(ambient_temp0));
static const struct device *const bmi270 = DEVICE_DT_GET(DT_ALIAS(accel0));
static const struct device *const dps368 = DEVICE_DT_GET(DT_ALIAS(pressure_sensor));

static void print_info(void)
{
	printk("OK BOARD=kit_pse84_ai APP=i2c_sensor_self_test VERSION=%s\n", APP_VERSION);
}

static bool scan_sensor(const char *name, uint8_t address, const struct device *device)
{
	if (!device_is_ready(device)) {
		printk("ERR I2C ADDRESS=0x%02x DEVICE=%s NOT_READY\n", address, name);
		return false;
	}

	printk("OK I2C ADDRESS=0x%02x DEVICE=%s READY\n", address, name);
	return true;
}

static void sensor_scan(void)
{
	bool pass = true;

	pass &= scan_sensor("SHT40", 0x44, sht40);
	pass &= scan_sensor("BMI270", 0x68, bmi270);
	pass &= scan_sensor("DPS368", 0x77, dps368);
	printk("%s SENSOR_SCAN COUNT=3\n", pass ? "OK" : "ERR");
}

static bool fetch_sensor(const char *name, const struct device *device)
{
	int ret = sensor_sample_fetch(device);

	if (ret < 0) {
		printk("ERR SENSOR=%s FETCH=%d\n", name, ret);
		return false;
	}

	printk("OK SENSOR=%s FETCH\n", name);
	return true;
}

static void sensor_status(void)
{
	bool pass = true;

	if (!device_is_ready(sht40) || !device_is_ready(bmi270) || !device_is_ready(dps368)) {
		printk("ERR SENSOR_STATUS=NOT_READY\n");
		return;
	}

	pass &= fetch_sensor("SHT40", sht40);
	pass &= fetch_sensor("BMI270", bmi270);
	pass &= fetch_sensor("DPS368", dps368);
	printk("%s SENSOR_STATUS=%s\n", pass ? "OK" : "ERR", pass ? "PASS" : "FAIL");
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		print_info();
	} else if (strcmp(command, "sensor scan") == 0) {
		sensor_scan();
	} else if (strcmp(command, "sensor status") == 0) {
		sensor_status();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;sensor scan;sensor status;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	console_init();
	print_info();
	printk("INFO COMMANDS=info;sensor scan;sensor status;help\n");

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
