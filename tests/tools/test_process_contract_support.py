#!/usr/bin/env python3
"""Identity-qualified cleanup controls, including Darwin API failure cases."""
import ctypes as c
import errno
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import process_contract_support as support


class DarwinIdentityTests(unittest.TestCase):
    def setUp(self):
        support._darwin_api.cache_clear()
        self.pid, self.unique, self.version, self.state = 1234, 0x123456789, 0xf1234567, 2
        self.size, self.error, self.signal_error = 192, 0, 0
        self.tokens = []

        def info(pid, flavor, zombies, output, capacity):
            self.assertEqual((pid, flavor, zombies, capacity), (1234, 18, 1, 192))
            # Apple proc_bsdinfo + proc_uniqidentifierinfo ABI offsets, independent
            # of ctypes field names, so a mistaken structure declaration fails.
            raw = bytearray(192)
            struct.pack_into('=I', raw, 4, self.state)
            struct.pack_into('=I', raw, 12, self.pid)
            struct.pack_into('=Q', raw, 152, self.unique)
            struct.pack_into('=I', raw, 168, self.version)
            c.memmove(output, bytes(raw), len(raw))
            c.set_errno(self.error)
            return self.size

        def send(token, number):
            self.assertEqual(number, signal.SIGKILL)
            self.tokens.append(list(c.cast(token, c.POINTER(c.c_uint32 * 8)).contents))
            return self.signal_error

        self.lib = SimpleNamespace(proc_pidinfo=Mock(side_effect=info),
                                   proc_signal_with_audittoken=Mock(side_effect=send))
        self.loader = patch.object(c, 'CDLL', return_value=self.lib)
        self.loader.start()

    def tearDown(self):
        self.loader.stop()
        support._darwin_api.cache_clear()

    @property
    def identity(self):
        return {'pid': 1234, 'created': (0x123456789 << 32) | 0xf1234567}

    def test_identity_and_zombie_decode_from_native_layout(self):
        self.assertEqual(support._darwin_identity(1234), (self.identity['created'], 'R'))
        self.state = 5
        self.assertEqual(support._darwin_identity(1234), (self.identity['created'], 'Z'))

    def test_incomplete_or_foreign_identity_fails_closed(self):
        for name, bad in (('size', 191), ('size', 0), ('pid', 4321), ('unique', 0), ('state', 99)):
            original = getattr(self, name)
            try:
                setattr(self, name, bad)
                with self.subTest(name=name, value=bad), self.assertRaises(RuntimeError):
                    support._darwin_identity(1234)
            finally:
                setattr(self, name, original)

    def test_permission_errors_are_not_missing_processes(self):
        self.size, self.error = 0, errno.EPERM
        with patch.object(support.sys, 'platform', 'darwin'), \
                patch.object(support.os, 'name', 'posix'), self.assertRaises(PermissionError):
            support.process_alive(self.identity)

    def test_missing_process_is_stopped(self):
        self.size, self.error = 0, errno.ESRCH
        with patch.object(support.sys, 'platform', 'darwin'), patch.object(support.os, 'name', 'posix'):
            self.assertFalse(support.process_alive(self.identity))
            support.cleanup_process(self.identity)
        self.assertEqual(self.tokens, [])

    def test_stale_lifetime_or_version_cannot_authorize_signal(self):
        for attribute in ('unique', 'version'):
            original = getattr(self, attribute)
            try:
                setattr(self, attribute, original + 1)
                with self.subTest(attribute=attribute), \
                        patch.object(support.sys, 'platform', 'darwin'), patch.object(support.os, 'name', 'posix'):
                    self.assertFalse(support.process_alive(self.identity))
                    support.cleanup_process(self.identity)
            finally:
                setattr(self, attribute, original)
        self.assertEqual(self.tokens, [])

    def test_cleanup_uses_recorded_version_and_atomic_signal_api(self):
        with patch.object(support.sys, 'platform', 'darwin'), patch.object(support.os, 'name', 'posix'):
            support.cleanup_process(self.identity)
        self.assertEqual(self.tokens, [[0, 0, 0, 0, 0, 1234, 0, 0xf1234567]])

    def test_kernel_rejecting_reused_identity_is_safe_but_other_errors_fail(self):
        with patch.object(support.sys, 'platform', 'darwin'), patch.object(support.os, 'name', 'posix'):
            self.signal_error = errno.ESRCH
            support.cleanup_process(self.identity)
            self.signal_error = errno.EPERM
            with self.assertRaises(PermissionError):
                support.cleanup_process(self.identity)

    def test_missing_atomic_signal_api_has_no_pid_only_fallback(self):
        del self.lib.proc_signal_with_audittoken
        with self.assertRaises(AttributeError):
            support._darwin_identity(1234)


class NativeIdentityTests(unittest.TestCase):
    def test_stale_identity_never_kills_owned_live_fixture(self):
        support.check_process_identity_guards()

    def test_published_identity_round_trips_without_fixed_integer_width(self):
        with tempfile.TemporaryDirectory(prefix='nil process identity ') as name:
            path = Path(name) / 'identity.json'
            support.record_processes(path, os.getpid())
            identity, = json.loads(path.read_text(encoding='utf-8'))
            self.assertEqual(identity, support.process_identity(os.getpid()))
            self.assertTrue(support.process_alive(identity))
            self.assertFalse(path.with_name(path.name + '.pending').exists())

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin kernel PID-version signal validation')
    def test_kernel_rejects_stale_audit_token_without_harming_owned_child(self):
        child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            identity = support.process_identity(child.pid)
            _, lib, _ = support._darwin_api()
            token = (c.c_uint32 * 8)()
            token[5], token[7] = child.pid, (identity['created'] + 1) & 0xffffffff
            self.assertEqual(lib.proc_signal_with_audittoken(c.byref(token), signal.SIGKILL), errno.ESRCH)
            self.assertTrue(support.process_alive(identity))
            self.assertIsNone(child.poll())
            support.cleanup_process(identity)
            support.assert_stopped(identity)
            child.wait(timeout=5)
        finally:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
