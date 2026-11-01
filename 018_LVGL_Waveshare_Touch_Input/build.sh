#!/usr/bin/env bash
set -euo pipefail

readonly APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DISPLAY_CHAPTER="${APP_DIR}/../017_LVGL_Waveshare_4_3_Display"
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh

west build -p auto -d "${APP_DIR}/build-cm55" \
  -b kit_pse84_ai/pse846gps2dbzc4a/m55 "${APP_DIR}" --sysbuild -- \
  -DOPENOCD=/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd \
  -Denable_cm55_EXTRA_ZEPHYR_MODULES="${DISPLAY_CHAPTER}/cm33_secure"
