"""Host tests for the independent record inspector and algorithm vectors."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/python"))
from psoc_e84_tools.encrypted_files_test import parse_record
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


class FormatTests(unittest.TestCase):
    def record(self, algorithm=1, bits=256, length=16):
        header = (b"EFS1" + bytes([1, algorithm]) + bits.to_bytes(2, "little")
                  + length.to_bytes(4, "little")
                  + (0x2300 + algorithm).to_bytes(4, "little") + (9).to_bytes(8, "little")
                  + (0x2300 + algorithm).to_bytes(4, "little") + bytes(4))
        return header + bytes(length + 16)

    def test_all_algorithms_and_lengths(self):
        for algorithm, bits in ((1, 256), (2, 128), (3, 256)):
            for length in (0, 1, 16, 1024):
                self.assertEqual(parse_record(self.record(algorithm, bits, length),
                                              algorithm, bits, "file", length), 9)

    def test_truncated_and_extended(self):
        data = self.record()
        for bad in (data[:-1], data + b"\0", data[:10]):
            with self.assertRaises(AssertionError):
                parse_record(bad, 1, 256, "file", 16)

    def test_header_fields(self):
        for offset in (0, 4, 5, 6, 8, 12, 24, 28):
            bad = bytearray(self.record())
            bad[offset] ^= 1
            with self.assertRaises(AssertionError):
                parse_record(bad, 1, 256, "file", 16)

    def test_oracle_against_nist_gcm_vectors(self):
        for bits, expected in (
            (128, "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf"),
            (256, "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919"),
        ):
            self.assertEqual(AESGCM(bytes(bits // 8)).encrypt(bytes(12), bytes(16), b""),
                             bytes.fromhex(expected))


if __name__ == "__main__":
    unittest.main()
