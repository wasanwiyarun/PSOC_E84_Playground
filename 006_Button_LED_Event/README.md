# Button LED Event

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
