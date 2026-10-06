#!/usr/bin/env python3
"""Verify the PSOC Edge84 I2C sensor self-test serial protocol."""
import argparse
import sys
import time

import serial


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
            received = received[-512:]
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()
    checks = [
        ("info", "OK BOARD=kit_pse84_ai APP=i2c_sensor_self_test VERSION=0.1.0"),
        ("help", "INFO COMMANDS=info;sensor scan;sensor status;help"),
        ("invalid", "ERR UNSUPPORTED invalid"),
        ("sensor scan", "OK SENSOR_SCAN COUNT=3"),
        ("sensor status", "OK SENSOR_STATUS=PASS"),
    ]
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
    print("PASS: I2C sensor self-test verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
