# 018 — FT5406 touch input on the Waveshare 4.3-inch DSI display

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

This chapter extends the verified Chapter 017 display pipeline with the
Waveshare panel's FT5406 capacitive-touch controller. The controller shares
SCB5 I2C with the panel-control IC but uses address `0x38`.

The display shows a large blue **TOUCH ME** button. A tap is converted from the
panel's rotated coordinate system, submitted to LVGL as a pointer input, and
updates both the on-screen counter and serial log.

## Build and flash

```sh
./build.sh
./flash.sh
python3 test_firmware.py
python3 test_firmware.py --wait-for-touch
```

The basic test verifies I2C communication and polling. The second command
waits for a real tap, checks the fresh token in the firmware response, and is
the physical-input test. Build output is intentionally reused from Chapter 017
only for the proven GFXSS display source and secure CM33 clock-startup module;
this chapter owns the touch code, screen, commands, and tests.

## Console and acceptance

At 115200 baud use `touch info TOKEN` for fresh status and `touch arm TOKEN`
before a physical tap; replace `TOKEN` with a unique short identifier. The
Python runner generates tokens automatically and saves timestamped
`test-results/ft5406-*.log` files. The readiness-only test ends with
`PASS: FT5406 responds over I2C and touch polling is active`.

The user previously confirmed visible button/count updates on this hardware.
For a new installation, run `--wait-for-touch` and verify both the serial event
and visible counter. I2C/polling success alone cannot prove LCD output, correct
touch coordinates or that the intended button was pressed. See Chapter 017
for the display startup/power requirements; do not rewire the panel while powered.
