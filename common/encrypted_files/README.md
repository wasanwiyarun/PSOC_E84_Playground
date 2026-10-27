# Authenticated files: shared implementation for 023–025

Three separately buildable and flashable examples:

- 023: AES-256-GCM, format ID 1.
- 024: AES-128-GCM, format ID 2.
- 025: ChaCha20-Poly1305, format ID 3.

Each has its own partition, persistent key and nonce state. All use the same
TF-M service configuration, so changing examples does not intentionally change
the secure-storage layout. These are **file-content encryption** examples,
not transparent flash encryption and not a modified LittleFS fork.

## Storage and security design

Application -> PSA authenticated encryption -> LittleFS -> external NOR flash.
The CM33 non-secure app calls secure TF-M services. Persistent keys are generated
inside TF-M, have only ENCRYPT and DECRYPT usage, and are not exported or stored
in external LittleFS. TF-M's on-chip RRAM ITS holds keys; protected storage holds
a separate 64-bit counter per algorithm.

A write reserves/persists the next counter **before** encryption. The 12-byte
nonce is a 32-bit key ID plus this 64-bit counter (little endian). Failed/interrupted
writes consume their nonce; reboot reloads the persisted value. An existing key
with absent/corrupt counter is rejected. An orphaned counter is also rejected.
Counter overflow fails closed. Formatting data must never reset nonce state.

The encrypted record is written to a private .pending file, synced, closed, then
atomically renamed over the destination. There is no unlink-before-replace window.
On boot a leftover pending file is discarded, never promoted. LittleFS metadata
and filenames remain plaintext; file sizes/access patterns remain visible.

Each record authenticates its full 32-byte header and the destination filename
(zero padded to 24 bytes). Authentication covers empty files too. The entire file
(up to 1024 bytes) is authenticated before returning plaintext. No partial
unauthenticated plaintext is printed. A 16-byte tag is always required.

### Version 1 record

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | ASCII EFS1 |
| 4 | 1 | Version = 1 |
| 5 | 1 | Algorithm ID |
| 6 | 2 | Key bits, little endian |
| 8 | 4 | Plaintext length, little endian |
| 12 | 12 | Nonce: key ID + counter |
| 24 | 4 | Persistent key ID, little endian |
| 28 | 4 | Reserved zero, authenticated |
| 32 | N | Ciphertext |
| 32 + N | 16 | Authentication tag |

Unknown versions/algorithms, mismatched lengths and nonzero reserved bytes are
rejected. No algorithm fallback or downgrade is performed.

### Development limits — not a production security claim

- UART commands are trusted developer access and can request decrypted plaintext.
  Remove or authenticate this interface in a product.
- This development kit has debug access. Non-exportable PSA policy and TrustZone
  isolation are not proof against a physical attacker, debugger, compromised
  authorized firmware or invasive RRAM extraction. ITS encryption is off in this
  board profile; keys are not claimed encrypted at rest inside RRAM.
- AEAD detects modification, not deletion or replay of an older valid file.
  The file layer has no anti-rollback freshness mechanism.
- Protected-storage counter updates rely on TF-M's persistence guarantees.
  Software-reset fault injection is tested; abrupt electrical power-cut testing,
  endurance testing and provisioning interruption recovery need separate work.
- Do not erase/reinitialize counter state while retaining the same key.
  Lost secure state is not silently regenerated for an existing counter/key pair.
- The simple example limits files to 1 KiB and uses one pending file, one
  command-processing thread, no concurrent writers and no general directory API.
- The boot code provisions a key/counter on first use. Power loss between those
  steps can leave an orphan counter; it fails closed and needs deliberate
  reprovisioning with a new key, not a counter reset under an old key.

## Commands

| Command | Meaning |
|---|---|
| efs info | Algorithm, mount/key state, partition, counter and payload bound |
| efs init | Mount existing storage or format only a completely erased partition |
| efs write NAME HEX | Authenticated atomic create/replace; '-' means empty |
| efs read NAME | Verified plaintext hex |
| efs raw NAME | Encoded stored header/ciphertext/tag, never the key |
| efs list | Filenames and encoded sizes |
| efs delete NAME | Delete that one file |
| efs reboot | Cold software reset |
| efs kat | Volatile fixed TEST key vector, independent of persistent keys |
| efs wrongkey NAME | Try a different volatile random key; require rejection |
| efs tamper NAME header/nonce/data/tag | Toggle one byte in the named test file |
| efs interrupt NAME HEX partial/synced | Software reset at one of two update stages |
| efs blanks | Read-only allocation aid: report first three erased MiB areas at/above 8 MiB |

Tamper is reversible by repeating the same command. It is a test facility, not
normal application functionality. There is intentionally no mass erase or
unconditional format command.

## Compatibility fixes kept inside this repository

1. The installed Zephyr QSPI driver chooses the first *global* mapped partition.
   With CM33 NS that can be internal RRAM, giving external flash incorrect page
   geometry/capacity. qspi_geometry.cmake generates a scoped driver copy using
   zephyr,flash instead. It checks the exact source pattern and fails if upstream
   changes, so it cannot silently apply to an unknown driver. The external
   Zephyr checkout is not modified. Runtime validates 256 KiB erase geometry.
2. TF-M's medium software-crypto profile defines
   MBEDTLS_AES_ONLY_128_BIT_KEY_LENGTH. crypto_config.h is appended through the
   TFM_TF_PSA_CRYPTO_PLATFORM_EXTRA_CONFIG_PATH hook to remove that restriction,
   retaining the board's cipher/CMAC settings. The override is deferred until
   the SoC has finished adding its default configuration.
3. GCM and ChaCha20-Poly1305 are enabled in the secure partition. The current
   build uses the TF-PSA-Crypto software backend (IFX_MBEDTLS_ACCELERATION_ENABLED
   is OFF). Timings must not be described as AES hardware-accelerator benchmarks.

## Verification

Physical-board results and firmware fingerprints: [VERIFICATION.md](VERIFICATION.md).

Host:

```sh
/home/wasanw/zephyrproject/.venv/bin/python -m unittest discover -s common/encrypted_files -p 'test_*.py' -v
```

Each example's test_firmware.py runs the same hardware acceptance suite with its
own expected algorithm and partition. It compares a fixed test-key result with
Python cryptography, tests binary payload lengths 0/1/15/16/17/255/256/1024,
checks rewritten ciphertext/nonces, wrong-key rejection, header/nonce/data/tag
tampering, reboot persistence, interrupted updates, parser limits, list/delete.

crypto_ms is only the PSA encryption-call elapsed time, measured in milliseconds.
write_uart_ms includes command transfer, secure counter update, filesystem write
and UART reply; it is not raw flash or cipher throughput.

References:
[PSA AEAD API](https://arm-software.github.io/psa-api/crypto/1.2/api/ops/aead.html),
[NIST GCM](https://csrc.nist.gov/pubs/sp/800/38/d/final),
[ChaCha20-Poly1305 RFC 8439](https://www.rfc-editor.org/rfc/rfc8439.html).
