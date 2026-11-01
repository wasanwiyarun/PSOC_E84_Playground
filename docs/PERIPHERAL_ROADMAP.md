# PSoC Edge E84 peripheral roadmap

Status reviewed 2026-11-01 against chapter sources, scripts and recorded results.
The [testing catalog](TESTING.md) is the complete chapter evidence index.

| Area | Implemented / recorded result | Next work or limit |
| --- | --- | --- |
| UART / LED / button | 001/002/006 console and GPIO protocols | Physical SW1 acceptance remains separate |
| I2C/environment/IMU | 003–005 live SHT40, BMI270 and DPS368 data | Calibration, accuracy and mounting |
| BMM350 (slot 007) | 020 native PDL I3C, ID 0x33, compensated XYZ | High-field environment, mounting/calibration; north disabled |
| Radar (slot 008) | 021 FIFO/ADC; 022 continuous acquisition and real model | Human gesture accuracy and radar LCD UI |
| PDM microphone (slot 009) | No runnable example | Confirm pins, clock, DMA/channel and audio format |
| Wi-Fi | 010 credential-free CYW55513 scan | Approved network/credentials before association |
| Bluetooth LE | 011 advertising and NUS loopback implementation | Repeatable phone acceptance, pairing/security |
| Multicore | 012 CM55→CM33-NS/TF-M relay, 100 ordered calls | Not a general bidirectional mailbox; absent-core wait unbounded |
| Raw flash | 013 pattern write/read/CRC | Destructive; no cleanup erase or JEDEC-ID command |
| Filesystem | 014 text-console lifecycle and reboot persistence | Auto-format risk, no atomic/truncating editor semantics |
| Settings | 015 Zephyr settings + CRC-enabled FCB | CRC is not encryption; corruption/power-cut injection pending |
| Crypto services | 016 TF-M PSA AES-GCM, P-256 ECDH/ECDSA | API behavior, not hardware-enclave acceleration certification |
| Display / touch | 017/018 Waveshare DSI / FT5406, historical user confirmation | Preserve clock startup; controller health is not visual acceptance |
| Particle fluid | 019 physics, injected gravity, user-confirmed direction | Display FPS below 30 target; visual approximation |
| Flight dashboard | 020 Kalman attitude, relative heading, pressure estimates | Educational only; gyro drift, no north/airspeed |
| Encrypted files | 023/024/025 separate AES-256/AES-128-GCM/ChaCha20-Poly1305 | Power cuts, endurance and adversarial rollback unverified |
| Encrypted logger | 026 live sensors, LVGL, AES-256-GCM, multicore-safe writes | 11 hardware groups pass; 2.099 s sample gap, manual LCD/touch and power cuts pending |

## Next validation work

1. Confirm 026's visible dashboard and physical RECORD / STOP + SAVE controls.
2. Address logger sampling gaps before describing it as continuous 10 Hz.
   Validate buffering/storage without weakening authentication or nonce safety.
3. Plan controlled power-cut, full-capacity and endurance campaigns on explicitly
   disposable data. Do not use a mass erase as test preparation.
4. Validate human radar gestures and investigate the magnetic environment and
   calibration before enabling north-referenced heading.
5. Complete the [TESA migration checklist](../THIRD_PARTY.md) after the owner
   specifies the destination and licensing decision.

No independent Secure Enclave hardware service or crypto accelerator is inferred
from PSA success. 023–026 use the documented TF-M software crypto backend.
Production provisioning, secure boot policy, debug locking and anti-rollback
remain separate engineering.

The old plan calling 013/014 “planned” and numbering a future secure service as
015 is superseded: 015 is Device Settings CRC, 016 is Secure Crypto Services.
See [storage ownership](STORAGE.md) and [boot families](SETUP.md) before changing
chapters; 013–015 deliberately share destructive scratch storage.
