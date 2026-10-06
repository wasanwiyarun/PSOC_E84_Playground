# PSOC Edge E84 AI Kit Peripheral Roadmap

Every completed chapter is built with the local Zephyr workspace, programmed
through KitProg3, and verified through `/dev/ttyACM0`.  Firmware test scripts
write timestamped serial logs that are intentionally ignored by Git.

| Chapter | Peripheral | State | Result |
| --- | --- | --- | --- |
| 001 | Console UART | Verified | Hello-world output received. |
| 002 | LED0 + console | Verified | Blink control, period changes, and repeated serial commands tested. |
| 003 | I²C SHT40, BMI270, DPS368 | Verified | All expected devices responded at `0x44`, `0x68`, and `0x77`. |
| 004 | SHT40 + DPS368 | Verified | Live temperature, humidity, and pressure measurements received. |
| 005 | BMI270 accelerometer + gyroscope | Blocked | Device initializes and fetch calls succeed, but all six axes remain zero even after explicitly enabling both 100 Hz data paths. |
| 006 | Button + LED event | Protocol verified | Firmware booted with GPIO setup; serial LED commands passed. A physical SW1 press is still required to verify the interrupt event end-to-end. |
| 007 | BMM350 magnetometer | Blocked | Zephyr contains a BMM350 driver, but this board DTS has no enabled I3C controller, pinctrl, or BMM350 device node. |
| 008 | BGT60TR13C radar | Blocked | No matching BGT60TR13C driver exists in this Zephyr tree and the board has no radar SPI device node. |
| 009 | PDM microphone | Blocked | An Infineon DMIC driver exists, but all E84 PDM controller channels and board microphone pin configuration are disabled. |
| 010 | Wi-Fi | Deferred | The CYW55513 SDIO hardware is declared; joining an access point needs approved network credentials. |

## BMI270 follow-up

This is deliberately not merged as a working application.  The local Zephyr
BMI270 driver recognizes the board device at I²C address `0x68` and its fetch
operation succeeds, but returned acceleration and gyroscope registers are all
zero.  The test also attempted `SENSOR_ATTR_SAMPLING_FREQUENCY` at 100 Hz for
both `SENSOR_CHAN_ACCEL_XYZ` and `SENSOR_CHAN_GYRO_XYZ`, followed by a sample
delay; readings stayed zero.  Investigate the E84 board's sensor power/reset
wiring or a Zephyr BMI270 board configuration update before promoting that
chapter.

## Capability review: remaining peripherals

The remaining onboard peripherals are not skipped because they are
uninteresting; they need board-support work before an application can safely
claim to use them.

- **BMM350:** The local tree has I3C BMM350 bindings and a driver. The E84
  board must first enable and pinmux `i3c0`, add the sensor node with its
  verified address, and establish any required pull-up/power-control setup.
- **BGT60TR13C:** There is no BGT60 radar driver or board SPI node in this
  Zephyr revision. A dedicated driver and validated reset/interrupt/SPI device
  tree integration are prerequisites.
- **PDM microphone:** The SoC and Infineon DMIC driver are present, but the
  E84 board leaves `dmic0` and every channel disabled. A board overlay needs
  the actual microphone pins, clock, channel choice, and DMA/audio format.
- **Wi-Fi:** The E84 M55 DTS declares the CYW55513 SDIO and control GPIOs.
  A connection test is intentionally deferred until an approved SSID and
  credential are supplied; the project should not embed or guess them.
