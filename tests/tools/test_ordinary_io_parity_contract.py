"""Host-only oracle/ownership controls, not additional language executions."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).parent
REPO = HERE.resolve().parents[1]
spec = importlib.util.spec_from_file_location('ordinary_io', HERE / 'ordinary_io_parity_contract.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


class OutputDefaultsTests(unittest.TestCase):
    def test_chunked_file_hash_is_exact_and_bounded(self):
        with tempfile.TemporaryDirectory(prefix='ordinary-file-hash-') as folder:
            target = Path(folder) / 'input.bin'
            payload = bytes(range(256)) * 1025
            target.write_bytes(payload)
            self.assertEqual(M.file_record(target), {
                'bytes': len(payload), 'sha256': M.sha(payload)})
            with self.assertRaisesRegex(AssertionError, 'exceeds bound'):
                M.file_record(target, len(payload) - 1)

    def test_cli_defaults_are_derived_from_repository_not_checkout_spelling(self):
        args = M.argument_parser().parse_args([
            '--compiler', sys.executable, '--interpreter', sys.executable])
        self.assertEqual(args.repository, REPO)
        self.assertIsNone(args.source)
        self.assertIsNone(args.nilstd)
        self.assertIsNone(args.output)
        alternate = REPO.parent / 'alternate checkout'
        args = M.argument_parser().parse_args([
            '--compiler', sys.executable, '--interpreter', sys.executable,
            '--repository', str(alternate)])
        self.assertEqual(args.repository, alternate)
        self.assertIsNone(args.source)

    def test_default_output_is_unique_and_retained(self):
        with tempfile.TemporaryDirectory(prefix='ordinary-default-output-') as folder:
            with patch.object(M.tempfile, 'gettempdir', return_value=folder):
                first = M.allocate_output(REPO, None)
                second = M.allocate_output(REPO, None)
            self.assertNotEqual(first, second)
            self.assertEqual(first.parent, Path(folder).resolve())
            self.assertTrue(first.is_dir())
            self.assertTrue(second.is_dir())
            (first / 'sentinel').write_bytes(b'retained')
            self.assertEqual((first / 'sentinel').read_bytes(), b'retained')

    def test_explicit_and_default_repository_outputs_are_rejected_before_creation(self):
        with tempfile.TemporaryDirectory(prefix='ordinary-output-guard-') as folder:
            repository = Path(folder).resolve()
            for requested in (repository, repository / 'new-evidence'):
                with self.assertRaisesRegex(AssertionError, 'outside the repository'):
                    M.allocate_output(repository, requested)
            with patch.object(M.tempfile, 'gettempdir', return_value=folder):
                with self.assertRaisesRegex(AssertionError, 'outside the repository'):
                    M.allocate_output(repository, None)
            self.assertEqual(list(repository.iterdir()), [])


class NamespaceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ordinary-io-oracle-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.arguments, self.expected, self.profiles = M.prepare_files(self.root)
        for name, data in self.expected.items():
            if data is not None:
                (self.root / name).write_bytes(data)

    def test_complete_namespace_exact_bytes_and_route_inputs(self):
        self.assertEqual(M.check_namespace(self.root, self.expected)['status'], 'PASS')
        self.assertEqual(len(self.arguments), 12)
        for index in range(6):
            self.assertEqual(Path(self.arguments[2*index]).is_absolute(), bool(index % 2))
            name = self.profiles[index]['seed']
            self.assertEqual((self.root / name).read_bytes(), bytes([index,10,13,128,255]))
        self.assertNotEqual((self.root / 'normalization/café.seed').read_bytes(),
                            (self.root / 'normalization/cafe\u0301.seed').read_bytes())

    def test_missing_expected_file(self):
        (self.root / 'ascii-parent/plain.text').unlink()
        self.assertEqual(M.check_namespace(self.root, self.expected)['missing'], ['ascii-parent/plain.text'])

    def test_alias_write_does_not_count_as_intended_write(self):
        source = self.root / 'parent-café/café.text'
        wrong = self.root / 'parent-café/cafÃ©.text'
        wrong.write_bytes(source.read_bytes())
        source.unlink()
        result = M.check_namespace(self.root, self.expected)
        self.assertIn('parent-café/café.text', result['missing'])
        self.assertIn('parent-café/cafÃ©.text', result['changed'])

    def test_normalization_collapse_caught_by_distinct_seed_bytes(self):
        (self.root / 'normalization/cafe\u0301.seed').write_bytes((self.root / 'normalization/café.seed').read_bytes())
        self.assertIn('normalization/cafe\u0301.seed', M.check_namespace(self.root, self.expected)['changed'])

    def test_copy_or_temporary_orphan_is_rejected(self):
        (self.root / 'ascii-parent/plain.copy').write_bytes(b'orphan')
        self.assertEqual(M.check_namespace(self.root, self.expected)['unexpected'], ['ascii-parent/plain.copy'])

    def test_wrong_bytes_and_touched_sentinel_rejected(self):
        (self.root / 'ascii-parent/plain.bytes').write_bytes(b'bad')
        (self.root / 'ascii-parent/plain.sentinel').write_bytes(b'bad')
        self.assertEqual(len(M.check_namespace(self.root, self.expected)['changed']), 2)

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink creation control; reparse classification is tested synthetically')
    def test_link_is_not_followed_or_accepted_as_expected_file(self):
        path = self.root / 'ascii-parent/plain.text'
        path.unlink()
        path.symlink_to(self.root / 'ascii-parent/plain.seed')
        result = M.check_namespace(self.root, self.expected)
        self.assertEqual(result['actual']['ascii-parent/plain.text']['kind'], 'link-or-reparse')
        self.assertIn('ascii-parent/plain.text', result['changed'])

    def test_synthetic_reparse_attribute_is_rejected_without_hashing_target(self):
        target = self.root / 'ascii-parent/plain.text'
        original_lstat = Path.lstat
        target_mode = target.lstat().st_mode

        def simulated_lstat(path, *args, **kwargs):
            if path == target:
                return SimpleNamespace(st_mode=target_mode, st_file_attributes=0x400)
            return original_lstat(path, *args, **kwargs)

        with patch.object(Path, 'lstat', simulated_lstat):
            with patch.object(M, 'file_record', wraps=M.file_record) as recorded:
                result = M.check_namespace(self.root, self.expected)
        self.assertEqual(result['actual']['ascii-parent/plain.text']['kind'], 'link-or-reparse')
        self.assertIn('ascii-parent/plain.text', result['changed'])
        self.assertNotIn(target, [call.args[0] for call in recorded.call_args_list])
        self.assertEqual(target.read_bytes(), M.TEXT_BYTES)

    def test_environment_setup_is_child_only_and_exact(self):
        before = dict(os.environ)
        with tempfile.TemporaryDirectory(prefix='ordinary-env-') as folder:
            root = Path(folder)
            argv, expected, env = M.prepare_environment(root)
            self.assertEqual(argv[0], argv[1])
            self.assertEqual(env, {argv[2]: M.HOST_ENV_VALUE})
            self.assertEqual(M.check_namespace(root, expected)['status'], 'PASS')
        self.assertEqual(dict(os.environ), before)


class OwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ordinary-process-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ownership = M.load_ownership(REPO)

    def run_python(self, source, timeout=2):
        return M.run_process(self.ownership, [sys.executable, '-I', '-S', '-c', source],
                             self.root, dict(os.environ), self.root, 'case', timeout)

    def test_exact_raw_binary_output_and_exit_preserved(self):
        row = self.run_python('import os,sys;os.write(1,bytes([0,10,13,128,255]));os.write(2,b"err");sys.exit(19)')
        self.assertEqual(row['exit_code'], 19)
        self.assertTrue(row['cleanup_verified'])
        self.assertEqual((self.root / row['stdout']).read_bytes(), bytes([0,10,13,128,255]))
        self.assertEqual((self.root / row['stderr']).read_bytes(), b'err')

    def test_timeout_is_bounded_and_retains_partial_output(self):
        row = self.run_python('import os,time;os.write(1,b"started");time.sleep(60)', .1)
        self.assertTrue(row['timed_out'])
        self.assertTrue(row['cleanup_verified'])
        self.assertLess(row['elapsed_seconds'], 8)
        self.assertEqual((self.root / row['stdout']).read_bytes(), b'started')

    def test_output_budget_terminates_owned_process(self):
        with patch.object(M, 'MAX_OUTPUT', 4096):
            row = self.run_python('import os\nwhile True:os.write(1,b"x"*65536)')
        self.assertEqual(row['status'], 'FAIL')
        self.assertTrue(row['cleanup_verified'])
        self.assertLessEqual(row['stdout_record']['bytes'], 4096)

    def test_constructor_failure_never_invents_cleanup_proof(self):
        with patch.object(self.ownership, 'OwnedProcess', side_effect=RuntimeError('constructor cleanup failed')):
            row = self.run_python('pass')
        self.assertFalse(row['cleanup_verified'])
        self.assertEqual(row['status'], 'FAIL')

    def test_reader_setup_failure_cleans_retained_owner(self):
        with patch.object(self.ownership, 'PipeReader', side_effect=RuntimeError('reader setup failed')):
            row = self.run_python('import time;time.sleep(60)')
        self.assertTrue(row['cleanup_verified'])
        self.assertEqual(row['status'], 'FAIL')


class CollectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ordinary-collection-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.library = self.root / 'input-stl'
        self.library.mkdir()
        (self.library / 'sample.ny').write_text('module synthetic\n')
        self.source = self.root / 'consumer.ny'
        self.source.write_text('module synthetic\n')
        self.args = argparse.Namespace(repository=REPO, compiler=Path(sys.executable),
            interpreter=Path(sys.executable), source=self.source, nilstd=self.library, output=self.root / 'evidence')
        self.calls = []
        self.unsafe = False
        self.compile_failure = False

    def fake_process(self, ownership, command, cwd, environment, logs, name, timeout):
        self.calls.append(name)
        require = self.assertTrue
        require(timeout > 0)
        code, out = 0, b''
        if name.startswith('compile-'):
            nil = Path(command[command.index('-o') + 1])
            nil.write_bytes(b'NIL\0synthetic' if name.endswith('binary') else b'; synthetic NIL')
            code = 1 if self.compile_failure else 0
        else:
            mode = name.split('-', 1)[1]
            out = {'lexical': M.LEXICAL_STDOUT, 'files': M.FILES_STDOUT, 'environment': M.ENVIRONMENT_STDOUT}[mode]
            if mode == 'files':
                argv = command[command.index('--') + 2:]
                for seed, stem in zip(argv[::2], argv[1::2]):
                    seed, stem = Path(seed), Path(stem)
                    seed = seed if seed.is_absolute() else cwd / seed
                    stem = stem if stem.is_absolute() else cwd / stem
                    for suffix in ('.bytes', '.moved', '.stream'):
                        Path(str(stem) + suffix).write_bytes(seed.read_bytes())
                    Path(str(stem) + '.text').write_bytes(M.TEXT_BYTES)
            if mode == 'lexical':
                code, out = 1, b'synthetic expected baseline failure\n'
        for key, value in (('stdout', out), ('stderr', b'')):
            (logs / (name + '.' + key)).write_bytes(value)
        return {'name':name, 'status':'PASS' if code == 0 else 'FAIL', 'exit_code':code,
                'cleanup_verified':not (self.unsafe and name == 'text-lexical'),
                'stdout':name+'.stdout','stderr':name+'.stderr'}

    def test_failed_api_observation_does_not_hide_later_modes(self):
        with patch.object(M, 'run_process', side_effect=self.fake_process):
            report = M.run_contract(self.args)
        self.assertEqual(len(self.calls), 8)
        self.assertEqual(len(report['observations']), 6)
        self.assertTrue(report['collection_complete'])
        self.assertTrue(report['cleanup_verified'])
        self.assertEqual([row['status'] for row in report['observations']], ['FAIL','PASS','PASS'] * 2)
        self.assertTrue(report['original_inputs_unchanged'])
        self.assertEqual((self.args.output/'retained-runner.py').read_bytes(), (HERE/'ordinary_io_parity_contract.py').read_bytes())

    def test_default_fixture_and_current_ownership_helper_are_bound(self):
        self.args.source = None
        with patch.object(M, 'run_process', side_effect=self.fake_process):
            report = M.run_contract(self.args)
        self.assertEqual(report['inputs']['source'], str(REPO / 'tests/language/ordinary-io-parity.ny'))
        helper = REPO / 'validation/ny-3d/scripts/debugger_process.py'
        self.assertEqual(report['before']['ownership_helper'], M.file_record(helper))
        self.assertEqual(report['after']['ownership_helper'], report['before']['ownership_helper'])
        self.assertTrue(report['original_inputs_unchanged'])

    def test_unverified_cleanup_stops_later_launches(self):
        self.unsafe = True
        with patch.object(M, 'run_process', side_effect=self.fake_process):
            report = M.run_contract(self.args)
        self.assertEqual(self.calls, ['compile-text','text-lexical'])
        self.assertFalse(report['cleanup_verified'])
        self.assertFalse(report['collection_complete'])

    def test_compile_failures_are_not_mislabeled_as_six_executions(self):
        self.compile_failure = True
        with patch.object(M, 'run_process', side_effect=self.fake_process):
            report = M.run_contract(self.args)
        self.assertEqual(self.calls, ['compile-text','compile-binary'])
        self.assertEqual(len(report['observations']), 6)
        self.assertTrue(all(row['status'] == 'NOT_RUN_COMPILE_FAILED' for row in report['observations']))
        self.assertFalse(report['collection_complete'])

    def test_existing_output_never_overwritten(self):
        self.args.output.mkdir()
        sentinel = self.args.output / 'sentinel'
        sentinel.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            M.run_contract(self.args)
        self.assertEqual(sentinel.read_bytes(), b'keep')


if __name__ == '__main__':
    unittest.main()

