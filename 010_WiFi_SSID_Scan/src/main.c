#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#define APP_VERSION "0.1.0"
#define COMMAND_MAX_LENGTH 32

static struct net_mgmt_event_callback wifi_events;
static atomic_t scan_active = ATOMIC_INIT(0);
static atomic_t scan_count = ATOMIC_INIT(0);

static void wifi_event_handler(struct net_mgmt_event_callback *callback,
			       uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *result = callback->info;

		if (result->ssid_length > 0U) {
			printk("WIFI SSID=%.*s RSSI_DBM=%d CHANNEL=%u\n", result->ssid_length,
			       result->ssid, result->rssi, result->channel);
		} else {
			printk("WIFI SSID=<hidden> RSSI_DBM=%d CHANNEL=%u\n", result->rssi,
			       result->channel);
		}
		atomic_inc(&scan_count);
	} else if (event == NET_EVENT_WIFI_SCAN_DONE) {
		const struct wifi_status *status = callback->info;

		printk("%s WIFI_SCAN_DONE COUNT=%ld STATUS=%d\n", status->status ? "ERR" : "OK",
		       (long)atomic_get(&scan_count), status->status);
		atomic_set(&scan_active, 0);
	}
}

static void start_scan(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_scan_params params = { 0 };
	int ret;

	if (atomic_cas(&scan_active, 0, 1) == false) {
		printk("ERR WIFI_SCAN_ACTIVE\n");
		return;
	}
	if (iface == NULL) {
		atomic_set(&scan_active, 0);
		printk("ERR WIFI_INTERFACE\n");
		return;
	}

	atomic_set(&scan_count, 0);
	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(params));
	if (ret < 0) {
		atomic_set(&scan_active, 0);
		printk("ERR WIFI_SCAN_START=%d\n", ret);
		return;
	}
	printk("OK WIFI_SCAN_START\n");
}

static void handle_command(char *command)
{
	if (strcmp(command, "info") == 0) {
		printk("OK BOARD=kit_pse84_ai APP=wifi_ssid_scan VERSION=%s\n", APP_VERSION);
	} else if (strcmp(command, "wifi scan") == 0) {
		start_scan();
	} else if (strcmp(command, "wifi status") == 0) {
		printk("OK WIFI_SCAN_ACTIVE=%s LAST_COUNT=%ld\n",
		       atomic_get(&scan_active) ? "YES" : "NO", (long)atomic_get(&scan_count));
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=info;wifi scan;wifi status;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0;

	net_mgmt_init_event_callback(&wifi_events, wifi_event_handler,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&wifi_events);
	console_init();
	printk("OK BOARD=kit_pse84_ai APP=wifi_ssid_scan VERSION=%s\n", APP_VERSION);
	printk("INFO COMMANDS=info;wifi scan;wifi status;help\n");
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
