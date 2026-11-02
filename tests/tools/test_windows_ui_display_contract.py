#!/usr/bin/env python3
"""Portable safety/oracle tests; not a substitute for a real Windows GTK run."""
import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('windows_ui_display_contract',
    Path(__file__).with_name('windows_ui_display_contract.py'))
contract = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = contract
spec.loader.exec_module(contract)


class DesktopPreflightTests(unittest.TestCase):
    def test_usable_desktop_is_ready(self):
        snapshot = {'sessionId': 3, 'desktop': 'Default', 'visibleWindowStation': True}
        api = SimpleNamespace(desktop=lambda: snapshot)
        self.assertEqual(contract.desktop_preflight(api), {'status': 'READY', 'desktop': snapshot})

    def test_known_unavailable_desktop_is_optional_skip(self):
        def unavailable():
            raise contract.DisplayUnavailable('noninteractive station')
        self.assertEqual(contract.desktop_preflight(SimpleNamespace(desktop=unavailable)),
                         {'status': 'SKIP', 'reason': 'noninteractive station'})

    def test_qualification_cannot_skip_known_unavailable_desktop(self):
        def unavailable():
            raise contract.DisplayUnavailable('noninteractive station')
        with self.assertRaises(contract.DisplayUnavailable):
            contract.desktop_preflight(SimpleNamespace(desktop=unavailable), require_display=True)

    def test_unknown_api_or_loader_errors_are_never_skipped(self):
        for error in (OSError('Win32 query failed'), RuntimeError('loader failed')):
            def failure():
                raise error
            with self.subTest(error=type(error)), self.assertRaises(type(error)):
                contract.desktop_preflight(SimpleNamespace(desktop=failure))


class ProviderSelectionTests(unittest.TestCase):
    def test_explicit_runtime_including_empty_value_requires_validation(self):
        for value in ('', 'missing runtime', 'real runtime'):
            with self.subTest(value=value):
                self.assertEqual(contract.configured_provider_selection('probe.exe',
                    {'NIL_UI_GTK_DIRECTORY': value}), 'environment')

    def test_each_supported_colocated_name_requires_validation(self):
        with tempfile.TemporaryDirectory(prefix='nil-ui-provider-selection-') as directory:
            root = Path(directory)
            for stem in ('gtk-4-1.dll', 'gobject-2.0-0.dll', 'glib-2.0-0.dll'):
                for name in (stem, 'lib' + stem):
                    with self.subTest(name=name):
                        candidate = root / name
                        candidate.write_bytes(b'invalid test DLL, never executed')
                        self.assertEqual(contract.configured_provider_selection(root / 'probe.exe', {}),
                                         'application-directory')
                        candidate.unlink()

    def test_no_provider_deployment_keeps_optional_headless_skip(self):
        with tempfile.TemporaryDirectory(prefix='nil-ui-no-provider-selection-') as directory:
            self.assertIsNone(contract.configured_provider_selection(Path(directory) / 'probe.exe', {}))

    def test_directory_masquerading_as_colocated_dll_requires_validation(self):
        with tempfile.TemporaryDirectory(prefix='nil-ui-invalid-provider-selection-') as directory:
            root = Path(directory)
            (root / 'libgtk-4-1.dll').mkdir()
            self.assertEqual(contract.configured_provider_selection(root / 'probe.exe', {}),
                             'application-directory')


class MainPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-ui-preflight-unit-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.argv = ['windows_ui_display_contract.py', '--evidence-directory', str(self.root)]
        for name in ('tester', 'compiler', 'interpreter', 'provider-test', 'lifecycle-test',
                     'source', 'interaction-source'):
            self.argv += ['--' + name, str(Path(__file__).resolve())]
        self.argv += ['--sdk', str(self.root)]
        self.api = SimpleNamespace(desktop=mock.Mock(side_effect=contract.DisplayUnavailable('noninteractive station')))

    def invoke(self, checked, *, configured=False, require=False):
        environment = dict(os.environ)
        environment.pop('NIL_UI_GTK_DIRECTORY', None)
        if configured:
            environment['NIL_UI_GTK_DIRECTORY'] = 'explicit-test-runtime'
        argv = self.argv + (['--require-display'] if require else [])
        with mock.patch.object(contract, 'Win32', return_value=self.api), \
             mock.patch.object(contract, 'checked', side_effect=checked), \
             mock.patch.object(sys, 'platform', 'win32'), \
             mock.patch.object(sys, 'argv', argv), \
             mock.patch.dict(os.environ, environment, clear=True), \
             contextlib.redirect_stdout(io.StringIO()):
            return contract.main()

    def report(self):
        reports = list(self.root.glob('nil-ui-windows-*/report.json'))
        self.assertEqual(len(reports), 1)
        return json.loads(reports[0].read_text(encoding='utf-8'))

    def test_optional_headless_preflight_skips_without_starting_display_cases(self):
        checked = mock.Mock(side_effect=AssertionError('must not launch display fixtures'))
        self.assertEqual(self.invoke(checked), 77)
        checked.assert_not_called()
        report = self.report()
        self.assertEqual(report['status'], 'SKIP')
        self.assertEqual(report['cases'], [])
        self.assertEqual(report['languageCases'], 0)

    def test_configured_provider_is_checked_before_optional_skip(self):
        def checked(command, env, root, record, **options):
            self.assertEqual(command[-1], '--provider-present')
            self.assertEqual(options['timeout'], 15)
            self.api.desktop.assert_not_called()
            record['cleanupVerified'] = True
        self.assertEqual(self.invoke(checked, configured=True), 77)
        report = self.report()
        self.assertEqual(report['status'], 'SKIP')
        self.assertEqual(report['cases'], [])
        self.assertEqual(len(report['prerequisites']), 1)

    def test_configured_loader_failure_is_not_hidden_by_headless_skip(self):
        def checked(*arguments, **options):
            raise RuntimeError('configured GTK is broken')
        with self.assertRaisesRegex(RuntimeError, 'GTK is broken'):
            self.invoke(checked, configured=True)
        self.api.desktop.assert_not_called()
        self.assertEqual(self.report()['status'], 'FAIL')

    def test_broken_colocated_provider_is_not_hidden_by_headless_skip(self):
        probe = self.root / 'probe.exe'
        probe.write_bytes(b'test-only placeholder, never executed')
        self.argv[self.argv.index('--provider-test') + 1] = str(probe)
        (self.root / 'libgobject-2.0-0.dll').write_bytes(b'incomplete colocated deployment')
        def checked(command, env, root, record, **options):
            self.assertEqual(command[-1], '--provider-present')
            self.assertEqual(record['selection'], 'application-directory')
            raise RuntimeError('colocated GTK is broken')
        with self.assertRaisesRegex(RuntimeError, 'colocated GTK is broken'):
            self.invoke(checked)
        self.api.desktop.assert_not_called()
        report = self.report()
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['cases'], [])

    def test_valid_colocated_provider_is_checked_before_optional_skip(self):
        probe = self.root / 'probe.exe'
        probe.write_bytes(b'test-only placeholder, never executed')
        self.argv[self.argv.index('--provider-test') + 1] = str(probe)
        (self.root / 'libgtk-4-1.dll').write_bytes(b'test-only placeholder, never executed')
        def checked(command, env, root, record, **options):
            self.assertEqual(command[-1], '--provider-present')
            self.assertEqual(record['selection'], 'application-directory')
            record['cleanupVerified'] = True
        self.assertEqual(self.invoke(checked), 77)
        self.assertEqual(self.report()['status'], 'SKIP')

    def test_required_headless_run_fails_and_retains_evidence(self):
        with self.assertRaises(contract.DisplayUnavailable):
            self.invoke(mock.Mock(), require=True)
        report = self.report()
        self.assertEqual(report['status'], 'FAIL')
        self.assertTrue(report['requireDisplay'])
        self.assertEqual(report['cases'], [])

    def test_no_skip_after_display_fixture_started(self):
        self.api.desktop.side_effect = None
        self.api.desktop.return_value = {'sessionId': 3, 'desktop': 'Default'}
        def checked(command, env, root, record, **options):
            self.assertEqual(command[-1], '--display')
            raise contract.DisplayUnavailable('display disappeared during fixture')
        with self.assertRaises(contract.DisplayUnavailable):
            self.invoke(checked)
        report = self.report()
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(len(report['cases']), 1)


class FakeApi:
    def __init__(self):
        self.table = {10: (101, 201, contract.KEYBOARD_TITLE),
                      20: (102, 202, contract.CLOSE_TITLE),
                      30: (999, 299, contract.KEYBOARD_TITLE)}
        self.validity, self.visibility = True, True
        self.title_reads, self.posts, self.retains = [], [], []
        self.before_post = None

    def windows(self):
        return list(self.table)

    def owner(self, hwnd):
        return self.table.get(hwnd, (0, 0, ''))[:2]

    def retain(self, pid, job):
        self.retains.append(pid)
        return contract.Member(pid, 100000 + pid, 200000 + pid)

    def visible(self, hwnd):
        return self.visibility and hwnd in self.table

    def title(self, hwnd):
        self.title_reads.append(hwnd)
        return self.table.get(hwnd, (0, 0, ''))[2]

    def valid(self, member, job):
        return self.validity

    def scan(self, key):
        return {contract.VK_TAB: 0x0F, contract.VK_SPACE: 0x39}[key]

    def post(self, hwnd, message, key, flags):
        self.posts.append((hwnd, message, key, flags))
        if self.before_post:
            self.before_post()


class InputBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.api, self.clock, self.record = FakeApi(), [1.0], {}
        job = SimpleNamespace(_member_ids=lambda: [101, 102])
        self.owned = SimpleNamespace(job=job, lock=threading.RLock())
        self.driver = contract.WindowsInput(self.owned, self.api, self.record,
                                           clock=lambda: self.clock[0])

    def ready(self):
        self.driver.tick()
        self.clock[0] += .2

    def test_unrelated_matching_title_is_not_read_or_targeted(self):
        self.ready()
        self.driver.tick()
        self.assertNotIn(30, self.api.title_reads)
        self.assertNotIn(999, self.api.retains)
        self.assertTrue(self.api.posts)
        self.assertTrue(all(post[0] in (10, 20) for post in self.api.posts))

    def test_complete_requires_both_exact_windows_and_full_keyboard_sequence(self):
        self.ready()
        with self.assertRaisesRegex(AssertionError, 'both owned'):
            self.driver.complete()
        for unused in range(12):
            self.driver.tick()
            self.clock[0] += .1
        self.driver.complete()
        chars = [post[2] for post in self.api.posts if post[1] == contract.WM_CHAR]
        self.assertEqual(chars, list(map(ord, 'Matrix')))
        self.assertEqual(len(self.api.posts), 27)  # Six packets, four key pairs, one close.
        self.assertEqual(len(self.record['windows']), 2)
        self.assertEqual(len(self.record['messages']), 27)

    def test_no_job_refuses_input(self):
        self.owned.job = None
        with self.assertRaisesRegex(RuntimeError, 'private Windows Job'):
            contract.WindowsInput(self.owned, self.api, {})

    def test_closed_job_refuses_input(self):
        self.ready()
        self.owned.job = None
        with self.assertRaisesRegex(RuntimeError, 'Job was closed'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_changed_process_handle_identity_refuses_input(self):
        self.ready()
        self.api.validity = False
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_reused_hwnd_with_unrelated_pid_refuses_input(self):
        self.ready()
        self.api.table[10] = (999, 201, contract.KEYBOARD_TITLE)
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_same_pid_different_thread_refuses_input(self):
        self.ready()
        window = next(window for window in self.driver.pending if window.hwnd == 10)
        self.api.table[10] = (101, 999, contract.KEYBOARD_TITLE)
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.verify(window)

    def test_changed_title_refuses_input(self):
        self.ready()
        self.api.table[10] = (101, 201, 'not the fixture')
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_hidden_window_refuses_input(self):
        self.ready()
        self.api.visibility = False
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_identity_is_checked_between_packet_messages(self):
        self.ready()
        self.api.before_post = lambda: setattr(self.api, 'validity', False)
        with self.assertRaisesRegex(RuntimeError, 'identity changed'):
            self.driver.tick()
        self.assertEqual(len(self.api.posts), 1)

    def test_duplicate_owned_title_is_not_accepted(self):
        self.api.table[40] = (101, 201, contract.KEYBOARD_TITLE)
        with self.assertRaisesRegex(RuntimeError, 'duplicate fixture'):
            self.driver.tick()
        self.assertEqual(self.api.posts, [])

    def test_keyboard_plan_uses_only_scoped_key_messages(self):
        packets = contract.keyboard_messages(self.api)
        self.assertEqual(len(packets), 10)
        self.assertEqual([packet[0][1] for packet in packets[6:]],
                         [contract.VK_TAB, contract.VK_SPACE] * 2)
        self.assertTrue(all(message in (contract.WM_KEYDOWN, contract.WM_CHAR, contract.WM_KEYUP)
                            for packet in packets for message, key, flags in packet))


class RetainedHandleTests(unittest.TestCase):
    def setUp(self):
        self.api = object.__new__(contract.Win32)
        self.api.creation = lambda handle: 555
        self.api.belongs = lambda handle, job: True
        self.api.k = SimpleNamespace(WaitForSingleObject=lambda handle, timeout: 258,
                                    GetProcessId=lambda handle: 101)
        self.member = contract.Member(101, 555, 987)
        self.job = SimpleNamespace(handle=123)

    def test_exact_handle_identity_accepts(self):
        self.assertTrue(self.api.valid(self.member, self.job))

    def test_terminated_handle_rejects(self):
        self.api.k.WaitForSingleObject = lambda handle, timeout: 0
        self.assertFalse(self.api.valid(self.member, self.job))

    def test_changed_creation_time_rejects(self):
        self.api.creation = lambda handle: 556
        self.assertFalse(self.api.valid(self.member, self.job))

    def test_changed_pid_rejects(self):
        self.api.k.GetProcessId = lambda handle: 102
        self.assertFalse(self.api.valid(self.member, self.job))

    def test_not_in_exact_job_rejects(self):
        self.api.belongs = lambda handle, job: False
        self.assertFalse(self.api.valid(self.member, self.job))


class OracleTests(unittest.TestCase):
    def report(self):
        return {'errors': [], 'tests': [{'name': name, 'route': route, 'status': 'passed'}
                for name in contract.DESKTOP_NAMES for route in ('vm', 'native')]}

    def check(self, report):
        return contract.validate_report(json.dumps(report), contract.DESKTOP_NAMES, ['vm', 'native'])

    def test_exact_vm_native_matrix_accepts(self):
        self.assertEqual(len(self.check(self.report())['tests']), 6)

    def test_missing_native_case_rejects(self):
        report = self.report()
        report['tests'].pop()
        with self.assertRaises(AssertionError):
            self.check(report)

    def test_duplicate_case_cannot_replace_missing_case(self):
        report = self.report()
        report['tests'][0] = report['tests'][1]
        with self.assertRaises(AssertionError):
            self.check(report)

    def test_skipped_failed_or_cancelled_case_rejects(self):
        for status in ('skipped', 'failed', 'cancelled'):
            report = self.report()
            report['tests'][0]['status'] = status
            with self.subTest(status=status), self.assertRaises(AssertionError):
                self.check(report)

    def test_nonempty_or_missing_errors_rejects(self):
        for value in (None, ['compile failed']):
            report = self.report()
            report['errors'] = value
            with self.subTest(errors=value), self.assertRaises(AssertionError):
                self.check(report)


class OwnedCaptureTests(unittest.TestCase):
    def run_child(self, source, *, timeout=5, limit=None):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-ui-harness-unit-')
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.record = {'name': 'child'}
        return contract.checked([sys.executable, '-I', '-S', '-c', source], dict(os.environ),
                                root, self.record, timeout=timeout, output_limit=limit)

    def test_real_owned_capture_exit_and_utf8_evidence(self):
        output = self.run_child("import os; os.write(1, 'café 中文'.encode('utf-8')); os.write(2,b'stderr')")
        self.assertEqual(output, 'café 中文')
        self.assertEqual(self.record['exit'], 0)
        self.assertTrue(self.record['cleanupVerified'])
        self.assertEqual(self.record['stderrBytes'], 6)
        self.assertEqual(len(self.record['stdoutSha256']), 64)

    def test_nonzero_exit_is_not_a_pass(self):
        with self.assertRaisesRegex(AssertionError, 'exit 19'):
            self.run_child('raise SystemExit(19)')
        self.assertTrue(self.record['cleanupVerified'])

    def test_timeout_terminates_owned_tree_and_retains_logs(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            self.run_child('import time; print("started",flush=True); time.sleep(60)', timeout=2)
        self.assertTrue(self.record['cleanupVerified'])
        self.assertGreater(self.record['stdoutBytes'], 0)

    def test_timeout_releases_live_descendant_cwd_and_exclusive_file(self):
        # Windows disallows immediate deletion while an exited-but-not-rundown
        # process retains either CWD or a share-denied file. No retry conceals a
        # premature cleanup claim. POSIX also exercises the owned group path.
        with tempfile.TemporaryDirectory(prefix='nil-ui-descendant-unit-') as directory:
            resource = Path(directory) / 'owned descendant'
            resource.mkdir()
            lock = resource / 'exclusive-file'
            descendant = '\n'.join([
                'import ctypes as c, os, sys, time',
                'os.chdir(sys.argv[1])',
                'if os.name == "nt":',
                ' k = c.WinDLL("kernel32",use_last_error=True)',
                ' k.CreateFileW.argtypes = [c.c_wchar_p,c.c_uint32,c.c_uint32,c.c_void_p,c.c_uint32,c.c_uint32,c.c_void_p]',
                ' k.CreateFileW.restype = c.c_void_p',
                ' handle = k.CreateFileW("exclusive-file",0xC0000000,0,None,2,0x80,None)',
                ' assert handle not in (None,c.c_void_p(-1).value)',
                'else:',
                ' handle = open("exclusive-file","wb")',
                'print("descendant owns resources",flush=True)',
                'time.sleep(60)',
            ])
            source = ('import subprocess,sys,time; '
                      f'subprocess.Popen([sys.executable,"-I","-S","-c",{descendant!r},{str(resource)!r}]); '
                      'time.sleep(60)')
            with self.assertRaises(subprocess.TimeoutExpired):
                self.run_child(source, timeout=3)
            self.assertTrue(self.record['cleanupVerified'])
            output = (Path(self.temporary.name) / self.record['stdout']).read_bytes()
            self.assertIn(b'descendant owns resources', output)
            lock.unlink()
            resource.rmdir()

    def test_output_flood_is_bounded_and_cleanup_verified(self):
        with self.assertRaisesRegex(RuntimeError, 'output exceeded'):
            self.run_child('import os\nwhile True: os.write(1,b"x"*65536)', limit=131072)
        self.assertTrue(self.record['cleanupVerified'])
        self.assertLessEqual(self.record['stdoutBytes'] + self.record['stderrBytes'], 131072)


if __name__ == '__main__':
    unittest.main()
