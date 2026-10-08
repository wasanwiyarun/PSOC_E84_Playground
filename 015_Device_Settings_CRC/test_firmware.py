#!/usr/bin/env python3
"""Verify Zephyr Settings over CRC-protected FCB on the PSoC E84 board."""
import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import sys

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee, wait_for


def command(port, text, expected, timeout):
    port.reset_input_buffer()
    print(f"--> {text}")
    port.write(f"{text}\n".encode())
    port.flush()
    if wait_for(port, expected, timeout):
        print(f"PASS: {text}")
        return True
    print(f"FAIL: expected {expected!r}", file=sys.stderr)
    return False


def run(port_name, timeout):
    with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
        if not command(port, "settings info", "OK BACKEND=FCB ENTRY_CRC=enabled", timeout):
            return 1
        if not command(port, "settings factory-reset", "OK FACTORY_RESET rc=0", timeout):
            return 1
        if not wait_for(port, "OK READY=1", timeout):
            print("FAIL: board did not reboot after factory reset", file=sys.stderr)
            return 1
        checks = [
            ("settings set interval_ms 1500", "OK SAVE device/interval_ms=1500 rc=0"),
            ("settings set enabled 1", "OK SAVE device/enabled=1 rc=0"),
            ("settings set name E84-CRC", "OK SAVE device/name=E84-CRC rc=0"),
            ("settings reboot", "OK REBOOT"),
        ]
        for text, expected in checks:
            if not command(port, text, expected, timeout):
                return 1
        if not wait_for(port, "OK READY=1", timeout):
            print("FAIL: board did not reboot after settings save", file=sys.stderr)
            return 1
        if not command(port, "settings show", "OK SETTINGS interval_ms=1500 enabled=1 name=E84-CRC", timeout):
            return 1
    print("PASS: Settings API persistence and FCB entry CRC configuration verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log = args.log or Path(__file__).parent / "test-results" / f"settings-crc-{datetime.now():%Y%m%d-%H%M%S}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as output:
        with contextlib.redirect_stdout(Tee(sys.stdout, output)):
            with contextlib.redirect_stderr(Tee(sys.stderr, output)):
                print(f"LOG: {log}")
                try:
                    return run(args.port, args.timeout)
                except serial.SerialException as error:
                    print(f"Serial error: {error}", file=sys.stderr)
                    return 2


if __name__ == "__main__":
    raise SystemExit(main())
