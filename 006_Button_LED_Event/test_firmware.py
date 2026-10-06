#!/usr/bin/env python3
"""Verify the PSOC Edge E84 button/LED console protocol."""
import argparse
from datetime import datetime
from pathlib import Path
import sys
import time

import serial


class Tee:
    def __init__(self, log_file):
        self.log_file = log_file

    def write(self, text):
        sys.__stdout__.write(text)
        self.log_file.write(text)
        self.log_file.flush()

    def flush(self):
        sys.__stdout__.flush()
        self.log_file.flush()


def wait_for(port, expected, timeout):
    deadline = time.monotonic() + timeout
    received = b""
    while time.monotonic() < deadline:
        data = port.read(port.in_waiting or 1)
        if data:
            print(data.decode(errors="replace"), end="", flush=True)
            received += data
            if expected.encode() in received:
                return True
            received = received[-2048:]
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path("test-results") / (
        f"button-led-event-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    checks = [
        ("info", "OK BOARD=kit_pse84_ai APP=button_led_event VERSION=0.1.0"),
        ("help", "INFO COMMANDS=info;button status;led on;led off;help"),
        ("invalid", "ERR UNSUPPORTED invalid"),
        ("led on", "OK BUTTON PRESSES="),
        ("led off", "LED0=OFF"),
        ("button status", "OK BUTTON PRESSES="),
    ]
    original_stdout = sys.stdout
    with log_path.open("w", encoding="utf-8") as log_file:
        sys.stdout = Tee(log_file)
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
            print("PASS: button/LED console protocol verified.")
            return 0
        finally:
            sys.stdout = original_stdout


if __name__ == "__main__":
    raise SystemExit(main())
