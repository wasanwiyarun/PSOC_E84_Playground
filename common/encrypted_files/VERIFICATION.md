# Hardware verification — 27 October 2026

Board: connected PSOC Edge E84 / KitProg3 19160998012D2400.
Port: /dev/ttyACM0, 115200 baud. All three images were physically programmed
and tested by the Python runner, not merely compiled.

| Example | Algorithm | Hardware test groups | Evidence directory |
|---|---|---:|---|
| 023 | AES-256-GCM | 12/12 PASS | [20261027-214427](../../023_AES256_GCM_Encrypted_Files/test-results/20261027-214427/results.json) |
| 024 | AES-128-GCM | 12/12 PASS | [20261027-214534](../../024_AES128_GCM_Encrypted_Files/test-results/20261027-214534/results.json) |
| 025 | ChaCha20-Poly1305 | 12/12 PASS | [20261027-214626](../../025_ChaCha20_Poly1305_Encrypted_Files/test-results/20261027-214626/results.json) |

Each evidence directory contains serial.log, flash.log and results.json. Raw
test results are local ignored artifacts; this summary is intended for Git.
Host format/oracle tests: **4/4 PASS**.

After testing 024 and 025, 023 was reflashed and all 12 groups passed again:
[final AES-256 rerun](../../023_AES256_GCM_Encrypted_Files/test-results/20261027-214736/results.json).
The board is left running **023 / AES-256-GCM**, ready for UART commands.

## Checks executed on each firmware

1. Physical-board programming.
2. Algorithm identity, dedicated partition and persistent key readiness.
3. LittleFS mount (blank-only initialization on first use).
4. Encryption vector compared with independent Python cryptography; modified tag rejected.
5. Binary payloads of 0, 1, 15, 16, 17, 255, 256 and 1024 bytes; ordered nonces.
6. Rewriting identical plaintext produces a new nonce and different ciphertext.
7. Wrong random key rejected; original file remains readable.
8. Header, nonce, ciphertext and tag tampering rejected; restored files readable.
9. File/key/nonce persistence across software reset.
10. Reset after partial temporary-file write and after synced temporary-file write:
    previous file survives and interrupted nonces are not reused.
11. Malformed commands, path traversal, oversized payload/name/line rejected;
    existing file unaffected and parser recovers.
12. Directory listing, delete and missing-file error.

## Firmware fingerprints

SHA-256 of the flashed tfm_merged.hex files:

```text
023 346d66c7ddede4076213481717d8e20c8e02f1f19a63eccca56a4a3f5a2546a1
024 dc5ec1310b1af2931b4971aec5dec17235ba6f3c2b032637a57f9e6fe29eaaf2
025 e67c54df566f947474fc116fcdcb2dbd0dad904be361e7c67bdd41f9bfc63be5
```

## Timing observations, not a full benchmark

For the 1024-byte write in these runs the PSA encryption call measured 8 ms
(AES-256), 9 ms (AES-128), and 1 ms (ChaCha20-Poly1305). These are single,
millisecond-resolution observations, not statistically controlled throughput
measurements or evidence that AES-256 is faster than AES-128.

The secure build uses software crypto, not the hardware AES accelerator.
End-to-end writes can take roughly 1.1–1.3 seconds because they also include NOR
erase/filesystem work, secure nonce persistence and UART transfer. The results
JSON keeps those timings separate from crypto_ms.

## Flash safety and limitations

The initial read-only scan found occupied areas at 8, 9, 10, 12 and 13 MiB and
erased 1 MiB areas at 11, 14 and 15 MiB. Only the erased areas were initialized
for these examples. Existing storage@0 and occupied data areas were not formatted.
Tests delete only their uniquely named test files; keys, counters and filesystems
remain for reuse. Firmware slots were updated as part of programming.

Physical power removal, flash wear endurance, anti-replay protection, secure
production provisioning and resistance to an attached debugger are **not**
verified by these tests. See [security limitations](README.md).
