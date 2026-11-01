# Button LED Event

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

Uses the PSOC Edge E84 AI Kit's `sw0` and `led0` aliases.  Each physical
button press creates an interrupt event; a separate thread toggles LED0 and
prints its count/status.  The split avoids GPIO work and serial output in the
interrupt callback.

```sh
./build.sh
../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 006_Button_LED_Event \
  --expect "INFO COMMANDS=info;button status;led on;led off;help"
source /home/wasanw/zephyrproject/.venv/bin/activate
python test_firmware.py --port /dev/ttyACM0
```

Commands: `info`, `button status`, `led on`, `led off`, and `help`.

Automated testing verifies the serial protocol and LED commands.  Final
hardware acceptance is manual: press SW1 and confirm LED0 toggles and the
`PRESSES` value increments on the serial console.

Success ends with `PASS: button/LED console protocol verified.` Timestamped
serial logs are stored in `test-results/` (or use `--log PATH`). A protocol PASS
does not substitute for the manual button/visible-LED acceptance above; carrier
switches are not necessarily the SoM's Zephyr `sw0` input.
