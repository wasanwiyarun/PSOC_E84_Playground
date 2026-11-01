#include <stdint.h>
#include <string.h>

#include <psa/client.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define COMMAND_MAX_LENGTH 32U
#define RELAY_REQUEST_COUNT 100U

enum relay_state {
	RELAY_IDLE,
	RELAY_RUNNING,
	RELAY_PASSED,
	RELAY_FAILED,
};

static enum relay_state state = RELAY_IDLE;
static uint32_t completed_requests;
static uint32_t relay_version;
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static const char *state_name(void)
{
	switch (state) {
	case RELAY_IDLE:
		return "IDLE";
	case RELAY_RUNNING:
		return "RUNNING";
	case RELAY_PASSED:
		return "PASSED";
	case RELAY_FAILED:
		return "FAILED";
	default:
		return "UNKNOWN";
	}
}

static void print_status(void)
{
	printk("OK RELAY STATUS=%s COMPLETED=%u PSA_VERSION=0x%08x\n",
	       state_name(), completed_requests, relay_version);
}

static void run_relay_test(void)
{
	uint32_t expected_version = 0U;

	if (state == RELAY_RUNNING) {
		printk("ERR RELAY BUSY\n");
		return;
	}

	state = RELAY_RUNNING;
	completed_requests = 0U;
	relay_version = 0U;
	printk("OK RELAY START REQUESTS=%u\n", RELAY_REQUEST_COUNT);

	for (uint32_t sequence = 1U; sequence <= RELAY_REQUEST_COUNT; sequence++) {
		uint32_t version = psa_framework_version();

		if (version == 0U) {
			printk("ERR RELAY SEQ=%u PSA_VERSION=0\n", sequence);
			state = RELAY_FAILED;
			return;
		}
		if (expected_version == 0U) {
			expected_version = version;
		} else if (version != expected_version) {
			printk("ERR RELAY SEQ=%u VERSION_CHANGED=0x%08x\n", sequence,
			       version);
			state = RELAY_FAILED;
			return;
		}

		completed_requests = sequence;
		relay_version = version;
		printk("OK RELAY SEQ=%u PSA_VERSION=0x%08x\n", sequence, version);
	}

	state = RELAY_PASSED;
	printk("PASS RELAY COUNT=%u PSA_VERSION=0x%08x\n", RELAY_REQUEST_COUNT,
	       expected_version);
}

static void handle_command(char *command)
{
	if (strcmp(command, "ipc start") == 0) {
		run_relay_test();
	} else if (strcmp(command, "ipc status") == 0) {
		print_status();
	} else if (strcmp(command, "help") == 0) {
		printk("INFO COMMANDS=ipc start;ipc status;help\n");
	} else {
		printk("ERR UNSUPPORTED %s\n", command);
	}
}

int main(void)
{
	char command[COMMAND_MAX_LENGTH];
	size_t length = 0U;

	if (!device_is_ready(console_uart)) {
		printk("ERR CONSOLE_UART_NOT_READY\n");
		return 1;
	}

	printk("OK CORE=CM55 ROLE=RELAY_CLIENT REQUESTS=%u\n", RELAY_REQUEST_COUNT);
	print_status();
	printk("INFO COMMANDS=ipc start;ipc status;help\n");

	for (;;) {
		uint8_t character;

		if (uart_poll_in(console_uart, &character) != 0) {
			k_sleep(K_MSEC(10));
			continue;
		}

		if (character == '\r' || character == '\n') {
			if (length > 0U) {
				command[length] = '\0';
				handle_command(command);
				length = 0U;
			}
			continue;
		}

		if (character == '\b' || character == 0x7fU) {
			if (length > 0U) {
				length--;
			}
			continue;
		}

		if (length < sizeof(command) - 1U) {
			command[length++] = (char)character;
		} else {
			length = 0U;
			printk("ERR COMMAND_TOO_LONG\n");
		}
	}
}
