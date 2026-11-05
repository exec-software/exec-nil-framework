#!/usr/bin/env python3
"""Portable harness regressions; runs without any SDK or compiler internals."""
import json
import ctypes
import errno
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

import debugger_process as processes
from debugger_process import OwnedProcess, PipeReader, WindowsJob, write_pipe
import validate_debugger as debugger


class ConsumerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil debugger régression ')
        self.directory = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def process(self, source, *arguments):
        return OwnedProcess([sys.executable, '-I', '-S', '-c', source, *arguments],
                            cwd=self.directory, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_argv_unicode_empty_quotes_and_exit(self):
        arguments = ['répertoire 雪', '', 'quoted " text', 'trailing\\']
        owned = self.process('import json,sys; print(json.dumps(sys.argv[1:])); '
                             'print("stderr", file=sys.stderr); sys.exit(19)', *arguments)
        out, err = owned.communicate(10)
        self.assertEqual(json.loads(out), arguments)
        self.assertEqual(err.replace(b'\r\n', b'\n'), b'stderr\n')
        self.assertEqual(owned.process.returncode, 19)

    def test_pipe_reader_fragmented_unicode_and_eof(self):
        owned = self.process('import os,time; '
                             '[os.write(1, bytes([n])) for n in "雪\\n".encode()]; time.sleep(.1)')
        reader = PipeReader(owned.process.stdout)
        try:
            data = b''
            while chunk := reader.read(10):
                data += chunk
            self.assertEqual(data.decode(), '雪\n')
            owned.process.wait(timeout=5)
            owned.finish()
        finally:
            if owned.process.returncode is None:
                owned.kill()
                owned.finish()
            reader.close()
            owned.process.stderr.close()

    def test_silent_process_timeout_is_bounded(self):
        owned = self.process('import time; time.sleep(60)')
        start = time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            owned.communicate(.15)
        self.assertLess(time.monotonic() - start, 8)
        self.assertIsNotNone(owned.process.returncode)

    @unittest.skipIf(os.name == 'nt', 'POSIX retained group leader')
    def test_exited_leader_remains_owned_until_group_cleanup(self):
        owned = self.process('raise SystemExit(23)')
        try:
            deadline = time.monotonic() + 5
            while owned.poll() is None and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(owned.poll(), 23)
            self.assertIsNone(owned.process.returncode, 'leader reaped before group cleanup')
            self.assertEqual(owned.wait(5), 23)
        finally:
            owned.kill()
            owned.finish()
            owned.process.stdout.close()
            owned.process.stderr.close()

    @unittest.skipIf(os.name == 'nt', 'POSIX repeated process-group termination')
    def test_repeated_termination_waits_for_owned_exit(self):
        # Exercise the output-budget/protocol-close pattern on the native host:
        # one callback signals, another requests cleanup during the exit.
        for attempt in range(8):
            with self.subTest(attempt=attempt):
                owned = self.process('import time; time.sleep(60)')
                try:
                    owned.terminate()
                    owned.terminate()
                    owned.kill()
                    self.assertIsNotNone(owned.process.returncode)
                finally:
                    owned.kill()
                    owned.finish()
                    owned.process.stdout.close()
                    owned.process.stderr.close()

    def test_reap_serializes_concurrent_budget_termination(self):
        owned = self.process('pass')
        original_wait = owned.process.wait
        signalled = threading.Event()
        callbacks = []

        def terminate():
            owned.terminate()
            signalled.set()

        def checked_wait(*args, **kwargs):
            callback = threading.Thread(target=terminate)
            callbacks.append(callback)
            callback.start()
            self.assertFalse(signalled.wait(.05), 'termination entered the final reap critical section')
            return original_wait(*args, **kwargs)

        try:
            with patch.object(owned.process, 'wait', side_effect=checked_wait):
                owned.wait(5)
            for callback in callbacks:
                callback.join(5)
                self.assertFalse(callback.is_alive())
            self.assertTrue(signalled.is_set())
        finally:
            owned.kill()
            owned.finish()
            owned.process.stdout.close()
            owned.process.stderr.close()

    def test_command_stdout_and_stderr_floods_are_bounded(self):
        for descriptor in (1, 2):
            owned = self.process(f'import os\nwhile True: os.write({descriptor}, b"x"*65536)')
            start = time.monotonic()
            with patch('debugger_process.OUTPUT_LIMIT', 131072):
                with self.assertRaisesRegex(RuntimeError, 'session budget'):
                    owned.communicate(10)
            self.assertLess(time.monotonic() - start, 8)
            self.assertIsNotNone(owned.process.returncode)

    def test_protocol_stderr_flood_is_bounded(self):
        with patch('debugger_process.OUTPUT_LIMIT', 131072):
            protocol = debugger.Protocol([sys.executable, '-I', '-S', '-c',
                                          'import os\nwhile True: os.write(2,b"x"*65536)'],
                                         'jsonl', self.directory / 'flood', 10, allow_stderr=True)
            with self.assertRaisesRegex(RuntimeError, 'session budget'):
                protocol.read()
            with self.assertRaisesRegex(RuntimeError, 'session budget'):
                protocol.close()
        self.assertLessEqual((protocol.directory / 'stderr.txt').stat().st_size, 131072)
        self.assertFalse(protocol.reader.thread.is_alive())
        self.assertFalse(protocol.stderr_reader.thread.is_alive())

    def test_blocked_protocol_write_times_out(self):
        owned = OwnedProcess([sys.executable, '-I', '-S', '-c', 'import time; time.sleep(60)'],
                             stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with self.assertRaises(TimeoutError):
                write_pipe(owned.process.stdin, b'x' * 1048576, .15, owned.terminate)
            owned.wait(5)
        finally:
            owned.kill()
            owned.finish()
            owned.process.stdin.close()

    def test_protocol_reader_setup_failure_cleans_owned_process(self):
        owners = []
        def create(*args, **kwargs):
            owned = OwnedProcess(*args, **kwargs)
            owners.append(owned)
            return owned
        with patch.object(debugger, 'OwnedProcess', side_effect=create), \
                patch.object(debugger, 'PipeReader', side_effect=RuntimeError('reader setup failure')):
            with self.assertRaisesRegex(RuntimeError, 'reader setup failure'):
                debugger.Protocol([sys.executable, '-I', '-S', '-c', 'import time; time.sleep(60)'],
                                  'jsonl', self.directory / 'setup-failure', 1)
        self.assertEqual(len(owners), 1)
        self.assertIsNotNone(owners[0].process.returncode)
        for pipe in (owners[0].process.stdin, owners[0].process.stdout, owners[0].process.stderr):
            pipe.close()

    def test_timeout_cleans_descendant_holding_stdout(self):
        owned = self.process('import subprocess,sys; '
                             'subprocess.Popen([sys.executable,"-I","-S","-c",'
                             '"import time; time.sleep(60)"]); print("parent done",flush=True)')
        start = time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            owned.communicate(.5)
        self.assertLess(time.monotonic() - start, 8)
        self.assertIsNotNone(owned.process.returncode)

    @unittest.skipUnless(os.name == 'nt', 'Windows Job descendant contract')
    def test_exited_parent_with_detached_stdio_child_fails_closed(self):
        # A zero Job active count can precede process-handle signaling/resource
        # rundown. Hold both a CWD and an exclusively opened file, then demand
        # immediate deletion after cleanup; no retry/sleep/ignore_errors hides it.
        child_source = ('import ctypes as c,pathlib,time\n'
                        'k=c.WinDLL("kernel32",use_last_error=True)\n'
                        'k.CreateFileW.argtypes=[c.c_wchar_p,c.c_uint32,c.c_uint32,c.c_void_p,'
                        'c.c_uint32,c.c_uint32,c.c_void_p]\n'
                        'k.CreateFileW.restype=c.c_void_p\n'
                        'lock=k.CreateFileW("exclusive.bin",0xC0000000,0,None,4,0,None)\n'
                        'assert lock not in (None,c.c_void_p(-1).value)\n'
                        'pathlib.Path("ready").touch()\n'
                        'time.sleep(60)\n')
        parent_source = ('import pathlib,subprocess,sys,time\n'
                         f'subprocess.Popen([sys.executable,"-I","-S","-c",{child_source!r}],'
                         'stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\n'
                         'deadline=time.monotonic()+5\n'
                         'while not pathlib.Path("ready").exists():\n'
                         ' if time.monotonic()>=deadline: raise RuntimeError("child did not acquire resources")\n'
                         ' time.sleep(.005)\n')
        for attempt in range(12):
            with self.subTest(attempt=attempt), tempfile.TemporaryDirectory(dir=self.directory) as directory:
                owned = OwnedProcess([sys.executable, '-I', '-S', '-c', parent_source], cwd=directory,
                                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                start = time.monotonic()
                with self.assertRaisesRegex(RuntimeError, 'live descendants'):
                    owned.communicate(10)
                self.assertLess(time.monotonic() - start, 8)
                (Path(directory) / 'exclusive.bin').unlink()

    @unittest.skipUnless(os.name == 'nt', 'Windows retained process-handle contract')
    def test_empty_accounting_does_not_replace_process_signal(self):
        owned = self.process('import time; print("ready",flush=True); time.sleep(60)')
        reader = PipeReader(owned.process.stdout)
        job = owned.job
        try:
            self.assertEqual(reader.read(10).strip(), b'ready')
            # Deterministically simulate the observed ActiveProcesses==0 race
            # while a retained owned handle is still unsignaled.
            job.cleanup_deadline = time.monotonic() + .05
            with patch.object(job.k, 'TerminateJobObject', return_value=1), \
                    patch.object(job, 'active', return_value=0):
                job.terminate()
                self.assertTrue(job.members)
                with self.assertRaisesRegex(RuntimeError, 'termination did not complete'):
                    job.drain()
        finally:
            job.cleanup_deadline = None  # Remove only this test's injected short deadline.
            owned.kill()
            owned.finish()
            reader.close()
            owned.process.stderr.close()

    @unittest.skipUnless(os.name == 'nt', 'Windows verified Job member identity')
    def test_unrelated_pid_snapshot_is_rejected_without_signaling_it(self):
        unrelated = subprocess.Popen([sys.executable, '-I', '-S', '-c', 'import time; time.sleep(60)'],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        owned = self.process('import time; print("ready",flush=True); time.sleep(60)')
        reader = PipeReader(owned.process.stdout)
        try:
            self.assertEqual(reader.read(10).strip(), b'ready')
            # Retain the real gate/target first so even this deliberately bad
            # snapshot test can wait for their actual resource rundown.
            owned.job.retain_members()
            with patch.object(owned.job, '_member_ids', return_value=[unrelated.pid]):
                with self.assertRaisesRegex(RuntimeError, 'snapshot could not be verified'):
                    owned.kill()
            self.assertIsNone(unrelated.poll(), 'unverified unrelated process was terminated')
            self.assertIsNotNone(owned.process.returncode)
        finally:
            # Verification failure stays recorded; closing the owned Job is a
            # final safety net, not an excuse to claim confirmed cleanup.
            owned.job.close()
            owned.job = None
            reader.close()
            owned.process.stderr.close()
            unrelated.kill()  # Original retained Popen handle, never reopened PID.
            unrelated.wait(timeout=5)

    @unittest.skipUnless(os.name == 'nt', 'Windows raced Job population guard')
    def test_new_process_during_snapshot_cannot_claim_verified_cleanup(self):
        owned = self.process('import time; print("ready",flush=True); time.sleep(60)')
        reader = PipeReader(owned.process.stdout)
        calls = 0
        accounting = owned.job.accounting

        def changed_population():
            nonlocal calls
            calls += 1
            result = accounting()
            if calls >= 2:
                result.total += 1  # Simulate a process born after the captured list.
            return result

        try:
            self.assertEqual(reader.read(10).strip(), b'ready')
            with patch.object(owned.job, 'accounting', side_effect=changed_population):
                with self.assertRaisesRegex(RuntimeError, 'population changed'):
                    owned.kill()
            self.assertIsNotNone(owned.process.returncode)
        finally:
            owned.job.close()
            owned.job = None
            reader.close()
            owned.process.stderr.close()

    @unittest.skipUnless(os.name == 'nt', 'Windows pre-launch assignment contract')
    def test_job_assignment_failure_does_not_launch_target(self):
        marker = self.directory / 'must-not-exist'
        job = WindowsJob()
        try:
            with patch.object(job.k, 'AssignProcessToJobObject', return_value=0):
                with self.assertRaises(OSError):
                    job.start([sys.executable, '-I', '-S', '-c',
                               'import pathlib,sys; pathlib.Path(sys.argv[1]).touch()', str(marker)],
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.assertFalse(marker.exists())
            self.assertEqual(job.active(), 0)
        finally:
            job.close()

    def test_jsonl_protocol_and_clean_shutdown(self):
        source = ('import json,sys\n'
                  'print(json.dumps({"event":"started","label":"雪"}),flush=True)\n'
                  'for line in sys.stdin:\n'
                  ' request=json.loads(line)\n'
                  ' if request["cmd"]=="quit":\n'
                  '  print(json.dumps({"event":"quit"}),flush=True)\n'
                  '  print(json.dumps({"event":"detached"}),flush=True)\n'
                  '  break\n')
        protocol = debugger.Protocol([sys.executable, '-I', '-S', '-c', source], 'jsonl',
                                     self.directory / 'jsonl', 10)
        try:
            self.assertEqual(protocol.until('started')['label'], '雪')
        finally:
            protocol.close()
        self.assertEqual(protocol.p.returncode, 0)
        self.assertFalse(protocol.reader.thread.is_alive())

    def test_dap_fragmented_message_and_disconnect(self):
        source = ('import json,os,sys\n'
                  'def send(message):\n'
                  ' body=json.dumps(message).encode()\n'
                  ' frame=("Content-Length: %d\\r\\n\\r\\n"%len(body)).encode()+body\n'
                  ' for byte in frame: os.write(1,bytes([byte]))\n'
                  'send({"event":"initialized","body":{"label":"雪"}})\n'
                  'while True:\n'
                  ' header=sys.stdin.buffer.readline()\n'
                  ' if not header: break\n'
                  ' size=int(header.split(b":")[1]); sys.stdin.buffer.readline()\n'
                  ' request=json.loads(sys.stdin.buffer.read(size))\n'
                  ' send({"type":"response","request_seq":request["seq"],"success":True})\n'
                  ' if request["command"]=="disconnect": break\n')
        protocol = debugger.Protocol([sys.executable, '-I', '-S', '-c', source], 'dap',
                                     self.directory / 'dap', 10)
        try:
            self.assertEqual(protocol.until('initialized')['body']['label'], '雪')
            self.assertTrue(protocol.request('initialize')['success'])
        finally:
            protocol.close()
        self.assertEqual(protocol.p.returncode, 0)

    def test_incomplete_protocol_frame_times_out_and_cleans_up(self):
        source = 'import os,time; os.write(1,b"Content-Length: 50\\r\\n\\r\\n{"); time.sleep(60)'
        protocol = debugger.Protocol([sys.executable, '-I', '-S', '-c', source], 'dap',
                                     self.directory / 'incomplete', .15)
        start = time.monotonic()
        with self.assertRaises(TimeoutError):
            protocol.read()
        with self.assertRaises(TimeoutError):
            protocol.close()
        self.assertLess(time.monotonic() - start, 8)
        self.assertFalse(protocol.reader.thread.is_alive())
        self.assertIsNotNone(protocol.p.returncode)

    def test_oversized_frame_rejected(self):
        source = 'import os; os.write(1,b"Content-Length: 10000001\\r\\n\\r\\n")'
        protocol = debugger.Protocol([sys.executable, '-I', '-S', '-c', source], 'dap',
                                     self.directory / 'oversized', 5)
        try:
            with self.assertRaisesRegex(ValueError, 'invalid frame size'):
                protocol.read()
            protocol.p.wait(timeout=5)
        finally:
            protocol.close()

    def test_environment_removes_framework_overrides_keeps_windows_toolchain(self):
        inputs = {'NY_IMPORT_PATH': 'bad', 'NIL_SDK': 'bad', 'LD_PRELOAD': 'bad',
                  'CPATH': 'bad', 'PATH': 'toolchain path', 'INCLUDE': 'headers',
                  'LIB': 'libraries', 'SystemRoot': 'windows', 'LIBPATH': 'framework'}
        with patch.dict(os.environ, inputs, clear=True):
            env = debugger.clean_env()
        for name in ('NY_IMPORT_PATH', 'NIL_SDK', 'LD_PRELOAD', 'CPATH'):
            self.assertNotIn(name, env)
        for name in ('INCLUDE', 'LIB', 'LIBPATH', 'SystemRoot'):
            self.assertEqual({key.upper(): value for key, value in env.items()}[name.upper()], inputs[name])
        expected_path = (inputs['PATH'] if os.name == 'nt' else
                         '/usr/bin:/bin:/usr/sbin:/sbin' if sys.platform == 'darwin' else
                         '/usr/local/bin:/usr/bin:/bin')
        self.assertEqual(env['PATH'], expected_path)

    def test_source_identity_not_path_basename(self):
        left, right = self.directory / 'left', self.directory / 'right'
        left.mkdir()
        right.mkdir()
        for directory in (left, right):
            (directory / 'source.ny').touch()
        self.assertTrue(debugger.same_source(str(left / 'source.ny'), left / 'source.ny'))
        self.assertFalse(debugger.same_source(str(right / 'source.ny'), left / 'source.ny'))
        self.assertFalse(debugger.same_source(None, left / 'source.ny'))

    def test_published_markers_are_unique(self):
        for marker in ('DBG_SCALE', 'DBG_CALL', 'DBG_AFTER_CALL', 'DBG_SHADOW_INNER', 'DBG_SHADOW_OUTER'):
            self.assertGreater(debugger.line(debugger.FIXTURE, marker), 0)
        self.assertGreater(debugger.line(debugger.ROOT / 'probes/debugger/pause.ny', 'DBG_PAUSE_LOOP'), 0)

    def test_report_cannot_replace_a_source_or_anchor(self):
        targets = [debugger.FIXTURE, Path(debugger.__file__), debugger.ROOT / 'src/scene_player.ny']
        for target in targets:
            before = target.read_bytes()
            with patch.object(sys, 'argv', ['validate_debugger.py', '--report', str(target)]):
                with self.assertRaises(SystemExit) as stopped:
                    debugger.main()
            self.assertEqual(stopped.exception.code, 2)
            self.assertEqual(target.read_bytes(), before)


class DarwinGroupTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        def advance(seconds):
            self.now += seconds
        for mocked in (patch.object(processes.time, 'monotonic', side_effect=lambda: self.now),
                       patch.object(processes.time, 'sleep', side_effect=advance),
                       patch.object(processes, 'CLEANUP_SECONDS', .03)):
            mocked.start()
            self.addCleanup(mocked.stop)

    def owner(self):
        owner = OwnedProcess.__new__(OwnedProcess)
        owner.lock, owner.windows = threading.RLock(), False
        owner.posix_cleanup_deadline = None
        owner.process = SimpleNamespace(pid=1234, returncode=None)
        return owner

    def test_permission_exception_requires_every_dead_group_precondition(self):
        for platform, error, exited, dead_group, accepted in (
                ('darwin', errno.EPERM, 0, True, True),
                ('linux', errno.EPERM, 0, True, False),
                ('darwin', errno.EACCES, 0, True, False),
                ('darwin', errno.EPERM, None, True, False),
                ('darwin', errno.EPERM, 0, False, False)):
            with self.subTest(platform=platform, error=error, exited=exited, dead_group=dead_group):
                owner = self.owner()
                # This mocked POSIX path also runs on Windows, which has no
                # SIGKILL constant. No native process receives this fixture signal.
                with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                        patch.object(processes.sys, 'platform', platform), \
                        patch.object(processes.os, 'killpg', side_effect=PermissionError(error, 'fixture'), create=True), \
                        patch.object(owner, 'poll', return_value=exited), \
                        patch.object(processes, '_darwin_group_exited', return_value=dead_group):
                    if accepted:
                        owner.terminate()
                    else:
                        with self.assertRaises(PermissionError):
                            owner.terminate()

    def test_repeated_termination_waits_for_exit_and_complete_group_proof(self):
        owner = self.owner()
        with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                patch.object(processes.sys, 'platform', 'darwin'), \
                patch.object(processes.os, 'killpg', side_effect=[None, PermissionError(errno.EPERM, 'fixture')], create=True) as killpg, \
                patch.object(owner, 'poll', side_effect=[None, 0, 0]), \
                patch.object(processes, '_darwin_group_exited', side_effect=[False, True]) as proof:
            owner.terminate()
            deadline = owner.posix_cleanup_deadline
            owner.terminate()
        self.assertEqual(killpg.call_count, 2)
        self.assertEqual(proof.call_count, 2)
        self.assertTrue(all(call.args == (1234, deadline) for call in proof.call_args_list))
        self.assertEqual(owner.posix_cleanup_deadline, deadline)
        self.assertLess(self.now, deadline)

    def test_real_permission_denial_exhausts_one_deadline_without_reaping(self):
        owner = self.owner()
        with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                patch.object(processes.sys, 'platform', 'darwin'), \
                patch.object(processes.os, 'killpg', side_effect=PermissionError(errno.EPERM, 'fixture'), create=True), \
                patch.object(owner, 'poll', return_value=None), \
                patch.object(processes, '_darwin_group_exited') as proof:
            with self.assertRaises(PermissionError):
                owner.terminate()
            deadline = owner.posix_cleanup_deadline
            with self.assertRaises(PermissionError):
                owner.terminate()
        self.assertEqual(self.now, deadline)
        self.assertIsNone(owner.process.returncode)
        proof.assert_not_called()

    def test_final_reap_uses_remaining_original_cleanup_budget(self):
        owner = self.owner()
        owner.job = None
        owner.process.wait = Mock(return_value=-9)
        with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                patch.object(processes.sys, 'platform', 'darwin'), \
                patch.object(processes.os, 'killpg', create=True):
            owner.terminate()
            self.now = .02
            owner.kill()
        self.assertEqual(owner.posix_cleanup_deadline, .03)
        owner.process.wait.assert_called_once_with(timeout=.01)

    def test_disappearing_member_requires_fresh_complete_proof(self):
        owner = self.owner()
        with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                patch.object(processes.sys, 'platform', 'darwin'), \
                patch.object(processes.os, 'killpg', side_effect=PermissionError(errno.EPERM, 'fixture'), create=True), \
                patch.object(owner, 'poll', return_value=0), \
                patch.object(processes, '_darwin_group_exited', side_effect=[ProcessLookupError(errno.ESRCH, 'fixture'), True]) as proof:
            owner.terminate()
        self.assertEqual(proof.call_count, 2)

    def test_observation_permission_failure_is_not_retried_or_accepted(self):
        owner = self.owner()
        with patch.object(processes.signal, 'SIGKILL', 9, create=True), \
                patch.object(processes.sys, 'platform', 'darwin'), \
                patch.object(processes.os, 'killpg', side_effect=PermissionError(errno.EPERM, 'fixture'), create=True), \
                patch.object(owner, 'poll', return_value=0), \
                patch.object(processes, '_darwin_group_exited', side_effect=PermissionError(errno.EACCES, 'lookup')) as proof:
            with self.assertRaises(PermissionError) as caught:
                owner.terminate()
        self.assertEqual(caught.exception.errno, errno.EACCES)
        self.assertEqual(proof.call_count, 1)

    def test_dead_group_proof_after_deadline_is_rejected(self):
        def snapshot(pgid, deadline):
            self.now += .02
            return {1234: 5}
        with patch.object(processes, '_darwin_group_snapshot', side_effect=snapshot):
            self.assertFalse(processes._darwin_group_exited(1234, .03))

    def test_two_complete_dead_snapshots_with_retained_leader_are_required(self):
        for first, second, expected in (
                ({1234: 5}, {1234: 5}, True),
                ({1234: 5, 1235: 5}, {1234: 5, 1235: 5}, True),
                ({1234: 5, 1235: 5}, {1234: 5}, True),
                ({1235: 5}, {1235: 5}, False),
                ({1234: 5, 1235: 2}, {1234: 5, 1235: 2}, False),
                ({1234: 5, 1235: 5}, {1235: 5}, False),
                ({1234: 5}, {1234: 2}, False),
                ({1234: 5}, {1234: 5, 1235: 5}, False),
                ({1234: 5}, {1234: 5, 1235: 2}, False)):
            with self.subTest(first=first, second=second), \
                    patch.object(processes, '_darwin_group_snapshot', side_effect=[first, second]):
                self.assertEqual(processes._darwin_group_exited(1234, self.now + .03), expected)

    def test_uncertain_group_lookup_does_not_authorize_cleanup(self):
        with patch.object(processes, '_darwin_group_snapshot', side_effect=PermissionError(errno.EPERM, 'fixture')):
            with self.assertRaises(PermissionError):
                processes._darwin_group_exited(1234, self.now + .03)

    def snapshot(self, *, ids=(1234,), list_size=None, lookup_error=0,
                 member_size=64, member_pid=1234, member_group=1234, deadline=None):
        def list_pids(kind, group, output, capacity):
            self.assertEqual((kind, group, capacity), (2, 1234, 4097 * 4))
            for index, pid in enumerate(ids):
                output[index] = pid
            ctypes.set_errno(lookup_error)
            return len(ids) * 4 if list_size is None else list_size

        def pid_info(pid, flavor, include_zombies, output, capacity):
            self.assertEqual((flavor, include_zombies, capacity), (13, 1, 64))
            info = output._obj
            info.pid, info.pgid, info.status = member_pid, member_group, 5
            return member_size

        lib = SimpleNamespace(proc_listpids=Mock(side_effect=list_pids),
                              proc_pidinfo=Mock(side_effect=pid_info))
        with patch.object(ctypes, 'CDLL', return_value=lib):
            return processes._darwin_group_snapshot(1234, time.monotonic() + 5 if deadline is None else deadline)

    def test_group_lookup_uses_exact_bounded_group_and_zombie_layout(self):
        self.assertEqual(self.snapshot(), {1234: 5})

    def test_incomplete_invalid_or_foreign_group_observations_fail(self):
        for options in ({'list_size': 4097 * 4}, {'list_size': 3}, {'ids': ()},
                        {'ids': (1234, 1234)}, {'ids': (-1,)}, {'member_size': 63},
                        {'member_pid': 4321}, {'member_group': 4321}, {'deadline': 0}):
            with self.subTest(options=options), self.assertRaises(RuntimeError):
                self.snapshot(**options)

    def test_group_lookup_permission_error_is_not_an_empty_group(self):
        with self.assertRaises(PermissionError):
            self.snapshot(ids=(), lookup_error=errno.EPERM)


if __name__ == '__main__':
    unittest.main()
