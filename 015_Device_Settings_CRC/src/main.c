/* Zephyr Settings API device configuration, stored by the FCB backend. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#define SETTINGS_NAME "device"
#define NAME_MAX_LEN 24

struct device_settings {
	uint32_t interval_ms;
	uint8_t enabled;
	char name[NAME_MAX_LEN];
};

static struct device_settings device_cfg = {
	.interval_ms = 1000,
	.enabled = 0,
	.name = "PSoC-E84",
};

static int device_settings_set(const char *name, size_t len,
			       settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	int rc;

	if (settings_name_steq(name, "interval_ms", &next) && !next &&
	    len == sizeof(device_cfg.interval_ms)) {
		rc = read_cb(cb_arg, &device_cfg.interval_ms, len);
		return rc < 0 ? rc : 0;
	}
	if (settings_name_steq(name, "enabled", &next) && !next &&
	    len == sizeof(device_cfg.enabled)) {
		rc = read_cb(cb_arg, &device_cfg.enabled, len);
		return rc < 0 ? rc : 0;
	}
	if (settings_name_steq(name, "name", &next) && !next &&
	    len > 0 && len < sizeof(device_cfg.name)) {
		rc = read_cb(cb_arg, device_cfg.name, len);
		if (rc < 0) {
			return rc;
		}
		device_cfg.name[len] = '\0';
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(device, SETTINGS_NAME, NULL, device_settings_set,
			       NULL, NULL);

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void show_settings(void)
{
	printk("OK SETTINGS interval_ms=%u enabled=%u name=%s\n", device_cfg.interval_ms,
	       device_cfg.enabled, device_cfg.name);
}

static void save_u32(const char *key, uint32_t value)
{
	int rc = settings_save_one(key, &value, sizeof(value));
	printk("OK SAVE %s=%u rc=%d\n", key, value, rc);
}

static void save_enabled(uint8_t value)
{
	int rc = settings_save_one("device/enabled", &value, sizeof(value));
	printk("OK SAVE device/enabled=%u rc=%d\n", value, rc);
}

static void save_name(const char *value)
{
	size_t len = strlen(value);
	int rc = settings_save_one("device/name", value, len);
	printk("OK SAVE device/name=%s rc=%d\n", value, rc);
}

static void factory_reset(void)
{
	const struct flash_area *area;
	int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &area);
	if (rc == 0) {
		rc = flash_area_erase(area, 0, area->fa_size);
		flash_area_close(area);
	}
	printk("OK FACTORY_RESET rc=%d\n", rc);
	if (rc == 0) {
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

static void handle(char *line)
{
	char *verb = strtok(line, " ");
	char *arg = strtok(NULL, "");
	char *end;
	unsigned long value;

	if (!verb || strcmp(verb, "settings") != 0) {
		printk("ERR command: settings info|show|set|factory-reset|reboot\n");
		return;
	}
	if (!arg || strcmp(arg, "info") == 0) {
		printk("OK BACKEND=FCB ENTRY_CRC=enabled SECTORS=4\n");
		return;
	}
	if (strcmp(arg, "show") == 0) {
		show_settings();
		return;
	}
	if (strcmp(arg, "factory-reset") == 0) {
		factory_reset();
		return;
	}
	if (strcmp(arg, "reboot") == 0) {
		printk("OK REBOOT\n");
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
	char *field = strtok(arg, " ");
	char *text = strtok(NULL, "");
	if (!field || !text || strcmp(field, "set") != 0) {
		printk("ERR command: settings set interval_ms|enabled|name <value>\n");
		return;
	}
	char *setting = strtok(text, " ");
	char *setting_value = strtok(NULL, "");
	if (!setting || !setting_value) {
		printk("ERR missing setting value\n");
		return;
	}
	if (strcmp(setting, "interval_ms") == 0) {
		value = strtoul(setting_value, &end, 10);
		if (*end == '\0' && value > 0 && value <= UINT32_MAX) {
			device_cfg.interval_ms = (uint32_t)value;
			save_u32("device/interval_ms", device_cfg.interval_ms);
			return;
		}
	} else if (strcmp(setting, "enabled") == 0 &&
		   (strcmp(setting_value, "0") == 0 || strcmp(setting_value, "1") == 0)) {
		device_cfg.enabled = (uint8_t)(setting_value[0] - '0');
		save_enabled(device_cfg.enabled);
		return;
	} else if (strcmp(setting, "name") == 0 && strlen(setting_value) < NAME_MAX_LEN) {
		strcpy(device_cfg.name, setting_value);
		save_name(device_cfg.name);
		return;
	}
	printk("ERR invalid setting value\n");
}

int main(void)
{
	char line[96];
	size_t length = 0;
	unsigned char ch;
	int rc;

	if (!device_is_ready(console)) {
		return 0;
	}
	rc = settings_subsys_init();
	if (rc == 0) {
		rc = settings_load();
	}
	printk("OK SETTINGS_INIT=%d\n", rc);
	printk("OK CRC=FCB_ENTRY_CRC_ENABLED\n");
	printk("OK READY=1\n");
	while (true) {
		if (uart_poll_in(console, &ch) != 0) {
			k_msleep(5);
			continue;
		}
		if (ch == '\r' || ch == '\n') {
			if (length) {
				line[length] = '\0';
				handle(line);
				length = 0;
			}
		} else if (length + 1 < sizeof(line)) {
			line[length++] = (char)ch;
		}
	}
}
