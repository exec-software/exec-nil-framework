"""Bounded, retained DAP evidence with exact child-tree ownership."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'validation/ny-3d/scripts'))
from debugger_process import CaptureBudget, OwnedProcess, PipeReader, write_pipe


def output_directory(parent, prefix):
    if parent:
        parent = parent.resolve()
        parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=parent))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment():
    return {key: value for key, value in os.environ.items()
            if not key.startswith(('NY_', 'NIL_'))}


def compile_program(compiler, source, output, record, env, *, binary=False, nilstd=None):
    command = [compiler, '--quiet', '--no-bundle', '--debug-info', '--no-stdlib']
    if nilstd:
        command += ['--sdk-path', nilstd]
    if binary:
        command += ['--emit-binary-nil']
    command += ['--output', output, source]
    command = [str(part) for part in command]
    owner = OwnedProcess(command, cwd=output.parent, env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = owner.communicate(timeout=90)
    output.with_suffix('.compile.stdout').write_bytes(stdout)
    output.with_suffix('.compile.stderr').write_bytes(stderr)
    record.append({'argv': command, 'exit_code': owner.process.returncode,
                   'source_sha256': digest(source), 'cleanup_verified': True})
    assert owner.process.returncode == 0, (command, stdout, stderr)
    assert output.read_bytes().startswith(b'NIL\0') == binary


class Client:
    def __init__(self, debugger, root, record, env, timeout=45):
        self.record = record
        record.update(status='FAIL', natural_exit=False, cleanup_verified=False, requests=[])
        self.deadline = time.monotonic() + timeout
        self.owner = OwnedProcess([str(debugger), '--dap'], cwd=root, env=env,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.budget = CaptureBudget(self.owner.terminate, maximum=4 * 1024 * 1024)
        self.stdout = PipeReader(self.owner.process.stdout, self.budget)
        self.wire = (root / (record['name'] + '.wire')).open('wb')
        self.errors = (root / (record['name'] + '.stderr')).open('wb')
        self.stderr = PipeReader(self.owner.process.stderr, self.budget, self.errors)
        self.buffer = b''
        self.pending = []
        self.seq = 0
        self.last_sequence = 0
        self.observed = []
        self.stdout_eof = False
        self.finished = False

    def remaining(self, maximum=8):
        remaining = min(maximum, self.deadline - time.monotonic())
        assert remaining > 0, 'DAP session deadline expired'
        self.budget.check()
        return remaining

    def send(self, data):
        write_pipe(self.owner.process.stdin, data, self.remaining(), self.owner.terminate)

    def _read(self):
        chunk = self.stdout.read(self.remaining())
        self.wire.write(chunk)
        self.wire.flush()
        self.stdout_eof = not chunk
        assert chunk, 'adapter stdout ended before the expected frame'
        self.buffer += chunk

    def frame(self):
        while b'\r\n\r\n' not in self.buffer:
            assert len(self.buffer) <= 8192, 'oversized DAP response header'
            self._read()
        header, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
        assert header.startswith(b'Content-Length: ') and b'\r\n' not in header, header
        size = int(header[16:])
        assert 0 < size <= 1024 * 1024
        while len(self.buffer) < size:
            self._read()
        body, self.buffer = self.buffer[:size], self.buffer[size:]
        message = json.loads(body)
        assert message['seq'] > self.last_sequence, message
        self.last_sequence = message['seq']
        assert len(self.observed) < 4096, 'too many adapter frames'
        self.observed.append({key: message[key] for key in ('seq', 'type', 'event', 'request_seq') if key in message})
        return message

    def wait(self, predicate):
        while True:
            self.remaining()
            for index, message in enumerate(self.pending):
                if predicate(message):
                    return self.pending.pop(index)
            assert len(self.pending) < 4096, 'too many unconsumed adapter messages'
            self.pending.append(self.frame())

    def request(self, command, arguments=None, success=True):
        self.seq += 1
        seq = self.seq
        body = json.dumps({'seq': seq, 'type': 'request', 'command': command,
                           'arguments': arguments or {}}).encode()
        first_frame = len(self.observed)
        self.send(f'Content-Length: {len(body)}\r\n\r\n'.encode() + body)
        response = self.wait(lambda message: message.get('type') == 'response' and
                             message.get('request_seq') == seq)
        self.record['requests'].append({'command': command, 'success': response.get('success')})
        assert response['success'] is success, response
        if success and command in ('configurationDone', 'continue', 'next', 'stepIn', 'stepOut'):
            # Searching a message queue alone would hide output emitted before
            # the acknowledgement. Verify the actual observed wire sequence.
            preceding = self.observed[first_frame:]
            assert len(preceding) == 1 and preceding[0].get('request_seq') == seq, preceding
            continued = self.frame()
            assert continued.get('event') == 'continued', continued
            self.record['requests'][-1]['start_wire_order_verified'] = True
        return response['body']

    def event(self, name):
        return self.wait(lambda message: message.get('type') == 'event' and
                         message.get('event') == name)['body']

    def initialize(self, program, *, stop_on_entry=False):
        self.request('initialize', {'adapterID': 'nil-contract', 'pathFormat': 'path'})
        self.request('launch', {'program': str(program), 'stopOnEntry': stop_on_entry})
        self.event('initialized')

    def evaluate(self, name):
        return self.request('evaluate', {'expression': name})['result']

    def stopped(self, reason):
        stopped = self.event('stopped')
        assert stopped['reason'] == reason, stopped
        return self.request('stackTrace', {'threadId': 1})['stackFrames'][0]

    def natural_exit(self, expected=0):
        # Do not close stdin or signal the child to make this oracle pass.
        deadline = time.monotonic() + self.remaining(5)
        while self.owner.poll() is None:
            assert time.monotonic() < deadline, 'adapter required forced cleanup instead of natural shutdown'
            self.budget.check()
            time.sleep(.01)
        self.record['stdin_open_at_exit'] = not self.owner.process.stdin.closed
        self.record['natural_exit'] = True
        self.owner.wait(max(.01, deadline - time.monotonic()))
        self.record['exit_code'] = self.owner.process.returncode
        assert self.owner.process.returncode == expected, self.record
        self.record['status'] = 'PASS'

    def close(self):
        if self.finished:
            return
        self.finished = True
        try:
            if self.owner.poll() is None:
                self.owner.kill()
            else:
                self.owner.wait(5)
            self.owner.finish()
            self.record['cleanup_verified'] = True
        finally:
            if not self.owner.process.stdin.closed:
                self.owner.process.stdin.close()
            try:
                # Preserve remaining framed bytes too; the reader is bounded
                # even when a failed assertion stops consuming protocol events.
                while not self.stdout_eof:
                    chunk = self.stdout.read(5)
                    self.wire.write(chunk)
                    self.stdout_eof = not chunk
            finally:
                self.stdout.close()
                self.stderr.close()
                self.wire.close()
                self.errors.close()
            self.budget.check()


def session(debugger, root, report, name, env, action):
    record = {'name': name}
    report['sessions'].append(record)
    client = Client(debugger, root, record, env)
    try:
        action(client)
    except BaseException as error:
        record.update(status='FAIL', error=str(error))
        raise
    finally:
        client.close()
