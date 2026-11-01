#!/usr/bin/env python3
"""Exercise the Chapter 014 LittleFS serial protocol."""
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
    checks = [
        ("fs format", "OK FORMAT=0"),
        ("fs mkdir /lfs/test", "OK MKDIR=0"),
        ("fs create /lfs/test/value.txt", "OK WRITE=0"),
        ("fs write /lfs/test/value.txt hello-littlefs", "OK WRITE=14"),
        ("fs reboot", "OK REBOOT"),
        ("fs ls", "OK LS test"),
        ("fs delete /lfs/test/value.txt", "OK DELETE=0"),
        ("fs rmdir /lfs/test", "OK RMDIR=0"),
    ]
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            for index, (text, expected) in enumerate(checks):
                if not command(port, text, expected, timeout):
                    return 1
                if text == "fs reboot":
                    if not wait_for(port, "OK MOUNT=0", timeout):
                        print("FAIL: LittleFS did not remount after reboot", file=sys.stderr)
                        return 1
                    if not command(port, "fs read /lfs/test/value.txt", "OK READ DATA=hello-littlefs", timeout):
                        return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: LittleFS lifecycle verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log = args.log or Path(__file__).parent / "test-results" / f"littlefs-{datetime.now():%Y%m%d-%H%M%S}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as output:
        with contextlib.redirect_stdout(Tee(sys.stdout, output)):
            with contextlib.redirect_stderr(Tee(sys.stderr, output)):
                print(f"LOG: {log}")
                return run(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
