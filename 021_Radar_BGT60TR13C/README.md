# Chapter 021 — BGT60TR13C radar bring-up

CM55 / Zephyr radar acquisition on TESA AIoT E84. Uses the board wiring and
P0 acquisition settings from the user's existing `Temp/Radar` ModusToolbox
DEEPCRAFT example. The Temp reference and global Zephyr tree are unchanged.

This first stage verifies identity, FIFO transport and real ADC acquisition.
It is **UART-only**: no LCD UI, gesture classifier, presence decision, range
or velocity estimate is claimed. Flashing it replaces Example 020 on the board;
use `../020_Kalman_Artificial_Horizon/flash.sh` to restore that dashboard.

## Wiring and implementation

| Signal | Board pin | Use |
| --- | --- | --- |
| MISO | P21.4 | SCB3 SPI input |
| MOSI | P21.5 | SCB3 SPI output |
| SCLK | P21.6 | SCB3 mode 0 clock |
| CS | P21.7 | Manually held low across header and FIFO phases |
| RESET | P20.7 | Active-low hardware reset, CS held high |
| IRQ | P20.3 | FIFO data-ready level, polled (not interrupt-driven) |

The chapter-local platform port exclusively owns SCB3 and peripheral divider
8-bit index 1 in its clock group (matching the reference BSP). It uses 8-bit register transfers and 12-bit
FIFO transfers with MOSI high during reads. Every transfer has a 20 ms deadline;
each capture has a 2 s acquisition deadline. Failures invalidate the session and
require `reset`. This is a PDL-backed application port, not a generic Zephyr
radar sensor driver. It reuses Chapter 017's secure-core clock startup.

P0: RX1, 128 samples/chirp, one chirp/frame, 2.352941 MS/s,
61.020099–61.479903 GHz sweep, nominal 5.00407 ms/frame (~200 frames/s).
The SPI clock is printed by `radar info`.

The portable [Infineon driver](https://github.com/Infineon/sensor-xensiv-bgt60trxx)
is vendored with its Apache-2.0 license and pinned revision. Infineon's own
driver example also uses the deterministic on-chip test sequence to validate
FIFO data transport. `selftest` is **not a physical target-detection test**.

## Build / program / test

```sh
./build.sh
./flash.sh
/home/wasanw/zephyrproject/.venv/bin/python test_firmware.py --case all
```

Individual cases: `id`, `selftest`, `capture`, `reset`, `parser`.
Close minicom or other serial readers before running tests. Results go to
ignored, timestamped `test-results/` directories. Raw capture arrays are saved
as JSON alongside the serial log.

## Separate serial commands

At 115200 baud, each command ends with a caller-selected token:

```text
radar info T1
radar id T2
radar selftest 64 T3
radar capture 16 T4
radar reset T5
```

Capture counts must be 1–64 frames. Capture/selftest first reply `OK RADAR START`,
then acquire into RAM, stop radar generation, and send one `DATA RADAR` line per
frame followed by `OK RADAR DONE`. Wait for DONE before sending another command.
On failure, the terminal result is `FAIL RADAR DONE`; partial samples are not
published as a successful capture. The board is idle between commands.

Each DATA line has 128 concatenated three-hex-digit ADC samples, plus min/max/sum.
DONE includes frame/sample counts, acquisition duration, test mismatches,
data-ready observations, FIFO status and CRC32 over little-endian uint16 samples.
Acquisition stops before transmitting data so 115200-baud UART cannot overflow
the radar FIFO. A continuously running stream is intentionally not exposed yet.

The Python test independently verifies every LFSR sample, UART CRC, frame order,
data-ready assertion, approximate frame rate, non-stuck real ADC samples,
freshness across captures, reset/restart, and malformed-command rejection.
Raw ADC variation alone does not prove gesture or presence detection.

## Verification

Built and programmed both CM55 and its secure CM33 boot companion on
2026-10-11. The final firmware passed `test_firmware.py --case all` on
`/dev/ttyACM0` at 115200 baud:

- BGT60TR13C identity **0x0303**, SPI **6.25 MHz**.
- **8,192 / 8,192** on-chip LFSR samples matched independently in Python;
  zero mismatches and CRC32 `e6a6b64a`. 64 frames acquired in 317,064 µs
  (201.85 frames/s over this finite window; configured interval 5.00407 ms).
- Repeated minimum-size one-frame self-test passed without rebooting.
- Two fresh **2,048-sample** real ADC captures passed sample statistics,
  frame ordering, UART CRC and stale/test-pattern rejection.
- Data-ready pin asserted during acquisition; no reported FIFO errors.
- Hard reset/reconfiguration followed by a 512-sample exact self-test passed.
- Invalid counts, unknown commands and overlong lines were rejected; the
  console remained responsive.

Final log: `test-results/20261011-111818/radar.log`; raw samples:
`test-results/20261011-111818/raw-captures.json` (generated, Git-ignored).
Earlier failed attempts are retained in their own timestamped log folders.
Startup was corrected to initialize the 17.2032 MHz ECO metadata before UART
setup and use valid divider index 1, matching the reference BSP.

The board is left running Chapter 021, idle between capture commands. Gesture
accuracy, physical target distance, continuous acquisition, injected wiring
faults and display interaction have **not** been validated by these tests.
