# Third-party dependencies and TESA migration checklist

## Included and external components

| Component | Location / provenance | Handling |
| --- | --- | --- |
| Bosch BMM350 SensorAPI | [020 vendor notes](020_Kalman_Artificial_Horizon/vendor/bmm350/README.md) | BSD-3-Clause license retained; pinned revision and local reset change documented |
| Infineon BGT60TRxx driver | [021 vendor notes](021_Radar_BGT60TR13C/vendor/bgt60trxx/README.md) | Apache-2.0 license retained; pinned portable driver |
| Zephyr, TF-M, LVGL, Infineon HAL/PDL/GFXSS | External west workspace | Built from installed modules under their own licenses; not relicensed by this repository |
| Radar model and TFLM archive | [022 dependency instructions](022_Radar_AI_Gestures/README.md) | Licensed local `Temp` dependencies; hash-checked, not included in Git; generated copies stay ignored |
| Hardware photos in 019/020 | User-supplied project photos | Included at the user's request; confirm publication/transfer rights with the owner |

There is currently **no repository-wide license grant** for original project
code documented in a top-level LICENSE file. Do not infer one from a vendor
subdirectory's license. The owner must choose the project license; this audit
does not invent or apply one. This inventory is not a legal redistribution
clearance for every linked SDK component or resulting firmware binary.

## Before moving to the TESA repository

- Choose whether to transfer this GitHub repository or push preserved history
  to an owner-specified new repository. Confirm destination, visibility and
  permissions; do not guess a TESA organization/repository name.
- Preserve commit history, retained vendor notices and documented manual-test
  limitations. Verify `main` is the intended baseline.
- Choose the original-code license and obtain any needed rights for photos and
  external model/runtime distribution. Keep restricted artifacts out of Git.
- Follow the setup guide on a clean machine. Existing scripts contain local
  workspace/programmer paths; chapter 022 additionally needs its licensed
  reference tree. Cached builds are not a portable distribution.
- Audit staged files for credentials, private keys, user recordings, wireless
  identifiers and generated artifacts. Use scrubbed compact evidence, not all
  local serial logs.
- Update clone links, issue/security contact, branch protections and CI settings
  after the destination is confirmed. Re-run `tools/check_docs.py` and relevant
  host/board tests. Do not claim this checklist performs the migration itself.

The current request merges reviewed work into `main`; it does not transfer
ownership or change repository visibility.
