#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define APP_VERSION "0.1.0"
#define BMI270_ADDRESS 0x68
#define BMI270_CHIP_ID_REG 0x00
#define BMI270_ERROR_REG 0x02
#define BMI270_STATUS_REG 0x03
#define BMI270_DATA_REG 0x0C
#define BMI270_INTERNAL_STATUS_REG 0x21
#define BMI270_ACC_CONF_REG 0x40
#define BMI270_GYR_CONF_REG 0x42
#define BMI270_PWR_CONF_REG 0x7C
#define BMI270_PWR_CTRL_REG 0x7D
#define COMMAND_MAX_LENGTH 32

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static bool read_register(uint8_t reg, uint8_t *value)
{
	return i2c_reg_read_byte(i2c, BMI270_ADDRESS, reg, value) == 0;
}

static void print_register(const char *name, uint8_t reg)
{
	uint8_t value;

	if (read_register(reg, &value)) {
		printk("OK BMI270 %s=0x%02x\n", name, value);
	} else {
		printk("ERR BMI270 %s\n", name);
	}
}

static void print_raw_data(void)
{
	uint8_t data[12];

	if (i2c_burst_read(i2c, BMI270_ADDRESS, BMI270_DATA_REG, data, sizeof(data)) < 0) {
		printk("ERR BMI270 DATA\n");
		return;
	}
	printk("OK BMI270 RAW ACC=%d,%d,%d GYR=%d,%d,%d\n",
	       (int16_t)sys_get_le16(&data[0]), (int16_t)sys_get_le16(&data[2]),
	       (int16_t)sys_get_le16(&data[4]), (int16_t)sys_get_le16(&data[6]),
	       (int16_t)sys_get_le16(&data[8]), (int16_t)sys_get_le16(&data[10]));
}

static void bmi_status(void)
{
	uint8_t chip_id;

	if (!device_is_ready(i2c) || !read_register(BMI270_CHIP_ID_REG, &chip_id)) {
		printk("ERR BMI270 I2C_ADDRESS=0x68\n");
		return;
	}
	printk("OK BMI270 CHIP_ID=0x%02x EXPECTED=0x24\n", chip_id);
	print_register("ERR_REG", BMI270_ERROR_REG);
	print_register("STATUS", BMI270_STATUS_REG);
	print_register("INTERNAL_STATUS", BMI270_INTERNAL_STATUS_REG);
	print_register("ACC_CONF", BMI270_ACC_CONF_REG);
	print_register("GYR_CONF", BMI270_GYR_CONF_REG);
	print_register("PWR_CONF", BMI270_PWR_CONF_REG);
	print_register("PWR_CTRL", BMI270_PWR_CTRL_REG);
	print_raw_data();
}

static void bmi_enable(void)
{
	/* Normal-performance 100 Hz ODR and both sensor data paths enabled. */
	if (i2c_reg_write_byte(i2c, BMI270_ADDRESS, BMI270_PWR_CONF_REG, 0x00) < 0 ||
	    i2c_reg_write_byte(i2c, BMI270_ADDRESS, BMI270_ACC_CONF_REG, 0xa8) < 0 ||
	    i2c_reg_write_byte(i2c, BMI270_ADDRESS, BMI270_GYR_CONF_REG, 0xa8) < 0 ||
	    i2c_reg_write_byte(i2c, BMI270_ADDRESS, BMI270_PWR_CTRL_REG, 0x0e) < 0) {
		printk("ERR BMI270 ENABLE\n");
		return;
	}
	k_sleep(K_MSEC(100));
	printk("OK BMI270 ENABLED ACC_GYR_100HZ\n");
	bmi_status();
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=bmi270_raw_register_diagnostic VERSION=%s\n",
		       APP_VERSION);
	} else if (strcmp(command, "bmi status") == 0) {
		bmi_status();
	} else if (strcmp(command, "bmi enable") == 0) {
		bmi_enable();
	} else if (strcmp(command, "bmi read") == 0) {
		print_raw_data();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;bmi status;bmi enable;bmi read;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	console_init();
	printk("OK BOARD=kit_pse84_ai APP=bmi270_raw_register_diagnostic VERSION=%s\n",
	       APP_VERSION);
	printk("INFO COMMANDS=info;bmi status;bmi enable;bmi read;help\n");
	bmi_status();
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
