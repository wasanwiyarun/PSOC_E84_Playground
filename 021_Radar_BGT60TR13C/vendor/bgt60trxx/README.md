# Infineon BGT60TRxx Sensor Driver

Unmodified portable driver files copied from the user's local
`Temp/mtb_shared/sensor-xensiv-bgt60trxx/release-v2.0.0` checkout.

- Upstream: https://github.com/Infineon/sensor-xensiv-bgt60trxx
- Commit: `78099697159a2a736504ef5bd1d33820b21f4e67`
- License: Apache-2.0, retained in `LICENSE` and file headers.

Chapter-local platform callbacks are implemented separately in `src/radar.c`.
The platform performs the hard reset explicitly before calling the driver init,
matching the reference's Edge adapter. No global Zephyr or Temp files are changed.
