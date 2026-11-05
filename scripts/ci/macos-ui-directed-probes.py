#!/usr/bin/env python3
"""Bounded directed input to a retained own child; no GTK/SDK qualification."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import secrets
import stat
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOTAL_SECONDS = 180
RESERVE_SECONDS = 20
MAX_OUTPUT = 256 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def paths(output, work, root=ROOT):
    require(not output.is_symlink() and not work.is_symlink(), 'Output/work may not be symlinks')
    output, work, root = output.resolve(), work.resolve(), root.resolve()
    require(not output.exists() and not work.exists(), 'Output/work must be fresh')
    require(output != work and output not in work.parents and work not in output.parents, 'Output/work overlap')
    require(work != root and root not in work.parents and work not in root.parents, 'Work must be outside checkout')
    require(output != root and output not in root.parents and
            (root not in output.parents or root/'build' in output.parents), 'Checkout evidence must be under build')
    return output, work


def record(path, deadline):
    original = Path(path); path = original.resolve(strict=True)
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
    digest = hashlib.sha256(); total = 0
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= 512 * 1024**2, 'Unbounded/nonregular input')
        while block := stream.read(1024**2):
            require(time.monotonic() < deadline, 'Seal deadline expired')
            total += len(block); require(total <= 512 * 1024**2, 'Input grew beyond bound'); digest.update(block)
        after = os.fstat(stream.fileno())
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_mode)
    require(identity(before) == identity(after) == identity(path.stat()) and total == after.st_size and
            original.resolve(strict=True) == path, 'Input changed while sealing')
    return dict(path=str(original), canonical_path=str(path), bytes=total, sha256=digest.hexdigest(),
                device=after.st_dev, inode=after.st_ino, mode=stat.S_IMODE(after.st_mode))


def decode(raw, phase):
    require(len(raw) <= 8192, 'Native frame exceeds bound')
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'Duplicate JSON key'); result[key] = value
        return result
    value = json.loads(raw, object_pairs_hook=pairs)
    require(type(value) is dict and type(value.get('schema')) is int and value['schema'] == 1 and
            value.get('qualification') is False and value.get('phase') == phase, 'Native frame scope/phase mismatch')
    return value


def ready_frame(value, pid):
    require(type(value.get('pid')) is int and value['pid'] == pid and type(value.get('window_number')) is int and
            0 < value['window_number'] < 2**31 and all(value.get(key) is True for key in
            ('active', 'key_window', 'field_editor', 'observer_subclass', 'main_thread')), 'Target readiness missing')


def typed_frame(value):
    require(value.get('text') == 'ny42' and type(value.get('callbacks')) is int and value['callbacks'] == 4 and
            type(value.get('marker_dispatches')) is int and value['marker_dispatches'] == 8 and
            value.get('during_marked_dispatch') is True, 'Real marked text callbacks missing')


def final_frame(value):
    require(all(value.get(key) is True for key in ('ready', 'typed', 'native_close', 'cleanup_verified', 'success')) and
            all(value.get(key) is False for key in ('unexpected_input', 'expired', 'exception')) and
            all(type(value.get(key)) is int and value[key] == count for key, count in
                (('marker_dispatches', 8), ('text_callbacks', 4), ('window_should_close', 1), ('window_did_close', 1))),
            'Actual app dispatch/text/native close proof incomplete')


def focus_proof(value):
    require(type(value) is dict, 'Missing bounded focus observations')
    if not value:
        return False
    errors = ('legacy_window_error', 'app_window_error', 'field_error', 'field_role_error', 'field_window_error')
    flags = ('legacy_window_boolean', 'legacy_window_value', 'app_window_element', 'app_window_pid_matches',
             'app_window_matches', 'field_element', 'field_pid_matches', 'field_text_role',
             'field_window_element', 'field_window_pid_matches', 'field_window_matches')
    require(set(value) == set(errors + flags + ('field_role_category',)) and all(type(value[k]) is int and -26000 <= value[k] <= 0 for k in errors)
            and all(type(value[k]) is bool for k in flags), 'Malformed focus observation')
    require(value['field_role_category'] in ('unavailable', 'text_field', 'text_area', 'other') and
            value['field_text_role'] == (value['field_role_category'] == 'text_field'), 'Inconsistent bounded field role')
    require(not value['legacy_window_value'] or (value['legacy_window_boolean'] and value['legacy_window_error'] == 0),
            'Inconsistent legacy window observation')
    # The old window AXFocused boolean remains diagnostic only. Admission uses
    # the application's focused window and focused field's own window identity.
    return all(value[k] == 0 for k in errors[1:]) and all(value[k] for k in flags[2:])


def driver_frame(value, phase, code):
    require(code in (0, 2), 'Unexpected driver exit')
    common = ('identity_verified', 'post_event_access', 'ax_trusted', 'own_window_selected', 'focus_verified')
    for key in (*common, 'marker_readback', 'close_action', 'success'):
        require(type(value.get(key)) is bool, 'Driver observation missing: ' + key)
    require(type(value.get('events_posted')) is int and 0 <= value['events_posted'] <= 8, 'Unbounded event count')
    require(value['focus_verified'] == focus_proof(value.get('focus_observation')), 'Focus proof/summary mismatch')
    success = all(value[key] for key in common) and (
        value['marker_readback'] and value['events_posted'] == 8 and not value['close_action'] if phase == 'keys'
        else value['close_action'] and value['events_posted'] == 0 and not value['marker_readback'])
    require(value['success'] == success and (code == 0) == success, 'Driver outcome/exit mismatch')
    return success


def driver_arguments(binary, phase, identity, parent, seal, nonce, marker):
    require(phase in ('keys', 'close') and set(identity) == {'pid', 'created'} and
            all(type(identity[k]) is int for k in identity) and 0 < identity['pid'] <= 2**31 - 1 and
            0 < identity['created'] < 2**96 and identity['created'] >> 32 > 0, 'Invalid process identity')
    require(type(parent) is int and 0 < parent <= 2**31 - 1 and re.fullmatch('[0-9a-f]{32}', nonce) and
            type(marker) is int and 0 < marker < 2**63, 'Invalid private driver identity')
    require(str(binary) == seal['canonical_path'] and Path(binary).is_absolute() and
            all(type(seal.get(k)) is int and seal[k] >= 0 for k in ('device', 'inode')), 'Invalid executable seal')
    return list(map(str, [binary, phase, identity['pid'], identity['created'] >> 32, identity['created'] & 0xffffffff,
                         parent, seal['device'], seal['inode'], binary, nonce, marker]))


class Frames:
    def __init__(self, reader, stream):
        self.reader, self.stream, self.buffer = reader, stream, b''
        self.eof = False

    def next(self, phase, deadline):
        while b'\n' not in self.buffer:
            require(time.monotonic() < deadline, 'Target protocol deadline')
            chunk = self.reader.read(max(.001, deadline - time.monotonic()))
            self.eof = not chunk
            require(chunk, 'Target exited before ' + phase)
            self.stream.write(chunk); self.stream.flush(); self.buffer += chunk
            require(len(self.buffer) <= 16384, 'Target protocol buffer exceeded bound')
        raw, self.buffer = self.buffer.split(b'\n', 1)
        return decode(raw, phase)

    def drain(self, deadline, reject_new=False):
        if self.eof:
            return
        saw_extra = False
        while True:
            require(time.monotonic() < deadline, 'Target drain deadline')
            chunk = self.reader.read(max(.001, deadline - time.monotonic()))
            if not chunk:
                self.eof = True
                require(not (reject_new and saw_extra), 'Unexpected trailing target stdout')
                return
            self.stream.write(chunk); self.stream.flush()
            saw_extra = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() == args.architecture, 'Matching native macOS host required')
    require(re.fullmatch('[0-9a-f]{40}', args.revision), 'Exact revision required')
    output, work = paths(args.output, args.work_dir)
    output.mkdir(parents=True, mode=0o700); work.mkdir(parents=True, mode=0o700)
    logs = output/'logs'; logs.mkdir(); (work/'tmp').mkdir()
    started = time.monotonic(); deadline = started + TOTAL_SECONDS
    variables = {key: os.environ[key] for key in ('HOME', 'USER', 'LOGNAME', 'DEVELOPER_DIR') if key in os.environ}
    variables.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', LC_ALL='C', TMPDIR=str(work/'tmp'),
                     PYTHONDONTWRITEBYTECODE='1', GIT_OPTIONAL_LOCKS='0')
    sources = [Path(__file__).resolve(), Path(__file__).with_suffix('.mm'),
               ROOT/'tests/tools/ordinary_io_parity_contract.py',
               ROOT/'validation/ny-3d/scripts/debugger_process.py', ROOT/'tests/tools/process_contract_support.py']
    report = dict(schema=1, status='INCOMPLETE', qualification=False, gtk_provider_built=False,
                  scope='Host prerequisite: directed keyboard and AX close to one retained own child only',
                  revision=args.revision, architecture=args.architecture, platform=platform.platform(),
                  total_seconds_limit=180, target_seconds_limit=30, driver_seconds_limit=10,
                  output_limit_bytes=4 * 1024**2, phases=[], problems=[],
                  image={key: os.environ.get(key) for key in ('ImageOS', 'ImageVersion', 'GITHUB_RUN_ID', 'GITHUB_SHA')})
    seals = {}; raw_bytes = 0; target = None; readers = []; streams = []; frames = None
    target_cleaned = False; target_started = None; target_deadline = None
    def seal(path):
        key = str(Path(path).absolute())
        if key not in seals: seals[key] = record(Path(key), deadline - RESERVE_SECONDS)
        return seals[key]
    def run(name, argv, seconds=10):
        nonlocal raw_bytes
        remaining = deadline - RESERVE_SECONDS - time.monotonic()
        if target_deadline is not None: remaining = min(remaining, target_deadline - time.monotonic())
        require(remaining > 0, 'Whole/target deadline exhausted')
        row = support.run_process(ownership, list(map(str, argv)), work, variables, logs, name, min(seconds, remaining))
        report['phases'].append(row)
        raw = (logs/row['stdout']).read_bytes(); errors = (logs/row['stderr']).read_bytes()
        raw_bytes += len(raw) + len(errors)
        require(raw_bytes <= report['output_limit_bytes'], 'Raw output bound exceeded')
        require(row.get('cleanup_verified') is True and not row.get('error') and not row.get('timed_out'),
                name + ': process/cleanup failure; raw logs retained')
        return row['exit_code'], raw
    def checked(name, argv, seconds=10):
        code, raw = run(name, argv, seconds); require(code == 0, name + ': nonzero exit; raw logs retained')
        return raw.decode('utf-8').strip()
    try:
        for path in sources: seal(path)
        support = load(sources[2], 'directed_io_support'); support.MAX_OUTPUT = MAX_OUTPUT
        ownership = support.load_ownership(ROOT)
        identity_support = load(sources[4], 'directed_identity_support')
        require(checked('source-revision', ['/usr/bin/git', '-C', ROOT, 'rev-parse', 'HEAD']) == args.revision,
                'Checkout revision mismatch')
        sdk = Path(checked('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']))
        compiler = Path(checked('compiler', ['/usr/bin/xcrun', '--find', 'clang++']))
        require(sdk.is_absolute() and compiler.is_absolute(), 'Nonabsolute toolchain selection')
        for path in (Path(sys.executable), Path('/usr/bin/xcrun'), compiler, sdk/'SDKSettings.json'): seal(path)
        report['toolchain'] = dict(sdk=str(sdk), compiler=str(compiler),
                                  version=checked('compiler-version', [compiler, '--version']))
        binary = work/'directed-probe'
        checked('compile', [compiler, '-std=c++17', '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-isysroot', sdk,
            '-mmacosx-version-min=15.0', sources[1], '-framework', 'AppKit', '-framework', 'ApplicationServices',
            '-o', binary], 30)
        binary_seal = seal(binary); binary = Path(binary_seal['canonical_path'])
        nonce = secrets.token_hex(16); marker = secrets.randbits(62) + 1
        command = [str(binary), 'target', nonce, str(marker)]
        target_started = time.monotonic(); target_deadline = min(target_started + 30, deadline - RESERVE_SECONDS)
        target = ownership.OwnedProcess(command, cwd=work, env=variables, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        report['target'] = dict(argv=command, pid=target.process.pid, cleanup_verified=False,
                                stdout='target.stdout', stderr='target.stderr')
        budget = ownership.CaptureBudget(target.terminate, maximum=64 * 1024)
        stdout = (logs/'target.stdout').open('wb'); stderr = (logs/'target.stderr').open('wb'); streams = [stdout, stderr]
        reader = ownership.PipeReader(target.process.stdout, budget); readers.append(reader)
        readers.append(ownership.PipeReader(target.process.stderr, budget, sink=stderr))
        frames = Frames(reader, stdout)
        identity = identity_support.process_identity(target.process.pid)
        report['target']['identity'] = identity
        observed = frames.next('ready', min(target_deadline, time.monotonic() + 7))
        report['ready'] = observed; ready_frame(observed, identity['pid'])
        for phase in ('keys', 'close'):
            require(identity_support.process_alive(identity), 'Target is dead or its lifetime changed')
            require(record(binary, deadline - RESERVE_SECONDS) == binary_seal, 'Private executable changed before driver')
            argv = driver_arguments(binary, phase, identity, os.getpid(), binary_seal, nonce, marker)
            # Never poll/reap the target while a PID-directed driver is active.
            # Even an unexpectedly dead target remains our unreaped child, pinning its PID.
            code, raw = run('driver-' + phase, argv, 10)
            value = decode(raw, phase); report[phase] = value
            require(driver_frame(value, phase, code), phase + ': directed capability/delivery prerequisite unavailable')
            if phase == 'keys':
                observed = frames.next('typed', min(target_deadline, time.monotonic() + 5))
                report['typed'] = observed; typed_frame(observed)
        observed = frames.next('final', target_deadline); report['final'] = observed; final_frame(observed)
        code = target.wait(max(.01, target_deadline - time.monotonic())); target.finish()
        report['target']['exit_code'] = code; require(code == 0, 'Target native close failed')
        frames.drain(deadline - 10, reject_new=True); require(not frames.buffer, 'Unexpected trailing target frames')
        budget.check(); report['observations_completed'] = True
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        if target is not None:
            try:
                # Drivers have finished (including their own cleanup) before any target wait/reap.
                if target.process.returncode is None: target.kill()
                target.finish()
                if frames is not None and not report.get('observations_completed'):
                    frames.drain(deadline - 10)
                for reader in readers: reader.close()
                target_cleaned = True
            except Exception as error:
                report['problems'].append('Target cleanup: ' + type(error).__name__ + ': ' + str(error))
            for stream in streams: stream.close()
            report['target'].update(cleanup_verified=target_cleaned, exit_code=target.process.returncode,
                                    elapsed_seconds=time.monotonic() - target_started)
            try:
                for name in ('target.stdout', 'target.stderr'):
                    path = logs/name
                    if path.exists():
                        row = record(path, deadline); raw_bytes += row['bytes']; report['target'][name + '_record'] = row
            except Exception as error:
                report['problems'].append('Target log seal: ' + type(error).__name__ + ': ' + str(error))
        report['inputs_before'] = seals
        try:
            report['inputs_after'] = {key: record(Path(key), deadline) for key in seals}
            report['inputs_unchanged'] = report['inputs_after'] == seals
        except Exception as error:
            report['inputs_unchanged'] = False
            report['problems'].append('Final seals: ' + type(error).__name__ + ': ' + str(error))
        report['cleanup_verified'] = all(row.get('cleanup_verified') is True for row in report['phases']) and (
            target is None or target_cleaned)
        report['elapsed_seconds'] = time.monotonic() - started; report['raw_output_bytes'] = raw_bytes
        report['directed_child_feasible'] = bool(report.get('observations_completed') and report['inputs_unchanged'] and
            report['cleanup_verified'] and not report['problems'] and report['elapsed_seconds'] < TOTAL_SECONDS and
            raw_bytes <= report['output_limit_bytes'])
        report['status'] = 'PASS' if report['directed_child_feasible'] else 'FAIL'
        (output/'report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
