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
| 006 | Button + LED event | Planned | Exercise the board `sw0` and `led0` aliases. |
| 007 | BMM350 magnetometer | Planned | Requires an I3C device-tree/driver integration review. |
| 008 | BGT60TR13C radar | Planned | Requires SPI device-tree/driver integration. |
| 009 | PDM microphone | Planned | Requires audio clock and PDM configuration validation. |
| 010 | Wi-Fi | Deferred | External connection needs an approved network and credentials. |

## BMI270 follow-up

This is deliberately not merged as a working application.  The local Zephyr
BMI270 driver recognizes the board device at I²C address `0x68` and its fetch
operation succeeds, but returned acceleration and gyroscope registers are all
zero.  The test also attempted `SENSOR_ATTR_SAMPLING_FREQUENCY` at 100 Hz for
both `SENSOR_CHAN_ACCEL_XYZ` and `SENSOR_CHAN_GYRO_XYZ`, followed by a sample
delay; readings stayed zero.  Investigate the E84 board's sensor power/reset
wiring or a Zephyr BMI270 board configuration update before promoting that
chapter.
