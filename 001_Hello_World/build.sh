#!/usr/bin/env bash
# Build the PSOC Edge84 AI Kit M55 hello-world application.

set -euo pipefail

readonly APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${APP_DIR}/build-pse84-ai"
readonly BOARD="kit_pse84_ai/pse846gps2dbzc4a/m55"
readonly OPENOCD="/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd"

usage() {
    cat <<'EOF'
Usage: 001_Hello_World/build.sh

Builds this example for the PSOC Edge84 AI Kit M55 core using Sysbuild.
EOF
}

if (($# > 0)); then
    case "$1" in
    -h|--help)
        usage
        exit 0
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        usage >&2
        exit 2
        ;;
    esac
fi

# shellcheck source=/dev/null
source /home/wasanw/zephyrproject/.venv/bin/activate
# shellcheck source=/dev/null
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh

west build -d "${BUILD_DIR}" -b "${BOARD}" --sysbuild "${APP_DIR}" \
    -- "-DOPENOCD=${OPENOCD}"
