/* Serialize each flash mutation with a secure, RAM-resident CM33 pause.
 * The helper blocks in PSA while the storage thread performs the actual write.
 * Callers must not start another PSA/SRF operation until this guard completes.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/sys/printk.h>
#include <tfm_platform_api.h>
#include <mtb_serial_memory.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_CACHE_MANAGEMENT), "Flash handshake needs cache maintenance");
BUILD_ASSERT(CONFIG_DCACHE_LINE_SIZE <= 64, "Handshake words must occupy separate cache lines");

static struct {
    volatile uint32_t request;
    uint8_t pad0[60];
    volatile uint32_t ack;
    uint8_t pad1[60];
} handshake __aligned(64);
K_SEM_DEFINE(park_request, 0, 1);
K_SEM_DEFINE(park_done, 0, 1);
K_MUTEX_DEFINE(guard_lock);
static enum tfm_platform_err_t park_result;

static void park_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&park_request, K_FOREVER);
        uint32_t address = (uint32_t)&handshake;
        psa_invec in = { &address, sizeof(address) };
        park_result = tfm_platform_ioctl(0x40000000, &in, NULL);
        k_sem_give(&park_done);
    }
}
K_THREAD_DEFINE(park_thread, 2048, park_worker, NULL, NULL, NULL, 2, 0, 0);

static int guard_begin(void)
{
    k_mutex_lock(&guard_lock, K_FOREVER);
    handshake.request = 1;
    sys_cache_data_flush_range((void *)&handshake.request, 64);
    k_sem_give(&park_request);
    int64_t deadline = k_uptime_get() + 5000;
    do {
        sys_cache_data_invd_range((void *)&handshake.ack, 64);
        if (handshake.ack == 1) return 0;
        k_sleep(K_MSEC(1));
    } while (k_uptime_get() < deadline);
    handshake.request = 0;
    sys_cache_data_flush_range((void *)&handshake.request, 64);
    /* Do not resume flash access if the secure call has not returned. */
    k_sem_take(&park_done, K_FOREVER);
    printk("ERR FLASH_PAUSE rc=%d\n", park_result);
    k_mutex_unlock(&guard_lock);
    return -EIO;
}
static void guard_end(void)
{
    handshake.request = 0;
    sys_cache_data_flush_range((void *)&handshake.request, 64);
    k_sem_take(&park_done, K_FOREVER);
    k_mutex_unlock(&guard_lock);
}

extern cy_rslt_t __real_mtb_serial_memory_write(mtb_serial_memory_t *, uint32_t,
                                               size_t, const uint8_t *);
extern cy_rslt_t __real_mtb_serial_memory_erase(mtb_serial_memory_t *, uint32_t, size_t);
cy_rslt_t __wrap_mtb_serial_memory_write(mtb_serial_memory_t *obj, uint32_t addr,
                                        size_t size, const uint8_t *data)
{
    if (guard_begin()) return (cy_rslt_t)-EIO;
    cy_rslt_t ret = __real_mtb_serial_memory_write(obj, addr, size, data);
    guard_end();
    return ret;
}
cy_rslt_t __wrap_mtb_serial_memory_erase(mtb_serial_memory_t *obj, uint32_t addr, size_t size)
{
    if (guard_begin()) return (cy_rslt_t)-EIO;
    cy_rslt_t ret = __real_mtb_serial_memory_erase(obj, addr, size);
    guard_end();
    return ret;
}
