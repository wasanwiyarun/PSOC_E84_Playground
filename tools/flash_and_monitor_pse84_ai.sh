#!/usr/bin/env bash
# Flash and verify a PSOC Edge84 AI Kit M55 Sysbuild application.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly OPENOCD="/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd"
readonly OPENOCD_SCRIPTS="/opt/Tools/ModusToolboxProgtools-1.7/openocd/scripts"

port="/dev/ttyACM0"
baud="115200"
timeout_seconds="90"
build_dir=""
companion_domain=""
app_domain=""
expected_output=""

usage() {
    cat <<'EOF'
Usage: tools/flash_and_monitor_pse84_ai.sh --build-dir DIR --app-domain NAME \
       --expect TEXT [options]

Flashes a PSOC Edge84 AI Kit M55 Sysbuild image through KitProg3, then waits
for its expected serial output. Build the example first using its own build
script.

Options:
  --build-dir DIR       Existing Sysbuild output directory (required)
  --companion-domain N  Optional domain to flash before the application
  --app-domain NAME     Application domain to flash (required)
  --expect TEXT         Serial text required for success (required)
  --port DEVICE         Serial device (default: /dev/ttyACM0)
  --baud RATE           Serial baud rate (default: 115200)
  --timeout SECONDS     Serial-output timeout (default: 90)
  -h, --help            Show this help text
EOF
}

option_value() {
    if (($# < 2)); then
        printf 'Option %s requires a value.\n' "$1" >&2
        usage >&2
        exit 2
    fi
}

while (($#)); do
    case "$1" in
    --build-dir)
        option_value "$@"
        build_dir="$2"
        shift 2
        ;;
    --companion-domain)
        option_value "$@"
        companion_domain="$2"
        shift 2
        ;;
    --app-domain)
        option_value "$@"
        app_domain="$2"
        shift 2
        ;;
    --expect)
        option_value "$@"
        expected_output="$2"
        shift 2
        ;;
    --port)
        option_value "$@"
        port="$2"
        shift 2
        ;;
    --baud)
        option_value "$@"
        baud="$2"
        shift 2
        ;;
    --timeout)
        option_value "$@"
        timeout_seconds="$2"
        shift 2
        ;;
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
done

if [[ -z "${build_dir}" || -z "${app_domain}" || -z "${expected_output}" ]]; then
    printf '%s\n' '--build-dir, --app-domain, and --expect are required.' >&2
    usage >&2
    exit 2
fi

if [[ ! -d "${build_dir}" ]]; then
    printf 'Build directory %s was not found. Build the example first.\n' "${build_dir}" >&2
    exit 2
fi

if [[ ! -x "${OPENOCD}" ]]; then
    printf 'Infineon OpenOCD was not found at %s\n' "${OPENOCD}" >&2
    exit 2
fi

if [[ ! -e "${port}" ]]; then
    printf 'Serial device %s was not found. Is the KitProg3 USB-C port connected?\n' "${port}" >&2
    exit 2
fi

# shellcheck source=/dev/null
source /home/wasanw/zephyrproject/.venv/bin/activate
# shellcheck source=/dev/null
source /home/wasanw/zephyrproject/zephyr/zephyr-env.sh

python "${SCRIPT_DIR}/serial_expect.py" \
    --port "${port}" \
    --baud "${baud}" \
    --expect "${expected_output}" \
    --timeout "${timeout_seconds}" &
monitor_pid=$!
trap 'kill "${monitor_pid}" 2>/dev/null || true' EXIT

if [[ -n "${companion_domain}" ]]; then
    west flash -d "${build_dir}" --domain "${companion_domain}" --no-rebuild
fi
west flash -d "${build_dir}" --domain "${app_domain}" --no-rebuild

# Flashing ends the OpenOCD session; reset the target so the new images boot.
"${OPENOCD}" -s "${OPENOCD_SCRIPTS}" \
    -f interface/kitprog3.cfg \
    -f target/infineon/pse84xgxs2.cfg \
    -c 'init; reset run; shutdown'

wait "${monitor_pid}"
trap - EXIT
