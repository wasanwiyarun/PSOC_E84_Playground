# Bosch BMM350 SensorAPI

Source: https://github.com/boschsensortec/BMM350_SensorAPI

Pinned commit: `3daf377ccaf589319c0d41af192105e9275987a2`.
Files: `bmm350.c`, `bmm350.h`, `bmm350_defs.h`, `LICENSE`.
See the retained BSD-3-Clause license and original file headers.

Local modification: `bmm350_init()` does not issue its soft-reset command.
I3C dynamic addressing has already been configured by `src/magnetometer.c`;
soft-reset would return the sensor to I2C. This follows Infineon's documented
workaround: https://github.com/Infineon/sensor-orientation-bmm350
The backend explicitly resets the sensor and restores its I3C address before
calling the patched init, including on warm MCU restarts. Simply omitting
all resets is insufficient: OTP can remain powered off from the previous run.
The factory OTP loading and magnetic reset remain enabled. This copy is
specific to the I3C application, not a replacement for the upstream API.

Bus callbacks enforce an initialization/read operation deadline, including
the API's OTP polling loop. No global SDK/library files are patched.
