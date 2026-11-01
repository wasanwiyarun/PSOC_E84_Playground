#!/usr/bin/env python3
"""Verify the PSOC Edge E84 I2C sensor self-test serial protocol."""
import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def run_checks(port_name, timeout):
    checks = [
        ("info", "OK BOARD=kit_pse84_ai APP=i2c_sensor_self_test VERSION=0.1.0"),
        ("help", "INFO COMMANDS=info;sensor scan;sensor status;help"),
        ("invalid", "ERR UNSUPPORTED invalid"),
        ("sensor scan", "OK SENSOR_SCAN COUNT=3"),
        ("sensor status", "OK SENSOR_STATUS=PASS"),
    ]
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            for command, expected in checks:
                port.reset_input_buffer()
                print(f"--> {command}")
                port.write(f"{command}\n".encode())
                port.flush()
                if not wait_for(port, expected, timeout):
                    print(f"FAIL: expected {expected!r}", file=sys.stderr)
                    return 1
                print(f"PASS: {command}")
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: I2C sensor self-test verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"i2c-sensor-self-test-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_checks(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
