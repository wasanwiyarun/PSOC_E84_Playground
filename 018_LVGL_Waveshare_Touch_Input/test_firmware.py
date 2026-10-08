#!/usr/bin/env python3
"""Verify FT5406 I2C health and optionally capture one physical touch."""

from __future__ import annotations

import argparse
import contextlib
import pathlib
import sys
import time
import uuid

import serial

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee


def read_until(port, token: str, marker: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line)
        if "ASSERTION FAIL" in line or "ZEPHYR FATAL" in line:
            raise RuntimeError("firmware fault")
        if marker in line and f"token={token}" in line:
            return line
    raise RuntimeError(f"no fresh {marker} response for {token}")


def command(port, command: str, token: str, marker: str, timeout: float) -> str:
    print(f"--> touch {command} {token}")
    port.write(f"touch {command} {token}\n".encode("ascii"))
    port.flush()
    return read_until(port, token, marker, timeout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--wait-for-touch", action="store_true",
                        help="wait for one real tap on the blue on-screen button")
    args = parser.parse_args()
    result_dir = pathlib.Path(__file__).with_name("test-results")
    result_dir.mkdir(exist_ok=True)
    log_path = result_dir / f"ft5406-{time.strftime('%Y%m%d-%H%M%S')}.log"
    with log_path.open("w", encoding="utf-8") as output:
        with contextlib.redirect_stdout(Tee(sys.stdout, output)), \
             contextlib.redirect_stderr(Tee(sys.stderr, output)):
            print(f"LOG: {log_path}")
            try:
                with serial.Serial(args.port, 115200, timeout=0.25) as port:
                    port.reset_input_buffer()
                    token = uuid.uuid4().hex[:8]
                    info = command(port, "info", f"{token}-health", "TOUCH STATUS", args.timeout)
                    if not info.startswith("OK TOUCH STATUS") or "ready=1" not in info or "ret=0" not in info:
                        raise RuntimeError("FT5406 did not report a healthy I2C state")
                    print("PASS: FT5406 responds over I2C and touch polling is active")
                    if args.wait_for_touch:
                        print("ACTION: tap the center of the blue TOUCH ME button now")
                        command(port, "arm", f"{token}-tap", "TOUCH ARMED", args.timeout)
                        tap = read_until(port, f"{token}-tap", "TOUCH TAP", args.timeout)
                        if not tap.startswith("OK TOUCH TAP"):
                            raise RuntimeError("touch tap was not accepted")
                        print("PASS: physical tap captured and forwarded to LVGL")
                    else:
                        print("VISUAL CHECK: run again with --wait-for-touch and tap the button.")
            except (serial.SerialException, RuntimeError) as error:
                print(f"FAIL: {error}", file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
