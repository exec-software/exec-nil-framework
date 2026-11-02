"""Fixture isolation/cleanup regressions; no PostgreSQL installation required."""
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import database_postgresql_isolated as fixture


class IsolatedFixtureTests(unittest.TestCase):
    def route_report(self):
        return {'summary': {'PASS': 6, 'FAIL': 0, 'SKIP': 0, 'XFAIL': 0, 'XPASS': 0},
                'results': [{'route': name, 'status': 'PASS'} for name in
                            ('text-interpreter', 'binary-interpreter', 'bundle-text', 'bundle-binary', 'llvm-jit', 'llvm-aot')]}

    def test_route_qualification_requires_six_unique_successes(self):
        report = self.route_report()
        self.assertEqual(fixture.validate_routes(report, True)['PASS'], 6)
        report['results'][1] = dict(report['results'][0])
        with self.assertRaisesRegex(RuntimeError, 'duplicate'):
            fixture.validate_routes(report, True)

    def test_llvm_off_skips_are_explicit_and_never_qualification(self):
        report = self.route_report()
        for row in report['results'][-2:]:
            row.update(status='SKIP', skip_reason='nilcompiler was built without a usable LLVM backend')
        report['summary'].update(PASS=4, SKIP=2)
        self.assertEqual(fixture.validate_routes(report, False)['PASS'], 4)
        with self.assertRaisesRegex(RuntimeError, 'exclusion'):
            fixture.validate_routes(report, True)
        report['results'][0]['status'] = 'SKIP'
        with self.assertRaisesRegex(RuntimeError, 'exclusion'):
            fixture.validate_routes(report, False)

    def test_environment_removes_provider_overrides_but_preserves_toolchain(self):
        self.assertEqual(fixture.clean_environment({'PGPASSWORD': 'secret', 'pgservice': 'external',
            'NY_HOME': 'other', 'nil_stdlib_path': 'other', 'PATH': 'tools', 'LIB': 'libs', 'INCLUDE': 'headers'}),
            {'PATH': 'tools', 'LIB': 'libs', 'INCLUDE': 'headers'})

    def test_windows_configuration_is_loopback_and_authenticated(self):
        config, hba = fixture.configuration(True, Path('unused'), 24567)
        self.assertIn("listen_addresses = '127.0.0.1'", config)
        self.assertIn("unix_socket_directories = ''", config)
        self.assertIn('port = 24567\n', config)
        self.assertIn("log_min_messages = 'log'", config)
        self.assertIn("log_min_error_statement = 'panic'", config)
        self.assertIn("lc_messages = 'C'", config)
        self.assertEqual(hba, 'host all nil_test 127.0.0.1/32 scram-sha-256\n'
            'host all all 0.0.0.0/0 reject\nhost all all ::/0 reject\n')
        self.assertNotIn('trust', hba)
        self.assertNotIn('password', hba)

    def test_posix_configuration_never_listens_on_tcp(self):
        config, hba = fixture.configuration(False, PurePosixPath('/private/socket'), 5432)
        self.assertIn("listen_addresses = ''", config)
        self.assertIn("unix_socket_directories = '/private/socket'", config)
        self.assertIn("log_min_messages = 'panic'", config)
        self.assertEqual(hba, 'local all nil_test trust\nlocal all all reject\n')

    def test_quoting_preserves_literal_spaces_quotes_and_backslashes(self):
        self.assertEqual(fixture.config_quote("a'b\\c d"), "'a''b\\\\c d'")
        self.assertEqual(fixture.conninfo_quote("a'b\\c d"), "'a\\'b\\\\c d'")

    def test_owned_command_collects_both_streams_and_status(self):
        with tempfile.TemporaryDirectory(prefix='nil-pg-unit-') as directory:
            status, output = fixture.run([sys.executable, '-c',
                'import sys; print("output"); print("error", file=sys.stderr); sys.exit(7)'],
                fixture.clean_environment(fixture.os.environ), directory, 5)
            self.assertEqual(status, 7)
            self.assertEqual(output.replace(b'\r\n', b'\n'), b'output\nerror\n')

    def test_owned_command_timeout_is_bounded(self):
        with tempfile.TemporaryDirectory(prefix='nil-pg-unit-') as directory:
            start = time.monotonic()
            with self.assertRaises(subprocess.TimeoutExpired):
                fixture.run([sys.executable, '-c', 'import time; time.sleep(30)'],
                            fixture.clean_environment(fixture.os.environ), directory, .15)
            self.assertLess(time.monotonic() - start, 7)

    def test_server_postlaunch_capture_failure_terminates_owner(self):
        owner = Mock()
        owner.poll.return_value = None
        with patch.object(fixture, 'OwnedProcess', return_value=owner), \
             patch.object(fixture, 'PipeReader', side_effect=RuntimeError('capture setup')):
            with self.assertRaisesRegex(RuntimeError, 'capture setup'):
                fixture.Server(['fake'], {}, Path('/unused'))
        owner.kill.assert_called_once()
        owner.finish.assert_called_once()

    def launcher_server(self, status=0, active=1):
        server = fixture.Server.__new__(fixture.Server)
        server.launcher = True
        server.budget = Mock()
        server.readers = []
        server.output, server.error = io.BytesIO(), io.BytesIO()
        server.owner = SimpleNamespace(lock=threading.RLock(), poll=Mock(return_value=status),
            job=SimpleNamespace(active=Mock(return_value=active), retain_members=Mock(), members={}))
        return server

    def test_successful_launcher_exit_requires_live_owned_job(self):
        self.launcher_server().healthy()
        with self.assertRaisesRegex(RuntimeError, 'no live server'):
            self.launcher_server(active=0).healthy()
        with self.assertRaisesRegex(RuntimeError, 'launcher failed'):
            self.launcher_server(status=1).healthy()

    def test_pending_launcher_is_not_ready_despite_live_job(self):
        server = self.launcher_server(status=None)
        self.assertFalse(server.ready(Path('/not-read-before-launcher-completes'), 24567))
        server.owner.job.retain_members.assert_not_called()

    def test_launcher_failure_between_health_and_readiness_is_rejected(self):
        server = self.launcher_server()
        server.owner.poll.side_effect = [None, 1]
        with self.assertRaisesRegex(RuntimeError, 'launcher failed before readiness'):
            server.ready(Path('/not-read-after-launcher-failure'), 24567)
        server.owner.job.retain_members.assert_not_called()

    def test_launcher_success_still_requires_owned_postmaster_identity(self):
        with tempfile.TemporaryDirectory(prefix='nil-pg-unit-identity-') as directory:
            data = Path(directory).resolve()
            (data / 'postmaster.pid').write_text(
                f'123\n{data}\n0\n24567\n\n\n\nready\n', encoding='utf-8')
            server = self.launcher_server()
            with self.assertRaisesRegex(RuntimeError, 'not in the owned Job'):
                server.ready(data, 24567)
            server.owner.job.members[(123, 456)] = object()
            self.assertTrue(server.ready(data, 24567))
            self.assertEqual(server.owner.job.retain_members.call_count, 2)

    def test_foreground_server_still_requires_live_leader(self):
        server = self.launcher_server()
        server.launcher = False
        with self.assertRaisesRegex(RuntimeError, 'exited prematurely'):
            server.healthy()

    def test_capture_requires_postgres_marker_not_launcher_message(self):
        server = self.launcher_server()
        server.output.write(b'waiting for server to start... done\nserver started\n')
        with self.assertRaisesRegex(RuntimeError, 'startup output was not captured'):
            server.verify_startup_capture()
        server.error.write(b'LOG: database system is ready to accept connections\n')
        server.verify_startup_capture()

    def test_failed_initialization_retains_evidence_but_removes_password(self):
        with tempfile.TemporaryDirectory(prefix='nil-pg-unit-report-') as directory:
            args = SimpleNamespace(stdlib=Path(directory), report_dir=Path(directory),
                                   postgres_bin=Path(directory), postgres_share=None, provider_test=None)
            report = {}
            try:
                with patch.object(fixture, 'private_permissions'), \
                     patch.object(fixture.secrets, 'token_urlsafe', return_value='fixture-private-secret'), \
                     patch.object(fixture, 'run', return_value=(1, b'failure fixture-private-secret')):
                    with self.assertRaisesRegex(RuntimeError, 'initialization failed'):
                        fixture.run_fixture(args, report)
                root = Path(report['work_directory'])
                self.assertTrue(report['work_directory_retained'])
                self.assertTrue(root.is_dir())
                self.assertFalse((root / 'init-password').exists())
                self.assertEqual((Path(directory) / 'initdb.log').read_text(), 'failure <private fixture value>')
            finally:
                if report.get('work_directory'):
                    shutil.rmtree(report['work_directory'])


if __name__ == '__main__':
    unittest.main()
