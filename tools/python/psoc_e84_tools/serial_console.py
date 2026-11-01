"""Small, dependency-free helpers for line-oriented serial firmware tests."""

import sys
import time


class LineConsole:
    """Buffered, logged line transport for token-based firmware protocols.

    The caller owns/open/closes the serial port. Retain partial lines across
    short reads and retain already-read lines for transaction assertions.
    Command syntax and success/failure interpretation belong to each example.
    """

    def __init__(self, port, log):
        self.serial = port
        self.log = log
        self.lines = []
        self.buffer = b""

    def send(self, command):
        self.log.write("> " + command + "\n")
        self.log.flush()
        self.serial.write((command + "\n").encode())
        self.serial.flush()

    def wait(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b"\n" not in self.buffer:
                self.buffer += self.serial.read(max(1, self.serial.in_waiting))
                continue
            data, self.buffer = self.buffer.split(b"\n", 1)
            line = data.decode("ascii", errors="replace").strip()
            if not line:
                continue
            self.log.write(line + "\n")
            self.log.flush()
            self.lines.append(line)
            if "FATAL" in line or "ASSERTION FAIL" in line:
                raise AssertionError(line)
            if predicate(line):
                return line
        raise TimeoutError("No expected response; last lines: " + repr(self.lines[-4:]))


class Tee:
    """Write to every supplied text stream."""

    def __init__(self, *streams):
        self.streams = streams

    def write(self, text):
        for stream in self.streams:
            stream.write(text)
        return len(text)

    def flush(self):
        for stream in self.streams:
            stream.flush()


def wait_for(port, expected, timeout):
    """Print serial data until *expected* arrives or the deadline expires."""
    deadline = time.monotonic() + timeout
    expected_bytes = expected.encode()
    received = b""

    while time.monotonic() < deadline:
        data = port.read(port.in_waiting or 1)
        if not data:
            continue
        sys.stdout.write(data.decode(errors="replace"))
        sys.stdout.flush()
        received += data
        if expected_bytes in received:
            return True
        received = received[-max(len(expected_bytes), 2048):]
    return False
