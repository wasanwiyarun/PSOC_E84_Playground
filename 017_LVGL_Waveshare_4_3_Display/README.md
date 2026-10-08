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

The intended screen displays:

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

The build includes a chapter-local `cm33_secure` module in the secure CM33
startup image. Before releasing CM55, it applies the board's generated
external-crystal/PLL clock configuration from the Zephyr Infineon HAL. CM55
also registers the 17.2032 MHz crystal frequency in its own PDL state before
peripheral initialization. There is no build dependency on `Temp/display_test`
or `Temp/diplay_test`.

On the attached board, internal-oscillator startup reproduced DSI DPI payload
underflow (`INT_ST1=0x80000`). Using the reference crystal-based clock setup
eliminated that error. The peripheral clock reports 99,999,999 Hz because of
fractional PLL rounding; the test also accepts exactly 100,000,000 Hz.

The Python test uses fresh tokens, not buffered boot messages. It checks panel
ID, clock, PHY lock, controller errors, three changed LVGL frames, framebuffer
CRCs, both buffers, completed scan-outs, and restoration of the text screen.
Logs are saved in `test-results/`. A successful software initialization prints
`OK DISPLAY INITIALIZATION_SUCCESS lvgl_frame_presented=1`; an unhealthy
pipeline reports `FAIL DISPLAY STATUS` instead.

UART commands (each requires a caller-selected token):

```text
display info TOKEN
display test TOKEN
display demo TOKEN
```

`info` reports hardware health; `test` renders the token; `demo` restores the
text above. Confirm the visible text on the LCD separately: even a passing
hardware-pipeline test cannot inspect the physical pixels or backlight.
