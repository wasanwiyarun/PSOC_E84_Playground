# I2C Sensor Self-Test

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

Verifies the three sensor devices already described by the PSOC Edge84 AI Kit
Zephyr board definition: SHT40 (`0x44`), BMI270 (`0x68`), and DPS368 (`0x77`).

```sh
./build.sh
../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 003_I2C_Sensor_Self_Test \
  --expect "INFO COMMANDS=info;sensor scan;sensor status;help"
source /home/wasanw/zephyrproject/.venv/bin/activate
python test_firmware.py --port /dev/ttyACM0
```

Commands: `info`, `sensor scan`, `sensor status`, and `help`. The test saves a
timestamped serial log in `test-results/` by default.

Success ends with `PASS: I2C sensor self-test verified.` This checks the
firmware's scan/status protocol and readiness of the three configured sensors;
it does not calibrate them or validate BMM350 I3C (see Chapter 020). No external
I2C module or resistor modification is needed on the tested kit configuration.
