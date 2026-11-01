# LittleFS storage console

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

Small text-file console on the first 1 MiB of external flash, shared with
013 raw access and 015 settings. **The test begins with `fs format` and erases
the existing filesystem/settings in that region.** The mount configuration also
allows LittleFS automatic formatting on unmountable media: even booting this
example can replace incompatible contents. Use only disposable scratch data.

## Build, flash and test

From this example directory, after the [host setup](../docs/SETUP.md):

```sh
bash build.sh
bash ../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 014_LittleFS_Storage \
  --expect "OK MOUNT=0"
python test_firmware.py --port /dev/ttyACM0
```

## Commands

Paths must include the `/lfs` mount point; this is a trusted developer console,
not a shell or a hardened path-restricted service.

```text
fs format
fs mkdir /lfs/test
fs create /lfs/test/value.txt
fs write /lfs/test/value.txt hello-littlefs
fs read /lfs/test/value.txt
fs ls
fs reboot
fs delete /lfs/test/value.txt
fs rmdir /lfs/test
```

`fs ls` lists root names only; `ls -al` is not implemented. Reads print at most
127 bytes. Writes start at offset zero but do not truncate an existing longer
file, so shorter replacements can leave trailing data. Delete/recreate first
when an exact replacement is needed. This console is not an atomic file editor.

## Verification and limits

The script formats, creates a folder/file, writes text, reboots, waits for
remount, verifies the original text, lists the directory, and deletes its test
file/folder. Success is `PASS: LittleFS lifecycle verified.` Timestamped logs
are under `test-results/`. Software reboot persistence is not physical power-cut
validation. There is no encryption here; use 023–026 for authenticated payloads.
See [storage safety](../docs/STORAGE.md) before changing chapters.
