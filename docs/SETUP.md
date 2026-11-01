# Host setup and build families

[Repository](../README.md) · [Tests](TESTING.md) · [Storage safety](STORAGE.md)

## Hardware and software prerequisites

The tested setup is a TESA AIoT E84 carrier with PSoC Edge E84 SoM, using the
Zephyr `kit_pse84_ai/pse846gps2dbzc4a` targets. Board/carrier revisions and
connector labels can differ; verify the wiring described in the chosen chapter.
Connect the **KitProg3/debug USB** with a data cable. The tested adapter is
`19160998012D2400`; the console is `/dev/ttyACM0`, 115200 baud, 8-N-1.
Close minicom and other serial readers before an automated test. Do not
rewire a powered display or change resistors to run a software test.

The current build scripts expect these local installations:

| Component | Tested installation / revision |
| --- | --- |
| Zephyr workspace | `/home/wasanw/zephyrproject` |
| Zephyr | `v4.5.0-rc1-231-g74b7173e9c92` (not a promise of latest-version compatibility) |
| Python environment | `/home/wasanw/zephyrproject/.venv` |
| Zephyr SDK | `/home/wasanw/zephyr-sdk-1.0.1` |
| Infineon OpenOCD | `/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd` |
| OpenOCD target | `target/infineon/pse84xgxs2.cfg` |
| Infineon HAL checkout at audit | `2191db4d7723` |
| TF-M checkout at audit | `b01f190e46af` |

On another computer, install a Zephyr west workspace with the matching Zephyr
revision and its manifest dependencies, the SDK, and Infineon's programming
tools. This repository is an application collection, **not** a standalone west
manifest or SDK installer. Adapt the absolute activation/OpenOCD paths in the
chapter scripts and shared helpers deliberately; do not assume changing one
environment variable relocates every script. In particular, the encrypted-file
programming helper also uses the tested workspace paths.

Activate the environment in every terminal that runs Python tests or `west`:

```sh
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh
python -m pip install pyserial cryptography
```

Zephyr additionally needs its normal CMake/Ninja/west/toolchain prerequisites.
Host C tests use a native C compiler and the math library; Pillow is optional
for saved preview images. Chapter 022 has separate local model/runtime
dependencies and `requirements-test.txt` for its desktop oracle. See its README;
those licensed artifacts are not downloaded or redistributed by this repo.

## Choose the correct boot arrangement

| Chapters | Application / companion | Procedure |
| --- | --- | --- |
| 001–006, 010–011, 013–015 | CM55 + Sysbuild `enable_cm55` | Chapter `build.sh`; explicit domains with the shared flash/monitor helper, or the documented Sysbuild flash command |
| 012 | CM55 + CM33-NS/TF-M relay | Build CM33-NS first, then CM55; `flash.sh` programs CM55 then CM33-NS |
| 016, 023–025 | CM33-NS + TF-M | Program the chapter's matching merged secure/non-secure image, not a CM55 companion |
| 017–022 | CM55 + Sysbuild companion with 017 clock-startup module | Use chapter `build.sh` and `flash.sh`; retain sibling driver directories |
| 026 | CM55 UI/sensors/FS + CM33-NS/TF-M crypto and SRAM pause service | Build both; its test `--flash` programs CM55 before CM33-NS |

Do not mix companions between build families. 026's display support is integrated
into TF-M; it must not be replaced with 017's standalone startup image. All
applications are alternatives, not simultaneously installed user programs.

Unless a chapter explicitly says **repository root**, run its README commands
from that chapter directory. `bash build.sh` works even if executable permissions
were not preserved during copying. Most tests require the board already flashed;
only 023–026 offer the documented `--flash` test option.

## First smoke test

From the repository root:

```sh
bash 001_Hello_World/build.sh
bash tools/flash_and_monitor_pse84_ai.sh \
  --build-dir 001_Hello_World/build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 001_Hello_World \
  --expect "Hello from the PSOC Edge84 Zephyr playground!"
```

This replaces the current firmware. Do not run it during a recording. For
013–015, read the storage guide **before booting or testing**: boot/test actions
can erase data. No mass erase is needed for the normal workflows.

## Troubleshooting

- Missing serial port: check the debug USB connector, data cable, USB permissions
  and actual `/dev/ttyACM*` name. Use `--port` where supported; do not blindly sudo
  the whole build/test workflow.
- Missing TF-M headers or hung PSA request: check paired image build/flash order;
  do not use a missing-core timeout as a test on the unbounded relay client.
- Dark LCD: verify 017's clock-startup companion or 026's TF-M integration,
  panel wiring/power and fresh display-health errors. A serial success marker
  alone does not prove visible pixels. 021/022 are intentionally UART-only.
- `HIGH FIELD` in 020: magnetometer transport can pass while heading is unusable;
  north is intentionally disabled. Do not suppress the warning to claim a compass.
- `init-blank` refuses media: retain its contents and inspect ownership. Never
  format occupied storage merely to make a test pass.
- After moving the repo, rebuild stale CMake caches with west's pristine-build
  option using that chapter's original targets/options; do not reuse binaries
  or pair unrelated secure/non-secure outputs.
