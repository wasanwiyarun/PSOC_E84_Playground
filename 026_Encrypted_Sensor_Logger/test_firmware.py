#!/usr/bin/env python3
"""Physical logger acceptance; only uniquely named test recordings are mutated.

No erase-all or overwrite. --init-blank permits format only if the entire
dedicated 3 MiB partition is erased. Reboot tests are NOT physical power cuts.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time

import serial
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "tools/python"))
from psoc_e84_tools.encrypted_files_test import flash
from psoc_e84_tools.serial_console import LineConsole


def fields(text):
    return dict(re.findall(r"(\w+)=([^ ]+)", text))


def record_parse(data):
    assert data[:6] in (b"SLG1\x01\x01", b"SLG1\x01\x02")
    count, length, seq, session = struct.unpack_from("<HIIQ", data, 6)
    assert len(data) == 64 + length + 16
    assert length == (count * 40 if data[5] == 1 else 8)
    assert data[24:28] == data[36:40] == struct.pack("<I", 0x2601)
    assert data[56:64] == bytes(8)
    return {"seq": seq, "session": session, "nonce": data[24:36], "count": count,
            "previous": data[40:56], "tag": data[-16:]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--init-blank", action="store_true")
    parser.add_argument("--soak-samples", type=int, default=0,
                        help="optional extended recording (>=1040) to cross a nonce reservation boundary")
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    if args.soak_samples and args.soak_samples < 1040:
        parser.error("--soak-samples must be 0 or at least 1040")
    output = args.results or ROOT / "test-results" / datetime.now().strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True, exist_ok=True)
    result = {"passed": False, "checks": [], "time_utc": datetime.now(timezone.utc).isoformat(),
              "physical_power_cut_tested": False, "visual_touch_confirmed": False}
    for core, image in (("cm55", "zephyr.hex"), ("cm33-ns", "tfm_merged.hex")):
        image_path = ROOT / f"build-{core}/zephyr/{image}"
        if image_path.exists():
            result[f"{core}_sha256"] = hashlib.sha256(image_path.read_bytes()).hexdigest()
    prefix = "t" + os.urandom(5).hex()
    result["test_prefix"] = prefix

    def check(name):
        result["checks"].append(name)
        print("PASS", name, flush=True)

    try:
        with (output / "serial.log").open("w") as log, serial.Serial(args.port, 115200, timeout=.05) as port:
            console = LineConsole(port, log)
            if args.flash:
                flash(ROOT / "build-cm55", output / "flash-cm55.log")
                flash(ROOT / "build-cm33-ns", output / "flash-cm33-ns.log")
                # Discard prior firmware/reboots accumulated during programming.
                port.reset_input_buffer()
                check("physical_paired_firmware_flash")
                console.wait(lambda x: x.startswith("READY LOGGER version="), 30)

            def command(text, expected="OK", timeout=40):
                start = len(console.lines)
                console.send(text)
                verb = text.split()[1] if text.startswith("log ") else ""
                success = {"status": "OK STATUS", "read": "OK READ", "verify": "OK READ",
                           "raw": "OK RAW", "list": "OK LIST", "reboot": "OK REBOOT"}.get(
                               verb, "OK COMMAND cmd=" + verb)
                def matches(line):
                    if line.startswith("ERR COMMAND cmd="):
                        return fields(line).get("cmd") == verb
                    return line.startswith(success) or line.startswith(("ERR COMMAND rc=", "ERR COMMAND_TOO_LONG", "ERR QUEUE"))
                reply = console.wait(matches, timeout)
                assert reply.startswith(expected), (text, reply, expected)
                return reply, console.lines[start:]

            def status():
                return fields(command("log status", "OK STATUS")[0])

            def wait_samples(n):
                deadline = time.monotonic() + max(40, n * .25 + 30)
                while time.monotonic() < deadline:
                    s = status()
                    assert s["recording"] == "1" and s["rc"] == "0", s
                    if int(s["samples"]) >= n:
                        return s
                    time.sleep(.35)
                raise TimeoutError("Logger did not commit enough samples")

            def read(name):
                reply, lines = command(f"log read {name}", "OK READ")
                rows = [tuple(map(int, x[4:].split(","))) for x in lines if x.startswith("ROW ")]
                assert len(rows) == int(fields(reply)["samples"])
                assert all(len(x) == 9 for x in rows)
                return fields(reply), rows

            def raw(name, index):
                reply, _ = command(f"log raw {name} {index}", "OK RAW")
                data = bytes.fromhex(reply.split("hex=", 1)[1])
                return data, record_parse(data)

            time.sleep(3)
            s = status()
            if s["mounted"] != "1" and args.init_blank:
                command("log init-blank", timeout=120)
                s = status()
            assert s["mounted"] == s["key"] == s["sensor"] == s["display"] == s["touch"] == "1", s
            assert s["recording"] == "0", "Stop your existing recording before testing"
            check("storage_secure_key_live_sensors_display_touch_ready")

            _, lines = command("log kat")
            actual = bytes.fromhex(next(x.split("hex=", 1)[1] for x in lines if x.startswith("DETAIL KAT hex=")))
            assert actual == AESGCM(bytes(range(32))).encrypt(bytes(range(12)), bytes(range(0x80, 0xa0)), b"SLG-KAT-v1")
            assert "DETAIL KAT wrong_key_rejected=1 persistent_key_export_denied=1" in lines
            check("aes256_gcm_matches_independent_python")

            complete, partial, after = (prefix + suffix for suffix in ("c", "p", "n"))
            before = status()
            command(f"log start {complete}")
            command(f"log read {complete}", "ERR")
            command(f"log delete {complete}", "ERR")
            command(f"log start {prefix}busy", "ERR")
            during = wait_samples(64)
            command("log stop")
            end = status()
            assert int(end["bytes"]) > 1024 and end["recording"] == "0", end
            assert int(end["renders"]) > int(before["renders"]) + 10
            assert end["dropped"] == "0" and end["errors"] == before["errors"]
            check("record_beyond_1KiB_no_drops_ui_continues_busy_guards")

            info, original = read(complete)
            assert info["sealed"] == "1" and len(original) >= 64
            assert all(b[0] > a[0] for a, b in zip(original, original[1:]))
            assert all(20000 < x[8] < 120000 and -40000 < x[7] < 100000 for x in original)
            result["recorded_samples"] = len(original)
            result["max_sample_gap_ms"] = max(b[0] - a[0] for a, b in zip(original, original[1:]))
            encrypted, rec0 = raw(complete, 0)
            _, rec1 = raw(complete, 1)
            assert rec0["seq"] == 0 and rec1["seq"] == 1
            assert rec0["session"] == rec1["session"]
            assert rec0["nonce"] != rec1["nonce"] and rec1["previous"] == rec0["tag"]
            assert struct.pack("<Q8i", *original[0]) not in encrypted
            check("verified_sensor_readback_ciphertext_nonce_and_chain")

            command(f"log start {complete}", "ERR")
            for text in ("log start ../bad", "log start " + "x" * 25,
                         f"log raw {complete} -1", "log stop extra", "log read", "log unknown"):
                command(text, "ERR")
            command("x" * 140, "ERR COMMAND_TOO_LONG")
            status()
            check("parser_bounds_path_and_overwrite_rejection")

            # Alter a later record: preflight must withhold even earlier plaintext.
            for part in ("header", "data", "tag"):
                command(f"log tamper {complete} 2 {part}")
                _, rejected = command(f"log read {complete}", "ERR")
                assert not any(x.startswith("ROW ") for x in rejected)
                command(f"log tamper {complete} 2 {part}")
                assert read(complete)[1] == original
            check("late_record_header_ciphertext_tag_tamper_no_plaintext_leak")

            command("log reboot", "OK REBOOT")
            console.wait(lambda x: x.startswith("READY LOGGER"), 25)
            time.sleep(1)
            assert read(complete)[1] == original
            check("sealed_file_key_persist_after_reboot")

            command(f"log start {partial}")
            committed = wait_samples(32)
            command("log reboot", "OK REBOOT")
            console.wait(lambda x: x.startswith("READY LOGGER"), 25)
            time.sleep(1)
            partial_info, partial_rows = read(partial)
            assert partial_info["sealed"] == "0" and len(partial_rows) >= int(committed["samples"])
            command(f"log start {after}")
            wait_samples(16)
            command("log stop")
            _, newer = raw(after, 0)
            assert int.from_bytes(newer["nonce"][4:], "little") > int(committed["nonce"])
            assert newer["nonce"] not in (rec0["nonce"], rec1["nonce"])
            check("interrupted_log_recovers_synced_prefix_nonce_not_reused")

            _, listing = command("log list", "OK LIST")
            for name in (complete, partial, after):
                assert any(f"name={name} " in x for x in listing), listing
                command(f"log delete {name}")
                command(f"log read {name}", "ERR")
            if args.soak_samples:
                soak = prefix + "s"
                before_soak = status()
                command(f"log start {soak}")
                wait_samples(args.soak_samples)
                command("log stop")
                after_soak = status()
                assert after_soak["rc"] == after_soak["dropped"] == "0", after_soak
                assert after_soak["errors"] == before_soak["errors"]
                assert int(after_soak["renders"]) > int(before_soak["renders"]) + 10
                soak_info, rows = read(soak)
                assert soak_info["sealed"] == "1" and len(rows) >= args.soak_samples
                gaps = [b[0] - a[0] for a, b in zip(rows, rows[1:])]
                assert min(gaps) > 0
                _, first = raw(soak, 0)
                _, last = raw(soak, int(soak_info["batches"]) - 1)
                first_nonce = int.from_bytes(first["nonce"][4:], "little")
                last_nonce = int.from_bytes(last["nonce"][4:], "little")
                assert last_nonce - first_nonce == int(soak_info["batches"]) - 1
                assert (first_nonce - 1) // 64 < (last_nonce - 1) // 64
                result["soak"] = {"samples": len(rows), "batches": int(soak_info["batches"]),
                                  "max_sample_gap_ms": max(gaps),
                                  "duration_ms": rows[-1][0] - rows[0][0],
                                  "first_nonce": first_nonce, "last_nonce": last_nonce,
                                  "sensor_errors": int(after_soak["errors"]),
                                  "dropped": int(after_soak["dropped"])}
                command(f"log delete {soak}")
                command(f"log read {soak}", "ERR")
                check("extended_recording_nonce_reservation_rollover_no_drops")
            final = status()
            assert final["recording"] == "0" and final["display"] == "1" and final["dropped"] == "0"
            check("list_delete_only_test_files_final_idle_display_healthy")
            result["final_status"] = final
            result["passed"] = True
    except Exception as exc:
        result["error"] = str(exc)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
        print("Results:", output, flush=True)


if __name__ == "__main__":
    main()
