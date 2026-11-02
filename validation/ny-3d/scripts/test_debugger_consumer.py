#!/usr/bin/env python3
"""Portable harness regressions; runs without any SDK or compiler internals."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

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
        owned = self.process('import subprocess,sys; '
                             'subprocess.Popen([sys.executable,"-I","-S","-c",'
                             '"import time; time.sleep(60)"], stdin=subprocess.DEVNULL,'
                             'stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)')
        start = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, 'live descendants'):
            owned.communicate(10)
        self.assertLess(time.monotonic() - start, 8)

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
        self.assertEqual(env['PATH'], inputs['PATH'] if os.name == 'nt' else '/usr/local/bin:/usr/bin:/bin')

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


if __name__ == '__main__':
    unittest.main()
