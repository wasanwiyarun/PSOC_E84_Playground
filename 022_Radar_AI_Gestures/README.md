# 022 — Radar AI gestures on Zephyr / CM55

Ports the actual DEEPCRAFT model and preprocessing from the local
`Temp/Radar` example to the TESA AIoT E84. Live BGT60TR13C radar samples feed
TensorFlow Lite Micro on CM55 and produce `unlabelled`, `Circle`, and `Push`
scores. This is **UART-only**, like the reference application: no radar LCD UI,
range/velocity output, presence guarantee, or NPU acceleration is claimed.

Hardware acquisition and inference numerics have been tested. Human gesture
accuracy remains a manual acceptance test; a valid score is not proof that
the gesture label is correct.

## Model and acquisition

- Model ID: `850a926a-3fd3-4acd-9280-478d7a438955`.
- Original 77,360-byte float32 model, unchanged weights and class order.
- Original 128-point FFT, imaginary-component selection, and ten-frame
  windowing. Do not substitute FFT magnitude or normalize ADC values.
- One RX channel, 128 unsigned 12-bit samples per chirp, one chirp/frame,
  nominal 5.00407 ms/frame (~200 Hz), same P0 settings as `Temp/Radar`.
- Continuous FIFO acquisition across windows, with a prediction every ten
  frames (~20 Hz). The FIFO buffers incoming frames during inference and UART
  output. Unlike the reference, this port does not stop the radar for inference.
- Float32 inference runs on the **CM55 CPU**, not the U55 NPU. The supplied
  Cortex-M55 runtime archive is in its vendor `COMPONENT_U55` directory, but
  this model uses ordinary CPU Conv2D/FC/Mean/Reshape/Softmax/LSTM operators.
- Arena capacity 16,384 bytes; runtime reports 8,240 bytes used.
- Model state resets on each new run. Recurrent state is retained between
  windows within that run, as in the reference pipeline.

The chapter-local SCB3 PDL port is derived from Example 021. Wiring: MISO P21.4,
MOSI P21.5, SCLK P21.6, manual CS P21.7, reset P20.7, polled IRQ P20.3.
SPI is 6.25 MHz; register transfers are 8-bit, FIFO transfers 12-bit. This is
not a generic Zephyr sensor driver. Example 017 supplies secure-core clock
startup, and Example 021 supplies the Apache-2.0 portable sensor driver.

SPI transfers have a 20 ms deadline and each window read has a 2 s deadline.
FIFO overflow/underflow, clock or burst errors stop the run. SPI failures use
hardware reset to stop RF and invalidate the sensor until `ai reset` succeeds.
Inference errors are propagated, not converted to stale predictions.

## Local dependencies / licensing

The original model is covered by the reference project's Infineon license.
It is **not copied into this repository**. `prepare_model.py` checks SHA-256
of the locally installed model sources, then creates a runtime-adapted copy
and `.tflite` file only inside the Git-ignored build directory. Original
copyright notices are retained. Review the licenses in the dependency folders
before sharing source, model, libraries, or firmware.

Defaults relative to this folder:

```text
../../Temp/Radar/proj_cm55/model/model.c
../../Temp/Radar/proj_cm55/model/model.h
../../Temp/mtb_shared/ml-tflite-micro/release-v3.4.0/
```

Install the reference and its dependencies through ModusToolbox Library Manager
if absent. The TFLM archive is hash-pinned; its revision is
`424235ab9eb882e21cd1958ab0594e58ed0f218b`. Matching headers must accompany it.
No global Zephyr files or files in `Temp` are modified.

The adaptation replaces only the ModusToolbox runtime binding and section
placement, makes weights read-only, and checks inference return codes. The
generated FFT arithmetic is unchanged. The C++ boundary uses the supplied
archive's short-enum ABI; the Zephyr application keeps its normal ABI.

## Build, flash, test

```sh
./build.sh
./flash.sh
/home/wasanw/zephyrproject/.venv/bin/python -m unittest -v test_host.py
/home/wasanw/zephyrproject/.venv/bin/python test_firmware.py --case all
```

Override dependency locations if needed:

```sh
./build.sh -DRADAR_REFERENCE_ROOT=/path/to/Radar \
  -DMTB_TFLM_ROOT=/path/to/ml-tflite-micro/release-v3.4.0
```

For independent desktop comparison, use a local test environment:

```sh
python3 -m venv build-cm55/host-test-venv
build-cm55/host-test-venv/bin/pip install -r requirements-test.txt
build-cm55/host-test-venv/bin/python test_firmware.py --case all --oracle --windows 200
```

Close minicom/other serial readers first. Separate cases: `info`, `selftest`,
`live`, `control`, `parser`, `reset`. `--oracle` requires desktop LiteRT and
NumPy and fails if they are missing; it never silently skips comparison.
Timestamped serial logs, raw snapshots, scores and JSON results are saved in
ignored `test-results/`. Shared serial buffering/logging lives in `../tools/python/`.

## Commands (115200 baud)

Each command ends with your own short alphanumeric/underscore/hyphen token.

| Command | Action |
| --- | --- |
| `ai info T1` | Model identity, input format, runtime and readiness |
| `ai selftest T2` | Two identical three-window synthetic runs; no RF |
| `ai run 100 T3` | Acquire and infer exactly 100 windows |
| `ai start T4` | Live gesture demo, automatically ends after 1,200 windows (~60 s) |
| `ai status T5` | Running/idle state and completed windows |
| `ai stop T6` | Request stop; wait for the run's `AI DONE` to confirm RF off |
| `ai dump T7` | While idle, dump the last successful ten-frame input window |
| `ai reset T8` | While idle, reset sensor/model and invalidate saved input |

`run` accepts 1–1,200 windows. START acknowledges the request; RESULT reports
the argmax class, three probabilities in millionths, exact float32 score bits,
window index, timings, and CRC32 of raw little-endian uint16 samples. `infer_us`
includes preprocessing plus inference. `capture_us` measures time spent reading
that window, not its full sampling interval (some samples are already in FIFO).
DONE reports completion/failure, counts and RF-off state. `stop` ACK alone does
not mean shutdown is complete. A new run/reset/selftest/dump is rejected while
busy; status and stop remain available.

`dump` contains ten lines of 128 three-hex-digit ADC values. A single-window
run starts with cleared model state and can be replayed independently. Replaying
only the last window of a multi-window run does not recreate earlier LSTM state.

## Manual gesture check

1. Open a serial terminal at 115200 baud and send `ai start demo`.
2. Face the radar antenna toward your hand, using the orientation demonstrated
   in `Temp/Radar/README.md`. The reference suggests approximately 60 cm distance.
3. Try a push gesture, a circle gesture, then no gesture. Inspect all three
   scores, not just the winning label. Repeat to assess false positives.
4. Send `ai stop stop1` and wait for `OK AI DONE token=demo ... rf=off`.

The unmodified example model is not production-ready. Mounting, background
motion and gesture style affect scores. No gesture-specific accuracy or
confidence threshold has been validated here. Flashing 022 replaces the prior
LCD firmware; a dark/unchanged screen is expected for this UART-only chapter.

## Physical-board evidence (2026-10-11)

Final continuous-acquisition validation:
`test-results/20261011-114347/radar-ai.log` and `results.json`.

- Programmed both core images through KitProg3; radar and model initialization succeeded.
- 1,200 live predictions / 12,000 frames in 60.092 s: **19.97 predictions/s**.
- Preprocessing plus inference: **4.24–4.44 ms** per window in that run.
- Six deterministic predictions matched exactly across model resets; nine-frame
  warm-up and invalid-shape/non-finite-input rejection were checked on board.
- NumPy FFT + desktop LiteRT matched all six synthetic and two actual captured
  windows; maximum absolute probability difference **5.97e-8**.
- Raw ADC dump CRCs passed; two captures were different and nonconstant.
- Busy rejection, start/stop (~59 ms in this test), invalid-command recovery,
  reset and post-reset live inference all passed. Four host unit tests passed.

Human gesture accuracy and deliberately injected hardware fault recovery were
not tested. These results verify acquisition, model execution and numerical
port fidelity, not successful recognition of a person's gestures.
