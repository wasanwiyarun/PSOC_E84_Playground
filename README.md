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

## Example roadmap

| Chapter | Status | Purpose |
| --- | --- | --- |
| `001_Hello_World` | verified | Basic build, flash, and serial output |
| `002_LED_Blink_Serial_Control` | verified | Console-controlled LED thread |
| `003_I2C_Sensor_Self_Test` | verified | Checks SHT40, BMI270, and DPS368 availability |
| `004_Environment_Monitor` | verified | Reads temperature, humidity, and pressure |
| `005_IMU_Motion_Monitor` | blocked | BMI270 initialization works but produces zero-valued motion data |
| `006_Button_LED_Event` | protocol verified | Console and LED GPIO verified; SW1 press remains a manual acceptance check |

See [the peripheral roadmap](docs/PERIPHERAL_ROADMAP.md) for test evidence,
known limits, and the next chapters.
