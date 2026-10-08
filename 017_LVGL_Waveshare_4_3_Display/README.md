# 017 — LVGL text on Waveshare 4.3-inch RPi DSI display

This is a custom Zephyr display driver for the Waveshare 4.3-inch Raspberry Pi
DSI Capacitive Touch Display connected to the TESA AIoT E84 board. It runs on
the CM55 because the PSoC Edge GFXSS/MIPI-DSI display pipeline is owned there.

It uses the panel configuration from the locally installed Infineon
`display-dsi-waveshare-4-3-lcd` example:

- 800 × 480 visible pixels; 832-pixel RGB565 framebuffer stride (128-byte aligned)
- one-lane RGB888 MIPI-DSI, 810 Mbps per lane
- I2C control interface on SCB5 / P17.0 (SCL), P17.1 (SDA), address `0x45`
- two LVGL framebuffers in the non-secure GFX memory region at `0x26200000`

The screen displays:

```
TESA AIoT E84
Waveshare 4.3 RPi DSI
LVGL / Zephyr
```

## Build, flash, and verify

```sh
./build.sh
./flash.sh
python3 test_firmware.py --port /dev/ttyACM0
```

The test passes only after the display driver has initialized GFXSS and the
panel responds with a valid I2C ID (`0xC3` or `0xDE`), then LVGL creates and
submits the text frame. Confirm the visible text on the LCD as the final visual
check; serial alone cannot inspect pixels on the physical panel.
