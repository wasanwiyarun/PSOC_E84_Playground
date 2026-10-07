#!/usr/bin/env python3
"""Verify the Bluetooth LE advertiser console protocol."""
import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee


def send(port, command, expected, timeout):
    port.reset_input_buffer()
    print(f"--> {command}")
    port.write(f"{command}\n".encode())
    port.flush()
    deadline = time.monotonic() + timeout
    received = ""
    while time.monotonic() < deadline and expected not in received:
        data = port.read(port.in_waiting or 1)
        if data:
            text = data.decode(errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            received += text
    if expected not in received:
        print(f"FAIL: expected {expected!r}", file=sys.stderr)
        return False
    print(f"PASS: {command}")
    return True


def run_checks(port_name, timeout):
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            time.sleep(0.2)
            port.reset_input_buffer()
            checks = [
                ("info", "APP=bluetooth_le_advertiser VERSION=0.1.0"),
                ("bt status", "OK BT ADVERTISING=ON NAME=PSE84-Playground"),
                ("bt advertise stop", "OK BT ADVERTISING=OFF NAME=PSE84-Playground"),
                ("bt advertise start", "OK BT ADVERTISING=ON NAME=PSE84-Playground"),
                ("invalid", "ERR UNSUPPORTED invalid"),
            ]
            for command, expected in checks:
                if not send(port, command, expected, timeout):
                    return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: Bluetooth controller initialized and LE advertising control verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"bluetooth-le-advertiser-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_checks(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
