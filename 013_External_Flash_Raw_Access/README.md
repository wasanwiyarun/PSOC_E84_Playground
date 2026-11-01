# External QSPI raw access

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

Uses the board-defined 1 MiB `storage_partition` at physical external-flash
offset zero. This is shared scratch space with 014 and 015. **`flash write`
destroys the first 256 KiB sector**, then writes a 256-byte test pattern.
There is no cleanup erase: the pattern remains after the test. Do not run it
over a filesystem or settings you need to keep.

## Build, flash and test

From this example directory, after the [host setup](../docs/SETUP.md):

```sh
bash build.sh
bash ../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 013_External_Flash_Raw_Access \
  --expect "INFO COMMANDS=flash info;flash write;flash read;help"
python test_firmware.py --port /dev/ttyACM0
```

| Command | Behavior |
| --- | --- |
| `flash info` | Device name, storage offset/size and assumed erase block size |
| `flash write` | Erase the first sector and write the deterministic pattern |
| `flash read` | Read the pattern and compare bytes plus CRC32 |
| `help` | Command list |

`flash read` requires a successful `flash write` in the same boot; its expected
pattern state is RAM-only. `flash info` is not a JEDEC silicon-ID query.
Neither `flash start` nor `flash get id` is implemented.

The Python verifier checks offset 0, length 1 MiB and 256 KiB erase geometry,
then requires `OK FLASH WRITE OFFSET=0` and `PASS FLASH READ OFFSET=0`.
It saves a timestamped transcript under `test-results/`. This verifies a small
scratch-sector round trip, not flash endurance, all addresses or power-fail
safety. See [flash ownership](../docs/STORAGE.md) before programming/testing.
