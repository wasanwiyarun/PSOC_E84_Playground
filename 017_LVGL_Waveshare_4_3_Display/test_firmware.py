#!/usr/bin/env python3
"""Physical smoke test for the Waveshare 4.3-inch LVGL display example."""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()

    result_dir = pathlib.Path(__file__).with_name("test-results")
    result_dir.mkdir(exist_ok=True)
    log_path = result_dir / f"lvgl-display-{time.strftime('%Y%m%d-%H%M%S')}.log"
    lines: list[str] = []

    try:
        with serial.Serial(args.port, 115200, timeout=0.25) as port:
            port.reset_input_buffer()
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                line = port.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                print(line)
                lines.append(line)
                if "OK DISPLAY LVGL_TEXT width=800 height=480" in line:
                    log_path.write_text("\n".join(lines) + "\n")
                    print("PASS: Waveshare panel I2C initialization and LVGL text rendering started.")
                    return 0
                if "FAIL DISPLAY" in line:
                    break
    except serial.SerialException as error:
        log_path.write_text("\n".join(lines) + "\n")
        print(f"FAIL: serial connection error: {error}", file=sys.stderr)
        return 2

    log_path.write_text("\n".join(lines) + "\n")
    print("FAIL: did not observe the LVGL display-ready marker.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
