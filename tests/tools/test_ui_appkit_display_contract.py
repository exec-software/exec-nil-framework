#!/usr/bin/env python3
"""Portable evidence/launch controls; actual AppKit input needs a Mac desktop."""
import contextlib
import argparse
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('appkit_display_contract',
    Path(__file__).with_name('ui_display_contract.py'))
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)

options = argparse.ArgumentParser(add_help=False)
options.add_argument('--compile-driver', type=Path)
options.add_argument('--input-driver', type=Path)
fixture_options, remaining = options.parse_known_args()
sys.argv = [sys.argv[0], *remaining]


class AppKitHarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-appkit-harness-unit-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.driver = self.root / 'test-only.dylib'
        self.driver.write_bytes(b'not a loadable library; mock launch only')
        self.token = 'a' * 32
        self.parent = 700

    def evidence(self, directory=None, token=None, route='vm'):
        directory = directory or self.root
        token = token or self.token
        tests = []
        for offset, (name, stage, keys) in enumerate((('keyboard_events', 3, 8), ('close_request', 1, 0)), 1):
            record = {'schema': 'nil.appkit.input.v1', 'token': token, 'route': route,
                      'test': name, 'pid': self.parent + offset, 'parentPid': self.parent,
                      'status': 'passed', 'reason': 'fixture', 'stage': stage, 'keys': keys,
                      'scope': 'owned-process AppKit responder input; no Accessibility or global input'}
            (directory / f'{token}-{record["pid"]}.json').write_text(json.dumps(record), encoding='utf-8')
            tests.append({'name': name, 'suite': 'nil.std.ui.gtk.interaction', 'route': route,
                          'status': 'passed', 'exitCode': 77, 'signal': 0, 'timedOut': False,
                          'cancelled': False, 'stdout': 'language assertions passed',
                          'stderr': 'NIL_APPKIT_INPUT ' + json.dumps(record) + '\n'})
        return {'schema': 'nil.test.results.v1', 'errors': [], 'tests': tests}

    def case_directory(self):
        directory = self.root / 'evidence'
        directory.mkdir()
        return directory

    def test_exact_language_cases_and_private_input_records_are_required(self):
        directory = self.case_directory()
        report = self.evidence(directory)
        self.assertEqual(len(contract.validate_appkit_interaction(report, 'vm', directory,
                                                                  self.token, self.parent)), 2)
        for mutate in (lambda value: value['tests'].pop(),
                       lambda value: value['tests'].append(value['tests'][0]),
                       lambda value: value['tests'][0].update(route='native'),
                       lambda value: value['tests'][0].update(status='skipped'),
                       lambda value: value['tests'][0].update(exitCode=0),
                       lambda value: value['tests'][0].update(timedOut=True),
                       lambda value: value['tests'][0].update(signal=15),
                       lambda value: value['tests'][0].update(cancelled=True),
                       lambda value: value['tests'][0].update(stderr=''),
                       lambda value: value['errors'].append('compile failed')):
            altered = copy.deepcopy(report)
            mutate(altered)
            with self.assertRaises(AssertionError):
                contract.validate_appkit_interaction(altered, 'vm', directory, self.token, self.parent)

    def test_foreign_pid_token_route_or_incomplete_delivery_cannot_qualify(self):
        directory = self.case_directory()
        report = self.evidence(directory)
        for change in ({'parentPid': self.parent + 90}, {'token': 'b' * 32}, {'route': 'native'},
                       {'pid': self.parent}, {'pid': -1}, {'pid': True}, {'test': 'close_request'},
                       {'keys': 7}, {'stage': 2}, {'status': 'failed'}):
            altered = copy.deepcopy(report)
            record = json.loads(altered['tests'][0]['stderr'].removeprefix('NIL_APPKIT_INPUT '))
            record.update(change)
            altered['tests'][0]['stderr'] = 'NIL_APPKIT_INPUT ' + json.dumps(record)
            with self.subTest(change=change), self.assertRaises(AssertionError):
                contract.validate_appkit_interaction(altered, 'vm', directory, self.token, self.parent)
        # Python bool compares equal to 0/1; typed record fields must reject it
        # even when the corresponding language case's expected value is 0/1.
        for field in ('stage', 'keys'):
            altered = copy.deepcopy(report)
            record = json.loads(altered['tests'][1]['stderr'].removeprefix('NIL_APPKIT_INPUT '))
            record[field] = bool(record[field])
            altered['tests'][1]['stderr'] = 'NIL_APPKIT_INPUT ' + json.dumps(record)
            with self.subTest(boolean_field=field), self.assertRaises(AssertionError):
                contract.validate_appkit_interaction(altered, 'vm', directory, self.token, self.parent)

    def test_extra_duplicate_missing_or_tampered_records_fail(self):
        directory = self.case_directory()
        report = self.evidence(directory)
        duplicate = copy.deepcopy(report)
        duplicate['tests'][0]['stderr'] *= 2
        with self.assertRaises(AssertionError):
            contract.validate_appkit_interaction(duplicate, 'vm', directory, self.token, self.parent)
        extra = directory / 'unexpected.json'
        extra.write_text('{}')
        with self.assertRaises(AssertionError):
            contract.validate_appkit_interaction(report, 'vm', directory, self.token, self.parent)
        extra.unlink()
        path = next(directory.iterdir())
        original = path.read_text()
        path.write_text('{}')
        with self.assertRaises(AssertionError):
            contract.validate_appkit_interaction(report, 'vm', directory, self.token, self.parent)
        path.unlink()
        with self.assertRaises(AssertionError):
            contract.validate_appkit_interaction(report, 'vm', directory, self.token, self.parent)
        path.write_text(original)

    def test_explicit_injection_preserves_environment_and_rejects_ambient_composition(self):
        original = {'PATH': '/fixture/bin', 'DYLD_FALLBACK_LIBRARY_PATH': '/fixture/runtime'}
        result = contract.appkit_interaction_environment(original, self.driver, self.root, self.token, 'vm', self.driver)
        self.assertEqual(result['PATH'], original['PATH'])
        self.assertEqual(result['DYLD_FALLBACK_LIBRARY_PATH'], original['DYLD_FALLBACK_LIBRARY_PATH'])
        self.assertNotIn('DYLD_INSERT_LIBRARIES', original)
        self.assertEqual(result['DYLD_INSERT_LIBRARIES'], str(self.driver))
        for ambient in (dict(original, DYLD_INSERT_LIBRARIES='/other.dylib'),
                        dict(original, NIL_TEST_APPKIT_INPUT_TOKEN=self.token)):
            with self.assertRaises(AssertionError):
                contract.appkit_interaction_environment(ambient, self.driver, self.root, self.token, 'vm', self.driver)
        with self.assertRaises(AssertionError):
            contract.appkit_interaction_environment(original, self.root / 'absent', self.root, self.token, 'vm', self.driver)

    def test_actual_route_orchestration_scopes_injection_and_retains_complete_evidence(self):
        source = self.root / 'unchanged.matrix'
        source.write_text('owned test fixture')
        args = SimpleNamespace(interaction_source=source, appkit_input_driver=self.driver,
                               appkit_compile_driver=self.driver, compiler=Path(sys.executable).resolve(),
                               appkit_evidence_directory=self.root / 'runs', llvm='ON')
        ambient = {'PATH': '/owned/tools'}
        seen = []
        def checked(command, environment, timeout, capture_prefix):
            self.assertEqual(timeout, 120)
            self.assertEqual(command[-1], source)
            route = command[command.index('--target') + 1]
            self.assertEqual(environment['NIL_TEST_APPKIT_INPUT_ROUTE'], route)
            self.assertEqual(environment['DYLD_INSERT_LIBRARIES'], str(self.driver))
            self.assertEqual(environment['NIL_TEST_APPKIT_INPUT_COMPILER'], str(args.compiler))
            self.assertEqual(command[command.index('--compiler') + 1], args.appkit_compile_driver)
            self.assertEqual(capture_prefix.name, route)
            seen.append(route)
            report = self.evidence(Path(environment['NIL_TEST_APPKIT_INPUT_DIRECTORY']),
                                   environment['NIL_TEST_APPKIT_INPUT_TOKEN'], route)
            return SimpleNamespace(stdout=json.dumps(report), stderr='', pid=self.parent)
        with mock.patch.object(contract, 'checked', side_effect=checked), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(contract.run_appkit_interaction(args, ['owned-niltest', 'run', '--compiler', args.compiler], ambient), 4)
        self.assertEqual(seen, ['vm', 'native'])
        self.assertEqual(ambient, {'PATH': '/owned/tools'})
        report = json.loads(next(args.appkit_evidence_directory.glob('*/report.json')).read_text())
        self.assertEqual(report['status'], 'passed')
        self.assertEqual([route['languageCases'] for route in report['routes']], [2, 2])
        self.assertEqual(report['crossProcessAccessibility'], 'not exercised')

    def test_failed_language_completion_retains_failure_instead_of_accepting_input_only(self):
        source = self.root / 'unchanged.matrix'
        source.write_text('owned test fixture')
        args = SimpleNamespace(interaction_source=source, appkit_input_driver=self.driver,
                               appkit_compile_driver=self.driver, compiler=Path(sys.executable).resolve(),
                               appkit_evidence_directory=self.root / 'runs', llvm='OFF')
        def failed(command, environment, timeout, capture_prefix):
            report = self.evidence(Path(environment['NIL_TEST_APPKIT_INPUT_DIRECTORY']),
                                   environment['NIL_TEST_APPKIT_INPUT_TOKEN'])
            report['tests'][0]['exitCode'] = 1
            return SimpleNamespace(stdout=json.dumps(report), stderr='', pid=self.parent)
        with mock.patch.object(contract, 'checked', side_effect=failed), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(AssertionError):
                contract.run_appkit_interaction(args, ['owned-niltest', 'run', '--compiler', args.compiler], {})
        report = json.loads(next(args.appkit_evidence_directory.glob('*/report.json')).read_text())
        self.assertEqual(report['status'], 'failed')

    def test_owned_process_must_exit_naturally_with_success(self):
        prefix = self.root / 'raw'
        result = contract.checked([sys.executable, '-c', 'print("owned")'], os.environ.copy(),
                                  timeout=5, capture_prefix=prefix)
        self.assertEqual(result.stdout.strip(), 'owned')
        self.assertEqual(prefix.with_suffix('.stdout').read_text(), 'owned\n')
        self.assertGreater(result.pid, 0)
        with self.assertRaises(AssertionError):
            contract.checked([sys.executable, '-c',
                              'import sys; print("failed stdout"); print("failed stderr", file=sys.stderr); raise SystemExit(7)'],
                             os.environ.copy(), timeout=5, capture_prefix=prefix)
        self.assertEqual(prefix.with_suffix('.stdout').read_text(), 'failed stdout\n')
        self.assertEqual(prefix.with_suffix('.stderr').read_text(), 'failed stderr\n')
        # The production Windows display harness uses Jobs. This POSIX helper
        # never owns a timed-out Windows process in the real display workflow.
        if os.name != 'nt':
            with self.assertRaises(subprocess.TimeoutExpired):
                contract.checked([sys.executable, '-c',
                                  'import sys,time; print("partial stdout", flush=True); print("partial stderr", file=sys.stderr, flush=True); time.sleep(30)'],
                                 os.environ.copy(), timeout=.2, capture_prefix=prefix)
            self.assertEqual(prefix.with_suffix('.stdout').read_text(), 'partial stdout\n')
            self.assertEqual(prefix.with_suffix('.stderr').read_text(), 'partial stderr\n')


if fixture_options.compile_driver:
    class CompiledAdapterTests(unittest.TestCase):
        def setUp(self):
            self.temporary = tempfile.TemporaryDirectory(prefix='nil-appkit-adapter-unit-')
            self.addCleanup(self.temporary.cleanup)
            self.root = Path(self.temporary.name).resolve()
            self.wrapper = fixture_options.compile_driver.resolve(strict=True)
            if sys.platform == 'darwin':
                self.assertIsNotNone(fixture_options.input_driver, 'Darwin adapter controls need the real compatible input dylib')
                self.driver = fixture_options.input_driver.resolve(strict=True)
            else:
                self.driver = self.root / 'input.dylib'
                self.driver.write_bytes(b'non-Darwin environment control; never loaded')
            # No inherited override is removed silently. The control requires a
            # clean caller and then supplies its own explicit fixture profile.
            self.assertFalse(os.environ.get('DYLD_INSERT_LIBRARIES'))
            self.env = contract.appkit_interaction_environment(dict(os.environ), self.driver, self.root,
                        'b' * 32, 'native', Path(sys.executable).resolve(strict=True))
            self.env['NIL_TEST_PRESERVED_SETTING'] = 'ordinary value with spaces $ and quotes'
            self.env['DYLD_FALLBACK_LIBRARY_PATH'] = str(self.root)

        def test_real_adapter_cleans_compile_environment_and_preserves_arguments(self):
            program = ('import json,os,sys; print(json.dumps({'
                       '"env":{key:os.environ.get(key) for key in ("PATH","NIL_TEST_PRESERVED_SETTING","DYLD_FALLBACK_LIBRARY_PATH","DYLD_INSERT_LIBRARIES")},'
                       '"fixtureNames":[key for key in os.environ if key.startswith("NIL_TEST_APPKIT_INPUT_")],'
                       '"args":sys.argv[1:],"pid":os.getpid(),"parentPid":os.getppid()}))')
            process = subprocess.Popen([str(self.wrapper), '-c', program, 'argument with spaces', '$literal'],
                                       env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                output, errors = process.communicate(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill(); process.communicate(timeout=3)
            self.assertEqual(process.returncode, 0, errors)
            observed = json.loads(output)
            self.assertEqual(observed['pid'], process.pid, 'exec must preserve the owned compiler PID')
            self.assertEqual(observed['parentPid'], os.getpid(), 'exec must preserve the parent relationship')
            self.assertEqual(observed['args'], ['argument with spaces', '$literal'])
            self.assertIsNone(observed['env']['DYLD_INSERT_LIBRARIES'])
            self.assertEqual(observed['fixtureNames'], [])
            self.assertEqual(observed['env']['NIL_TEST_PRESERVED_SETTING'], self.env['NIL_TEST_PRESERVED_SETTING'])
            self.assertEqual(observed['env']['PATH'], self.env['PATH'])
            self.assertEqual(observed['env'].get('DYLD_FALLBACK_LIBRARY_PATH'), self.env['DYLD_FALLBACK_LIBRARY_PATH'])
            # The parent environment still drives niltest's application route.
            self.assertEqual(self.env['DYLD_INSERT_LIBRARIES'], str(self.driver))

        def test_real_adapter_propagates_compiler_status(self):
            result = subprocess.run([str(self.wrapper), '-c', 'raise SystemExit(7)'], env=self.env,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 7, result.stderr)

        def test_real_adapter_rejects_mismatched_missing_and_recursive_profiles(self):
            other = self.root / 'other-driver.dylib'
            other.write_bytes(b'declared only; never loaded')
            nonexecutable = self.root / 'nonexecutable-compiler'
            nonexecutable.write_bytes(b'not executable')
            for change in ({'NIL_TEST_APPKIT_INPUT_DRIVER': str(other)},
                           {'DYLD_INSERT_LIBRARIES': ''},
                           {'NIL_TEST_APPKIT_INPUT_DRIVER': ''},
                           {'NIL_TEST_APPKIT_INPUT_COMPILER': str(self.root / 'absent')},
                           {'NIL_TEST_APPKIT_INPUT_COMPILER': 'relative/compiler'},
                           {'NIL_TEST_APPKIT_INPUT_COMPILER': str(self.wrapper)},
                           {'NIL_TEST_APPKIT_INPUT_COMPILER': str(nonexecutable)},
                           {'NIL_TEST_APPKIT_INPUT_DIRECTORY': str(self.root / 'absent')}):
                with self.subTest(change=change):
                    result = subprocess.run([str(self.wrapper), '-c', 'print("UNEXPECTED COMPILER EXECUTION")'],
                                            env=dict(self.env, **change), capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 85, result.stderr)
                    self.assertNotIn('UNEXPECTED COMPILER EXECUTION', result.stdout)
                    self.assertIn('AppKit test compiler adapter:', result.stderr)


if __name__ == '__main__':
    unittest.main()
