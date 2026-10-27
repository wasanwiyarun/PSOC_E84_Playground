#!/usr/bin/env python3
"""Hardware tests for CHACHA20_POLY1305; see README before running."""
from pathlib import Path
import sys

APP = Path(__file__).resolve().parent
sys.path.insert(0, str(APP.parent / "tools/python"))
from psoc_e84_tools.encrypted_files_test import main

if __name__ == "__main__":
    raise SystemExit(main(APP, 3, 256, "CHACHA20_POLY1305"))

