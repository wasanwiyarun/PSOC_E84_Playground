# 015 — Device Settings API with CRC-protected FCB

This example uses Zephyr's `settings` API for device configuration. The FCB
(Flash Circular Buffer) backend stores settings in the board
`storage_partition` (the first 1 MiB of external flash). FCB is enabled with
CRC support: every FCB entry has an integrity CRC which is verified while the
Settings service reloads entries.

Commands (115200 8N1):

```text
settings info
settings show
settings set interval_ms 1500
settings set enabled 1
settings set name E84-CRC
settings reboot
settings factory-reset
```

`settings factory-reset` erases this example's NVS partition and reboots. It
also removes Chapter 014 LittleFS contents because both standalone examples
intentionally use the board's shared `storage_partition`.

Build, program, then test:

```bash
./build.sh
west flash -d build-pse84-ai
python3 test_firmware.py --port /dev/ttyACM0
```
