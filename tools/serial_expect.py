#!/usr/bin/env python3
"""Print serial output and succeed only after an expected string is received."""

import argparse
import sys
import time

import serial


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Monitor a serial port until an expected string arrives. "
            "Exit 0 when it matches, 1 on timeout, and 2 for serial errors."
        )
    )
    parser.add_argument("--port", required=True, help="Serial device, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument(
        "--expect",
        required=True,
        action="append",
        help="String required in the received output; repeat to require several strings.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Maximum seconds to wait for all expected strings (default: 20).",
    )
    parser.add_argument(
        "--encoding", default="utf-8", help="Output encoding (default: utf-8)."
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.timeout <= 0:
        print("--timeout must be greater than zero", file=sys.stderr)
        return 2

    pending = set(args.expect)
    max_expected_length = max(len(item.encode(args.encoding)) for item in pending)
    rolling = b""
    deadline = time.monotonic() + args.timeout

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1) as device:
            print(
                f"Monitoring {args.port} at {args.baud} baud for: "
                f"{', '.join(repr(item) for item in args.expect)}",
                file=sys.stderr,
            )
            while time.monotonic() < deadline:
                data = device.read(device.in_waiting or 1)
                if not data:
                    continue

                sys.stdout.write(data.decode(args.encoding, errors="replace"))
                sys.stdout.flush()
                received = rolling + data

                for expected in tuple(pending):
                    if expected.encode(args.encoding) in received:
                        pending.remove(expected)
                        print(f"Matched: {expected!r}", file=sys.stderr)

                if not pending:
                    return 0

                rolling = received[-max_expected_length:]
    except serial.SerialException as error:
        print(f"Unable to open/read {args.port}: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("Serial monitor interrupted.", file=sys.stderr)
        return 130

    print(
        f"Timed out after {args.timeout:g}s; still waiting for: "
        f"{', '.join(repr(item) for item in sorted(pending))}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
