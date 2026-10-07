#!/usr/bin/env python3
"""Wait for the CM55 PSA mailbox relay acceptance marker."""
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()

    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"cm55-cm33-relay-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)

    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                try:
                    with serial.Serial(args.port, 115200, timeout=0.1) as port:
                        if not send_and_expect(
                            port,
                            "ipc status",
                            "OK RELAY STATUS=IDLE COMPLETED=0 PSA_VERSION=0x00000000",
                            args.timeout,
                        ):
                            return 1
                        if send_and_expect(port, "ipc start", "PASS RELAY COUNT=100", args.timeout):
                            print("PASS: 100 CM55-to-CM33-NS PSA relay requests completed.")
                            return 0

                        print("FAIL: CM55 relay pass marker was not received", file=sys.stderr)
                        return 1
                except serial.SerialException as error:
                    print(f"Serial error: {error}", file=sys.stderr)
                    return 2


if __name__ == "__main__":
    raise SystemExit(main())
