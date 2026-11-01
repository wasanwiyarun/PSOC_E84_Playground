#!/usr/bin/env python3
"""Verify the TF-M PSA Crypto acceptance markers over board UART."""
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

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log = args.log or Path(__file__).parent / "test-results" / f"secure-crypto-{datetime.now():%Y%m%d-%H%M%S}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as output:
        with contextlib.redirect_stdout(Tee(sys.stdout, output)), contextlib.redirect_stderr(Tee(sys.stderr, output)):
            print(f"LOG: {log}")
            try:
                with serial.Serial(args.port, 115200, timeout=0.1) as port:
                    checks = (
                        ("crypto info", "OK CRYPTO COMMANDS=aes,ecdh,ecdsa,all,info"),
                        ("crypto aes", "PASS CRYPTO AES_GCM"),
                        ("crypto ecdh", "PASS CRYPTO ECDH"),
                        ("crypto ecdsa", "PASS CRYPTO ECDSA"),
                        ("crypto all", "PASS SECURE_CRYPTO AES_GCM ECDH ECDSA"),
                    )
                    for text, expected in checks:
                        if not command(port, text, expected, args.timeout):
                            return 1
            except serial.SerialException as error:
                print(f"Serial error: {error}", file=sys.stderr)
                return 2
            print("PASS: TF-M AES-GCM, ECDH, and ECDSA services verified.")
            return 0

if __name__ == "__main__":
    raise SystemExit(main())
