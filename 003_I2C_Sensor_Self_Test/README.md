# I2C Sensor Self-Test

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
