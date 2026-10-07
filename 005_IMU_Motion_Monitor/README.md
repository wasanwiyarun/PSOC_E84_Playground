# BMI270 motion monitor

This example uses Zephyr's normal BMI270 sensor driver for converted
accelerometer and gyroscope readings. At startup it enables both channels at
100 Hz and reports the return codes. `imu read` prints all axes in micro-SI
units (`m/s²` and `rad/s`).

`bmi raw` is a read-only diagnostic cross-check: it reads the BMI270 chip ID,
power registers, and raw data over I2C0 at address `0x68`. It does not bypass
or reconfigure the normal driver.

Build with `./build.sh`, flash with the repository's
`tools/flash_and_monitor_pse84_ai.sh` helper, then run:

```sh
./test_firmware.py --port /dev/ttyACM0
```

The test writes an auditable serial transcript to `test-results/` and fails if
the driver reports a zero gravity-axis value or implausibly zero raw data.
