# 015 — Device Settings API with CRC-protected FCB

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

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

`settings factory-reset` erases this example's FCB storage partition and reboots. It
also removes Chapter 014 LittleFS contents because both standalone examples
intentionally use the board's shared `storage_partition`.

Build, program, then test:

```bash
./build.sh
west flash -d build-pse84-ai
python3 test_firmware.py --port /dev/ttyACM0
```

Run these commands from this example directory after activating the Python
environment in the [setup guide](../docs/SETUP.md). The automated test performs
`settings factory-reset` before saving values: it destroys existing settings
and any 014 filesystem in the shared region. Successful acceptance prints
`PASS: Settings API persistence and FCB entry CRC configuration verified.`
The script verifies backend configuration and reboot persistence; it does not
inject corrupt FCB records or prove power-loss recovery. CRC is error detection,
not encryption or authentication. Logs are saved under `test-results/`.
