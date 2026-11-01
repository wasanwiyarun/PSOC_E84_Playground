/* Checked CPU-only TFLM adapter for the reference float32 model.
 * No NPU initialization, RTOS shim, dynamic allocation or fake predictions. */
#include "ai_runtime.h"
#include <cmath>
#include <cstring>
#include <new>
#include <cstdarg>
#include <zephyr/sys/printk.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static tflite::MicroMutableOpResolver<6> resolver;
alignas(tflite::MicroInterpreter) static unsigned char storage[sizeof(tflite::MicroInterpreter)];
static tflite::MicroInterpreter *interpreter;
static bool registered;

extern "C" void DebugLog(const char *message) { printk("TFLM %s", message); }
void MicroPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk(format, args);
    va_end(args);
    printk("\n");
}

int ai_runtime_init(const void *data, size_t size, void *arena, size_t arena_size)
{
    ai_runtime_deinit();
    flatbuffers::Verifier verifier(static_cast<const uint8_t *>(data), size);
    if (!tflite::VerifyModelBuffer(verifier)) return -2;
    auto model = tflite::GetModel(data);
    if (model->version() != 3) return -2;
    if (!registered) {
        if (resolver.AddConv2D() != kTfLiteOk || resolver.AddFullyConnected() != kTfLiteOk ||
            resolver.AddMean() != kTfLiteOk || resolver.AddReshape() != kTfLiteOk ||
            resolver.AddSoftmax() != kTfLiteOk || resolver.AddUnidirectionalSequenceLSTM() != kTfLiteOk)
            return -2;
        registered = true;
    }
    memset(arena, 0, arena_size);
    interpreter = new (storage) tflite::MicroInterpreter(model, resolver,
        static_cast<uint8_t *>(arena), arena_size);
    if (interpreter->AllocateTensors() != kTfLiteOk ||
        interpreter->inputs_size() != 1 || interpreter->outputs_size() != 1) goto fail;
    if (interpreter->input(0)->type != kTfLiteFloat32 || interpreter->output(0)->type != kTfLiteFloat32 ||
        interpreter->input(0)->bytes != 650 * sizeof(float) ||
        interpreter->output(0)->bytes != 3 * sizeof(float)) goto fail;
    return 0;
fail:
    ai_runtime_deinit();
    return -2;
}

int ai_runtime_run(const float *input, int inputs, float *output, int outputs)
{
    if (!interpreter || !input || !output || inputs != 650 || outputs != 3) return -2;
    for (int i = 0; i < inputs; ++i) if (!std::isfinite(input[i])) return -2;
    memcpy(interpreter->input(0)->data.f, input, inputs * sizeof(float));
    if (interpreter->Invoke() != kTfLiteOk) return -2;
    const float *scores = interpreter->output(0)->data.f;
    float sum = 0;
    for (int i = 0; i < outputs; ++i) {
        if (!std::isfinite(scores[i]) || scores[i] < 0 || scores[i] > 1) return -2;
        sum += scores[i];
    }
    if (fabsf(sum - 1) > 0.001f) return -2;
    memcpy(output, scores, outputs * sizeof(float));
    return 0;
}

void ai_runtime_deinit(void)
{
    if (interpreter) interpreter->~MicroInterpreter();
    interpreter = nullptr;
}
size_t ai_runtime_arena_used(void) { return interpreter ? interpreter->arena_used_bytes() : 0; }
