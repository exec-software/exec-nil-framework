#!/usr/bin/env python3
"""Negative controls for the independent native ordinary-I/O namespace oracle."""
from pathlib import Path
import tempfile
import unittest

import ordinary_io_native_contract as contract


class OracleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-native-io-oracle-unit-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'fixture'
        self.arguments, self.expected, self.environment = contract.prepare(self.root)

    def materialize_outputs(self):
        for name, contents in self.expected.items():
            if contents is not None:
                (self.root / name).write_bytes(contents)

    def check(self):
        return contract.support.check_namespace(self.root, self.expected)

    def test_missing_outputs_fail(self):
        self.assertEqual(self.check()['status'], 'FAIL')
        self.assertEqual(len(self.check()['missing']), 3)

    def test_exact_outputs_pass(self):
        self.materialize_outputs()
        self.assertEqual(self.check()['status'], 'PASS')
        self.assertFalse(Path(self.arguments[0]).is_absolute())
        self.assertTrue(Path(self.arguments[1]).is_absolute())
        self.assertEqual(len(self.environment), 1)

    def test_wrong_bytes_fail(self):
        self.materialize_outputs()
        Path(self.arguments[1] + '.bytes').write_bytes(b'wrong')
        self.assertEqual(self.check()['status'], 'FAIL')
        self.assertEqual(len(self.check()['changed']), 1)

    def test_normalization_alias_write_fails(self):
        self.materialize_outputs()
        (self.root / 'parent-café-日本語-🦊' / 'café.bytes').write_bytes(contract.PAYLOAD)
        self.assertEqual(self.check()['status'], 'FAIL')

    def test_cwd_sentinel_change_fails(self):
        self.materialize_outputs()
        (Path(self.arguments[2]) / 'sentinel').write_bytes(b'changed')
        self.assertEqual(self.check()['status'], 'FAIL')

    def test_unexpected_file_fails(self):
        self.materialize_outputs()
        (self.root / 'unexpected').write_bytes(b'')
        self.assertEqual(self.check()['status'], 'FAIL')
        self.assertEqual(self.check()['unexpected'], ['unexpected'])


if __name__ == '__main__':
    unittest.main()
