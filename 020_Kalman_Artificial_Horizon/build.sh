#!/usr/bin/env bash
set -euo pipefail
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh
here_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
display_dir="$here_dir/../017_LVGL_Waveshare_4_3_Display"
west build -p auto -d "$here_dir/build-cm55" \
  -b kit_pse84_ai/pse846gps2dbzc4a/m55 "$here_dir" --sysbuild -- \
  -DOPENOCD=/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd \
  -Denable_cm55_EXTRA_ZEPHYR_MODULES="$display_dir/cm33_secure"
