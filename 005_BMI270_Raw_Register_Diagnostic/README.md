# BMI270 raw-register diagnostic

This firmware talks to the board's BMI270 directly over I2C0 at address `0x68`.
It deliberately disables the normal Zephyr BMI270 sensor driver so a driver
initialization problem cannot hide electrical or bus-level evidence.

The `bmi status` command reads the BMI270 chip ID (`0x00`, expected `0x24`),
configuration/power registers, and the raw accelerometer and gyroscope output
registers. `bmi enable` sets normal-performance 100 Hz acceleration and gyro
operation, then prints the same register snapshot.

Build with `./build.sh`, flash with the repository's
`tools/flash_and_monitor_pse84_ai.sh` helper, then run:

```sh
./test_firmware.py --port /dev/ttyACM0
```

The test writes an auditable serial transcript to `test-results/`.
