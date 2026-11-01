/* All storage/PSA functions belong to the single storage worker. */
#ifndef LOGGER_STORE_H
#define LOGGER_STORE_H
#include <stdint.h>
#include <stdbool.h>
#define SAMPLE_BYTES 40
#define BATCH_SAMPLES 16
#define LOG_NAME_MAX 24
struct store_status {
    bool mounted, key_ready, opened;
    uint32_t samples, batches, bytes, free_bytes;
    uint64_t nonce, session;
    char name[LOG_NAME_MAX + 1];
};
int store_init(void);
int store_init_blank(void);
int store_start(const char *name);
int store_append(const uint8_t *samples, unsigned count);
int store_stop(void);
void store_abort(void);
void store_snapshot(struct store_status *out);
bool store_name_valid(const char *name);
int store_read(const char *name, bool emit);
int store_delete(const char *name);
int store_list(void);
int store_raw(const char *name, unsigned index);
int store_tamper(const char *name, unsigned index, const char *part);
int store_kat(void);
#endif
