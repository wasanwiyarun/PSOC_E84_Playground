#!/usr/bin/env python3
"""Exercise live LVGL frames and GFXSS/DSI health on the physical board."""

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


def exchange(port, command: str, token: str, timeout: float, *, retry: bool = False) -> dict[str, int]:
    print(f"--> display {command} {token}")
    port.write(f"display {command} {token}\n".encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    retry_at = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        if retry and time.monotonic() >= retry_at:
            port.write(f"\ndisplay {command} {token}\n".encode("ascii"))
            port.flush()
            retry_at = time.monotonic() + 1.0
        line = port.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        print(line)
        if "ASSERTION FAIL" in line or "ZEPHYR FATAL" in line:
            raise RuntimeError("firmware fault")
        # A USB/UART bridge can prefix a fresh reply with a truncated boot
        # line. Resynchronize at the status marker, then still require the
        # exact fresh token and all health fields below.
        offset = max(line.rfind("OK DISPLAY STATUS "), line.rfind("FAIL DISPLAY STATUS "))
        if offset < 0:
            continue
        line = line[offset:]
        fields = dict(part.split("=", 1) for part in line.split() if "=" in part)
        # Never accept buffered boot logs or a response from an earlier run.
        if fields.get("token") != token:
            continue
        if not line.startswith("OK DISPLAY STATUS "):
            raise RuntimeError(f"display reports unhealthy state for {token}")
        required = ("panel", "clock", "frames", "flushes", "fb", "crc",
                    "phy", "dc", "dsi0", "dsi1", "ret")
        values = {name: int(fields[name], 0) for name in required}
        if values["panel"] not in (0xC3, 0xDE) or values["clock"] not in (99_999_999, 100_000_000):
            raise RuntimeError("incorrect panel ID or peripheral clock")
        if any(values[name] != 0 for name in ("dc", "dsi0", "dsi1", "ret")):
            raise RuntimeError("display controller or DSI error")
        if not values["phy"] & 1 or values["frames"] < 2 or values["flushes"] < 1:
            raise RuntimeError("PHY unlocked or no completed LVGL frame")
        if not 0x26200000 <= values["fb"] < 0x26500000 or values["fb"] % 128:
            raise RuntimeError("invalid framebuffer address")
        return values
    raise RuntimeError(f"no fresh status response for {token}")


def run_checks(port, timeout: float) -> None:
    token = uuid.uuid4().hex[:8]
    port.reset_input_buffer()
    port.write(b"\n")
    # Retry only this read-only handshake while firmware may still be booting.
    exchange(port, "info", f"{token}-ready", timeout, retry=True)
    baseline = exchange(port, "demo", f"{token}-start", timeout)
    previous = baseline
    buffers = {baseline["fb"]}
    for index in range(3):
        current = exchange(port, "test", f"{token}-{index}", timeout)
        if current["flushes"] <= previous["flushes"]:
            raise RuntimeError("LVGL did not submit another frame")
        if current["frames"] - previous["frames"] < 2:
            raise RuntimeError("frame swap did not complete")
        if current["crc"] == previous["crc"]:
            raise RuntimeError("rendered framebuffer did not change")
        buffers.add(current["fb"])
        previous = current
        print(f"PASS: rendered update {index + 1}, CRC changed, scan-out completed")
    if len(buffers) != 2:
        raise RuntimeError("expected both LVGL buffers to be used")
    restored = exchange(port, "demo", f"{token}-end", timeout)
    if restored["crc"] != baseline["crc"]:
        raise RuntimeError("restoring the demo did not restore the framebuffer")
    time.sleep(0.2)
    final = exchange(port, "info", f"{token}-health", timeout)
    if final["frames"] <= restored["frames"]:
        raise RuntimeError("scan-out stopped after the last update")
    print("PASS: panel ID, clocks, LVGL rendering, buffer swaps, frame completion, and DSI health")
    print("VISUAL CHECK REQUIRED: confirm the text on the physical LCD; UART cannot verify pixels.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--require-lvgl", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--log", type=pathlib.Path)
    args = parser.parse_args()

    result_dir = pathlib.Path(__file__).with_name("test-results")
    result_dir.mkdir(exist_ok=True)
    log_path = args.log or result_dir / f"lvgl-display-{time.strftime('%Y%m%d-%H%M%S')}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as output:
        with contextlib.redirect_stdout(Tee(sys.stdout, output)), \
             contextlib.redirect_stderr(Tee(sys.stderr, output)):
            print(f"LOG: {log_path}")
            try:
                with serial.Serial(args.port, 115200, timeout=0.25) as port:
                    run_checks(port, args.timeout)
            except serial.SerialException as error:
                print(f"FAIL: serial connection error: {error}", file=sys.stderr)
                return 2
            except (RuntimeError, ValueError, KeyError) as error:
                print(f"FAIL: {error}", file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
