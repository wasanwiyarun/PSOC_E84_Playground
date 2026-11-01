#!/usr/bin/env python3
"""Exercise the LED blink serial-control protocol on a connected board."""

import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Send and verify LED-control commands. The default sends 20 "
            "alternating stop/start commands, then checks info, status, help, "
            "and an invalid command."
        )
    )
    parser.add_argument("--port", required=True, help="Serial device, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument(
        "--toggles",
        type=int,
        default=20,
        help="Number of alternating stop/start commands to test (default: 20)",
    )
    parser.add_argument(
        "--timeout-per-command",
        type=float,
        default=2.0,
        help="Seconds allowed for each expected response (default: 2)",
    )
    parser.add_argument(
        "--period-ms",
        type=int,
        default=250,
        help="Non-default blink period exercised by the test (default: 250)",
    )
    parser.add_argument(
        "--log",
        type=Path,
        help=(
            "Log-file path. Defaults to test-results/serial-control-<timestamp>.log "
            "beside this script."
        ),
    )
    return parser.parse_args()


def send_and_expect(
    device: serial.Serial, command: str, expected: str, timeout: float
) -> bool:
    device.reset_input_buffer()
    print(f"--> {command}")
    device.write(f"{command}\n".encode())
    device.flush()

    if wait_for(device, expected, timeout):
        print(f"PASS: {command!r} returned {expected!r}")
        return True

    print(
        f"FAIL: {command!r} did not return {expected!r} within {timeout:g} seconds.",
        file=sys.stderr,
    )
    return False


def run_test(args: argparse.Namespace) -> int:
    if args.toggles <= 0 or args.timeout_per_command <= 0:
        print("--toggles and --timeout-per-command must be greater than zero.", file=sys.stderr)
        return 2
    if not 50 <= args.period_ms <= 10000:
        print("--period-ms must be between 50 and 10000.", file=sys.stderr)
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=1) as device:
            print(f"Testing {args.port} at {args.baud} baud with {args.toggles} toggles.")

            checks = [
                ("info", "OK BOARD=kit_pse84_ai APP=led_blink_serial_control VERSION=0.1.0"),
                (
                    "help",
                    "INFO COMMANDS=info;led blink start;led blink stop;"
                    "led blink status;led blink period <ms>;help",
                ),
                ("invalid", "ERR UNSUPPORTED invalid"),
                ("led blink period 10", "ERR INVALID PERIOD_MS=10 RANGE=50..10000"),
                (
                    f"led blink period {args.period_ms}",
                    f"OK LED0 BLINK=ON PERIOD_MS={args.period_ms}",
                ),
            ]
            for index in range(args.toggles):
                if index % 2 == 0:
                    checks.append(
                        ("led blink stop", f"OK LED0 BLINK=OFF PERIOD_MS={args.period_ms}")
                    )
                else:
                    checks.append(
                        ("led blink start", f"OK LED0 BLINK=ON PERIOD_MS={args.period_ms}")
                    )

            # Leave the board in its default, blinking-enabled state.
            if args.toggles % 2 != 0:
                checks.append(
                    ("led blink start", f"OK LED0 BLINK=ON PERIOD_MS={args.period_ms}")
                )
            checks.append(("led blink period 500", "OK LED0 BLINK=ON PERIOD_MS=500"))
            checks.append(("led blink status", "OK LED0 BLINK=ON PERIOD_MS=500"))

            for command, expected in checks:
                if not send_and_expect(device, command, expected, args.timeout_per_command):
                    return 1
    except serial.SerialException as error:
        print(f"Unable to open/use {args.port}: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("Serial test interrupted.", file=sys.stderr)
        return 130

    print("PASS: serial command protocol verified.")
    return 0


def main() -> int:
    args = parse_args()
    default_log = Path(__file__).parent / "test-results" / (
        f"serial-control-{datetime.now().strftime('%Y%m%d-%H%M%S')}.log"
    )
    log_path = args.log or default_log

    try:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("w", encoding="utf-8") as log_file:
            with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
                with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                    print(f"Test log: {log_path}")
                    return run_test(args)
    except OSError as error:
        print(f"Unable to create test log {log_path}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
