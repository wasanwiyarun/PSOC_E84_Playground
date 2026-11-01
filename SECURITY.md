# Security scope and reporting

This is an educational development-board repository, not production security
firmware and not a flight/navigation product. Debug access is enabled. TF-M
examples use development provisioning (including dummy provisioning where
configured). A non-exportable PSA handle is an API policy, not proof against
an attached debugger, compromised authorized firmware or physical extraction.

- UART and touch controls are trusted. Some commands export plaintext, erase
  storage, tamper with test records or reboot the board.
- 015's FCB CRC detects accidental corruption; it provides no confidentiality
  or adversarial authentication.
- 016 embeds a public AES test key/nonce. Never use them for real data. Its raw
  ECDH shared secrets are returned to the non-secure application in RAM.
- 023–026 use authenticated payload encryption with persistent TF-M keys and
  counters. Filenames, lengths and access patterns remain visible. Whole-file
  replay/deletion and rollback of secure counter state are not prevented by
  the file layer. Never reset a counter under an existing key.
- 026's privileged SRAM pause service assumes trusted non-secure code; a caller
  that never releases the handshake can stall CM33. Flash pauses also affect
  acquisition timing. It is not a multi-tenant hardened platform service.
- Physical power-cut resilience, endurance, secure manufacturing/provisioning,
  debug locking and adversarial anti-rollback require separate validation.

See [shared encrypted-file design](common/encrypted_files/README.md),
[026 limits](026_Encrypted_Sensor_Logger/README.md), and
[flash ownership](docs/STORAGE.md).

No dedicated private vulnerability-reporting channel or response SLA is
currently documented. Contact the repository owner through an established
private channel for sensitive findings before publishing exploitable details.
Do not put keys, credentials, plaintext recordings or identifying wireless logs
in a public issue. A report should name the commit, chapter, configuration,
reproduction steps and impact, with scrubbed evidence.
