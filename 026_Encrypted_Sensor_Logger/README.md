# 026 — Encrypted sensor data logger

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

An educational, touchscreen-controlled recorder using real BMI270 acceleration/
angular velocity and DPS368 temperature/pressure, with AES-256-GCM authenticated
records stored in external LittleFS. This is not a production key vault.

## Architecture

CM55 runs Zephyr, LVGL, the display/touch drivers, sensor acquisition and LittleFS.
CM33-NS relays PSA requests to TF-M on CM33 secure. The persistent AES-256 key is
generated in TF-M with encrypt/decrypt permission, **without export permission**.
The application does not contain a hard-coded storage key.

Three application threads separate UI, nominal 10 Hz acquisition and storage.
Records contain up to 16 samples; each batch is authenticated and `fs_sync()`ed.
STOP flushes the final batch and appends an authenticated completion record.
Queue overflow, storage failure or sensor failure stops recording with an error;
the application does not silently substitute simulated sensor readings.

The dedicated external-flash region is offset `0x01780000`, length `0x00300000`
(23.5–26.5 MiB), with twelve 256 KiB erase sectors. Earlier examples' regions
are unchanged. LittleFS needs metadata and copy-on-write space, so the whole
3 MiB is not available for samples. Firmware images occupy separate regions.
This region was checked blank on the test board; that is not a guarantee for
another board. Initialization always checks the entire region before formatting.

## Build, program and test

From the repository root, using the configured local Zephyr environment:

```sh
bash 026_Encrypted_Sensor_Logger/build.sh
/home/wasanw/zephyrproject/.venv/bin/python \
  026_Encrypted_Sensor_Logger/test_firmware.py \
  --port /dev/ttyACM0 --flash --init-blank --soak-samples 1152
```

The script programs CM55 **first**, then the matching CM33-NS/TF-M image. Both
images are required. It uses the shared programming helpers in `tools/python`.
`--init-blank` allows initialization only when the entire dedicated region is
erased (`0xff`). An unmountable, non-erased partition is never auto-formatted.
Do not run the script during an important recording. It creates unique test
filenames and deletes only its own successful test recordings.
The optional extended recording crosses a 64-nonce reservation boundary while
checking live sensor acquisition, UI progress and authenticated readback.

Logs, image SHA-256 hashes, checks and failures are saved under `test-results/`.
The script does not claim visual confirmation or a physical power-cut test.
See [VERIFICATION.md](VERIFICATION.md) for the actual board test outcome.

## Touch and UART controls

RECORD starts a uniquely named file. STOP + SAVE seals it. STATUS prints the
current counters on the UART. The dashboard shows live sensor values and free
space. UART is 115200 baud; commands terminate with newline.

| Command | Purpose |
| --- | --- |
| `log status` | Mounted/key/sensor/display/touch status, counts, errors and nonce |
| `log init-blank` | Initialize only a completely erased dedicated partition |
| `log start trip01` | Create a new recording; refuses existing names |
| `log start auto` | Generate a name from the persistent counter |
| `log stop` | Flush samples and append the authenticated completion record |
| `log list` | List filenames and file sizes |
| `log read trip01` | Preflight the entire file, then export verified sample rows |
| `log verify trip01` | Verify without exporting rows |
| `log delete trip01` | Delete one file while recording is stopped |
| `log kat` | Public known-answer crypto test, wrong-key and key-export checks |
| `log raw trip01 0` | Inspect one encrypted record in hexadecimal |
| `log tamper trip01 2 tag` | **Test only:** flip one bit in a record's header/data/tag |
| `log reboot` | Reboot without sealing an active file, for recovery testing |

Names contain 1–24 ASCII letters, digits, `_` or `-`; paths are not allowed.
Read/delete/tamper operations are rejected while recording. The diagnostic
tamper command changes real data: use disposable test files only. Repeating
the identical tamper command restores the same bit. Delete is a logical
LittleFS deletion, not a guarantee of physical secure erasure.

Each `ROW` is `uptime_ms, ax, ay, az, gx, gy, gz, temperature, pressure`.
Acceleration is milli-m/s²; angular velocity is milli-rad/s; temperature is
milli-°C; pressure is Pa. Uptime starts over on reboot. Records are not UTC
timestamped and samples are sequential sensor reads, not simultaneous captures.

## Authenticated streaming format

Each record is `64-byte header || ciphertext || 16-byte GCM tag`. Header magic
is `SLG1`, version 1. Integers are little-endian. The authenticated header
contains record type, sample count, payload length, sequence number, session
ID, 96-bit nonce, key ID and the previous record's tag. AAD additionally binds
the filename (zero-padded to 24 bytes). Samples are 40 bytes each. The final
record authenticates total sample and batch counts.

Key ID `0x2601` and protected-storage UID `0x534c4701` are chapter-specific.
The nonce is key ID + a 64-bit monotonic counter. Counter ranges of 64 are
persisted **before use**; reboot skips unused values. Missing counter state
with an existing key fails closed. No automatic key replacement is attempted.

Readback makes two passes: no sample rows are exported until preflight has
checked every complete record. Invalid tags, changed headers, reordered or
missing middle records are rejected. An unsealed file or torn final record is
reported as incomplete; only its authenticated prefix can be recovered.

## Trust boundaries and limits

- UART and touchscreen control are trusted; there is no user authentication.
  A caller authorized to use this firmware can request plaintext export.
- Filenames, record headers, sizes and timing remain visible. AEAD encrypts
  payloads and authenticates their metadata; it is not whole-partition encryption.
- Deleting a file, replaying a complete older file, or truncating a tail cannot
  all be distinguished from legitimate deletion/reset without an external
  trusted manifest. `sealed=0` must never be treated as a complete recording.
- The nonce counter is persistent protected-storage state, not a hardware
  anti-rollback counter. Restoring old secure storage together with an old key
  can undo nonce reservations. Adversarial whole-device rollback is outside
  this example's threat model.
- This development board has debug access enabled. TF-M API isolation and a
  non-exportable key do not imply physical extraction resistance. Production
  provisioning, debug locking, secure boot policy and storage hardening are
  separate work. Do not erase TF-M's persistent storage to reset a counter.
  The development TF-M build enables **dummy provisioning** and is explicitly
  unsuitable for production, even though the application storage key is
  randomly generated and its PSA export permission is denied.
- Reboot recovery is not proof of physical power-loss behavior during an erase,
  program or counter update. A controlled power-cut campaign is future work.
- Acquisition is nominally 10 Hz, not hard real-time. Flash writes/erases,
  protected-storage updates and UART exports serialize with sensor I2C and
  display/touch control. Sample timestamps expose these gaps; a zero queue-drop
  counter does **not** mean every nominal 100 ms acquisition deadline was met.

## Display + TF-M integration

This example reuses the drivers from 017/018 and the sensor-bus recovery from
020, but not their standalone secure bootstrap. The paired TF-M image adds:

1. Secure initialization of the fixed MIPI 0.90 V supply. The CM55 library's
   matching direct register write is intercepted; arbitrary configurations are
   rejected. Protected SRSS registers remain secure.
2. `APP_AXI_MS3_MSC` access-gate response set to `WAITED_TRFR`, allowing display
   scanout to wait during access gating. Its **security-error response remains
   `ERR`**; memory isolation is not disabled. The scoped generated config keeps
   TF-M readback validation consistent and does not modify the SDK checkout.
3. CM55 ECO frequency metadata matching the secure BSP's 17.2032 MHz crystal.
4. Correct 256 KiB erase geometry on the CM55 flash alias, plus the existing
   chapter-scoped QSPI geometry compatibility fix.
5. A multicore flash guard: before a CM55 program/erase, a dedicated PSA helper
   parks CM33 in secure SRAM with interrupts masked. CM55's flash critical path
   runs from ITCM. The request and acknowledgement occupy separate cache lines.
   After the operation CM55 releases CM33 and waits for the secure call to return.

The CM33 pause loop alone is relocated to secure **system SRAM alias
`0x34030000`** through the TF-M startup copy table. Vendor boot-time delay/GPIO
code stays in flash: moving it would execute uninitialized RAM before the copy
table runs. The linker and platform hooks are scoped to this example's generated
build files; the Zephyr/TF-M/PDL checkout is not modified. Configuration guards
reject an incompatible linker layout, isolation level or unprivileged service.

This custom service assumes trusted non-secure firmware. It only accepts an
aligned handshake in the fixed CM55 application SRAM range, but a caller that
never releases the request can stall CM33 indefinitely. A failed handshake
never permits a flash mutation; recovery may require resetting the board.
This is an educational coordination mechanism, not a hardened service for
untrusted tenants. Sensor I2C, touch/display control and PSA calls are serialized
so they cannot call the parked core in the middle of a flash operation.

PSA inputs are RAM-backed: CM55 XIP constant pointers must not be handed
directly to a CM33 secure service. Startup success is reported only after
actual scanout, LVGL flush, panel ID and error-register checks.
