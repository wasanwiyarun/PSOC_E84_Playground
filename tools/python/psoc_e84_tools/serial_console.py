"""Small, dependency-free helpers for line-oriented serial firmware tests."""

import sys
import time


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
