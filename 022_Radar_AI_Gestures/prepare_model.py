#!/usr/bin/env python3
"""Adapt the user's licensed, local DEEPCRAFT export into the build directory.

Do not redistribute the model source. FFT, windowing and trained weights are
unchanged; only the ModusToolbox runtime binding and memory placement change.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

HASHES = {
    "model.c": "9cce12727ca8284be6185ea5afabe2cc1cfec838c44341efcc9d0bf145f54141",
    "model.h": "8589875cedf181db76ce27e07259cf60fdc38f02ff08b583adc751ddccc016e6",
}

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected exactly one source fragment: {old[:80]}")
    return text.replace(old, new)

def prepare(reference, output):
    original = {}
    for name, expected in HASHES.items():
        data = (reference / "proj_cm55/model" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"{name}: unsupported model revision; review before porting")
        original[name] = data.decode()
    c = original["model.c"]
    weights = re.search(r"uint32_t _K5\[\] = \{(.*?)\};", c, re.S).group(1)
    model = b"".join(struct.pack("<I", int(n, 16)) for n in re.findall(r"0x[0-9a-fA-F]+", weights))
    assert len(model) == 77360
    for include in ("cy_retarget_io.h", "cy_utils.h", "mtb_ml_model.h", "mtb_ml_utils.h", "mtb_ml.h"):
        c = replace_once(c, f'#include "{include}"', '')
    c = replace_once(c, '#include "model.h"', '#include "model.h"\n#include "ai_runtime.h"')
    c = c.replace('CY_SECTION(".cy_socmem_data")', '')
    c = replace_once(c, 'uint32_t _K5[]', 'const uint32_t _K5[]')
    begin = c.index('static inline void mtb_model_f32(')
    end = c.index('\nstatic void makeipt', begin)
    c = c[:begin] + '''static inline int mtb_model_f32(const void* handle, const float* restrict src, int src_count, float* restrict dst, int dst_count)
{
    (void)handle;
    return ai_runtime_run(src, src_count, dst, dst_count);
}
''' + c[end:]
    begin = c.index('static inline void mtb_model_free(')
    end = c.index('\n/**', begin)
    c = c[:begin] + '''static inline void mtb_model_free(const void* handle)
{
    (void)handle;
    ai_runtime_deinit();
}
''' + c[end:]
    begin = c.index('void IMAI_mtb_models_print_info()')
    end = c.index('#ifndef __CLOSE_HOOKS', begin)
    c = c[:begin] + '''static int mtb_init(const void* handle, const void* model_bin, unsigned int model_size, uint8_t* arena_buffer, int arena_size, int npu_priority, char model_name[])
{
    (void)handle; (void)npu_priority; (void)model_name;
    return ai_runtime_init(model_bin, model_size, arena_buffer, arena_size);
}

''' + c[end:]
    c = replace_once(c, '    mtb_model_f32(_K8, _K2, 650, data_out, 3);',
                     '    __RETURN_ERROR(mtb_model_f32(_K8, _K2, 650, data_out, 3));')
    h = replace_once(original['model.h'], '#include "mtb_ml_model.h"', '')
    begin = h.index('// Symbol IMAI_PROFILING')
    end = h.index('// Profiling regions', begin)
    h = h[:begin] + h[end:]
    notice = '/* Modified for Zephyr: CPU TFLM binding, checked inference errors, RAM placement.\n * FFT, windowing, model weights and class order unchanged. See prepare_model.py. */\n'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'model.c').write_text(notice + c)
    (output / 'model.h').write_text(notice + h)
    (output / 'model.tflite').write_bytes(model)
    (output / 'manifest.json').write_text(json.dumps({
        'id': '850a926a-3fd3-4acd-9280-478d7a438955', 'source_sha256': HASHES,
        'flatbuffer_sha256': hashlib.sha256(model).hexdigest(),
        'classes': ['unlabelled', 'Circle', 'Push'], 'flatbuffer_bytes': len(model),
    }, indent=2) + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.reference, args.output)
