#!/usr/bin/env python3
"""Check the BMI270-controlled water animation on the physical PSoC E84."""
import argparse
import contextlib
import pathlib
import sys
import time
import uuid

import serial

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools" / "python"))
from psoc_e84_tools.serial_console import Tee

def request(port, token, timeout, command="info"):
    port.write(f"water {command} {token}\n".encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    next_retry = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line)
        if "FAIL " in line or "FATAL" in line or "ASSERTION" in line:
            raise RuntimeError(line)
        if "WATER STATUS" in line and f"token={token}" in line:
            return line
        # Flashing can leave us opening UART before main starts accepting
        # commands; retry the read-only probe through that boot window.
        if command == "info" and time.monotonic() >= next_retry:
            port.write(f"water info {token}\n".encode("ascii"))
            port.flush()
            next_retry = time.monotonic() + 1.0
    raise RuntimeError("no fresh WATER STATUS response")

def fields(line):
    return dict(item.split("=", 1) for item in line.split() if "=" in item)

def healthy(status):
    for name, expected in (("imu_ready", "1"), ("touch_ready", "1"),
                           ("valid", "1"), ("particles", "480"), ("ret", "0")):
        if status.get(name) != expected:
            raise RuntimeError(f"unhealthy {name}: {status.get(name)}")
    if not 1500 < int(status["pixels"]) < 10000:
        raise RuntimeError("fluid bitmap is empty or flooded")

def check_direction(port, command, timeout):
    initial = fields(request(port, uuid.uuid4().hex[:8], timeout, command))
    deadline = time.monotonic() + min(timeout, 12.0)
    while time.monotonic() < deadline:
        time.sleep(0.35)
        status = fields(request(port, uuid.uuid4().hex[:8], 3.0))
        healthy(status)
        if status["mode"] != command:
            raise RuntimeError("test gravity expired or wrong mode")
        cx, cy = map(int, status["center"].split(","))
        moved = {"left": cx < 55, "right": cx > 125,
                 "up": cy < 28, "down": cy > 50}[command]
        if (moved and int(status["updates"]) > int(initial["updates"]) + 20
                and status["crc"] != initial["crc"]):
            print(f"PASS injected gravity {command}: center={cx},{cy}, raster changed, particles inside walls")
            return
    raise RuntimeError(f"liquid did not reach {command} side before timeout")

def check_response(port, timeout):
    request(port, uuid.uuid4().hex[:8], timeout, "reset")
    initial = fields(request(port, uuid.uuid4().hex[:8], timeout, "left"))
    started = time.monotonic()
    while time.monotonic() - started < timeout:
        time.sleep(0.05)
        status = fields(request(port, uuid.uuid4().hex[:8], 3.0))
        healthy(status)
        if status["mode"] != "left":
            raise RuntimeError("response test gravity is not active")
        if int(status["center"].split(",")[0]) < 55:
            elapsed = time.monotonic() - started
            frames = int(status["updates"]) - int(initial["updates"])
            print(f"PASS response: center crossed x=55 in {elapsed:.3f}s ({frames} frames), frame_ms={status['frame_ms']}")
            return
    raise RuntimeError("response test timed out")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--wait-for-tilt", action="store_true",
                        help="wait for a physical left/right tilt of the board")
    parser.add_argument("--case", choices=["all", "health", "left", "right", "up", "down", "reset", "response"],
                        default="all", help="independent hardware checks; directions inject test gravity")
    args = parser.parse_args()
    results = pathlib.Path(__file__).with_name("test-results")
    results.mkdir(exist_ok=True)
    path = results / f"waveform-{time.strftime('%Y%m%d-%H%M%S')}.log"
    with path.open("w", encoding="utf-8") as output, \
         contextlib.redirect_stdout(Tee(sys.stdout, output)), \
         contextlib.redirect_stderr(Tee(sys.stderr, output)):
        print(f"LOG: {path}")
        try:
            with serial.Serial(args.port, 115200, timeout=0.25) as port:
                port.reset_input_buffer()
                token = uuid.uuid4().hex[:8]
                status = request(port, token, args.timeout)
                first = fields(status)
                healthy(first)
                time.sleep(0.5)
                second = fields(request(port, uuid.uuid4().hex[:8], args.timeout))
                healthy(second)
                for counter in ("updates", "motion_reads", "reads"):
                    if int(second[counter]) <= int(first[counter]):
                        raise RuntimeError(f"{counter} did not advance")
                print(f"PASS health: fluid, BMI270, and FT5406 counters advancing; frame_ms={second['frame_ms']}")
                try:
                    if args.case == "response":
                        check_response(port, args.timeout)
                    if args.case in ("all", "reset"):
                        reset = fields(request(port, uuid.uuid4().hex[:8], args.timeout, "reset"))
                        if reset["center"] != "87,52" or reset["mode"] != "imu":
                            raise RuntimeError("reset did not restore the initial particle pool")
                        print("PASS reset: liquid volume and position restored")
                    cases = ["left", "right", "up", "down"] if args.case == "all" else [args.case]
                    for case in cases:
                        if case in ("left", "right", "up", "down"):
                            check_direction(port, case, args.timeout)
                finally:
                    restored = fields(request(port, uuid.uuid4().hex[:8], args.timeout, "auto"))
                    if restored["mode"] != "imu":
                        raise RuntimeError("could not restore live accelerometer control")
                    print("PASS live accelerometer control restored (mode=imu)")
                if args.wait_for_tilt:
                    print("ACTION: tilt the board left or right now")
                    baseline = tuple(map(int, restored["accel"].split(",")))
                    deadline = time.monotonic() + args.timeout
                    while time.monotonic() < deadline:
                        tilt_status = request(port, uuid.uuid4().hex[:8], 3.0)
                        current = fields(tilt_status)
                        healthy(current)
                        accel = tuple(map(int, current["accel"].split(",")))
                        delta = max(abs(a - b) for a, b in zip(accel, baseline))
                        if current["mode"] == "imu" and delta >= 150:
                            print(f"PASS: physical accelerometer change captured ({delta} mg)")
                            break
                        time.sleep(0.25)
                    else:
                        raise RuntimeError("no physical accelerometer change of at least 150 mg captured")
        except (serial.SerialException, RuntimeError, ValueError, KeyError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
