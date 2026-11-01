# PSOC_E84_Playground

Educational Zephyr examples for the PSoC Edge E84 / TESA AIoT E84: sensors,
connectivity, dual-core PSA services, LVGL, radar AI and authenticated storage.
Each chapter is a separate firmware application, not a feature to enable in
one combined image. Programming a chapter replaces the board's current firmware.

## Documentation

- [Setup and build families](docs/SETUP.md)
- [All 23 examples: tests, evidence and limits](docs/TESTING.md)
- [Flash ownership and destructive operations](docs/STORAGE.md)
- [Current peripheral roadmap](docs/PERIPHERAL_ROADMAP.md)
- [Contributing and documentation checklist](CONTRIBUTING.md)
- [Security limitations](SECURITY.md)
- [Third-party dependencies and migration checklist](THIRD_PARTY.md)
- [Documentation audit](docs/DOCUMENTATION_AUDIT.md)

Chapter numbers 007–009 are roadmap slots, not missing example directories.
Magnetometer work is in 020; radar is in 021/022; microphone support is pending.
Follow each linked chapter README for its actual build/flash order.

## Tested local environment

The scripts currently use this machine's Zephyr workspace (not a portable
installer). See the setup guide before using another computer:

```text
/home/wasanw/zephyrproject
```

Activate its Python environment before building Zephyr applications:

```sh
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh
```

## Serial-output check

`tools/serial_expect.py` prints the board's serial output and waits for one or
more expected strings. It exits with status `0` only when every string has
arrived, so it can be used in automated checks.

```sh
source /home/wasanw/zephyrproject/.venv/bin/activate
python tools/serial_expect.py \
  --port /dev/ttyACM0 \
  --baud 115200 \
  --expect "Hello from the PSOC Edge84 Zephyr playground!" \
  --timeout 20
```

Exit codes: `0` matched, `1` timed out, `2` serial-port error.

Example-specific firmware tests keep their commands and expected responses next
to each firmware project.  Their common serial waiting and log tee support is
kept in `tools/python/psoc_e84_tools/`, so new tests do not duplicate the
transport behavior.

## Build, flash, and verify the PSOC Edge84 AI Kit

Each example owns its build script because its board, build directory, and
Sysbuild choices are application-specific. Build the hello-world example:

```sh
001_Hello_World/build.sh
```

With the KitProg3 USB-C connector (J1) attached, the shared utility flashes
the image domains and verifies the expected text on `/dev/ttyACM0`:

```sh
tools/flash_and_monitor_pse84_ai.sh \
  --build-dir 001_Hello_World/build-pse84-ai \
  --companion-domain enable_cm55 \
  --app-domain 001_Hello_World \
  --expect "Hello from the PSOC Edge84 Zephyr playground!"
```

The shared tool accepts `--port`, `--baud`, and `--timeout` overrides. It
does not contain an individual example's build configuration or expected
serial text.

## LED blink with serial control

`002_LED_Blink_Serial_Control` runs independent LED and serial-input threads.
LED_0 blinks every 500 ms by default; send `led blink start`, `led blink stop`,
`led blink status`, or `help`
through the serial console to control it. See the example's README for build,
programming, and command details.

## Examples and roadmap

| Chapter | Status | Purpose |
| --- | --- | --- |
| [001_Hello_World](001_Hello_World/README.md) | verified | Basic build, flash, and serial output |
| [002_LED_Blink_Serial_Control](002_LED_Blink_Serial_Control/README.md) | verified | Console-controlled LED thread |
| [003_I2C_Sensor_Self_Test](003_I2C_Sensor_Self_Test/README.md) | verified | Checks SHT40, BMI270, and DPS368 availability |
| [004_Environment_Monitor](004_Environment_Monitor/README.md) | verified | Reads temperature, humidity, and pressure |
| [005_IMU_Motion_Monitor](005_IMU_Motion_Monitor/README.md) | verified | Zephyr BMI270 driver reports converted live motion data, with raw-register cross-check |
| [006_Button_LED_Event](006_Button_LED_Event/README.md) | protocol verified | Console and LED GPIO verified; SW1 press remains a manual acceptance check |
| [010_WiFi_SSID_Scan](010_WiFi_SSID_Scan/README.md) | verified | CYW55513 scan prints nearby SSIDs without connecting |
| [011_Bluetooth_LE_Advertiser](011_Bluetooth_LE_Advertiser/README.md) | verified | CYW55513 advertises `PSE84-Playground` and the standard Nordic UART Service over BLE |
| [012_CM55_CM33_IPC_Mailbox](012_CM55_CM33_IPC_Mailbox/README.md) | verified | CM55 made 100 ordered PSA requests through the supported mailbox relay to CM33-NS/TF-M |
| [013_External_Flash_Raw_Access](013_External_Flash_Raw_Access/README.md) | verified | Identify, erase, write, read back, and verify the dedicated external-QSPI test region |
| [014_LittleFS_Storage](014_LittleFS_Storage/README.md) | verified | Mount LittleFS on the dedicated storage partition and persist a settings/log record across reset |
| [015_Device_Settings_CRC](015_Device_Settings_CRC/README.md) | verified | Store, retrieve, and validate a Zephyr settings record with a CRC integrity check |
| [016_Secure_Crypto_Services](016_Secure_Crypto_Services/README.md) | verified | Exercise TF-M PSA random, AES, ECC/ECDH, and signature services from non-secure firmware |
| [017_LVGL_Waveshare_4_3_Display](017_LVGL_Waveshare_4_3_Display/README.md) | hardware verified | Custom CM55 LVGL driver for the Waveshare 4.3-inch RPi DSI display; GFXSS, MIPI-DSI, I2C panel control, and LVGL text frame verified |
| [018_LVGL_Waveshare_Touch_Input](018_LVGL_Waveshare_Touch_Input/README.md) | hardware verified | FT5406 touch input and LVGL button interaction |
| [019_LVGL_Animated_Waveform](019_LVGL_Animated_Waveform/README.md) | hardware verified | BMI270-driven particle fluid with confirmed tilt direction and elapsed-time physics |
| [020_Kalman_Artificial_Horizon](020_Kalman_Artificial_Horizon/README.md) | hardware protocol verified | Upright Kalman flight dashboard, ground zero, relative-heading dial, DPS368 relative altitude/climb, BMM350 high-field warning; calibrated north pending |
| [021_Radar_BGT60TR13C](021_Radar_BGT60TR13C/README.md) | hardware acquisition verified | CM55 radar ID, exact FIFO self-test and raw ADC capture using the Temp/Radar reference; gesture recognition and LCD UI not yet implemented |
| [022_Radar_AI_Gestures](022_Radar_AI_Gestures/README.md) | hardware inference verified | Ports the real Temp/Radar DEEPCRAFT model to CM55/Zephyr; continuous ~200 Hz acquisition, ~20 Hz UART predictions, desktop numerical cross-check; human gesture accuracy pending |
| [023_AES256_GCM_Encrypted_Files](023_AES256_GCM_Encrypted_Files/README.md) | hardware storage verified | AES-256-GCM authenticated LittleFS files, persistent TF-M keys/counters, independent Python crypto checks, tamper/reboot/interrupted-update tests |
| [024_AES128_GCM_Encrypted_Files](024_AES128_GCM_Encrypted_Files/README.md) | hardware storage verified | Separate AES-128-GCM firmware, key and flash partition with the same acceptance suite |
| [025_ChaCha20_Poly1305_Encrypted_Files](025_ChaCha20_Poly1305_Encrypted_Files/README.md) | hardware storage verified | Separate ChaCha20-Poly1305 firmware, key and flash partition with the same acceptance suite |
| [026_Encrypted_Sensor_Logger](026_Encrypted_Sensor_Logger/README.md) | hardware protocol verified | Live BMI270/DPS368 logger with LVGL, persistent TF-M AES-256-GCM, multicore-safe LittleFS writes, tamper rejection and reboot recovery; visual/touch and power-cut checks pending |

Statuses summarize recorded evidence, not a fresh full-board regression on
every documentation update. The [testing guide](docs/TESTING.md) separates host,
protocol and manual acceptance. In particular, 026 passed 11 hardware checks
but its long recording had sampling gaps up to 2.099 seconds; it is not a
deterministic 10 Hz recorder. Chapters 013–015 share destructive scratch storage.
Do not run their tests on data you need to keep.

TF-M PSA Crypto services are demonstrated in 016 and 023–026. This is not a
claim that an independent Secure Enclave hardware crypto engine is used or
that the development provisioning is production-secure.

## Remaining onboard peripherals

| Peripheral | Current position | Next prerequisite |
| --- | --- | --- |
| BMM350 magnetometer | I3C ID/data verified in 020 | Chapter-local PDL backend, chip ID 0x33, 25 Hz compensated XYZ; high-field warning, calibrated heading pending |
| BGT60TR13C radar | Acquisition verified in 021; AI inference in 022 | Continuous input and DEEPCRAFT model numerics verified; next steps are human gesture validation and radar UI |
| PDM microphone | blocked | Provide microphone pin, clock, DMA, and audio-format board configuration |
| Wi-Fi | scan verified | Obtain an approved SSID and credentials before adding an association example |
| Bluetooth LE | NUS loopback verified | Future scope: pairing, custom GATT, and repeatable phone acceptance automation |
| CM55→CM33-NS relay | verified | CM55 completed 100 ordered PSA relay requests to CM33-NS on the board |
| External QSPI flash | raw access in 013; encrypted records in 023–026 | Preserve partition ownership; 026 needs sampling-latency/endurance work |
| LittleFS | console in 014; authenticated files/logs in 023–026 | Physical power-cut campaigns and long-duration/full-capacity tests remain |
| TF-M PSA Crypto | 016 and 023–026 verified within their documented scope | Production provisioning, debug locking and anti-rollback remain separate work |

See [the peripheral roadmap](docs/PERIPHERAL_ROADMAP.md) for test evidence,
known limits, chapter-level acceptance criteria, and the next chapters.
