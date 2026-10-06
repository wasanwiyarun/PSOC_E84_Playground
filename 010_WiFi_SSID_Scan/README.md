# Wi-Fi SSID Scan

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
