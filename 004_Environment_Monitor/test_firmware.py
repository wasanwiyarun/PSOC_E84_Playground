#!/usr/bin/env python3
"""Verify the PSOC Edge E84 environment-monitor serial protocol."""
import argparse
from datetime import datetime
from pathlib import Path
import sys

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"environment-monitor-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    checks = [
        ("info", "OK BOARD=kit_pse84_ai APP=environment_monitor VERSION=0.1.0"),
        ("help", "INFO COMMANDS=info;env read;help"),
        ("invalid", "ERR UNSUPPORTED invalid"),
        ("env read", "OK ENV_READ=PASS"),
    ]
    original_stdout = sys.stdout
    with log_path.open("w", encoding="utf-8") as log_file:
        sys.stdout = Tee(original_stdout, log_file)
        try:
            print(f"LOG: {log_path}")
            try:
                with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=1) as port:
                    for command, expected in checks:
                        port.reset_input_buffer()
                        print(f"--> {command}")
                        port.write(f"{command}\n".encode())
                        port.flush()
                        if not wait_for(port, expected, args.timeout):
                            print(f"FAIL: expected {expected!r}", file=sys.stderr)
                            return 1
                        print(f"PASS: {command}")
            except serial.SerialException as error:
                print(f"Serial error: {error}", file=sys.stderr)
                return 2
            print("PASS: environment monitor verified.")
            return 0
        finally:
            sys.stdout = original_stdout


if __name__ == "__main__":
    raise SystemExit(main())
