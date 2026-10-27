#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <cy_sysclk.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "ai_runtime.h"
#include "radar.h"

#define WINDOW_FRAMES 10U
#define MAX_WINDOWS 1200U
#define SELF_WINDOWS 3U
static const char *const labels[] = IMAI_DATA_OUT_SYMBOLS;
static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static uint16_t capture[WINDOW_FRAMES * RADAR_SAMPLES];
static bool capture_valid;
static int radar_ret, model_ret;
static unsigned arena_used, requested;
static char run_token[33];
static atomic_t active, stopping, completed;
K_MUTEX_DEFINE(console_lock);
K_SEM_DEFINE(work, 0, 1);

/* Match the secure-core clock setup reused from Example 017. */
static int clock_metadata_init(void)
{ Cy_SysClk_EcoSetFrequency(17203200U); return 0; }
SYS_INIT(clock_metadata_init, PRE_KERNEL_1, 0);

static int reset_model(void)
{
    model_ret = IMAI_init();
    arena_used = model_ret ? 0 : ai_runtime_arena_used();
    return model_ret;
}

static void scores(const char *kind, const char *token, unsigned window,
                   unsigned pass, const float output[3], uint32_t capture_us,
                   uint32_t infer_us, uint32_t crc)
{
    unsigned best = 0;
    uint32_t bits[3];
    memcpy(bits, output, sizeof(bits));
    for (unsigned i = 1; i < 3; ++i) if (output[i] > output[best]) best = i;
    printk("DATA AI %s token=%s window=%u pass=%u class=%s p0=%u p1=%u p2=%u bits=%08x,%08x,%08x capture_us=%u infer_us=%u crc=%08x\n",
           kind, token, window, pass, labels[best],
           (unsigned)(output[0] * 1000000), (unsigned)(output[1] * 1000000),
           (unsigned)(output[2] * 1000000), bits[0], bits[1], bits[2], capture_us, infer_us, crc);
}

/* Preserve the exact generated FFT and 10-frame queue contract. */
static int predict(const uint16_t *raw, float output[3])
{
    float input[RADAR_SAMPLES];
    for (unsigned frame = 0; frame < WINDOW_FRAMES; ++frame) {
        for (unsigned i = 0; i < RADAR_SAMPLES; ++i) input[i] = raw[frame * RADAR_SAMPLES + i];
        int ret = IMAI_enqueue(input);
        if (ret) return ret;
        ret = IMAI_dequeue(output);
        if (frame + 1 < WINDOW_FRAMES) {
            if (ret != IMAI_RET_NODATA) return -EPROTO;
        } else if (ret) return ret;
    }
    return 0;
}

static void inference_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&work, K_FOREVER);
        k_mutex_lock(&console_lock, K_FOREVER);
        int ret = reset_model();
        k_mutex_unlock(&console_lock);
        unsigned windows = 0;
        int64_t begin = k_uptime_get();
        if (!ret) {
            ret = radar_stream_start();
            k_mutex_lock(&console_lock, K_FOREVER);
            radar_ret = ret;
            k_mutex_unlock(&console_lock);
        }
        while (!ret && !atomic_get(&stopping) && windows < requested) {
            struct radar_result acquisition;
            capture_valid = false;
            ret = radar_stream_read(WINDOW_FRAMES, capture, &acquisition);
            if (ret) {
                k_mutex_lock(&console_lock, K_FOREVER);
                radar_ret = ret;
                k_mutex_unlock(&console_lock);
                break;
            }
            float output[3];
            uint32_t start = k_cycle_get_32();
            ret = predict(capture, output);
            uint32_t infer_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
            k_mutex_lock(&console_lock, K_FOREVER);
            if (ret) model_ret = ret;
            else {
                capture_valid = true;
                scores("RESULT", run_token, windows, 0, output, acquisition.elapsed_us, infer_us,
                       crc32_ieee((const uint8_t *)capture, sizeof(capture)));
                atomic_set(&completed, ++windows);
            }
            k_mutex_unlock(&console_lock);
        }
        int stop_ret = radar_stream_stop();
        if (!ret) ret = stop_ret;
        k_mutex_lock(&console_lock, K_FOREVER);
        if (stop_ret) radar_ret = stop_ret;
        printk("%s AI DONE token=%s ret=%d windows=%u frames=%u elapsed_ms=%lld stopped=%u rf=off\n",
               ret ? "FAIL" : "OK", run_token, ret, windows, windows * WINDOW_FRAMES,
               (long long)(k_uptime_get() - begin), !!atomic_get(&stopping));
        atomic_clear(&active);
        k_mutex_unlock(&console_lock);
    }
}
K_THREAD_DEFINE(ai_worker, 16384, inference_worker, NULL, NULL, NULL, 5, 0, 0);

/* Deterministic ADC fixture, not gesture training data. Checks the real
 * FFT/window/model path and exact reproducibility after reset, without RF. */
static int selftest(const char *token)
{
    float first[SELF_WINDOWS][3], output[3];
    capture_valid = false;
    for (unsigned pass = 0; pass < 2; ++pass) {
        int ret = reset_model();
        if (ret) return ret;
        float invalid[650] = {NAN};
        if (ai_runtime_run(invalid, 650, output, 3) != IMAI_RET_ERROR ||
            ai_runtime_run(invalid, 649, output, 3) != IMAI_RET_ERROR) return -EPROTO;
        for (unsigned window = 0; window < SELF_WINDOWS; ++window) {
            for (unsigned f = 0; f < WINDOW_FRAMES; ++f)
                for (unsigned i = 0; i < RADAR_SAMPLES; ++i)
                    capture[f * RADAR_SAMPLES + i] = 1024 +
                        (((window * WINDOW_FRAMES + f) * 37 + i * 53 + (i * i) % 97) % 2048);
            uint32_t start = k_cycle_get_32();
            ret = predict(capture, output);
            if (ret) return ret;
            if (!pass) memcpy(first[window], output, sizeof(output));
            else if (memcmp(first[window], output, sizeof(output))) return -EBADMSG;
            scores("SELF", token, window, pass, output, 0,
                   k_cyc_to_us_floor32(k_cycle_get_32() - start),
                   crc32_ieee((const uint8_t *)capture, sizeof(capture)));
        }
    }
    return reset_model();
}

static bool valid_token(const char *s)
{
    size_t n = strlen(s);
    if (!n || n > 32) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '_')) return false;
    return true;
}

/* Under console_lock; live acquisition and inference run on ai_worker. */
static void command(char *line)
{
    char *args[5], *save;
    unsigned argc = 0;
    for (char *s = strtok_r(line, " \t", &save); s; s = strtok_r(NULL, " \t", &save)) {
        if (argc == ARRAY_SIZE(args)) { printk("FAIL AI COMMAND\n"); return; }
        args[argc++] = s;
    }
    if (argc < 3 || strcmp(args[0], "ai")) { printk("FAIL AI COMMAND\n"); return; }
    const char *cmd = args[1], *token = args[argc - 1];
    if (!valid_token(token)) { printk("FAIL AI TOKEN\n"); return; }
    if (argc == 3 && (!strcmp(cmd, "info") || !strcmp(cmd, "status"))) {
        printk("%s AI INFO token=%s chapter=022 model=850a926a-3fd3-4acd-9280-478d7a438955 classes=unlabelled,Circle,Push samples=128 frames_per_window=10 adc_bits=12 engine=TFLM_CPU_FLOAT32 arena=%u radar_ret=%d model_ret=%d active=%u windows=%u max_windows=%u\n",
               radar_ret || model_ret ? "FAIL" : "OK", token, arena_used, radar_ret, model_ret,
               !!atomic_get(&active), (unsigned)atomic_get(&completed), MAX_WINDOWS);
        return;
    }
    if (argc == 3 && !strcmp(cmd, "stop")) {
        atomic_set(&stopping, 1);
        printk("OK AI STOP token=%s active=%u\n", token, !!atomic_get(&active));
        return;
    }
    if (atomic_get(&active)) { printk("FAIL AI BUSY token=%s\n", token); return; }
    if (argc == 3 && !strcmp(cmd, "reset")) {
        capture_valid = false;
        radar_ret = radar_init();
        reset_model();
        atomic_clear(&completed);
        printk("%s AI RESET token=%s radar_ret=%d model_ret=%d\n",
               radar_ret || model_ret ? "FAIL" : "OK", token, radar_ret, model_ret);
        return;
    }
    if (argc == 3 && !strcmp(cmd, "selftest")) {
        int ret = selftest(token);
        model_ret = ret;
        printk("%s AI SELFTEST token=%s ret=%d windows=6 repeat_exact=%u warmup_checked=1 guards_checked=1 rf=off\n",
               ret ? "FAIL" : "OK", token, ret, !ret);
        return;
    }
    if (argc == 3 && !strcmp(cmd, "dump")) {
        if (!capture_valid) { printk("FAIL AI NO_CAPTURE token=%s\n", token); return; }
        for (unsigned frame = 0; frame < WINDOW_FRAMES; ++frame) {
            printk("DATA AI RAW token=%s frame=%u raw=", token, frame);
            for (unsigned i = 0; i < RADAR_SAMPLES; ++i) printk("%03x", capture[frame * RADAR_SAMPLES + i]);
            printk("\n");
        }
        printk("OK AI DUMP token=%s frames=10 crc=%08x\n", token,
               crc32_ieee((const uint8_t *)capture, sizeof(capture)));
        return;
    }
    if ((argc == 4 && !strcmp(cmd, "run")) || (argc == 3 && !strcmp(cmd, "start"))) {
        unsigned long n = MAX_WINDOWS;
        if (argc == 4) {
            char *end;
            n = strtoul(args[2], &end, 10);
            if (!*args[2] || *end || args[2][0] == '-' || args[2][0] == '+' || !n || n > MAX_WINDOWS) {
                printk("FAIL AI ARGUMENT token=%s\n", token); return;
            }
        }
        if (radar_ret || model_ret) { printk("FAIL AI NOT_READY token=%s\n", token); return; }
        requested = n;
        strcpy(run_token, token);
        capture_valid = false;
        atomic_clear(&stopping);
        atomic_clear(&completed);
        atomic_set(&active, 1);
        printk("OK AI START token=%s requested=%u\n", token, requested);
        k_sem_give(&work);
        return;
    }
    printk("FAIL AI COMMAND token=%s\n", token);
}

int main(void)
{
    printk("RADAR_AI BOOT chapter=022\n");
    radar_ret = radar_init();
    reset_model();
    printk("%s AI INITIALIZATION_%s chapter=022 radar_ret=%d model_ret=%d arena=%u rf=off\n",
           radar_ret || model_ret ? "FAIL" : "OK", radar_ret || model_ret ? "FAILED" : "SUCCESS",
           radar_ret, model_ret, arena_used);
    printk("AI READY commands=info,status,selftest,run,start,stop,dump,reset uart=115200\n");
    char line[128]; unsigned used = 0; bool overflow = false;
    for (;;) {
        unsigned char c;
        while (!uart_poll_in(uart, &c)) {
            if (c == '\r' || c == '\n') {
                k_mutex_lock(&console_lock, K_FOREVER);
                if (overflow) printk("FAIL AI COMMAND_TOO_LONG\n");
                else if (used) { line[used] = 0; command(line); }
                k_mutex_unlock(&console_lock);
                used = 0; overflow = false;
            } else if (used < sizeof(line) - 1 && ((c >= 32 && c < 127) || c == '\t')) line[used++] = c;
            else overflow = true;
        }
        k_msleep(1);
    }
}
