#!/usr/bin/env python3
"""Bounded real-interpreter pause/capture/mutate/restore contract on both hosts.

Only private NIL fixtures are launched. Each child is waited or terminated
in finally; no arbitrary process discovery or desktop input.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

PROGRAM = '''module checkpoint_contract
function main:
    PUSH_CONST 10
    STORE_GLOBAL G
    PUSH_CONST 5
    CALL helper 1
    STORE_GLOBAL RESULT
    LOAD_GLOBAL RESULT
    RET
end function
function helper(value: i64):
    STORE_LOCAL value
    LOAD_LOCAL value
    PUSH_CONST 1
    ADD
    STORE_LOCAL value
    LOAD_GLOBAL G
    PUSH_CONST 1
    ADD
    STORE_GLOBAL G
    LOAD_LOCAL value
    RET
end function
'''

EXIT_PROGRAM = '''module control_exit_contract
function main:
    PUSH_CONST 71
    CAST i32
    INTEROP_CALL std_sys exit 1 void
    PUSH_CONST 99
    INTEROP_CALL std_sys sys_write 1 void
    PUSH_CONST 0
    RET
end function
'''


def require(value, message):
    if not value:
        raise AssertionError(message)


class Channel:
    def __init__(self, process):
        self.process, self.pending, self.sequence = process, bytearray(), 0
        self.handle, self.sock = None, None
        deadline = time.monotonic() + 10
        if os.name == 'nt':
            from ctypes import wintypes as w
            self.k = ctypes.WinDLL('kernel32', use_last_error=True)
            self.k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
            self.k.CreateFileW.restype = w.HANDLE
            self.k.ReadFile.argtypes = [w.HANDLE, w.LPVOID, w.DWORD, ctypes.POINTER(w.DWORD), w.LPVOID]
            self.k.WriteFile.argtypes = self.k.ReadFile.argtypes
            self.k.SetNamedPipeHandleState.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD), w.LPVOID, w.LPVOID]
            self.k.GetNamedPipeServerProcessId.argtypes = [w.HANDLE, ctypes.POINTER(w.ULONG)]
            self.k.CloseHandle.argtypes = [w.HANDLE]
            endpoint = r'\\.\pipe\ny_ai_' + str(process.pid)
            while time.monotonic() < deadline:
                handle = self.k.CreateFileW(endpoint, 0xc0000000, 0, None, 3, 0x00110000, None)
                if handle != ctypes.c_void_p(-1).value:
                    self.handle = handle
                    break
                require(process.poll() is None, 'interpreter exited before endpoint readiness')
                time.sleep(.01)
            require(self.handle is not None, 'pipe connection deadline')
            mode, server = w.DWORD(1), w.ULONG()
            require(self.k.SetNamedPipeHandleState(self.handle, ctypes.byref(mode), None, None), 'set nonblocking pipe')
            require(self.k.GetNamedPipeServerProcessId(self.handle, ctypes.byref(server)) and server.value == process.pid,
                    'connected to wrong process')
        else:
            endpoint = '/tmp/ny_ai_' + str(process.pid) + '.sock'
            while time.monotonic() < deadline:
                candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                candidate.settimeout(.1)
                try:
                    candidate.connect(endpoint)
                    self.sock = candidate
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    candidate.close()
                    require(process.poll() is None, 'interpreter exited before endpoint readiness')
                    time.sleep(.01)
            require(self.sock is not None, 'socket connection deadline')
            require(Path(endpoint).stat().st_mode & 0o777 == 0o600, 'socket is not owner only')
        greeting = self.read()
        require(greeting['event'] == 'connected' and greeting['pid'] == process.pid and greeting['ai_version'] == 1,
                'wrong handshake')

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None
        if self.handle is not None:
            self.k.CloseHandle(self.handle)
            self.handle = None

    def write(self, payload):
        if self.sock is not None:
            self.sock.sendall(payload)
            return
        from ctypes import wintypes as w
        deadline, offset = time.monotonic() + 5, 0
        while offset < len(payload) and time.monotonic() < deadline:
            count = w.DWORD()
            # A NOWAIT pipe can accept zero bytes if a request is larger than
            # its quota. Bound each write while retaining the frame deadline.
            chunk = payload[offset:offset + 4096]
            data = ctypes.create_string_buffer(chunk)
            ok = self.k.WriteFile(self.handle, data, len(chunk), ctypes.byref(count), None)
            if not ok:
                require(ctypes.get_last_error() in (232, 535), 'pipe write failed')
            offset += count.value
            if not count.value:
                time.sleep(.005)
        require(offset == len(payload), 'write deadline')

    def read(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if b'\n' in self.pending:
                line, _, remaining = self.pending.partition(b'\n')
                self.pending = bytearray(remaining)
                return json.loads(line.decode('utf-8'))
            require(len(self.pending) <= 65536, 'response too large')
            if self.sock is not None:
                try:
                    data = self.sock.recv(4096)
                    require(data, 'unexpected EOF')
                    self.pending.extend(data)
                except socket.timeout:
                    pass
            else:
                from ctypes import wintypes as w
                count, buffer = w.DWORD(), ctypes.create_string_buffer(4096)
                if self.k.ReadFile(self.handle, buffer, len(buffer), ctypes.byref(count), None):
                    self.pending.extend(buffer.raw[:count.value])
                else:
                    require(ctypes.get_last_error() == 232, 'pipe read failed')
                if not count.value:
                    time.sleep(.005)
        raise AssertionError('response deadline')

    def call(self, method, *, error=False, **fields):
        self.sequence += 1
        key = str(self.sequence)
        self.write((json.dumps({'id': key, 'method': method, **fields}) + '\n').encode())
        response = self.read()
        require(response['id'] == key, 'response correlation lost')
        require((response['error'] is not None) == error, repr(response))
        if error:
            require(response['result'] is None, 'error also supplied a result')
            return response['error']
        return response['result']


def run(interpreter, directory, rejected=False):
    source = directory / ('rejected.nil' if rejected else 'checkpoint.nil')
    source.write_text(PROGRAM.replace('PUSH_CONST 10', 'INTEROP_CALL std_sys arg_count 0 i64') if rejected else PROGRAM,
                      encoding='utf-8')
    environment = {key: value for key, value in os.environ.items() if not key.startswith(('NY_', 'NIL_'))}
    process = subprocess.Popen([str(interpreter), '--ai-wait', str(source)], cwd=directory,
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    channel = None
    try:
        channel = Channel(process)
        initial = channel.call('vm.status')
        require(initial['paused'] and not initial['running'], '--ai-wait did not pause')
        require('checkpoint.same-process.v1' in initial['capabilities'], 'missing checkpoint capability')
        for _ in range(3):
            require(channel.call('vm.status')['pc'] == initial['pc'], 'paused execution moved')
        channel.call('vm.restore', checkpoint='99', error=True)
        channel.call('vm.snapshot', unknown='x', error=True)
        # Error reporting itself must not abort the VM on large parser/dispatch diagnostics.
        key = 'x' * 62000
        channel.sequence += 1
        channel.write(json.dumps({'id': str(channel.sequence), 'method': 'vm.status', key: 1}).encode() + b'\n')
        invalid = channel.read()
        require(invalid['error'] and invalid['result'] is None, 'large invalid field accepted')
        require(channel.call('vm.status')['pc'] == initial['pc'], 'rejected request mutated VM')
        if rejected:
            error = channel.call('vm.snapshot', error=True)
            require('excludes' in error, 'effectful program did not fail admission explicitly')
            channel.call('vm.resume')
        else:
            for _ in range(30):
                state = channel.call('vm.inspect')
                if len(state['frames']) == 2 and state['frames'][-1]['locals'].get('value', {}).get('value') == '5':
                    break
                channel.call('vm.step')
            else:
                raise AssertionError('did not reach nested local state')
            baseline = channel.call('vm.inspect')
            captured = channel.call('vm.snapshot')
            require(captured['size'] > 0 and captured['pc'] == baseline['pc'], 'empty checkpoint')
            for _ in range(9):
                channel.call('vm.step')
            changed = channel.call('vm.inspect')
            require(changed != baseline and changed['globals']['G']['value'] == '11', 'fixture did not mutate')
            for _ in range(2):
                channel.call('vm.restore', checkpoint=captured['checkpoint'])
                require(channel.call('vm.inspect') == baseline, 'restore lost PC/stack/frame/local/global state')
                channel.call('vm.step')
            channel.call('vm.restore', checkpoint=captured['checkpoint'])
            replacement = channel.call('vm.snapshot')
            require(replacement['checkpoint'] != captured['checkpoint'], 'checkpoint identity reused')
            channel.call('vm.restore', checkpoint=captured['checkpoint'], error=True)
            require(channel.call('vm.inspect') == baseline, 'bad restore mutated VM')
            channel.call('vm.restore', checkpoint=replacement['checkpoint'])
            channel.call('vm.reload', error=True)  # no fake hot-reload acknowledgement
            channel.call('vm.resume')
        channel.close()
        stdout, stderr = process.communicate(timeout=10)
        require(process.returncode == 6 and not stdout, repr((process.returncode, stdout, stderr)))
        require(b'runtime error' not in stderr, repr(stderr))
        if os.name != 'nt':
            require(not Path('/tmp/ny_ai_' + str(process.pid) + '.sock').exists(), 'endpoint leaked')
    finally:
        if channel is not None:
            channel.close()
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=10)


def run_exit(interpreter, directory, wait, profile=False):
    """Explicit process exit must still join control and clean its endpoint."""
    source = directory / 'explicit-exit.nil'
    source.write_text(EXIT_PROGRAM, encoding='utf-8')
    environment = {key: value for key, value in os.environ.items() if not key.startswith(('NY_', 'NIL_'))}
    profile_path = directory / 'explicit-exit-profile.json'
    if profile:
        environment['NIL_MEMORY_PROFILE'] = str(profile_path)
    process = subprocess.Popen([str(interpreter), '--ai-wait' if wait else '--ai', str(source)],
                               cwd=directory, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    channel = None
    try:
        if wait:
            channel = Channel(process)
            require(channel.call('vm.status')['paused'], 'explicit-exit fixture did not pause')
            channel.call('vm.resume')  # Must receive the last reply even on immediate exit.
            channel.close()
        stdout, stderr = process.communicate(timeout=10)
        require(process.returncode == 71 and not stdout, repr((process.returncode, stdout, stderr)))
        require(b'[ny_ai] Listening on ' in stderr and b'runtime error' not in stderr, repr(stderr))
        if profile:
            result = json.loads(profile_path.read_text(encoding='utf-8'))
            require(result['schema'] == 'aperture.memory.v1' and result['route'] == 'vm' and
                    result['status'] == 'completed' and result['completed'] is True and not result['errors'],
                    'explicit AI exit did not finalize profiling: ' + repr(result))
        if os.name != 'nt':
            require(not Path('/tmp/ny_ai_' + str(process.pid) + '.sock').exists(), 'explicit exit leaked endpoint')
    finally:
        if channel is not None:
            channel.close()
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interpreter', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nil-ai-checkpoint-') as temporary:
        directory = Path(temporary)
        run(args.interpreter.resolve(strict=True), directory)
        run(args.interpreter.resolve(strict=True), directory, rejected=True)
        run_exit(args.interpreter.resolve(strict=True), directory, wait=True)
        run_exit(args.interpreter.resolve(strict=True), directory, wait=False)
        run_exit(args.interpreter.resolve(strict=True), directory, wait=False, profile=True)
    print('AI checkpoint contract: capture/mutate/reusable restore/continuation, rejection and explicit-exit cleanup passed')


if __name__ == '__main__':
    main()
