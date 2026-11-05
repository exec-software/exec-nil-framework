#!/usr/bin/env python3
"""Real socket backpressure controls for the checkpoint acceptance client."""
import importlib.util
from pathlib import Path
import socket
import threading
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "checkpoint_contract", Path(__file__).with_name("ai_checkpoint_contract.py"))
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


class SocketWrites(unittest.TestCase):
    def pair(self):
        writer, reader = socket.socketpair()
        self.addCleanup(writer.close)
        self.addCleanup(reader.close)
        writer.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
        writer.settimeout(.1)
        reader.settimeout(2)
        channel = contract.Channel.__new__(contract.Channel)
        channel.sock = writer
        return channel, writer, reader

    def test_delayed_reader_receives_complete_frame_and_poll_timeout_is_restored(self):
        channel, writer, reader = self.pair()
        payload = b'x' * 62000 + b'\n'
        received, errors = bytearray(), []

        def receive():
            try:
                # Exceed the read-poll interval while staying inside the
                # five-second whole-frame write deadline.
                time.sleep(.25)
                while len(received) < len(payload):
                    data = reader.recv(4096)
                    if not data:
                        break
                    received.extend(data)
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=receive)
        worker.start()
        try:
            channel.write(payload)
        finally:
            writer.shutdown(socket.SHUT_WR)
            worker.join(3)
        self.assertFalse(worker.is_alive(), "owned reader did not stop")
        self.assertFalse(errors, errors)
        self.assertEqual(received, payload)
        self.assertEqual(writer.gettimeout(), .1)

    def test_stalled_reader_hits_frame_deadline_and_restores_poll_timeout(self):
        channel, writer, _ = self.pair()
        started = time.monotonic()
        with mock.patch.object(contract, "FRAME_WRITE_TIMEOUT", .3):
            with self.assertRaises(socket.timeout):
                channel.write(b'x' * 62000 + b'\n')
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(writer.gettimeout(), .1)

    def test_closed_peer_error_restores_poll_timeout(self):
        channel, writer, reader = self.pair()
        reader.close()
        with self.assertRaises(OSError):
            channel.write(b'frame\n')
        self.assertEqual(writer.gettimeout(), .1)


if __name__ == '__main__':
    unittest.main()
