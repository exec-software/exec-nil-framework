#!/usr/bin/env python3
"""Pure controls for bounded diagnostic parsing; no Darwin availability claims."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import time
import unittest

PATH = Path(__file__).with_name('macos-cache-probes.py')
spec = importlib.util.spec_from_file_location('cache_probes', PATH)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class DependencyRecords(unittest.TestCase):
    def test_consumed_missing_and_output_remain_distinct(self):
        data = b'\x00ld version\0\x10/a file/lib.tbd\0\x11/new candidate\0\x40/output\0'
        rows = probe.dependency_records(data)
        self.assertEqual([r['opcode'] for r in rows], [0, 16, 17, 64])
        self.assertEqual(rows[1]['value'], '/a file/lib.tbd')

    def test_unknown_truncated_empty_and_duplicate_version_fail(self):
        for data in (b'', b'\x00version', b'\x00\0', b'\x10/file\0',
                     b'\x00v\0\x20/file\0', b'\x00v\0\x10\0', b'\x00v\0\x00v\0'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                probe.dependency_records(data)

    def test_trailing_unterminated_record_fails(self):
        with self.assertRaises(ValueError):
            probe.dependency_records(b'\x00v\0\x10/path')


class BackingFiles(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name).resolve() / 'backing'
        self.path.write_bytes(b'backing bytes')

    def budget(self, size=1024, seconds=10):
        return {'remaining_bytes': size, 'deadline': time.monotonic() + seconds}

    def test_exact_hash_and_alias_are_recorded(self):
        alias = self.path.with_name('alias')
        try:
            alias.symlink_to(self.path)
        except OSError as error:
            self.skipTest('Host cannot create the controlled symlink: ' + str(error))
        row = probe.file_observation(alias, full_hash=True, budget=self.budget())
        self.assertTrue(row['complete'])
        self.assertEqual(row['path'], str(alias))
        self.assertEqual(row['canonical_path'], str(self.path))
        self.assertEqual(row['sha256'], hashlib.sha256(b'backing bytes').hexdigest())

    def test_missing_file_is_explicit_incomplete(self):
        row = probe.file_observation(self.path.with_name('missing'), full_hash=True, budget=self.budget())
        self.assertFalse(row['complete'])
        self.assertIn('FileNotFoundError', row['error'])
        self.assertNotIn('sha256', row)

    def test_insufficient_aggregate_budget_never_certifies_prefix(self):
        row = probe.file_observation(self.path, full_hash=True, budget=self.budget(size=2))
        self.assertFalse(row['complete'])
        self.assertNotIn('sha256', row)

    def test_expired_hash_deadline_is_incomplete(self):
        row = probe.file_observation(self.path, full_hash=True, budget=self.budget(seconds=-1))
        self.assertFalse(row['complete'])
        self.assertIn('TimeoutError', row['error'])

    def test_relative_input_is_not_silently_rebased(self):
        self.assertFalse(probe.file_observation('backing')['complete'])

    def test_uuid_header_does_not_replace_byte_hash(self):
        contents = b'dyld_v1 arm64'.ljust(88, b'\0') + bytes(range(16)) + b'payload'
        self.path.write_bytes(contents)
        row = probe.file_observation(self.path, full_hash=True, budget=self.budget())
        self.assertTrue(row['complete'])
        self.assertEqual(row['cache_header_uuid'], bytes(range(16)).hex())
        self.assertEqual(row['sha256'], hashlib.sha256(contents).hexdigest())


if __name__ == '__main__':
    unittest.main()
