#!/usr/bin/env bash
set -euo pipefail
source /home/wasanw/zephyrproject/.venv/bin/activate
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh
here_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
west flash -d "$here_dir/build-cm55"
