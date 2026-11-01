#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define APP_VERSION "0.2.0"
#define COMMAND_MAX_LENGTH 128
#define NUS_LOOPBACK_MAX_LENGTH 244

static bool advertising;
static bool nus_notifications_enabled;
#if defined(CONFIG_APP_NUS_LOOPBACK)
static uint8_t loopback_data[NUS_LOOPBACK_MAX_LENGTH];
static uint16_t loopback_length;
#endif

static const struct bt_data advertisement[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static const struct bt_data scan_response[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void print_advertising_status(void)
{
	printk("OK BT ADVERTISING=%s NAME=%s SERVICE=NUS\n",
	       advertising ? "ON" : "OFF", CONFIG_BT_DEVICE_NAME);
}

static int start_advertising(void)
{
	int ret;

	if (advertising) {
		print_advertising_status();
		return 0;
	}

	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertisement,
			      ARRAY_SIZE(advertisement), scan_response,
			      ARRAY_SIZE(scan_response));
	if (ret) {
		printk("ERR BT ADVERTISE_START_RET=%d\n", ret);
		return ret;
	}
	advertising = true;
	print_advertising_status();
	return 0;
}

static void stop_advertising(void)
{
	int ret;

	if (!advertising) {
		print_advertising_status();
		return;
	}
	ret = bt_le_adv_stop();
	if (ret) {
		printk("ERR BT ADVERTISE_STOP_RET=%d\n", ret);
		return;
	}
	advertising = false;
	print_advertising_status();
}

static void send_loopback(void)
{
#if defined(CONFIG_APP_NUS_LOOPBACK)
	int ret;

	if (loopback_length == 0U) {
		return;
	}

	ret = bt_nus_send(NULL, loopback_data, loopback_length);
	if (ret) {
		printk("ERR NUS LOOPBACK_TX_RET=%d\n", ret);
	} else {
		printk("OK NUS LOOPBACK_TX LEN=%u\n", loopback_length);
		loopback_length = 0U;
	}
#endif
}

static void nus_received(struct bt_conn *conn, const void *data, uint16_t len,
			 void *ctx)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ctx);
	printk("OK NUS RX LEN=%u DATA=%.*s\n", len, (int)len,
	       (const char *)data);
#if defined(CONFIG_APP_NUS_LOOPBACK)
	if (len > sizeof(loopback_data)) {
		printk("ERR NUS LOOPBACK_TOO_LONG LEN=%u\n", len);
		return;
	}

	memcpy(loopback_data, data, len);
	loopback_length = len;
	if (!nus_notifications_enabled) {
		printk("INFO NUS LOOPBACK_QUEUED NOTIFY=OFF\n");
		return;
	}

	send_loopback();
#endif
}

static void nus_notifications_changed(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);
	nus_notifications_enabled = enabled;
	printk("OK NUS NOTIFY=%s\n", enabled ? "ON" : "OFF");
	if (enabled) {
		send_loopback();
	}
}

static struct bt_nus_cb nus_callbacks = {
	.received = nus_received,
	.notif_enabled = nus_notifications_changed,
};

static void connected(struct bt_conn *conn, uint8_t error)
{
	ARG_UNUSED(conn);
	if (error) {
		printk("ERR BT CONNECT_RET=%u\n", error);
		return;
	}

	advertising = false;
	printk("OK BT CONNECTED\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	nus_notifications_enabled = false;
#if defined(CONFIG_APP_NUS_LOOPBACK)
	loopback_length = 0U;
#endif
	printk("OK BT DISCONNECTED REASON=%u\n", reason);
	(void)start_advertising();
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

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
		printk("OK NUS SERVICE_UUID=6e400001-b5a3-f393-e0a9-e50e24dcca9e\n");
		printk("OK NUS LOOPBACK=%s\n",
		       IS_ENABLED(CONFIG_APP_NUS_LOOPBACK) ? "ON" : "OFF");
	} else if (strcmp(command, "bt status") == 0) {
		print_advertising_status();
	} else if (strcmp(command, "bt advertise start") == 0) {
		(void)start_advertising();
	} else if (strcmp(command, "bt advertise stop") == 0) {
		stop_advertising();
	} else if (strncmp(command, "ble send ", 9) == 0 && command[9] != '\0') {
		int ret = bt_nus_send(NULL, &command[9], strlen(&command[9]));

		if (ret) {
			printk("ERR NUS TX_RET=%d\n", ret);
		} else {
			printk("OK NUS TX LEN=%u\n", (unsigned int)strlen(&command[9]));
		}
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;bt status;bt advertise start;bt advertise stop;ble send <text>;help\n");
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
	ret = bt_nus_cb_register(&nus_callbacks, NULL);
	if (ret) {
		printk("ERR NUS CALLBACK_RET=%d\n", ret);
	}
	ret = bt_enable(NULL);
	if (ret) {
		printk("ERR BT ENABLE_RET=%d\n", ret);
	} else {
		printk("OK BT ENABLE_RET=0\n");
		print_identity();
		(void)start_advertising();
	}
	printk("INFO COMMANDS=info;bt status;bt advertise start;bt advertise stop;ble send <text>;help\n");

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
