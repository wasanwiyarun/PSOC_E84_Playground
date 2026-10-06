#!/usr/bin/env python3
"""Record and verify direct BMI270 register access on the E84 AI Kit."""

import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def send_and_expect(port, command, expected, timeout):
    port.reset_input_buffer()
    print(f"--> {command}")
    port.write(f"{command}\n".encode())
    port.flush()
    if wait_for(port, expected, timeout):
        # The expected marker may occur before the rest of a multi-line
        # register dump. Keep the transcript grouped by its command.
        quiet_until = time.monotonic() + 0.5
        while time.monotonic() < quiet_until:
            data = port.read(port.in_waiting or 1)
            if data:
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
                quiet_until = time.monotonic() + 0.15
        print(f"PASS: {command}")
        return True
    print(f"FAIL: expected {expected!r}", file=sys.stderr)
    return False


def run_checks(port_name, timeout):
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            # Discard any boot banner that is still being transmitted.
            time.sleep(0.2)
            port.reset_input_buffer()
            checks = [
                ("info", "APP=bmi270_raw_register_diagnostic VERSION=0.1.0"),
                ("bmi status", "OK BMI270 CHIP_ID=0x24 EXPECTED=0x24"),
                ("bmi enable", "OK BMI270 ENABLED ACC_GYR_100HZ"),
                ("bmi read", "OK BMI270 RAW ACC="),
                ("help", "INFO COMMANDS=info;bmi status;bmi enable;bmi read;help"),
            ]
            for command, expected in checks:
                if not send_and_expect(port, command, expected, timeout):
                    return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: BMI270 raw I2C identity and registers verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"bmi270-raw-register-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_checks(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
