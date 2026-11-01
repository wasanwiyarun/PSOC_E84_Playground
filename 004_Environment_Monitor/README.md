# Environment Monitor

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

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

Success ends with `PASS: environment monitor verified.` This verifies live
read responses, not metrological accuracy. Board heating and nearby airflow
can affect readings; this chapter does not estimate altitude or calibrate
against a reference instrument. No additional sensor wiring is required for
the tested kit's onboard SHT40 and DPS368.
