# BMI270 motion monitor

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

This example uses Zephyr's normal BMI270 sensor driver for converted
accelerometer and gyroscope readings. At startup it enables both channels at
100 Hz and reports the return codes. `imu read` prints all axes in micro-SI
units (`m/s²` and `rad/s`).

`bmi raw` is a read-only diagnostic cross-check: it reads the BMI270 chip ID,
power registers, and raw data over I2C0 at address `0x68`. It does not bypass
or reconfigure the normal driver.

From this example directory, in the configured Python environment:

```sh
bash build.sh
bash ../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 005_IMU_Motion_Monitor \
  --expect "INFO COMMANDS="
python test_firmware.py --port /dev/ttyACM0
```

The test writes an auditable serial transcript to `test-results/` and fails if
the driver reports a zero gravity-axis value or implausibly zero raw data.
Keep the board still and approximately flat for this check. Motion-data
availability does not validate all mounting signs, calibration or attitude.
