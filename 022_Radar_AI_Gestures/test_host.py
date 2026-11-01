"""No board needed: verify the shared line reader and model import guards."""
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/python'))
from psoc_e84_tools.serial_console import LineConsole
from prepare_model import replace_once

class FakePort:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.sent = b''
        self.in_waiting = 1
    def read(self, _): return next(self.chunks, b'')
    def write(self, data): self.sent += data
    def flush(self): pass

class HostTests(unittest.TestCase):
    def test_split_and_buffered_lines(self):
        port = FakePort([b'\r\nDATA tok', b'en=x\r\nOK token=x\nOK token=y\n'])
        log = io.StringIO()
        reader = LineConsole(port, log)
        reader.send('info')
        self.assertEqual(port.sent, b'info\n')
        self.assertEqual(reader.wait(lambda s: s == 'OK token=x'), 'OK token=x')
        self.assertEqual(reader.wait(lambda s: s == 'OK token=y'), 'OK token=y')
        self.assertEqual(len(reader.lines), 3)
        self.assertIn('DATA token=x', log.getvalue())

    def test_timeout(self):
        with self.assertRaises(TimeoutError):
            LineConsole(FakePort([]), io.StringIO()).wait(lambda s: True, .001)

    def test_fatal(self):
        with self.assertRaises(AssertionError):
            LineConsole(FakePort([b'FATAL ERROR\n']), io.StringIO()).wait(lambda s: True)

    def test_import_guards(self):
        self.assertEqual(replace_once('abc', 'b', 'x'), 'axc')
        for text in ['ac', 'abbc']:
            with self.assertRaises(ValueError): replace_once(text, 'b', 'x')

if __name__ == '__main__': unittest.main()
