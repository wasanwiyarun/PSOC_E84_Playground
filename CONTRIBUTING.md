# Contributing

Read [setup](docs/SETUP.md), [testing](docs/TESTING.md), and
[storage safety](docs/STORAGE.md) first. Follow [AGENTS.md](AGENTS.md): each
chapter owns its build setup, while reusable programming/serial transport stays
parameterized in `tools/`. Keep vendor workarounds chapter-scoped; do not silently
patch the user's global SDK or change security permissions to hide errors.

## Required chapter documentation

Every example README must include, directly or through an explicit shared link:

1. Purpose, core ownership, hardware/wiring and dependencies on sibling examples.
2. Host prerequisites and command working directory.
3. Exact build and programming steps, including all required companion images.
4. UART/touch commands, arguments/units and expected completion markers.
5. Automated test invocation, output/log location and manual acceptance steps.
6. What was actually verified, dated evidence where available, and remaining limits.
7. Destructive operations, persistent state and security/privacy implications.
8. Links back to the repository and shared setup/testing guides.

A separate `VERIFICATION.md` is useful for detailed measurements; it is not
mandatory when a concise verification section in the README is sufficient.
Keep one source of truth and link to it instead of copying changing test results.

## Before a merge

```sh
python3 tools/check_docs.py
git diff --check
```

Run the applicable host tests from the testing guide. For firmware changes,
build the affected chapter and test the actual board when available; record
image hashes, test command, result, board/port and limitations. If hardware is
unavailable, state that explicitly. Never label a build-only result “hardware
verified,” or equate software counters with visible pixels or real gestures.

Do not commit build outputs, generated graphs, serial logs with nearby SSIDs or
user data, credentials, private keys, licensed local models/runtime archives,
or Python caches. Compact scrubbed verification JSON and user-approved hardware
photos can be committed. Retain third-party copyright/license notices.

Use focused commits on development branches and review the diff before merging
to `main`. Do not force-push, flatten unrelated history, or merge abandoned
feature branches merely because they still exist. Repository transfer, public
visibility changes and choice of a top-level project license require the owner's
explicit decision; see [third-party and migration notes](THIRD_PARTY.md).
