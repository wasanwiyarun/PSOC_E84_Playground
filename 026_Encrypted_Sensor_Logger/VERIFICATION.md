# Physical board verification — 2026-11-01

Result: **11 protocol acceptance checks passed on the physical board**, after
pristine CM33-NS/TF-M and CM55 builds. This is not a visual acceptance result,
a hard real-time performance claim, or a production security certification.

Board: TESA AIoT E84, PSE846GPS2DBZC4A; KitProg3 `19160998012D2400`;
UART `/dev/ttyACM0`, 115200 baud. Zephyr
`v4.5.0-rc1-231-g74b7173e9c92`, Zephyr SDK 1.0.1, TF-M 2.3.1 development
configuration with isolation level 2 and dummy provisioning.

Command from the repository root:

```sh
/home/wasanw/zephyrproject/.venv/bin/python \
  026_Encrypted_Sensor_Logger/test_firmware.py \
  --flash --init-blank --soak-samples 1152 \
  --results 026_Encrypted_Sensor_Logger/test-results/final-pristine-soak
```

The compact [machine-readable result](verification/2026-11-01-hardware.json)
contains both programmed-image SHA-256 hashes and all check names. Full UART
and programming logs remain locally in the ignored results directory above.

## Passed checks

- Programmed both physical cores in the required order, CM55 then CM33/TF-M.
- Mounted LittleFS; persistent key, live sensors, panel/scanout health and touch
  initialization all reported ready.
- AES-256-GCM matched independent Python `cryptography` output; a different key
  rejected the tag and export of the persistent storage key was denied.
- Recorded beyond 1 KiB while UI updates continued, with no queue drops or
  sensor errors; conflicting recording/read/delete requests were rejected.
- Exported authenticated, plausible sensor samples; checked ciphertext,
  distinct nonces and the previous-tag chain.
- Rejected malformed commands, invalid names/paths, oversized input and attempts
  to overwrite an existing recording.
- Tampered a later record's header, ciphertext and tag separately; each failed
  before any plaintext row was exported. Restoring the test bit restored data.
- Rebooted and recovered the original sealed recording with the persistent key.
- Rebooted during recording and recovered its synchronized, unsealed prefix;
  the next recording skipped the old reserved nonce range.
- Completed an extended recording across a nonce-reservation boundary.
- Listed and deleted only this run's disposable test files; ended idle with
  healthy display status and no errors/drops.

## Measured extended recording

| Measurement | Observed |
| --- | ---: |
| Authenticated samples | 1,155 |
| Data batches | 73 |
| Saved file size including seal | 52,128 bytes |
| First-to-last sample interval | 182.906 s |
| Effective sample rate over that interval | approximately 6.3 Hz |
| Largest sample timestamp gap | 2,099 ms |
| Sensor errors / queue drops | 0 / 0 |
| First / last data nonce counter | 774 / 846 |
| Final nonce after seal | 847 |

The requested acquisition cadence is 100 ms, but flash synchronization blocks
sensor-bus access and can stall XIP execution. The shorter initial recording's
largest gap was 178 ms; the longer recording exposed the 2.099 s worst gap.
Zero queue drops does not mean zero missed acquisition deadlines. Continuous
fixed-rate logging needs further buffering/storage architecture work; this
version demonstrates authenticated logging and recovery, not deterministic
10 Hz sampling. Frequent LittleFS synchronization also has erase/write-wear
costs; no endurance qualification was performed.

## Fixes and storage safety

The earlier crash during external-flash mutation was resolved by parking CM33
in a secure SRAM loop while CM55's RAM-resident flash critical section runs.
The final ELF was inspected: `park_in_ram` is at `0x34030000`, has no outbound
calls while parked, and boot-time vendor delay/GPIO functions remain in flash.
Both images rebuilt successfully from pristine build directories.

The original 1 MiB logger region had insufficient LittleFS copy-on-write space
for this multi-file test. Proposed larger regions were checked and refused
because they contained data; they were not formatted. A read-only sector scan
found the final 3 MiB region, `0x01780000`–`0x01a80000` (end exclusive), blank.
The firmware then independently checked the entire region before formatting.
The old 16–17 MiB test filesystem was left intact with its known disposable
test files removed. No mass erase was used. No unrelated recording was deleted.

The temporary broad read-only scan command was removed from the final firmware.
Unrelated example 019 and graph files were not part of this change.

## Not verified / known limits

- User-visible LCD appearance and physical touch-button operation on this
  firmware still require confirmation. Initialization and controller counters
  are not a substitute for seeing the display.
- Software reboot recovery was tested; physical power removal during program,
  erase or protected-counter updates was not.
- No endurance, full-partition exhaustion, multi-hour soak, hostile firmware
  denial-of-service, physical extraction or anti-rollback qualification.
- TF-M dummy provisioning and enabled development debug access are unsuitable
  for production. See the README threat-model and nonce-counter limitations.
- Builds completed with existing upstream warnings (TF-M development/version
  configuration, a mailbox printf type, a vendor graphics float promotion,
  custom display-library layout and enabled assertions); no new chapter-source
  compiler warnings were reported.

After acceptance, the firmware is left running and idle on the board.
