# PSOC_E84_Playground

ZEPHYR_PROJECT:

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
LED_0 blinks every 500 ms by default; send `on`, `off`, `status`, or `help`
through the serial console to control it. See the example's README for build,
programming, and command details.

## Examples and roadmap

| Chapter | Status | Purpose |
| --- | --- | --- |
| `001_Hello_World` | verified | Basic build, flash, and serial output |
| `002_LED_Blink_Serial_Control` | verified | Console-controlled LED thread |
| `003_I2C_Sensor_Self_Test` | verified | Checks SHT40, BMI270, and DPS368 availability |
| `004_Environment_Monitor` | verified | Reads temperature, humidity, and pressure |
| `005_IMU_Motion_Monitor` | verified | Zephyr BMI270 driver reports converted live motion data, with raw-register cross-check |
| `006_Button_LED_Event` | protocol verified | Console and LED GPIO verified; SW1 press remains a manual acceptance check |
| `010_WiFi_SSID_Scan` | verified | CYW55513 scan prints nearby SSIDs without connecting |
| `011_Bluetooth_LE_Advertiser` | verified | CYW55513 advertises `PSE84-Playground` and the standard Nordic UART Service over BLE |
| `012_CM55_CM33_IPC_Mailbox` | verified | CM55 made 100 ordered PSA requests through the supported mailbox relay to CM33-NS/TF-M |
| `013_External_Flash_Raw_Access` | planned | Safely identify, erase, write, read back, and verify an explicitly reserved external-QSPI flash region |
| `014_LittleFS_Storage` | planned | Mount LittleFS on the dedicated storage partition and persist a small settings/log record across reset |
| `015_Secure_Enclave_Service` | planned | Call a narrowly scoped secure service from non-secure firmware without exposing secret material |

The next work is deliberately ordered: validate the supported CM55→CM33-NS
mailbox relay first, then prove safe raw flash access before placing a
filesystem on it, then add LittleFS.
The secure-enclave chapter follows a separate secure/non-secure CM33 build
path and must not be combined with the storage or IPC examples until its trust
boundaries are verified.

## Remaining onboard peripherals

| Peripheral | Current position | Next prerequisite |
| --- | --- | --- |
| BMM350 magnetometer | blocked | Enable and pinmux I3C, add the verified board sensor node, then run an ID/read test |
| BGT60TR13C radar | blocked | Add the board SPI/reset/interrupt integration and a compatible Zephyr driver |
| PDM microphone | blocked | Provide microphone pin, clock, DMA, and audio-format board configuration |
| Wi-Fi | scan verified | Obtain an approved SSID and credentials before adding an association example |
| Bluetooth LE | NUS loopback verified | Future scope: pairing, custom GATT, and repeatable phone acceptance automation |
| CM55→CM33-NS relay | verified | CM55 completed 100 ordered PSA relay requests to CM33-NS on the board |
| External QSPI flash | planned | Reserve a non-image partition before any destructive read/write test |
| LittleFS | planned | Complete raw-flash verification and confirm partition ownership |
| Secure Enclave | planned | Define one public secure operation and validate the secure/non-secure image chain |

See [the peripheral roadmap](docs/PERIPHERAL_ROADMAP.md) for test evidence,
known limits, chapter-level acceptance criteria, and the next chapters.
