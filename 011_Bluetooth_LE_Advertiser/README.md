# Bluetooth LE advertiser and Nordic UART Service

This example runs Zephyr's Bluetooth host stack with the E84 AI kit's CYW55513
controller over its on-board H:4 UART. It starts a connectable BLE advertiser
named `PSE84-Playground` and exposes the standard Nordic UART Service (NUS),
compatible with the nRF Connect mobile app and Nordic nRF52/nRF53 examples.

The advertised service UUID is `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
Its RX (write) characteristic is `...0002`; its TX (notify) characteristic is
`...0003`. The 128-bit NUS UUID is in the advertising packet. The device name
is in the scan response because both cannot fit in a legacy 31-byte advertising
packet together.

Commands:

- `info` — application version and controller identity address
- `bt status` — advertising state
- `bt advertise start` / `bt advertise stop` — control advertising
- `ble send <text>` — send a NUS notification to a connected client that has
  enabled TX notifications

## Optional NUS loopback

Loopback is enabled by default. The application echoes every value written to
the NUS RX characteristic back through the NUS TX notification characteristic.
To disable it for a firmware build, remove or comment out this line in
`prj.conf`, then rebuild:

```conf
# CONFIG_APP_NUS_LOOPBACK is not set
```

The connected client must enable TX notifications. `info` reports whether the
compiled image has loopback enabled, and the serial console logs each received
message and echo result.

## nRF Connect acceptance check

1. Scan for `PSE84-Playground`, then connect.
2. Confirm the Nordic UART Service UUID above is listed.
3. Enable notifications on characteristic `6E400003-...`.
4. Write text to `6E400002-...`; the board serial console prints
   `OK NUS RX ...`.
5. Send `ble send hello` through the board serial console; nRF Connect receives
   `hello` as a notification.

Build with `./build.sh`; use the repository flash helper; then run
`./test_firmware.py --port /dev/ttyACM0`. The automated test verifies the
controller initialization, NUS UUID advertising, and advertise start/stop
protocol. The nRF Connect steps above are the final over-the-air NUS check.
