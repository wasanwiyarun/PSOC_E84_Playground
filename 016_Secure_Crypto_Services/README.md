# 016 — Secure Crypto Services

[All examples](../README.md) · [Host setup](../docs/SETUP.md) · [Test scope](../docs/TESTING.md) · [Flash safety](../docs/STORAGE.md)

Unless a command explicitly says repository root, run it from this chapter
directory using the environment in the setup guide. Programming replaces the
current board firmware; check the storage guide before running write tests.

CM33 Non-Secure calls TF-M's PSA Crypto service for AES-128-GCM, P-256 ECDH,
and P-256 ECDSA with SHA-256. The example never prints a private key, AES key,
or ECDH shared secret. It reports only operation outcomes.

The firmware accepts these serial commands (115200 8N1):

```text
crypto info
crypto aes
crypto ecdh
crypto ecdsa
crypto all
```

Each command runs one operation, while `crypto all` runs the complete suite:

- AES-GCM encrypt/decrypt and a modified-tag rejection.
- Two TF-M PSA-generated P-256 key pairs performing ECDH and comparing their
  derived shared secrets in the non-secure application's RAM.
- A TF-M PSA-generated P-256 signing key performing ECDSA verification and a
  modified-message rejection.

```bash
./build.sh
./flash.sh
python3 test_firmware.py --port /dev/ttyACM0
```

Run from this example directory in the configured Python environment. The test
requires the per-command pass markers and final `PASS SECURE_CRYPTO AES_GCM ECDH ECDSA`;
logs go to `test-results/`. CM33-NS plus its matching TF-M image is required;
this is not the CM55 Sysbuild/display boot companion.

The AES test imports a public fixed test key and nonce embedded in source.
They must never protect real data. ECDH outputs are returned to the non-secure
caller (not printed); private ECC keys remain behind PSA handles. These tests
demonstrate API behavior, not a hardware-enclave acceleration benchmark or a
production key-protection guarantee. See [security limits](../SECURITY.md).
