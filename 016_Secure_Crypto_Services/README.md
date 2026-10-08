# 016 — Secure Crypto Services

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
- Two enclave-generated P-256 key pairs performing ECDH and comparing their
  derived shared secrets internally.
- An enclave-generated P-256 signing key performing ECDSA verification and a
  modified-message rejection.

```bash
./build.sh
./flash.sh
python3 test_firmware.py --port /dev/ttyACM0
```
