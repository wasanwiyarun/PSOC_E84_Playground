"""Hardware acceptance tests shared by examples 023, 024 and 025.

Only uniquely named test files are modified. No format/erase-all operation.
--init-blank explicitly asks firmware to initialize an entirely erased area.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from datetime import datetime, timezone

from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
import serial

from .serial_console import LineConsole


def parse_record(data, algorithm, bits, name, expected_length):
    assert len(data) == 32 + expected_length + 16, "record length mismatch"
    assert data[:6] == b"EFS1" + bytes([1, algorithm]), "wrong format/algorithm"
    assert int.from_bytes(data[6:8], "little") == bits
    assert int.from_bytes(data[8:12], "little") == expected_length
    assert int.from_bytes(data[24:28], "little") == 0x2300 + algorithm
    assert data[28:32] == bytes(4)
    assert data[12:16] == data[24:28], "nonce domain differs from key ID"
    return int.from_bytes(data[16:24], "little")


def flash(build_dir, log_path):
    """Only program the addresses in the merged image, never mass erase."""
    zephyr = Path("/home/wasanw/zephyrproject/zephyr")
    west = Path("/home/wasanw/zephyrproject/.venv/bin/west")
    env = dict(os.environ, ZEPHYR_BASE=str(zephyr))
    env["PATH"] = str(west.parent) + os.pathsep + env.get("PATH", "")
    with log_path.open("w") as out:
        subprocess.run([str(west), "flash", "-d", str(build_dir), "--no-rebuild"],
                       cwd=zephyr.parent, env=env, stdout=out, stderr=subprocess.STDOUT,
                       check=True, timeout=180)
        subprocess.run([
            "/opt/Tools/ModusToolboxProgtools-1.7/openocd/bin/openocd",
            "-s", "/opt/Tools/ModusToolboxProgtools-1.7/openocd/scripts",
            "-f", "interface/kitprog3.cfg", "-f", "target/infineon/pse84xgxs2.cfg",
            "-c", "init; reset run; shutdown"], stdout=out, stderr=subprocess.STDOUT,
            check=True, timeout=60)


def main(example_dir, algorithm, bits, algorithm_name):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--init-blank", action="store_true")
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    example_dir = Path(example_dir)
    output = args.results or example_dir / "test-results" / datetime.now().strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True, exist_ok=True)
    results = {"algorithm": algorithm_name, "time_utc": datetime.now(timezone.utc).isoformat(),
               "checks": [], "passed": False, "physical_power_cut_tested": False}
    image = example_dir / "build-cm33-ns/zephyr/tfm_merged.hex"
    if image.exists():
        results["image_sha256"] = hashlib.sha256(image.read_bytes()).hexdigest()
    prefix = "t" + os.urandom(5).hex()
    results["test_prefix"] = prefix

    def check(name):
        results["checks"].append(name)
        print("PASS", name, flush=True)

    try:
        with (output / "serial.log").open("w") as log, serial.Serial(args.port, 115200, timeout=0.05) as port:
            console = LineConsole(port, log)
            if args.flash:
                flash(example_dir / "build-cm33-ns", output / "flash.log")
                console.wait(lambda x: x.startswith("READY EFS"), timeout=30)
                check("physical_board_flash")

            def command(text, expected="OK", timeout=30):
                console.send(text)
                line = console.wait(lambda x: x.startswith(("OK ", "ERR ", "FAIL ")), timeout)
                assert line.startswith(expected), (text, line, expected)
                return line

            def info():
                text = command("efs info", "OK INFO")
                assert f"alg={algorithm_name} " in text and "key=1 " in text, text
                return dict(re.findall(r"(\w+)=([^ ]+)", text))

            def raw(name, length):
                data = bytes.fromhex(command(f"efs raw {name}", "OK RAW hex=").split("hex=", 1)[1])
                seq = parse_record(data, algorithm, bits, name, length)
                return data, seq

            def read(name, data):
                result = command(f"efs read {name}", "OK READ hex=")
                assert bytes.fromhex(result.split("hex=", 1)[1]) == data

            def write(name, data):
                return command(f"efs write {name} {data.hex() or '-'}", "OK WRITE")

            initial = info()
            assert int(initial["offset"], 16) == {1: 0xb00000, 2: 0xe00000, 3: 0xf00000}[algorithm]
            assert int(initial["size"]) == 0x100000
            check("identity_partition_persistent_key")
            if initial["mounted"] != "1":
                assert args.init_blank, "Area not mounted; use --init-blank only for an erased area"
                command("efs init", "OK INIT", timeout=60)
            assert info()["mounted"] == "1"
            check("littlefs_mount")

            text = command("efs kat", "OK KAT")
            ciphertext = bytes.fromhex(text.split("hex=", 1)[1].split()[0])
            cipher = ChaCha20Poly1305(bytes(range(32))) if algorithm == 3 else AESGCM(bytes(range(bits // 8)))
            assert ciphertext == cipher.encrypt(bytes(range(12)), bytes(range(0x80, 0xa0)), b"EFS-KAT-v1")
            assert "tamper_rejected=1" in text
            check("independent_python_crypto_vector_and_tag_rejection")

            name = prefix + "file"
            lengths = [0, 1, 15, 16, 17, 255, 256, 1024]
            sequences = []
            timings = []
            for length in lengths:
                data = bytes((i * 7 + length) & 255 for i in range(length))
                begin = time.monotonic()
                result = write(name, data)
                timings.append({"bytes": length, "write_uart_ms": round((time.monotonic() - begin) * 1000, 2),
                                "crypto_ms": int(re.search(r"crypto_ms=(\d+)", result)[1])})
                read(name, data)
                encoded, sequence = raw(name, length)
                sequences.append(sequence)
                if length >= 32:
                    assert data not in encoded, "plaintext appears in stored record"
            assert all(a < b for a, b in zip(sequences, sequences[1:]))
            results["timings"] = timings
            check("binary_empty_boundary_max_length_and_monotonic_nonces")

            data = b"Persistent encrypted file: " + os.urandom(32)
            write(name, data)
            before, seq_before = raw(name, len(data))
            write(name, data)
            after, seq_after = raw(name, len(data))
            assert seq_after > seq_before and before != after
            check("rewrite_uses_new_nonce_and_ciphertext")
            command(f"efs wrongkey {name}", "OK WRONGKEY rejected=1")
            read(name, data)
            check("wrong_key_rejected_original_unchanged")

            for field in ("header", "nonce", "data", "tag"):
                command(f"efs tamper {name} {field}", "OK TAMPER")
                command(f"efs read {name}", "ERR")
                command(f"efs tamper {name} {field}", "OK TAMPER")
                read(name, data)
            check("header_nonce_ciphertext_tag_tampering_rejected")

            command("efs reboot", "OK REBOOT")
            console.wait(lambda x: x.startswith("READY EFS"), timeout=30)
            assert info()["mounted"] == "1"
            read(name, data)
            write(name, data)
            _, next_seq = raw(name, len(data))
            assert next_seq > seq_after
            check("reboot_key_file_and_nonce_persistence")

            for stage in ("partial", "synced"):
                command(f"efs interrupt {name} {b'new-uncommitted-data'.hex()} {stage}", "OK INTERRUPT")
                console.wait(lambda x: x.startswith("READY EFS"), timeout=30)
                info()
                read(name, data)
                write(name, data)
                _, new_seq = raw(name, len(data))
                assert new_seq > next_seq + 1, "interrupted nonce reused"
                next_seq = new_seq
            check("interrupted_update_partial_and_synced_preserve_old_file")

            for bad in ("efs write ../bad 00", f"efs write {name} zz", f"efs write {name} 0",
                        f"efs write {name} " + "aa" * 1025, "efs read " + "a" * 25,
                        "efs info extra", "efs unknown", "efs write " + "a" * 2200):
                command(bad, "ERR")
            info()
            read(name, data)
            check("invalid_commands_and_overflow_rejected_no_file_damage")
            command("efs list", "OK LIST")
            command(f"efs delete {name}", "OK DELETE")
            command(f"efs read {name}", "ERR")
            check("list_delete_missing_file")
            results["passed"] = True
    except Exception as exc:
        results["error"] = repr(exc)
        print("FAIL", repr(exc), flush=True)
    finally:
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print("Evidence:", output, flush=True)
    return 0 if results["passed"] else 1
