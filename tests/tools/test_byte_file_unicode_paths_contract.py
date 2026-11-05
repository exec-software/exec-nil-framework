#!/usr/bin/env python3
"""The independent filename oracle must reject plausible false positives."""
from pathlib import Path
import shutil
import tempfile
import unittest
import unicodedata

from byte_file_unicode_paths_contract import (
    NAMES, check_namespace, prepare_namespace, private_workspace, summarize_report,
)


class ReportSummaryTests(unittest.TestCase):
    def test_failure_summary_identifies_retained_evidence(self):
        report = {'status': 'FAIL', 'executions': [], 'work_directory': '/private/fixture',
                  'work_directory_retained': True, 'error': 'namespace mismatch'}
        self.assertEqual(summarize_report(report), {
            'status': 'FAIL', 'executions': 0, 'work_directory': '/private/fixture',
            'work_directory_retained': True, 'error': 'namespace mismatch'})

    def test_success_summary_reports_verified_removal(self):
        report = {'status': 'PASS', 'executions': [{}], 'work_directory': '/private/fixture',
                  'work_directory_retained': False}
        self.assertEqual(summarize_report(report), {
            'status': 'PASS', 'executions': 1, 'work_directory': '/private/fixture',
            'work_directory_retained': False})

    def test_setup_failure_does_not_invent_retained_evidence(self):
        summary = summarize_report({'status': 'FAIL', 'executions': [], 'error': 'missing input'})
        self.assertIsNone(summary['work_directory'])
        self.assertFalse(summary['work_directory_retained'])


class WorkspaceEvidenceTests(unittest.TestCase):
    def test_successful_workspace_cleanup_is_verified(self):
        report = {}
        with private_workspace(report) as root:
            (root / 'evidence').write_bytes(b'complete')
        self.assertFalse(root.exists())
        self.assertEqual(report['work_directory'], str(root))
        self.assertFalse(report['work_directory_retained'])

    def test_failed_workspace_preserves_evidence(self):
        report = {}
        with self.assertRaisesRegex(RuntimeError, 'intentional failure'):
            with private_workspace(report) as root:
                self.addCleanup(shutil.rmtree, root)
                (root / 'evidence').write_bytes(b'partial')
                raise RuntimeError('intentional failure')
        self.assertTrue(report['work_directory_retained'])
        self.assertEqual((root / 'evidence').read_bytes(), b'partial')


class NamespaceOracleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-path-oracle-unit-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'data'
        self.arguments, self.expected = prepare_namespace(self.root)
        for name, content in self.expected.items():
            if content is not None:
                (self.root / name).write_bytes(content)

    def test_exact_namespace_and_all_bytes_pass(self):
        self.assertEqual(len(self.arguments), 32)
        self.assertEqual(len(check_namespace(self.root, self.expected)), 34)
        self.assertTrue(all(not Path(value).is_absolute() for value in self.arguments[:16]))
        self.assertTrue(all(Path(value).is_absolute() for value in self.arguments[16:]))
        self.assertNotEqual(NAMES[0], NAMES[3])
        for name, content in self.expected.items():
            if name.endswith('.input.bin'):
                self.assertEqual(set(content), set(range(256)))

    def test_mojibake_alias_is_not_a_successful_roundtrip(self):
        name = next(name for name in self.expected if name.endswith('.new.bin'))
        original = self.root / name
        original.rename(original.with_name(original.name.replace('caf\u00e9', 'caf\u00c3\u00a9')))
        with self.assertRaisesRegex(AssertionError, 'namespace mismatch'):
            check_namespace(self.root, self.expected)

    def test_normalized_output_spelling_is_rejected(self):
        name = next(name for name in self.expected
                    if NAMES[3] in name and name.endswith('.new.bin'))
        original = self.root / name
        content = original.read_bytes()
        original.unlink()
        # Recreate instead of renaming: some hosts can treat an equivalent-name
        # rename as a no-op. This deliberately changes the stored spelling.
        normalized = original.with_name(unicodedata.normalize('NFC', original.name))
        normalized.write_bytes(content)
        with self.assertRaisesRegex(AssertionError, 'namespace mismatch'):
            check_namespace(self.root, self.expected)

    def test_missing_file_rejected(self):
        (self.root / next(name for name in self.expected if name.endswith('.new.bin'))).unlink()
        with self.assertRaisesRegex(AssertionError, 'namespace mismatch'):
            check_namespace(self.root, self.expected)

    def test_changed_protected_sentinel_rejected(self):
        (self.root / next(name for name in self.expected if name.endswith('.protected.bin'))).write_bytes(b'clobbered')
        with self.assertRaisesRegex(AssertionError, 'file bytes differ'):
            check_namespace(self.root, self.expected)

    def test_wrong_replacement_payload_rejected(self):
        (self.root / next(name for name in self.expected if name.endswith('.replace.bin'))).write_bytes(b'old')
        with self.assertRaisesRegex(AssertionError, 'file bytes differ'):
            check_namespace(self.root, self.expected)

    def test_staging_orphan_rejected(self):
        (self.root / '.ny-write-orphan').write_bytes(b'partial')
        with self.assertRaisesRegex(AssertionError, 'namespace mismatch'):
            check_namespace(self.root, self.expected)

    def test_directory_instead_of_output_rejected(self):
        path = self.root / next(name for name in self.expected if name.endswith('.new.bin'))
        path.unlink()
        path.mkdir()
        with self.assertRaisesRegex(AssertionError, 'expected a regular file'):
            check_namespace(self.root, self.expected)


if __name__ == '__main__':
    unittest.main()
