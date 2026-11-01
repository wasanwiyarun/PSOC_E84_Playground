# Wi-Fi SSID Scan

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

Uses the PSOC Edge E84 AI Kit's CYW55513 Wi-Fi interface to scan nearby access
points. It does not connect to any network and does not store credentials.

```sh
./build.sh
../tools/flash_and_monitor_pse84_ai.sh --build-dir build-pse84-ai \
  --companion-domain enable_cm55 --app-domain 010_WiFi_SSID_Scan \
  --expect "INFO COMMANDS=info;wifi scan;wifi status;help"
source /home/wasanw/zephyrproject/.venv/bin/activate
python test_firmware.py --port /dev/ttyACM0
```

Commands: `info`, `wifi scan`, `wifi status`, and `help`. Scan results are
printed as `WIFI SSID=<name> RSSI_DBM=<value> CHANNEL=<value>`, followed by
`OK WIFI_SCAN_DONE`. The test saves a timestamped serial log in `test-results/`.

The scan result depends on nearby networks, antenna and RF conditions. A scan
completion does not demonstrate association, Internet access or throughput.
Review logs for nearby SSIDs before publishing them; leave the kit's antenna
connection intact during testing.
