#!/usr/bin/env python3
"""Verify normal Zephyr BMI270 motion data on the PSoC Edge E84 AI Kit."""
import argparse
import contextlib
from datetime import datetime
from pathlib import Path
import re
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
        if not data:
            continue
        text = data.decode(errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()
        received += text
    if expected not in received:
        print(f"FAIL: expected {expected!r}", file=sys.stderr)
        return None
    transcript = received
    quiet_until = time.monotonic() + 0.5
    while time.monotonic() < quiet_until:
        data = port.read(port.in_waiting or 1)
        if data:
            text = data.decode(errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            transcript += text
            quiet_until = time.monotonic() + 0.15
    print(f"PASS: {command}")
    return transcript


def parse_vector(transcript, label):
    match = re.search(rf"OK {label}=(-?\d+),(-?\d+),(-?\d+)", transcript)
    if not match:
        print(f"FAIL: {label} vector missing", file=sys.stderr)
        return None
    return tuple(int(value) for value in match.groups())


def run_checks(port_name, timeout):
    try:
        with serial.Serial(port_name, 115200, timeout=0.1, write_timeout=1) as port:
            time.sleep(0.2)
            port.reset_input_buffer()
            if send(port, "info", "APP=imu_motion_monitor VERSION=0.2.0", timeout) is None:
                return 1
            if send(port, "imu configure", "OK IMU CONFIGURE ACCEL_RET=0 GYRO_RET=0 ODR_HZ=100", timeout) is None:
                return 1
            readout = send(port, "imu read", "OK IMU_READ=PASS", timeout)
            if readout is None:
                return 1
            accel = parse_vector(readout, "ACCEL_MS2_U")
            gyro = parse_vector(readout, "GYRO_RADPS_U")
            if accel is None or gyro is None:
                return 1
            if abs(accel[2]) < 5_000_000:
                print(f"FAIL: gravity-axis acceleration is implausible: {accel}", file=sys.stderr)
                return 1
            raw = send(port, "bmi raw", "OK BMI270 CHIP_ID=0x24", timeout)
            if raw is None:
                return 1
            raw_vector = parse_vector(raw, "BMI270 RAW ACC")
            if raw_vector is None or max(abs(value) for value in raw_vector) < 500:
                print(f"FAIL: raw BMI270 acceleration is implausible: {raw_vector}", file=sys.stderr)
                return 1
            if send(port, "invalid", "ERR UNSUPPORTED invalid", timeout) is None:
                return 1
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 2
    print("PASS: Zephyr BMI270 driver returned live converted and raw motion data.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log_path = args.log or Path(__file__).parent / "test-results" / (
        f"imu-motion-monitor-{datetime.now():%Y%m%d-%H%M%S}.log"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        with contextlib.redirect_stdout(Tee(sys.stdout, log_file)):
            with contextlib.redirect_stderr(Tee(sys.stderr, log_file)):
                print(f"LOG: {log_path}")
                return run_checks(args.port, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
