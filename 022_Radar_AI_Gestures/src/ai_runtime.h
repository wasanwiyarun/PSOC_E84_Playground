#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int ai_runtime_init(const void *model, size_t size, void *arena, size_t arena_size);
int ai_runtime_run(const float *input, int inputs, float *output, int outputs);
void ai_runtime_deinit(void);
size_t ai_runtime_arena_used(void);
#ifdef __cplusplus
}
#endif
