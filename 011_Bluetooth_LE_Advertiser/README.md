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
To disable it for a firmware build, replace the enabled line in `prj.conf`
with the following, then rebuild:

```conf
# CONFIG_APP_NUS_LOOPBACK is not set
```

The connected client must enable TX notifications. `info` reports whether the
compiled image has loopback enabled, and the serial console logs each received
message and echo result.

## nRF Connect loopback test

1. Flash the image, then confirm that the serial output includes:

   ```text
   OK BT ADVERTISING=ON NAME=PSE84-Playground SERVICE=NUS
   ```

2. In nRF Connect, scan for and connect to `PSE84-Playground`.
3. Confirm that **Nordic UART Service** (`6E400001-...`) is listed.
4. On **UART TX Characteristic** (`6E400003-...`), enable notifications using
   the down-arrow control. The app should report that notifications are enabled.
5. Write a short value such as `hello bro` to **UART RX Characteristic**
   (`6E400002-...`).
6. Confirm that the exact value appears as the latest TX notification. In nRF
   Connect, both **Last Write** under RX and **Last Read** under TX should show
   `hello bro`.

The board serial console records the same exchange:

```text
OK NUS NOTIFY=ON
OK NUS RX LEN=9 DATA=hello bro
OK NUS LOOPBACK_TX LEN=9
```

If a write reaches the board before notification enablement finishes, it is
queued and automatically sent once notifications turn on. This makes the test
reliable even when nRF Connect applies the CCC setting slightly after the
first write.

## Serial-to-BLE test

With the nRF Connect TX notifications enabled, send this through the board's
serial console:

```text
ble send hello
```

nRF Connect receives `hello` as a UART TX notification. This tests the
opposite direction without using RX loopback.

Build with `./build.sh`; use the repository flash helper; then run
`./test_firmware.py --port /dev/ttyACM0`. The automated test verifies the
controller initialization, NUS UUID advertising, and advertise start/stop
protocol. The nRF Connect steps above are the final over-the-air NUS check.
