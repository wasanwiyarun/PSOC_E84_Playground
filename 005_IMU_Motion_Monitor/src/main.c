#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define APP_VERSION "0.2.0"
#define BMI270_ADDRESS 0x68
#define BMI270_CHIP_ID_REG 0x00
#define BMI270_DATA_REG 0x0C
#define BMI270_PWR_CONF_REG 0x7C
#define BMI270_PWR_CTRL_REG 0x7D
#define COMMAND_MAX_LENGTH 32

static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(accel0));
static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

static int64_t value_to_micro(const struct sensor_value *value)
{
	return ((int64_t)value->val1 * 1000000LL) + value->val2;
}

static void print_raw_status(void)
{
	uint8_t chip_id;
	uint8_t pwr_conf;
	uint8_t pwr_ctrl;
	uint8_t data[12];

	if (!device_is_ready(i2c) ||
	    i2c_reg_read_byte(i2c, BMI270_ADDRESS, BMI270_CHIP_ID_REG, &chip_id) < 0 ||
	    i2c_reg_read_byte(i2c, BMI270_ADDRESS, BMI270_PWR_CONF_REG, &pwr_conf) < 0 ||
	    i2c_reg_read_byte(i2c, BMI270_ADDRESS, BMI270_PWR_CTRL_REG, &pwr_ctrl) < 0 ||
	    i2c_burst_read(i2c, BMI270_ADDRESS, BMI270_DATA_REG, data, sizeof(data)) < 0) {
		printk("ERR BMI270 RAW_I2C\n");
		return;
	}

	printk("OK BMI270 CHIP_ID=0x%02x PWR_CONF=0x%02x PWR_CTRL=0x%02x\n",
	       chip_id, pwr_conf, pwr_ctrl);
	printk("OK BMI270 RAW ACC=%d,%d,%d GYR=%d,%d,%d\n",
	       (int16_t)sys_get_le16(&data[0]), (int16_t)sys_get_le16(&data[2]),
	       (int16_t)sys_get_le16(&data[4]), (int16_t)sys_get_le16(&data[6]),
	       (int16_t)sys_get_le16(&data[8]), (int16_t)sys_get_le16(&data[10]));
}

static bool configure_imu(void)
{
	const struct sensor_value frequency = { .val1 = 100, .val2 = 0 };
	int accel_ret;
	int gyro_ret;

	accel_ret = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SAMPLING_FREQUENCY, &frequency);
	gyro_ret = sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ,
				SENSOR_ATTR_SAMPLING_FREQUENCY, &frequency);
	printk("OK IMU CONFIGURE ACCEL_RET=%d GYRO_RET=%d ODR_HZ=100\n",
	       accel_ret, gyro_ret);
	if (accel_ret || gyro_ret) {
		return false;
	}

	k_sleep(K_MSEC(200));
	return true;
}

static void read_imu(void)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	int ret;

	ret = sensor_sample_fetch(imu);
	if (ret) {
		printk("ERR IMU FETCH_RET=%d\n", ret);
		return;
	}
	ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (ret) {
		printk("ERR IMU ACCEL_GET_RET=%d\n", ret);
		return;
	}
	ret = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (ret) {
		printk("ERR IMU GYRO_GET_RET=%d\n", ret);
		return;
	}

	printk("OK ACCEL_MS2_U=%lld,%lld,%lld\n", (long long)value_to_micro(&accel[0]),
	       (long long)value_to_micro(&accel[1]), (long long)value_to_micro(&accel[2]));
	printk("OK GYRO_RADPS_U=%lld,%lld,%lld\n", (long long)value_to_micro(&gyro[0]),
	       (long long)value_to_micro(&gyro[1]), (long long)value_to_micro(&gyro[2]));
	printk("OK IMU_READ=PASS\n");
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=imu_motion_monitor VERSION=%s\n", APP_VERSION);
	} else if (strcmp(command, "imu configure") == 0) {
		(void)configure_imu();
	} else if (strcmp(command, "imu read") == 0) {
		read_imu();
	} else if (strcmp(command, "bmi raw") == 0) {
		print_raw_status();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;imu configure;imu read;bmi raw;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	console_init();
	printk("OK BOARD=kit_pse84_ai APP=imu_motion_monitor VERSION=%s\n", APP_VERSION);
	printk("OK IMU DEVICE_READY=%d\n", device_is_ready(imu));
	if (device_is_ready(imu)) {
		(void)configure_imu();
	}
	printk("INFO COMMANDS=info;imu configure;imu read;bmi raw;help\n");

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
