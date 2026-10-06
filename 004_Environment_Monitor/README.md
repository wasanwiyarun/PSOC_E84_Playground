# Environment Monitor

Reads the SHT40 temperature/humidity sensor and DPS368 pressure sensor on the
PSOC Edge E84 AI Kit.  Values are returned as integer micro-units to keep the
serial protocol deterministic: temperature in micro-degrees C, humidity in
micro-percent, and pressure in micro-kPa.

```sh
./build.sh
../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 004_Environment_Monitor \
  --expect "INFO COMMANDS=info;env read;help"
source /home/wasanw/zephyrproject/.venv/bin/activate
python test_firmware.py --port /dev/ttyACM0
```

Commands: `info`, `env read`, and `help`.  The test saves a timestamped serial
log in `test-results/` by default.
