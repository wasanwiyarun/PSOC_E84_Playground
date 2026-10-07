#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define APP_VERSION "0.1.0"
#define COMMAND_MAX_LENGTH 32

static bool advertising;

static const struct bt_data advertisement[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int start_advertising(void)
{
	int ret;

	if (advertising) {
		printk("OK BT ADVERTISING=ON NAME=%s\n", CONFIG_BT_DEVICE_NAME);
		return 0;
	}

	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertisement,
			      ARRAY_SIZE(advertisement), NULL, 0);
	if (ret) {
		printk("ERR BT ADVERTISE_START_RET=%d\n", ret);
		return ret;
	}
	advertising = true;
	printk("OK BT ADVERTISING=ON NAME=%s\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static void stop_advertising(void)
{
	int ret;

	if (!advertising) {
		printk("OK BT ADVERTISING=OFF NAME=%s\n", CONFIG_BT_DEVICE_NAME);
		return;
	}
	ret = bt_le_adv_stop();
	if (ret) {
		printk("ERR BT ADVERTISE_STOP_RET=%d\n", ret);
		return;
	}
	advertising = false;
	printk("OK BT ADVERTISING=OFF NAME=%s\n", CONFIG_BT_DEVICE_NAME);
}

static void print_identity(void)
{
	bt_addr_le_t address;
	size_t count = 1;

	bt_id_get(&address, &count);
	if (count == 1) {
		printk("OK BT IDENTITY=%s\n", bt_addr_le_str(&address));
	} else {
		printk("ERR BT IDENTITY\n");
	}
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=bluetooth_le_advertiser VERSION=%s\n",
		       APP_VERSION);
		print_identity();
	} else if (strcmp(command, "bt status") == 0) {
		printk("OK BT ADVERTISING=%s NAME=%s\n", advertising ? "ON" : "OFF",
		       CONFIG_BT_DEVICE_NAME);
	} else if (strcmp(command, "bt advertise start") == 0) {
		(void)start_advertising();
	} else if (strcmp(command, "bt advertise stop") == 0) {
		stop_advertising();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;bt status;bt advertise start;bt advertise stop;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;
	int ret;

	console_init();
	printk("OK BOARD=kit_pse84_ai APP=bluetooth_le_advertiser VERSION=%s\n", APP_VERSION);
	ret = bt_enable(NULL);
	if (ret) {
		printk("ERR BT ENABLE_RET=%d\n", ret);
	} else {
		printk("OK BT ENABLE_RET=0\n");
		print_identity();
		(void)start_advertising();
	}
	printk("INFO COMMANDS=info;bt status;bt advertise start;bt advertise stop;help\n");

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
