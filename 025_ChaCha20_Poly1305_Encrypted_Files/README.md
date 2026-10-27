# 025 — CHACHA20_POLY1305 encrypted files

CM33 non-secure Zephyr application using **TF-M PSA Crypto** and a separate
LittleFS external-flash partition. This example selects only CHACHA20_POLY1305
(256-bit key); the common implementation is shared with the other two examples.

## Build, program, test

Close minicom or other users of the serial port first. From the repository root:

```sh
bash 025_ChaCha20_Poly1305_Encrypted_Files/build.sh
/home/wasanw/zephyrproject/.venv/bin/python 025_ChaCha20_Poly1305_Encrypted_Files/test_firmware.py --port /dev/ttyACM0 --flash --init-blank
```

The test script programs the physical board, resets it, and exercises the UART
commands. It exits nonzero on any failed check and saves serial output, flash
output, firmware SHA-256, timings and results.json under test-results/.

**--init-blank is not permission to erase existing data.** Firmware first scans
the complete 1 MiB partition and only formats it if every byte is erased (0xff).
An existing valid filesystem is mounted without formatting. An occupied or
corrupt filesystem fails closed; do not erase it to make the test pass.

| Resource | Assignment |
|---|---|
| External-flash physical offset | 0xf00000 |
| Partition length | 0x100000 (1 MiB / four 256 KiB erase blocks) |
| Persistent PSA key ID | 0x2303 |
| Protected-storage nonce-counter UID | 0x45465303 |
| Algorithm format ID | 3 |
| Maximum file payload | 1024 bytes |
| UART | 115200, 8-N-1 |

These areas were selected after a read-only board scan, leaving occupied data
at 8, 9, 10, 12 and 13 MiB intact. Do not assume these locations are free on a
different board: the full blank check remains mandatory.

## Try it

```text
efs info
efs init
efs kat
efs write hello 48656c6c6f
efs read hello
efs raw hello
efs list
efs reboot
efs read hello
efs delete hello
```

The plaintext above is `Hello`. Use `-` for an empty payload. File names use
letters, digits, underscores or hyphens, maximum 24 characters. The read command
returns plaintext as hex **only after authentication succeeds**.

Additional test commands, key/nonce design, file format and security limitations:
[shared implementation notes](../common/encrypted_files/README.md).

