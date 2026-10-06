# PSOC Edge84 LED Blink with Serial Control

This Zephyr example runs two threads on the PSOC Edge84 AI Kit M55 core:

- The LED thread toggles board alias `led0` (LED_0) every 500 ms.
- The serial thread reads line-oriented commands from the KitProg3 console.

Blinking is enabled by default. Build the Sysbuild image from this directory:

```sh
./build.sh
```

Flash the CM33 companion, flash the M55 application, reset the board, and
wait for its startup message:

```sh
../tools/flash_and_monitor_pse84_ai.sh \
  --build-dir build-pse84-ai \
  --companion-domain enable_cm55 \
  --app-domain 002_LED_Blink_Serial_Control \
  --expect "INFO COMMANDS=info;led blink start;led blink stop;led blink status;led blink period <ms>;help"
```

The KitProg3 console is `/dev/ttyACM0` at 115200 8N1. Send a complete command
followed by Enter using a serial terminal:

```text
info              Print board, application, and firmware version information
led blink start   Enable 500 ms LED_0 blinking
led blink stop    Stop blinking and turn LED_0 off
led blink status  Print the current blink state and period
led blink period 250  Set the blink period from 50 to 10000 ms
help              Print the command list
```

## Automated serial test

After flashing the board, run the example-specific serial test. It sends 20
alternating `led blink stop` and `led blink start` commands by default,
verifies every response, tests `info`, `help`, invalid commands, and blink
period configuration, then restores the default 500 ms period and leaves
blinking enabled.

```sh
source /home/wasanw/zephyrproject/.venv/bin/activate
python test_serial_control.py --port /dev/ttyACM0
```

Use `--toggles 40` for a longer run. This verifies the serial command protocol;
confirming that LED_0 physically turns on and off still requires visual
inspection (or external optical/electrical measurement).

Use `--period-ms 1000` to make the automated test exercise a different valid
blink period.

Each run writes a timestamped log below `test-results/`. Use `--log PATH` to
choose a specific log-file path.
