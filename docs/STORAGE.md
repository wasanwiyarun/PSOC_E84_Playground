# Flash ownership and destructive operations

[Repository](../README.md) · [Setup](SETUP.md) · [Security](../SECURITY.md)

Offsets below are physical external-flash offsets, not CPU XIP aliases. End
addresses are exclusive. The tested external NOR has 256 KiB erase sectors.
Confirm the actual generated device tree and board before writing. Blank areas
found on one board are not universal reservations for all boards/products.

| Use | Start | Length | Risk / initialization policy |
| --- | --- | --- | --- |
| 013 raw scratch | `0x00000000` | first `0x40000` of 1 MiB shared storage | `flash write` erases first sector; 256-byte pattern remains |
| 014 LittleFS | `0x00000000` | `0x100000` | Boot may auto-format incompatible media; test starts with unconditional `fs format` |
| 015 settings FCB | `0x00000000` | `0x100000` | Test uses factory reset; removes 014 data and prior settings |
| 023 AES-256-GCM | `0x00b00000` | `0x100000` | Entire-region blank check before first format |
| 024 AES-128-GCM | `0x00e00000` | `0x100000` | Same policy, different key/counter |
| 025 ChaCha20-Poly1305 | `0x00f00000` | `0x100000` | Same policy, different key/counter |
| 026 encrypted logger | `0x01780000` | `0x300000` | Entire-region blank check; ends at `0x01a80000` |

These are application-data assignments, **not a complete boot/TF-M flash map**.
Firmware programming writes the addresses in the selected build images.
Do not assume all other addresses are unused, or use this table to erase secure
assets. Historical occupied areas and 026's former 16–17 MiB test filesystem
are intentionally not reclaimed automatically.

013–015 are mutually incompatible scratch demonstrations. Back up any needed
data before choosing one; they are not safe non-destructive diagnostics for an
unknown board. 023–026 tests create unique disposable names and delete only
their own successful test files; a failed run can leave files for inspection.
Raw local logs may contain plaintext exports. Review before sharing them.

Keys and nonce counters are owned by TF-M, separate from the LittleFS payload
partitions. Never reset a nonce counter while retaining its key. A protected
counter is not a hardware anti-rollback primitive. A successful software reboot
test is not evidence for arbitrary electrical power loss during erase/program.
LittleFS deletion is logical deletion, not guaranteed physical secure erasure.

026 coordinates CM55 flash writes with a secure SRAM-resident CM33 pause.
It is a trusted-firmware development mechanism, not a general concurrent
filesystem service or a guarantee of uninterrupted acquisition. The measured
2.099 s worst sample gap and other limits are in its verification report.
