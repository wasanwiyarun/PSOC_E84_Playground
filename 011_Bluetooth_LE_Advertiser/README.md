# Bluetooth LE advertiser

This example runs Zephyr's Bluetooth host stack with the E84 AI kit's CYW55513
controller over its on-board H:4 UART. It starts a connectable BLE advertiser
named `PSE84-Playground`.

Commands:

- `info` — application version and controller identity address
- `bt status` — advertising state
- `bt advertise start` / `bt advertise stop` — control advertising

Build with `./build.sh`; use the repository flash helper; then run
`./test_firmware.py --port /dev/ttyACM0`. The automated test verifies the
controller initialization and advertise start/stop protocol. A phone Bluetooth
scanner seeing `PSE84-Playground` is the final RF acceptance check.
