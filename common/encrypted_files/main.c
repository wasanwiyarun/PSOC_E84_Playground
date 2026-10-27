/* Educational authenticated file storage. Never export the persistent key.
 * The UART is a trusted development console, not an authenticated interface. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <psa/crypto.h>
#include <psa/protected_storage.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#define MAX_DATA 1024
#define NAME_SIZE 24
#define HEADER_SIZE 32
#define TAG_SIZE 16
#define RECORD_SIZE (HEADER_SIZE + MAX_DATA + TAG_SIZE)
#define KEY_ID (0x2300 + EF_ALGORITHM)
#define COUNTER_UID (0x45465300ULL + EF_ALGORITHM)
#if EF_ALGORITHM == 3
#define ALGORITHM PSA_ALG_CHACHA20_POLY1305
#define KEY_TYPE PSA_KEY_TYPE_CHACHA20
#else
#define ALGORITHM PSA_ALG_GCM
#define KEY_TYPE PSA_KEY_TYPE_AES
#endif

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs);
static struct fs_mount_t mountp = {
	.type = FS_LITTLEFS, .fs_data = &lfs,
	.storage_dev = (void *)PARTITION_ID(encrypted_partition),
	.mnt_point = "/efs", .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct flash_area *area;
static bool mounted, key_ready;
static uint64_t counter;
static uint8_t record[RECORD_SIZE], plain[MAX_DATA], aad[HEADER_SIZE + NAME_SIZE];
static char line[2 * MAX_DATA + 100];

static void wipe(void *p, size_t n)
{
	volatile uint8_t *v = p;
	while (n--) *v++ = 0;
}

static void hex_out(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; ++i) printk("%02x", data[i]);
}

static int nibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int unhex(const char *s, uint8_t *out)
{
	if (!strcmp(s, "-")) return 0;
	size_t n = strlen(s);
	if (n % 2 || n > MAX_DATA * 2 || !n) return -EINVAL;
	for (size_t i = 0; i < n / 2; ++i) {
		int a = nibble(s[2 * i]), b = nibble(s[2 * i + 1]);
		if (a < 0 || b < 0) return -EINVAL;
		out[i] = (uint8_t)((a << 4) | b);
	}
	return n / 2;
}

static bool valid_name(const char *s)
{
	if (!s || !*s || strlen(s) > NAME_SIZE) return false;
	for (; *s; ++s) {
		if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
		      (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
	}
	return true;
}

static void path_for(char *out, const char *name)
{
	snprintk(out, 40, "/efs/%s", name);
}

static void attributes(psa_key_attributes_t *attr, bool persistent)
{
	psa_set_key_type(attr, KEY_TYPE);
	psa_set_key_bits(attr, EF_KEY_BITS);
	psa_set_key_algorithm(attr, ALGORITHM);
	psa_set_key_usage_flags(attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	if (persistent) {
		psa_set_key_lifetime(attr, PSA_KEY_LIFETIME_PERSISTENT);
		psa_set_key_id(attr, KEY_ID);
	}
}

/* Missing/corrupt counter with an existing key is fatal: never reset a nonce
 * sequence under an old key. Provision counter before generating a new key. */
static int key_init(void)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t ks = psa_get_key_attributes(KEY_ID, &attr);
	uint8_t state[8];
	size_t n = 0;
	psa_status_t cs = psa_ps_get(COUNTER_UID, 0, sizeof(state), state, &n);
	printk("DETAIL KEY_LOOKUP=%d COUNTER_LOOKUP=%d size=%zu\n", ks, cs, n);
	if (ks == PSA_SUCCESS) {
		bool valid = psa_get_key_type(&attr) == KEY_TYPE &&
			psa_get_key_bits(&attr) == EF_KEY_BITS &&
			psa_get_key_algorithm(&attr) == ALGORITHM &&
			psa_get_key_usage_flags(&attr) == (PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
		psa_reset_key_attributes(&attr);
		if (!valid || cs != PSA_SUCCESS || n != sizeof(state)) return -EKEYREJECTED;
		counter = sys_get_le64(state);
		return 0;
	}
	psa_reset_key_attributes(&attr);
	if ((ks != PSA_ERROR_DOES_NOT_EXIST && ks != PSA_ERROR_INVALID_HANDLE) ||
	    cs != PSA_ERROR_DOES_NOT_EXIST) return -EKEYREJECTED;
	memset(state, 0, sizeof(state));
	cs = psa_ps_set(COUNTER_UID, sizeof(state), state, PSA_STORAGE_FLAG_NONE);
	if (cs != PSA_SUCCESS) { printk("DETAIL COUNTER_CREATE=%d\n", cs); return -EIO; }
	attributes(&attr, true);
	psa_key_id_t id;
	ks = psa_generate_key(&attr, &id);
	psa_reset_key_attributes(&attr);
	if (ks != PSA_SUCCESS) { printk("DETAIL KEY_CREATE=%d\n", ks); return -EIO; }
	counter = 0;
	return 0;
}

static int next_nonce(uint8_t nonce[12])
{
	if (!key_ready || counter == UINT64_MAX) return -EOVERFLOW;
	uint8_t state[8];
	uint64_t next = counter + 1;
	sys_put_le64(next, state);
	if (psa_ps_set(COUNTER_UID, sizeof(state), state, PSA_STORAGE_FLAG_NONE) != PSA_SUCCESS) {
		/* Persistence outcome can be uncertain. Refuse further writes until reboot. */
		key_ready = false;
		return -EIO;
	}
	counter = next;
	sys_put_le32(KEY_ID, nonce);
	sys_put_le64(counter, nonce + 4);
	return 0;
}

static void make_aad(const char *name)
{
	memcpy(aad, record, HEADER_SIZE);
	memset(aad + HEADER_SIZE, 0, NAME_SIZE);
	memcpy(aad + HEADER_SIZE, name, strlen(name));
}

static int load(const char *name)
{
	char path[40];
	struct fs_dirent entry;
	path_for(path, name);
	int rc = fs_stat(path, &entry);
	if (rc) return rc;
	if (entry.type != FS_DIR_ENTRY_FILE || entry.size < HEADER_SIZE + TAG_SIZE ||
	    entry.size > RECORD_SIZE) return -EBADMSG;
	struct fs_file_t f;
	fs_file_t_init(&f);
	rc = fs_open(&f, path, FS_O_READ);
	if (rc) return rc;
	int n = fs_read(&f, record, entry.size);
	int close_rc = fs_close(&f);
	if (n != entry.size || close_rc) return -EIO;
	return n;
}

static int decrypt(const char *name, int size, psa_key_id_t key)
{
	uint32_t length = sys_get_le32(record + 8);
	if (memcmp(record, "EFS1", 4) || record[4] != 1 || record[5] != EF_ALGORITHM ||
	    sys_get_le16(record + 6) != EF_KEY_BITS || length > MAX_DATA ||
	    size != HEADER_SIZE + length + TAG_SIZE || sys_get_le32(record + 24) != KEY_ID ||
	    sys_get_le32(record + 28) != 0) return -EBADMSG;
	make_aad(name);
	size_t out_len = 0;
	psa_status_t status = psa_aead_decrypt(key, ALGORITHM, record + 12, 12,
		aad, sizeof(aad), record + HEADER_SIZE, length + TAG_SIZE,
		plain, sizeof(plain), &out_len);
	if (status != PSA_SUCCESS || out_len != length) {
		wipe(plain, sizeof(plain));
		return -EBADMSG;
	}
	return (int)out_len;
}

/* mode=1: reboot with a partial pending record; mode=2: reboot after sync but
 * before rename. These deterministic fault points are not physical power cuts. */
static int save(const char *name, int size, int mode)
{
	struct fs_file_t f;
	fs_file_t_init(&f);
	int rc = fs_open(&f, "/efs/.pending", FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc) return rc;
	int amount = mode == 1 ? size / 2 : size;
	int n = fs_write(&f, record, amount);
	if (n != amount) rc = n < 0 ? n : -EIO;
	if (!rc) rc = fs_sync(&f);
	int close_rc = fs_close(&f);
	if (!rc) rc = close_rc;
	if (rc) return rc;
	if (mode) {
		printk("OK INTERRUPT stage=%d\n", mode);
		k_msleep(100);
		sys_reboot(SYS_REBOOT_COLD);
		return -EIO;
	}
	char path[40];
	path_for(path, name);
	/* LittleFS atomically replaces the destination: never unlink it first. */
	return fs_rename("/efs/.pending", path);
}

static int write_file(const char *name, int len, int mode)
{
	memset(record, 0, HEADER_SIZE);
	memcpy(record, "EFS1", 4);
	record[4] = 1;
	record[5] = EF_ALGORITHM;
	sys_put_le16(EF_KEY_BITS, record + 6);
	sys_put_le32(len, record + 8);
	sys_put_le32(KEY_ID, record + 24);
	int rc = next_nonce(record + 12);
	if (rc) return rc;
	make_aad(name);
	size_t out_len;
	int64_t start = k_uptime_get();
	psa_status_t status = psa_aead_encrypt(KEY_ID, ALGORITHM, record + 12, 12,
		aad, sizeof(aad), plain, len, record + HEADER_SIZE,
		sizeof(record) - HEADER_SIZE, &out_len);
	int64_t crypto_ms = k_uptime_get() - start;
	wipe(plain, sizeof(plain));
	if (status != PSA_SUCCESS) { printk("DETAIL ENCRYPT=%d\n", status); return -EIO; }
	if (out_len != len + TAG_SIZE) return -EIO;
	rc = save(name, HEADER_SIZE + out_len, mode);
	if (!rc) printk("OK WRITE bytes=%d crypto_ms=%lld nonce=%llu\n", len, crypto_ms, counter);
	return rc;
}

static int mount_existing(void)
{
	if (mounted) return 0;
	int rc = fs_mount(&mountp);
	if (!rc) {
		mounted = true;
		/* Uncommitted ciphertext is never promoted after reboot. */
		rc = fs_unlink("/efs/.pending");
		if (rc == -ENOENT) rc = 0;
	}
	return rc;
}

static int init_blank(void)
{
	if (mounted) return 0;
	/* Explicit command, full partition check: refuse to format nonblank data. */
	uint8_t buf[256];
	for (off_t offset = 0; offset < area->fa_size; offset += sizeof(buf)) {
		int rc = flash_area_read(area, offset, buf, sizeof(buf));
		if (rc) return rc;
		for (size_t i = 0; i < sizeof(buf); ++i) if (buf[i] != 0xff) return -EEXIST;
	}
	int rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)PARTITION_ID(encrypted_partition), &lfs, 0);
	if (rc) return rc;
	return mount_existing();
}

/* Fixed TEST KEY only, volatile and separate from all storage keys. Host checks
 * this result against Python cryptography, independently of firmware decrypt. */
static int kat(void)
{
	uint8_t key_bytes[32], nonce[12], input[32], output[48], recovered[32];
	for (int i = 0; i < 32; ++i) { key_bytes[i] = i; input[i] = 0x80 + i; }
	for (int i = 0; i < 12; ++i) nonce[i] = i;
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	attributes(&attr, false);
	psa_key_id_t id = 0;
	psa_status_t status = psa_import_key(&attr, key_bytes, EF_KEY_BITS / 8, &id);
	printk("DETAIL KAT_IMPORT=%d\n", status);
	psa_reset_key_attributes(&attr);
	size_t n = 0, p = 0;
	if (!status) status = psa_aead_encrypt(id, ALGORITHM, nonce, sizeof(nonce),
		(const uint8_t *)"EFS-KAT-v1", 10, input, sizeof(input), output, sizeof(output), &n);
	printk("DETAIL KAT_ENCRYPT=%d size=%zu\n", status, n);
	if (!status) status = psa_aead_decrypt(id, ALGORITHM, nonce, sizeof(nonce),
		(const uint8_t *)"EFS-KAT-v1", 10, output, n, recovered, sizeof(recovered), &p);
	printk("DETAIL KAT_DECRYPT=%d size=%zu\n", status, p);
	if (!status && (p != sizeof(input) || memcmp(input, recovered, p))) status = PSA_ERROR_GENERIC_ERROR;
	if (!status) {
		output[n - 1] ^= 1;
		psa_status_t bad = psa_aead_decrypt(id, ALGORITHM, nonce, sizeof(nonce),
			(const uint8_t *)"EFS-KAT-v1", 10, output, n, recovered, sizeof(recovered), &p);
		output[n - 1] ^= 1;
		if (bad != PSA_ERROR_INVALID_SIGNATURE) status = PSA_ERROR_GENERIC_ERROR;
	}
	if (id) (void)psa_destroy_key(id);
	wipe(key_bytes, sizeof(key_bytes));
	if (status) { printk("DETAIL KAT=%d\n", status); return -EIO; }
	printk("OK KAT hex="); hex_out(output, n); printk(" tamper_rejected=1\n");
	return 0;
}

static int wrong_key(const char *name)
{
	int n = load(name);
	if (n < 0) return n;
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	attributes(&attr, false);
	psa_key_id_t id;
	psa_status_t status = psa_generate_key(&attr, &id);
	psa_reset_key_attributes(&attr);
	if (status) return -EIO;
	int rc = decrypt(name, n, id);
	(void)psa_destroy_key(id);
	wipe(plain, sizeof(plain));
	if (rc != -EBADMSG) return -EIO;
	printk("OK WRONGKEY rejected=1\n");
	return 0;
}

static int execute(char *text)
{
	char *args[5], *state;
	int argc = 0;
	for (char *s = strtok_r(text, " ", &state); s; s = strtok_r(NULL, " ", &state)) {
		if (argc == 5) return -EINVAL;
		args[argc++] = s;
	}
	if (argc < 2 || strcmp(args[0], "efs")) return -EINVAL;
	const char *cmd = args[1];
	if (!strcmp(cmd, "blanks") && argc == 2) {
		/* Read-only allocation aid. Never scan firmware/security below 8 MiB. */
		static uint8_t probe[4096];
		unsigned found = 0;
		for (off_t base = 0x800000; base < 0x4000000 && found < 3; base += 0x100000) {
			bool blank = true;
			for (off_t off = base; off < base + 0x100000 && blank; off += sizeof(probe)) {
				int rc = flash_read(area->fa_dev, off, probe, sizeof(probe));
				if (rc) return rc;
				for (size_t i = 0; i < sizeof(probe); ++i) if (probe[i] != 0xff) { blank = false; break; }
			}
			printk("AREA offset=0x%lx blank=%d\n", (unsigned long)base, blank);
			if (blank) ++found;
		}
		printk("OK BLANKS found=%u\n", found); return 0;
	}
	if (!strcmp(cmd, "info") && argc == 2) {
		printk("OK INFO alg=%s bits=%d mounted=%d key=%d offset=0x%lx size=%lu max=%d counter=%llu\n",
			EF_NAME, EF_KEY_BITS, mounted, key_ready, (unsigned long)area->fa_off,
			(unsigned long)area->fa_size, MAX_DATA, counter);
		return 0;
	}
	if (!strcmp(cmd, "reboot") && argc == 2) {
		printk("OK REBOOT\n"); k_msleep(100); sys_reboot(SYS_REBOOT_COLD); return 0;
	}
	if (!key_ready) return -EKEYREJECTED;
	if (!strcmp(cmd, "kat") && argc == 2) return kat();
	if (!strcmp(cmd, "init") && argc == 2) {
		int rc = init_blank(); if (!rc) printk("OK INIT mounted=1\n"); return rc;
	}
	if (!mounted) return -ENODEV;
	if (!strcmp(cmd, "list") && argc == 2) {
		struct fs_dir_t dir;
		struct fs_dirent ent;
		fs_dir_t_init(&dir);
		int rc = fs_opendir(&dir, "/efs");
		if (rc) return rc;
		while (!(rc = fs_readdir(&dir, &ent)) && ent.name[0])
			printk("FILE %s bytes=%zu\n", ent.name, ent.size);
		int close_rc = fs_closedir(&dir);
		if (!rc) rc = close_rc;
		if (!rc) printk("OK LIST\n");
		return rc;
	}
	if (argc < 3 || !valid_name(args[2])) return -EINVAL;
	const char *name = args[2];
	if ((!strcmp(cmd, "write") && argc == 4) || (!strcmp(cmd, "interrupt") && argc == 5)) {
		int mode = 0;
		if (!strcmp(cmd, "interrupt")) {
			if (!strcmp(args[4], "partial")) mode = 1;
			else if (!strcmp(args[4], "synced")) mode = 2;
			else return -EINVAL;
		}
		int len = unhex(args[3], plain);
		if (len < 0) return len;
		return write_file(name, len, mode);
	}
	if (!strcmp(cmd, "wrongkey") && argc == 3) return wrong_key(name);
	if ((!strcmp(cmd, "read") || !strcmp(cmd, "raw")) && argc == 3) {
		int n = load(name);
		if (n < 0) return n;
		if (!strcmp(cmd, "raw")) { printk("OK RAW hex="); hex_out(record, n); printk("\n"); return 0; }
		n = decrypt(name, n, KEY_ID);
		if (n < 0) return n;
		printk("OK READ hex="); hex_out(plain, n); printk("\n"); return 0;
	}
	if (!strcmp(cmd, "delete") && argc == 3) {
		char path[40]; path_for(path, name);
		int rc = fs_unlink(path); if (!rc) printk("OK DELETE\n"); return rc;
	}
	/* Fault injection only affects the explicitly named file. A second call
 * restores the byte. No raw flash write/erase command is exposed. */
	if (!strcmp(cmd, "tamper") && argc == 4) {
		int n = load(name);
		if (n < 0) return n;
		int offset;
		if (!strcmp(args[3], "tag")) offset = n - 1;
		else if (!strcmp(args[3], "nonce")) offset = 12;
		else if (!strcmp(args[3], "header")) offset = 4;
		else if (!strcmp(args[3], "data") && n > HEADER_SIZE + TAG_SIZE) offset = HEADER_SIZE;
		else return -EINVAL;
		record[offset] ^= 1;
		int rc = save(name, n, 0); if (!rc) printk("OK TAMPER\n"); return rc;
	}
	return -EINVAL;
}

int main(void)
{
	if (!device_is_ready(uart)) return 0;
	int rc = flash_area_open(PARTITION_ID(encrypted_partition), &area);
	if (rc) { printk("FAIL FLASH rc=%d\n", rc); return 0; }
	struct flash_pages_info geometry;
	uint64_t capacity;
	if (flash_get_size(area->fa_dev, &capacity) ||
	    flash_get_page_info_by_offs(area->fa_dev, area->fa_off, &geometry) ||
	    geometry.size != 0x40000 || area->fa_off < 0x800000 ||
	    area->fa_off + area->fa_size > capacity || area->fa_size != 0x100000) {
		printk("FAIL FLASH_GEOMETRY\n"); return 0;
	}
	psa_status_t status = psa_crypto_init();
	rc = status == PSA_SUCCESS ? key_init() : -EIO;
	key_ready = rc == 0;
	printk("BOOT EFS alg=%s crypto=%d key_rc=%d\n", EF_NAME, status, rc);
	/* No implicit formatting, even when the mount fails. */
	rc = mount_existing();
	printk("READY EFS alg=%s mounted=%d mount_rc=%d key=%d\n", EF_NAME, mounted, rc, key_ready);
	size_t pos = 0;
	bool invalid = false;
	for (;;) {
		uint8_t c;
		if (uart_poll_in(uart, &c)) { k_msleep(1); continue; }
		if (c == '\n' || c == '\r') {
			if (invalid) printk("ERR rc=%d\n", -EMSGSIZE);
			else if (pos) {
				line[pos] = 0;
				rc = execute(line);
				if (rc) printk("ERR rc=%d\n", rc);
			}
			wipe(plain, sizeof(plain)); wipe(line, sizeof(line));
			pos = 0; invalid = false;
		} else if (c < 32 || c > 126 || pos == sizeof(line) - 1) invalid = true;
		else if (!invalid) line[pos++] = c;
	}
}
