# Documentation audit — 1 November 2026

[Repository](../README.md) · [Setup](SETUP.md) · [Test inventory](TESTING.md)

## Coverage and changes

Reviewed the overall README, roadmap, shared support documentation and the
README/build/test entry points for all **23 actual examples** (001–006, 010–026).
007–009 are roadmap slots, not missing runnable folders. Direct source and
test inspection were used to check claims; the existing local graph index was
consulted but was not treated as a current documentation inventory.

- Added shared host prerequisites, boot/image pairing, troubleshooting and
  explicit working-directory conventions. Each chapter links back to these.
- Added a per-example test/evidence inventory, separating host, board protocol,
  human visual acceptance and untested power-loss/security claims.
- Corrected 013's nonexistent command and cleanup claim, 014's destructive
  format/boot behavior and file semantics, and 015's FCB-versus-NVS description.
- Clarified 016's development vectors and non-secure ECDH shared-secret output.
- Documented application flash ownership, storage conflicts and nonce-state
  precautions. Updated completed work in the roadmap.
- Added contribution, security, dependency/license and future migration guides.
- Included the previously untracked 019 particle-fluid example, user-supplied
  photo and tests. No fluid firmware changes were made during this audit.
- Retained compact 023–025 October hardware result snapshots in Git; raw logs,
  build products and the local graph index remain excluded.
- Added `tools/check_docs.py` to check chapter coverage and local inline
  Markdown destinations against files that will actually be published. It
  deliberately does not fetch external URLs or validate heading anchors.

## Checks performed during this audit

The following commands were run using the documented host environment:

| Check | Result / scope |
| --- | --- |
| 019 `bash build.sh` | PASS; both Sysbuild domains built |
| 019 `python test_fluid.py` | PASS; pooling, 1,200 shake frames, reset, mounting poses, elapsed-time physics and stronger-force stability |
| 020 `python test_attitude.py` | PASS; standalone host attitude harness |
| 020 `python test_flight_math.py` | PASS; standalone host flight-math harness |
| 020 `python test_bus_recovery.py` | PASS; successful and deliberately failing recovery paths |
| 022 host unittest discovery | PASS; 4 tests, not live model/gesture acceptance |
| Shared encrypted-file host unittest discovery | PASS; 4 format/oracle tests |

Final merge validation passed the documentation checker (**23 examples,
38 Markdown files, 201 local links**), shell/Python syntax checks and
`git diff --check`. Reproduction commands are in the
[testing guide](TESTING.md). The C harnesses in 020 must be executed directly:
running them through unittest does not execute their full standalone tests.

This was a documentation/integration audit, **not a new flash-and-test run of
all chapters**. Destructive 013–015 tests were not rerun. The connected board's
026 firmware was not replaced. Its preceding physical-board run is recorded in
[026 verification](../026_Encrypted_Sensor_Logger/VERIFICATION.md): 11 checks,
1,155 samples, zero reported errors/drops, but a 2.099 s maximum sample gap.
That result is not uninterrupted 10 Hz acquisition.

## Explicit remaining limits

- Earlier chapters' raw logs are local and do not all have retained image
  fingerprints. Their READMEs describe historical results or acceptance
  procedures, not a promise of a fresh pass on another board.
- 020 is educational, not for flight. Magnetic north remains unavailable under
  the observed high field. Relative gyro direction can drift.
- 022 needs its licensed local reference model/runtime; human gesture accuracy
  is not established by the numerical port tests.
- 026 still needs manual LCD/touch acceptance, physical power-cut testing and
  further sampling-latency work. Software resets do not prove power-cut safety.
- Build scripts still contain machine-local SDK/tool paths. A clean-machine
  setup exercise is required before calling the repository portable.
- The owner must choose the original-code license and approve the destination
  and third-party/photo rights before the TESA move. This audit neither selects
  a license nor transfers repository ownership.

See [third-party and migration notes](../THIRD_PARTY.md) and
[security limitations](../SECURITY.md) before redistribution or production use.
