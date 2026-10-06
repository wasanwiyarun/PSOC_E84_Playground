#!/usr/bin/env python3
"""Verify the PSOC Edge E84 credential-free Wi-Fi scan protocol."""
import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def send_and_expect(port, command, expected, timeout):
    port.reset_input_buffer()
    print(f"--> {command}")
    port.write(f"{command}\n".encode())
    port.flush()
    if not wait_for(port, expected, timeout):
        print(f"FAIL: expected {expected!r}", file=sys.stderr)
        return False
    print(f"PASS: {command}")
    return True


def run_checks(port_name, timeout):
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            checks = [
                ("info", "OK BOARD=kit_pse84_ai APP=wifi_ssid_scan VERSION=0.1.0", timeout),
                ("help", "INFO COMMANDS=info;wifi scan;wifi status;help", timeout),
                ("invalid", "ERR UNSUPPORTED invalid", timeout),
                ("wifi scan", "OK WIFI_SCAN_START", timeout),
            ]
            for command, expected, command_timeout in checks:
                if not send_and_expect(port, command, expected, command_timeout):
                    return 1
            if not wait_for(port, "WIFI_SCAN_DONE", 30.0):
                print("FAIL: scan did not complete", file=sys.stderr)
                return 1
            if not send_and_expect(port, "wifi status", "OK WIFI_SCAN_ACTIVE=NO", timeout):
                return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: Wi-Fi scan completed; SSIDs are recorded above.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"wifi-ssid-scan-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_checks(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
