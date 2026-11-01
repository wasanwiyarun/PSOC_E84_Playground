# Tests, evidence and acceptance limits

[Repository](../README.md) · [Setup](SETUP.md) · [Storage safety](STORAGE.md)

Run chapter commands from that chapter directory unless its README says
otherwise. Activate the documented Python environment first. Hardware scripts
generally expect already-programmed firmware; 023–026 can program with `--flash`.
Do not run every hardware script in a loop: some format shared storage or
require a person to move/tap the board.

The table summarizes prior recorded results, not a fresh hardware run during
this documentation audit. Early chapter logs are local ignored artifacts and
have no retained immutable image fingerprints; reproducibility is limited.
A script's success marker is a criterion, not proof of success on your board.

## All 23 examples

Tests below are Python files; use `python FILE ...`. 001 uses the shared helper.

| Chapter / README | Test entry point | Evidence and remaining acceptance |
| --- | --- | --- |
| [001 Hello world](../001_Hello_World/README.md) | shared flash/monitor helper | Recorded UART greeting; smoke test only |
| [002 LED control](../002_LED_Blink_Serial_Control/README.md) | `test_serial_control.py --port /dev/ttyACM0` | Command/period protocol; observe physical LED separately |
| [003 I2C sensors](../003_I2C_Sensor_Self_Test/README.md) | `test_firmware.py --port /dev/ttyACM0` | Device scan/read readiness; not calibration |
| [004 Environment](../004_Environment_Monitor/README.md) | `test_firmware.py --port /dev/ttyACM0` | Live sensor values; not accuracy certification |
| [005 IMU](../005_IMU_Motion_Monitor/README.md) | `test_firmware.py --port /dev/ttyACM0` | Converted/raw nonzero data; mounting/attitude not established |
| [006 Button](../006_Button_LED_Event/README.md) | `test_firmware.py --port /dev/ttyACM0` | Protocol verified; physical SW1 interrupt/toggle manual |
| [010 Wi-Fi](../010_WiFi_SSID_Scan/README.md) | `test_firmware.py --port /dev/ttyACM0` | Scan only; scrub nearby SSIDs before sharing logs |
| [011 BLE NUS](../011_Bluetooth_LE_Advertiser/README.md) | `test_firmware.py --port /dev/ttyACM0` | Advertising protocol; phone notification/loopback separate |
| [012 PSA relay](../012_CM55_CM33_IPC_Mailbox/README.md) | `test_firmware.py --port /dev/ttyACM0` | 100 ordered calls; absent-core timeout not tested/safe |
| [013 Raw flash](../013_External_Flash_Raw_Access/README.md) | `test_firmware.py --port /dev/ttyACM0` | Destructive write/read/CRC; no cleanup erase or JEDEC-ID command |
| [014 LittleFS](../014_LittleFS_Storage/README.md) | `test_firmware.py --port /dev/ttyACM0` | Destructive format, lifecycle and software-reboot persistence |
| [015 Settings](../015_Device_Settings_CRC/README.md) | `test_firmware.py --port /dev/ttyACM0` | Destructive reset; FCB CRC configuration, not corruption injection |
| [016 PSA crypto](../016_Secure_Crypto_Services/README.md) | `test_firmware.py --port /dev/ttyACM0` | AES-GCM/ECDH/ECDSA behavior; development keys, no production claim |
| [017 Display](../017_LVGL_Waveshare_4_3_Display/README.md) | `test_firmware.py` | Pipeline/CRC/token checks, historical user text confirmation |
| [018 Touch](../018_LVGL_Waveshare_Touch_Input/README.md) | `test_firmware.py`; `--wait-for-touch` | Readiness vs physical tap are distinct; historical user touch photo |
| [019 Fluid](../019_LVGL_Animated_Waveform/README.md) | `test_fluid.py`; `test_firmware.py --case all` | Host physics, injected gravity, dated user direction/photo confirmation |
| [020 Flight dashboard](../020_Kalman_Artificial_Horizon/README.md) | `test_attitude.py`, `test_flight_math.py`, `test_bus_recovery.py`; `test_firmware.py`, `test_compass.py` | Host math/hardware protocol/photo; north unavailable, not for flight |
| [021 Radar](../021_Radar_BGT60TR13C/README.md) | `test_firmware.py --case all` | Exact FIFO self-test and ADC capture; not target/gesture detection |
| [022 Radar AI](../022_Radar_AI_Gestures/README.md) | `-m unittest -v test_host.py`; `test_firmware.py --case all --oracle` | Numerical port/live acquisition; local model required, human accuracy pending |
| [023 AES-256](../023_AES256_GCM_Encrypted_Files/README.md) | `test_firmware.py --flash --init-blank` | 12 hardware groups; [shared result](../common/encrypted_files/VERIFICATION.md) |
| [024 AES-128](../024_AES128_GCM_Encrypted_Files/README.md) | `test_firmware.py --flash --init-blank` | Same suite, separate partition/key; shared result |
| [025 ChaCha20](../025_ChaCha20_Poly1305_Encrypted_Files/README.md) | `test_firmware.py --flash --init-blank` | Same suite, separate partition/key; shared result |
| [026 Logger](../026_Encrypted_Sensor_Logger/README.md) | `test_firmware.py --flash --init-blank --soak-samples 1152` | [11 groups, 1,155 samples](../026_Encrypted_Sensor_Logger/VERIFICATION.md); 2.099 s max gap, manual LCD/touch and power cuts pending |

007–009 are roadmap slots: magnetometer in 020, radar in 021/022, microphone
pending. They do not have runnable chapter folders.

## Safe host-only checks

From the repository root; none of these programs the board:

```sh
python tools/check_docs.py
python 019_LVGL_Animated_Waveform/test_fluid.py
python 020_Kalman_Artificial_Horizon/test_attitude.py
python 020_Kalman_Artificial_Horizon/test_flight_math.py
python 020_Kalman_Artificial_Horizon/test_bus_recovery.py
python -m unittest discover -s 022_Radar_AI_Gestures -p test_host.py -v
python -m unittest discover -s common/encrypted_files -p 'test_*.py' -v
```

019/020 are standalone C-engine harnesses, **not unittest modules**. The
bus-recovery model intentionally prints two `FAIL SENSOR` diagnostics for
stuck-line negative cases; its final PASS and exit code identify suite success.
Some tests write ignored preview artifacts. The 022 host test checks import
guards/transport, not physical inference or the desktop model oracle.

## Evidence policy

Record command, date, commit/image hash, image pairing, board/port, exact
results and limits. Keep failed logs locally. Commit scrubbed compact reports,
not private/large raw data. Do not silently overwrite published snapshots.

- Build/static: compiles/configures, no hardware operation demonstrated.
- Host: algorithms/parsers/oracles run on the computer.
- Hardware protocol: fresh board responses/controller state/data checked.
- Manual/physical: observed pixels, taps, direction or gestures, attributed to
  the person/photo/video that confirmed them.
- Power-fail/endurance/security: separate campaigns, not inferred from reboot,
  CRC, a crypto vector or a screenshot.

The 2026-11-01 documentation audit did not reflash all chapters or run destructive
013–015 tests. It retained 026 on the connected board.
