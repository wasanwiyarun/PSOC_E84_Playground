#!/usr/bin/env python3
"""Exercise the reserved external-flash raw-access protocol."""
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
    if wait_for(port, expected, timeout):
        print(f"PASS: {command}")
        return True
    print(f"FAIL: expected {expected!r}", file=sys.stderr)
    return False


def run_test(args):
    try:
        with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=1) as port:
            if not send_and_expect(
                port,
                "flash info",
                "PARTITION=storage OFFSET=0 SIZE=1048576 ERASE_BLOCK=262144",
                args.timeout,
            ):
                return 1
            if not send_and_expect(port, "flash write", "OK FLASH WRITE OFFSET=0", args.timeout):
                return 1
            if not send_and_expect(port, "flash read", "PASS FLASH READ OFFSET=0", args.timeout):
                return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2

    print("PASS: raw external-flash write/read/CRC/cleanup verified.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"external-flash-raw-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_test(args)


if __name__ == "__main__":
    raise SystemExit(main())
