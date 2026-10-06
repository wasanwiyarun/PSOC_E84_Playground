#!/usr/bin/env bash
set -euo pipefail
readonly APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${APP_DIR}/build-pse84-ai"
readonly BOARD="kit_pse84_ai/pse846gps2dbzc4a/m55"
readonly OPENOCD="/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd"
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh
west build -d "${BUILD_DIR}" -b "${BOARD}" --sysbuild "${APP_DIR}" -- "-DOPENOCD=${OPENOCD}"
