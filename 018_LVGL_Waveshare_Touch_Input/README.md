# 018 — FT5406 touch input on the Waveshare 4.3-inch DSI display

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
