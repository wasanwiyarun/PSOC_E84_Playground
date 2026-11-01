# Project conventions

- Keep build setup in each example or application folder (for example,
  `build.sh`). That script owns its board target, Sysbuild settings, source
  directory, and build directory.
- Keep reusable programming and serial-monitoring implementation in `tools/`.
  Shared tools must accept example-specific values such as build directory,
  image domain, and expected serial output as arguments; do not hard-code a
  particular example.
- Example build scripts call shared tools with explicit configuration when a
  combined workflow is needed.
- Evaluate project-structure proposals against the technical constraints and
  tradeoffs. Do not agree merely to agree.
