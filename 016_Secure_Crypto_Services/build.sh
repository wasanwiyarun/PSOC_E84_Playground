#!/usr/bin/env bash
set -euo pipefail

readonly APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh

west build -p auto -d "${APP_DIR}/build-cm33-ns" \
  -b kit_pse84_ai/pse846gps2dbzc4a/m33/ns "${APP_DIR}" -- \
  -DOPENOCD=/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd
