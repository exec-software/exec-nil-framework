#!/usr/bin/env python3
"""Copied-SDK debugger consumer validation; no compiler test internals required."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import select
import signal
import subprocess
import time

from sdk_integrity import verify

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('NIL_SDK') or ROOT.parents[1] / 'sdk').resolve()
FIXTURE = ROOT / 'probes/debugger/session.ny'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def clean_env():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('NY') and k not in
           {'LD_PRELOAD', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_INSERT_LIBRARIES',
            'LIBRARY_PATH', 'CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH'}}
    env['PATH'] = '/usr/local/bin:/usr/bin:/bin'
    env['LC_ALL'] = 'C'
    return env


def line(path, marker):
    matches = [i for i, s in enumerate(path.read_text().splitlines(), 1) if marker in s]
    if len(matches) != 1:
        raise ValueError(f'{path}: expected one source marker {marker!r}, found {matches}')
    return matches[0]


class Protocol:
    def __init__(self, command, mode, directory, timeout, allow_stderr=False):
        self.mode, self.directory, self.timeout = mode, directory, timeout
        self.allow_stderr = allow_stderr
        self.deadline = time.monotonic() + timeout
        directory.mkdir(parents=True, exist_ok=True)
        self.events, self.pending, self.buffer, self.seq = [], [], b'', 0
        self.transcript = (directory / 'transcript.jsonl').open('w')
        self.errfile = (directory / 'stderr.txt').open('wb')
        self.log('start', {'command': command, 'cwd': str(directory)})
        self.p = subprocess.Popen(command, cwd=directory, env=clean_env(), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.errfile, start_new_session=True)

    def log(self, direction, message):
        self.transcript.write(json.dumps({'direction': direction, 'message': message}) + '\n')
        self.transcript.flush()

    def write(self, message):
        self.log('send', message)
        raw = json.dumps(message, separators=(',', ':')).encode()
        if self.mode == 'dap':
            raw = f'Content-Length: {len(raw)}\r\n\r\n'.encode() + raw
        else:
            raw += b'\n'
        self.p.stdin.write(raw)
        self.p.stdin.flush()

    def read(self):
        deadline = min(time.monotonic() + self.timeout, self.deadline)
        while True:
            if self.mode == 'jsonl' and b'\n' in self.buffer:
                raw, self.buffer = self.buffer.split(b'\n', 1)
                break
            if self.mode == 'dap' and b'\r\n\r\n' in self.buffer:
                header, body = self.buffer.split(b'\r\n\r\n', 1)
                headers = dict(item.split(b':', 1) for item in header.split(b'\r\n'))
                size = int(headers[b'Content-Length'].strip())
                if size < 0 or size > 10_000_000:
                    raise ValueError(f'invalid frame size {size}')
                if len(body) >= size:
                    raw, self.buffer = body[:size], body[size:]
                    break
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.p.stdout], [], [], remaining)[0]:
                raise TimeoutError('debugger message timed out')
            chunk = os.read(self.p.stdout.fileno(), 65536)
            if not chunk:
                raise EOFError(f'debugger closed stdout; exit={self.p.poll()}, pending={self.buffer!r}')
            self.buffer += chunk
        message = json.loads(raw)
        self.log('receive', message)
        if message.get('event'):
            self.events.append(message)
        return message

    def request(self, command, arguments=None, expected=None):
        if self.mode == 'dap':
            self.seq += 1
            self.write({'seq': self.seq, 'type': 'request', 'command': command,
                        'arguments': arguments or {}})
            for _ in range(20000):
                message = self.read()
                if message.get('type') == 'response' and message.get('request_seq') == self.seq:
                    return message
                self.pending.append(message)
        else:
            self.write({'cmd': command, **(arguments or {})})
            return self.until(expected)
        raise RuntimeError('debugger response budget exceeded')

    def until(self, event):
        for i, message in enumerate(self.pending):
            if message.get('event') == event:
                return self.pending.pop(i)
        for _ in range(20000):
            message = self.read()
            if message.get('event') == event:
                return message
            self.pending.append(message)
        raise RuntimeError(f'debugger event budget exceeded: {event}')

    def stdout(self):
        chunks = []
        for message in self.events:
            if message.get('event') == 'output':
                body = message.get('body', message)
                if body.get('category') == 'stdout':
                    chunks.append(body.get('output', ''))
        return ''.join(chunks)

    def close(self):
        if self.p.poll() is None:
            try:
                if any(e.get('event') == 'detached' for e in self.events):
                    pass
                elif self.mode == 'dap':
                    self.request('disconnect')
                else:
                    self.request('quit', expected='quit')
                    self.until('detached')
                self.p.wait(timeout=2)
            except (OSError, EOFError, TimeoutError, subprocess.TimeoutExpired):
                try:
                    os.killpg(self.p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.p.wait()
        self.log('exit', {'code': self.p.returncode})
        self.transcript.close()
        self.errfile.close()
        if self.p.returncode != 0:
            raise RuntimeError(f'debugger process exited with {self.p.returncode}; see {self.directory}')
        if not self.allow_stderr and (self.directory / 'stderr.txt').stat().st_size:
            raise RuntimeError(f'debugger wrote unexpected stderr; see {self.directory}')


class Audit:
    def __init__(self, timeout):
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        self.work = ROOT / 'work/debugger' / stamp
        self.work.mkdir(parents=True)
        self.timeout, self.rows, self.commands = timeout, [], []
        self.completed = False
        self.source_before = {str(p.relative_to(ROOT)): sha(p) for p in
                              [Path(__file__), *sorted((ROOT / 'probes/debugger').glob('*.ny')),
                               *sorted((ROOT / 'src').rglob('*.ny'))]}
        self.report = {'schema': 1, 'started_utc': stamp, 'work': str(self.work),
                       'sdk_before': verify(SDK, ROOT / 'sdk-origin.json'),
                       'checks': self.rows, 'commands': self.commands,
                       'boundaries': ['Native DWARF/attach, interactive stdin, worker-thread stepping and hit counts/logpoints are documented unavailable.',
                                      'DAP restart is not advertised; JSONL run is the documented pristine restart operation.',
                                      'These are copied-SDK command-line protocol tests, not an editor UI test.']}

    def check(self, name, condition, evidence=None, issue=None):
        row = {'name': name, 'status': 'PASS' if condition else 'FAIL'}
        if evidence is not None:
            row['evidence'] = evidence
        if issue:
            row['issue'] = issue
        self.rows.append(row)
        print(f'{row["status"]}: {name}', flush=True)
        return condition

    def command(self, label, command, output=None):
        target = self.work / label
        target.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        p = subprocess.Popen([str(x) for x in command], cwd=target, env=clean_env(),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        timed_out = False
        try:
            stdout, stderr = p.communicate(timeout=self.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(p.pid, signal.SIGKILL)
            stdout, stderr = p.communicate()
        except BaseException:
            if p.poll() is None:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            p.communicate()
            raise
        (target / 'stdout.txt').write_bytes(stdout)
        (target / 'stderr.txt').write_bytes(stderr)
        self.commands.append({'label': label, 'command': [str(x) for x in command],
                              'cwd': str(target), 'exit_code': p.returncode, 'timeout': timed_out,
                              'seconds': round(time.monotonic() - start, 4),
                              'stdout': str(target / 'stdout.txt'), 'stderr': str(target / 'stderr.txt'),
                              'stdout_sha256': hashlib.sha256(stdout).hexdigest(),
                              'stderr_sha256': hashlib.sha256(stderr).hexdigest()})
        if not self.check(label, p.returncode == 0 and not timed_out,
                          {'exit_code': p.returncode, 'stderr': stderr.decode(errors='replace')}):
            raise RuntimeError(f'{label} failed')
        if output:
            Path(output).write_bytes(stdout)
        return stdout.decode()

    def compile(self, label, source, binary=False, debug=True, native=False):
        artifact = self.work / (label + ('.native' if native else '.nil'))
        command = [SDK / 'nilcompiler', '-q', '--sdk', SDK, '-I', ROOT / 'src',
                   '-I', source.parent, '--no-bundle']
        if debug:
            command.append('--debug-info')
        if binary:
            command.append('--emit-binary-nil')
        if native:
            command.append('--native')
        self.command('compile-' + label, command + ['-o', artifact, source])
        self.report.setdefault('artifacts', {})[str(artifact)] = sha(artifact)
        return artifact

    def guard(self, name, function, *args):
        try:
            function(*args)
        except Exception as exc:
            self.check(name + '/session-completed', False, {'error': f'{type(exc).__name__}: {exc}'})

    def jsonl(self, tag, artifact, source, baseline, real=False):
        p = Protocol([str(SDK / 'nildebugger'), '--json', 'run', str(artifact)] +
                     (['--', '8', '8'] if real else []), 'jsonl', self.work / tag, self.timeout)
        try:
            p.until('started')
            target = line(source, 'let mesh = build_mesh()') if real else line(source, 'DBG_CALL')
            bp = p.request('break', {'file': str(source), 'line': target,
                                     'condition': '' if real else 'index == 1'}, 'bp_set')
            stop = p.request('run', expected='stopped')
            self.check(tag + '/conditional-source-breakpoint', stop.get('reason') == 'breakpoint' and
                       stop.get('line') == target and stop['stack'][0]['file'] == str(source), stop)
            self.report.setdefault('observations', []).append({'id': 'NYDBG-001', 'session': tag,
                'topic': 'JSONL stopped file versus source stack file', 'stopped_file': stop.get('file'),
                'top_frame_file': stop['stack'][0]['file'], 'line': stop['line'],
                'note': 'Source-aware clients must currently take the source path from stack[0].file.'})
            if not real:
                locals_ = p.request('locals', expected='locals')['vars']
                self.check(tag + '/named-locals', all(locals_.get(k) == v for k, v in
                           {'index': 1, 'current': 2, 'total': 2, 'limit': 3}.items()) and
                           'transformed' not in locals_ and isinstance(locals_.get('buffer'), int), locals_)
                evaluation = p.request('eval', {'expr': 'current'}, 'eval_result')
                self.check(tag + '/evaluate', evaluation.get('value') == '2', evaluation)
                watch = p.request('watch', {'expr': 'current'}, 'watch_added')
                watches = p.request('watches', expected='watches')
                self.check(tag + '/watch', watches['items'][0].get('value') == '2', watches)
                p.request('unwatch', {'id': watch['id']}, 'watch_removed')
                step = p.request('step', expected='stopped')
                self.check(tag + '/step-in', step['line'] == line(source, 'DBG_SCALE') and
                           len(step['stack']) == 3 and step['locals']['vars'].get('value') == 2, step)
                outer = p.request('out', expected='stopped')
                self.check(tag + '/step-out', len(outer['stack']) == 2 and outer['line'] == target, outer)
                next_ = p.request('next', expected='stopped')
                self.check(tag + '/step-over', next_['line'] == line(source, 'DBG_AFTER_CALL') and
                           next_['locals']['vars'].get('transformed') == 4, next_)
            else:
                p.request('delete', {'id': bp['id']}, 'bp_removed')
            p.request('continue', expected='terminated')
            if not real:
                restarted = p.request('run', expected='stopped')
                self.check(tag + '/pristine-restart', restarted['locals']['vars'].get('total') == 2 and
                           restarted['locals']['vars'].get('current') == 2, restarted)
                p.request('continue', expected='terminated')
            p.request('quit', expected='quit')
            p.until('detached')
            self.check(tag + '/stdout-parity', p.stdout() == baseline * (1 if real else 2),
                       {'expected_sha256': hashlib.sha256((baseline * (1 if real else 2)).encode()).hexdigest(),
                        'actual_sha256': hashlib.sha256(p.stdout().encode()).hexdigest()})
        finally:
            p.close()

    def dap_request(self, p, command, arguments=None):
        if command == 'initialize':
            arguments = {**(arguments or {}), 'supportsVariableType': True}
        result = p.request(command, arguments)
        if not result.get('success'):
            raise RuntimeError(f'{command}: {result}')
        return result.get('body', {})

    def frames(self, p):
        return self.dap_request(p, 'stackTrace', {'threadId': 1})['stackFrames']

    def locals(self, p, frame):
        scopes = self.dap_request(p, 'scopes', {'frameId': frame['id']})['scopes']
        scope = next(s for s in scopes if s['name'] == 'Locals')
        values = self.dap_request(p, 'variables', {'variablesReference': scope['variablesReference']})['variables']
        return {item['name']: item for item in values}, scope['variablesReference']

    def dap(self, tag, artifact, source, baseline, real=False):
        p = Protocol([str(SDK / 'nildebugger'), '--dap'], 'dap', self.work / tag, self.timeout)
        try:
            capabilities = self.dap_request(p, 'initialize', {'adapterID': 'ny-validation',
                               'linesStartAt1': True, 'columnsStartAt1': True, 'pathFormat': 'path'})
            self.check(tag + '/capabilities', capabilities.get('supportsConditionalBreakpoints') is True,
                       capabilities)
            self.dap_request(p, 'launch', {'program': str(artifact), 'stopOnEntry': True,
                                         'args': ['8', '8'] if real else []})
            target = line(source, 'let mesh = build_mesh()') if real else line(source, 'DBG_CALL')
            bps = [{'line': target, 'condition': '' if real else 'index == 1'}, {'line': 99999}]
            if not real:
                bps += [{'line': line(source, 'DBG_SHADOW_INNER')}, {'line': line(source, 'DBG_SHADOW_OUTER')}]
            result = self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)}, 'breakpoints': bps})
            self.check(tag + '/breakpoint-validation', result['breakpoints'][0].get('verified') is True and
                       result['breakpoints'][1].get('verified') is False and
                       bool(result['breakpoints'][1].get('message')), result)
            missing = self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source.parent / 'absent.ny')},
                                                            'breakpoints': [{'line': target}]})
            self.check(tag + '/path-qualified-breakpoint', missing['breakpoints'][0].get('verified') is False,
                       missing)
            self.dap_request(p, 'configurationDone')
            stop = p.until('stopped')
            self.check(tag + '/entry', stop['body']['reason'] == 'entry', stop)
            self.dap_request(p, 'continue', {'threadId': 1})
            p.until('stopped')
            frames = self.frames(p)
            self.check(tag + '/source-stack', frames[0]['line'] == target and
                       frames[0]['source']['path'] == str(source) and (real or len(frames) == 2), frames)
            if not real:
                locals_, reference = self.locals(p, frames[0])
                self.check(tag + '/named-locals', all(locals_.get(k, {}).get('value') == str(v) for k, v in
                           {'index': 1, 'current': 2, 'total': 2, 'limit': 3}.items()) and
                           'transformed' not in locals_ and locals_.get('buffer', {}).get('value', '').isdigit(), locals_)
                evaluation = self.dap_request(p, 'evaluate', {'expression': 'current', 'frameId': frames[0]['id'],
                                                              'context': 'watch'})
                self.check(tag + '/evaluate', evaluation.get('result') == '2', evaluation)
                caller, _ = self.locals(p, frames[1])
                self.check(tag + '/caller-frame', caller.get('count', {}).get('value') == '3', caller)
                self.dap_request(p, 'stepIn', {'threadId': 1})
                p.until('stopped')
                invalidated = p.request('variables', {'variablesReference': reference})
                self.check(tag + '/stale-variables-rejected', invalidated.get('success') is False, invalidated)
                entered = self.frames(p)
                self.check(tag + '/step-in', len(entered) == 3 and entered[0]['line'] == line(source, 'DBG_SCALE'), entered)
                self.dap_request(p, 'stepOut', {'threadId': 1})
                p.until('stopped')
                returned = self.frames(p)
                self.check(tag + '/step-out', len(returned) == 2 and returned[0]['line'] == target, returned)
                self.dap_request(p, 'next', {'threadId': 1})
                p.until('stopped')
                stepped = self.frames(p)
                values, _ = self.locals(p, stepped[0])
                self.check(tag + '/step-over', stepped[0]['line'] == line(source, 'DBG_AFTER_CALL') and
                           values.get('transformed', {}).get('value') == '4', {'frame': stepped[0], 'locals': values})
                self.dap_request(p, 'continue', {'threadId': 1})
                p.until('stopped')
                inner = self.frames(p)
                inner_values, _ = self.locals(p, inner[0])
                self.check(tag + '/shadowed-inner-binding', inner[0]['line'] == line(source, 'DBG_SHADOW_INNER') and
                           inner_values.get('value', {}).get('value') == '99', inner_values)
                self.dap_request(p, 'continue', {'threadId': 1})
                p.until('stopped')
                outer = self.frames(p)
                outer_values, _ = self.locals(p, outer[0])
                self.check(tag + '/restored-outer-binding', outer[0]['line'] == line(source, 'DBG_SHADOW_OUTER') and
                           outer_values.get('value', {}).get('value') == '3' and 'observed' not in outer_values, outer_values)
            else:
                self.dap_request(p, 'stepIn', {'threadId': 1})
                p.until('stopped')
                entered = self.frames(p)
                shared = ROOT / 'src/scene/mesh.ny'
                expected_source = shared if shared.exists() else source
                self.check(tag + '/realapp-cross-module-step', entered[0]['source']['path'] == str(expected_source)
                           and entered[0]['name'].endswith('build_mesh'), entered)
                self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)}, 'breakpoints': []})
            self.dap_request(p, 'continue', {'threadId': 1})
            p.until('terminated')
            self.check(tag + '/exit-code', any(e.get('event') == 'exited' and e.get('body', {}).get('exitCode') == 0
                                              for e in p.events), p.events[-3:])
            self.check(tag + '/stdout-parity', p.stdout() == baseline,
                       {'expected_sha256': hashlib.sha256(baseline.encode()).hexdigest(),
                        'actual_sha256': hashlib.sha256(p.stdout().encode()).hexdigest()})
            restart = p.request('restart')
            self.check(tag + '/unadvertised-restart-boundary', not capabilities.get('supportsRestartRequest') and
                       restart.get('success') is False, restart)
        finally:
            p.close()

    def scene_dap(self, tag, artifact, baseline, checkpoint):
        source = ROOT / 'src/scene_player.ny'
        shared = ROOT / 'src/scene/mesh.ny'
        p = Protocol([str(SDK / 'nildebugger'), '--dap'], 'dap', self.work / tag, self.timeout)
        try:
            self.dap_request(p, 'initialize', {'adapterID': 'ny-validation', 'linesStartAt1': True,
                                             'columnsStartAt1': True, 'pathFormat': 'path'})
            self.dap_request(p, 'launch', {'program': str(artifact), 'args': ['8', '8', '3'], 'stopOnEntry': False})
            update = line(source, '        update_scene(mesh, base_x, base_y, base_z, frame)')
            render = line(source, 'let written = render_frame(')
            configured = self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)},
                'breakpoints': [{'line': update, 'condition': 'frame == 1'},
                                {'line': render, 'condition': 'frame == 1'}]})
            self.check(tag + '/frame-breakpoints', all(b.get('verified') for b in configured['breakpoints']), configured)
            self.dap_request(p, 'configurationDone')
            p.until('stopped')
            frames = self.frames(p)
            values, _ = self.locals(p, frames[0])
            self.check(tag + '/second-frame-state', frames[0]['line'] == update and
                       all(values.get(k, {}).get('value') == str(v) for k, v in
                           {'frame': 1, 'frames': 3, 'width': 8, 'height': 8}.items()) and
                       all(values.get(k, {}).get('value', '').isdigit() for k in ('rgba', 'depth', 'rgb', 'row_hashes')),
                       {'frame': frames[0], 'locals': values})
            members = self.dap_request(p, 'variables', {'variablesReference': values['mesh']['variablesReference']})['variables']
            self.check(tag + '/struct-members', {'x', 'y', 'z', 'indices', 'materials'}.issubset(
                       {v['name'] for v in members}), members)
            array_members = self.dap_request(p, 'variables', {'variablesReference': values['base_x']['variablesReference']})['variables']
            self.report.setdefault('observations', []).append({'id': 'NYDBG-002', 'session': tag,
                'topic': 'Array and pointer presentation', 'array_children': array_members,
                'pointer_variable': values['rgba'],
                'note': 'Presentation is characterized separately from execution correctness; no undocumented array UI shape is required.'})
            self.dap_request(p, 'stepIn', {'threadId': 1})
            p.until('stopped')
            entered = self.frames(p)
            current, _ = self.locals(p, entered[0])
            self.check(tag + '/step-into-update', entered[0]['name'].endswith('update_scene') and
                       entered[0]['source']['path'] == str(source) and current.get('frame', {}).get('value') == '1', entered)
            self.dap_request(p, 'continue', {'threadId': 1})
            p.until('stopped')
            rendering = self.frames(p)
            self.check(tag + '/same-frame-render-breakpoint', rendering[0]['line'] == render, rendering)
            self.dap_request(p, 'stepIn', {'threadId': 1})
            p.until('stopped')
            entered = self.frames(p)
            self.check(tag + '/shared-renderer-step', entered[0]['name'].endswith('render_frame') and
                       entered[0]['source']['path'] == str(shared) and entered[1]['source']['path'] == str(source), entered)
            self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)}, 'breakpoints': []})
            self.dap_request(p, 'continue', {'threadId': 1})
            p.until('terminated')
            self.check(tag + '/stdout-parity', p.stdout() == baseline,
                       {'expected_sha256': hashlib.sha256(baseline.encode()).hexdigest(),
                        'actual_sha256': hashlib.sha256(p.stdout().encode()).hexdigest()})
            actual = p.directory / 'scene-state.bin'
            self.check(tag + '/checkpoint-parity', actual.is_file() and actual.read_bytes() == checkpoint,
                       {'bytes': len(checkpoint), 'expected_sha256': hashlib.sha256(checkpoint).hexdigest(),
                        'actual_sha256': sha(actual) if actual.is_file() else None})
            self.check(tag + '/exit-code', any(e.get('event') == 'exited' and e.get('body', {}).get('exitCode') == 0
                                              for e in p.events))
        finally:
            p.close()

    def pause_dap(self, artifact):
        tag = 'dap-pause-terminate'
        p = Protocol([str(SDK / 'nildebugger'), '--dap'], 'dap', self.work / tag, self.timeout)
        try:
            self.dap_request(p, 'initialize', {'adapterID': 'ny-validation'})
            self.dap_request(p, 'launch', {'program': str(artifact), 'stopOnEntry': False})
            source = ROOT / 'probes/debugger/pause.ny'
            self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)},
                                                 'breakpoints': [{'line': line(source, 'DBG_PAUSE_LOOP')}]})
            self.dap_request(p, 'configurationDone')
            p.until('stopped')
            started = self.frames(p)
            self.check(tag + '/entered-loop', started[0]['line'] == line(source, 'DBG_PAUSE_LOOP'), started)
            self.dap_request(p, 'setBreakpoints', {'source': {'path': str(source)}, 'breakpoints': []})
            self.dap_request(p, 'continue', {'threadId': 1})
            self.dap_request(p, 'pause', {'threadId': 1})
            paused = p.until('stopped')
            self.check(tag + '/cooperative-pause', paused.get('body', {}).get('reason') == 'pause', paused)
            frames = self.frames(p)
            self.check(tag + '/paused-source-stack', frames[0]['source']['path'] == str(ROOT / 'probes/debugger/pause.ny'), frames)
            self.dap_request(p, 'terminate')
            terminated = p.until('terminated')
            self.check(tag + '/terminate', terminated.get('event') == 'terminated' and p.stdout() == '', terminated)
        finally:
            p.close()

    def reject_unmapped(self, tag, artifact):
        # An invalid artifact may also produce a loader diagnostic on stderr.
        p = Protocol([str(SDK / 'nildebugger'), '--dap'], 'dap', self.work / tag, self.timeout, allow_stderr=True)
        try:
            self.dap_request(p, 'initialize', {'adapterID': 'ny-validation'})
            result = p.request('launch', {'program': str(artifact), 'stopOnEntry': True})
            self.check(tag + '/launch-rejected', result.get('success') is False and bool(result.get('message')), result)
        finally:
            p.close()

    def run(self):
        self.check('sdk-integrity-before', self.report['sdk_before']['status'] == 'PASS')
        if self.report['sdk_before']['status'] != 'PASS':
            return
        fixture_baselines = []
        for binary in (False, True):
            fmt = 'binary' if binary else 'text'
            artifact = self.compile('fixture-' + fmt, FIXTURE, binary)
            baseline = self.command('fixture-interpreter-' + fmt, [SDK / 'nilinterpreter', artifact])
            fixture_baselines.append(baseline)
            self.check('fixture-output-' + fmt, baseline == 'debug-result=12\n3\n', {'stdout': baseline})
            self.guard('jsonl-' + fmt, self.jsonl, 'jsonl-' + fmt, artifact, FIXTURE, baseline)
            self.guard('dap-' + fmt, self.dap, 'dap-' + fmt, artifact, FIXTURE, baseline)
        native = self.compile('fixture-native', FIXTURE, native=True, debug=False)
        native_stdout = self.command('fixture-native-execution', [native])
        self.check('fixture-native-interpreter-parity', native_stdout == fixture_baselines[0] == fixture_baselines[1])
        self.guard('native-debug-boundary', self.reject_unmapped, 'native-debug-boundary', native)
        unmapped = self.compile('fixture-unmapped', FIXTURE, debug=False)
        self.guard('unmapped-debug-boundary', self.reject_unmapped, 'unmapped-debug-boundary', unmapped)
        source = ROOT / 'src/mesh_renderer.ny'
        for binary in (False, True):
            fmt = 'binary' if binary else 'text'
            artifact = self.compile('mesh-' + fmt, source, binary)
            baseline = self.command('mesh-interpreter-' + fmt, [SDK / 'nilinterpreter', artifact, '8', '8'])
            self.guard('mesh-jsonl-' + fmt, self.jsonl, 'mesh-jsonl-' + fmt, artifact, source, baseline, True)
            self.guard('mesh-dap-' + fmt, self.dap, 'mesh-dap-' + fmt, artifact, source, baseline, True)

        for binary in (False, True):
            fmt = 'binary' if binary else 'text'
            artifact = self.compile('scene-' + fmt, ROOT / 'src/scene_player.ny', binary)
            label = 'scene-interpreter-' + fmt
            baseline = self.command(label, [SDK / 'nilinterpreter', artifact, '8', '8', '3'])
            checkpoint = (self.work / label / 'scene-state.bin').read_bytes()
            self.guard('scene-dap-' + fmt, self.scene_dap, 'scene-dap-' + fmt, artifact, baseline, checkpoint)
        artifact = self.compile('pause', ROOT / 'probes/debugger/pause.ny')
        self.guard('dap-pause-terminate', self.pause_dap, artifact)
        self.completed = True

    def finish(self, report_path):
        self.report['sdk_after'] = verify(SDK, ROOT / 'sdk-origin.json')
        self.check('sdk-integrity-after', self.report['sdk_after']['status'] == 'PASS')
        self.check('sdk-origin-anchor-unchanged', all(self.report['sdk_before'].get(key) ==
                   self.report['sdk_after'].get(key) for key in ('manifest_sha256', 'origin_manifest_sha256')))
        self.report['completed'] = self.completed
        self.check('all-planned-sessions-attempted', self.completed)
        self.report['source_hashes'] = {str(p.relative_to(ROOT)): sha(p) for p in
                                       [Path(__file__), *sorted((ROOT / 'probes/debugger').glob('*.ny')),
                                        *sorted((ROOT / 'src').rglob('*.ny'))]}
        self.report['source_hashes_before'] = self.source_before
        self.check('source-integrity-during-run', self.source_before == self.report['source_hashes'])
        self.report['counts'] = {status: sum(r['status'] == status for r in self.rows) for status in ('PASS', 'FAIL')}
        self.report['status'] = 'PASS' if self.completed and self.rows and not self.report['counts']['FAIL'] else 'FAIL'
        self.report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        content = json.dumps(self.report, indent=2) + '\n'
        report_path.write_text(content)
        (self.work / 'report.json').write_text(content)
        print(json.dumps({'status': self.report['status'], 'counts': self.report['counts'],
                          'report': str(report_path), 'work': str(self.work)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--report', type=Path, default=ROOT / 'reports/debugger.json')
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be finite and positive')
    if args.report.resolve().is_relative_to(SDK.resolve()):
        parser.error('--report must not write inside the copied SDK')
    audit = Audit(args.timeout)
    try:
        audit.run()
    except (Exception, KeyboardInterrupt) as exc:
        audit.check('audit-completed', False, {'error': f'{type(exc).__name__}: {exc}'})
    finally:
        audit.finish(args.report.resolve())
    return 0 if audit.report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
