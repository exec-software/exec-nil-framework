#!/usr/bin/env python3
"""Bounded ordinary-I/O regression: six source-STL interpreter observations.

Both passing and failing evidence directories are retained. This does not test
native execution or binary STL packages, and does not qualify a release SDK.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unicodedata

REPOSITORY = Path(__file__).resolve().parents[2]
SCOPE = 'ORDINARY_IO_INTERPRETER_REGRESSION_NOT_NATIVE_OR_RELEASE_QUALIFICATION'
MODES = ('lexical', 'files', 'environment')
ENCODINGS = ('text', 'binary')
MAX_OUTPUT = 1024 * 1024
TEXT_BYTES = 'café日😀\r\ntail\n'.encode('utf-8')
HOST_ENV_VALUE = 'host-café日😀'
# Explicit, host-independent public path contract, not pathlib-derived results.
LEXICAL_VALUES = (
    ('separator', '/'), ('combine.slash', 'alpha/beta/file.ny'),
    ('combine.backslash', 'alpha/beta/file.ny'), ('normalize.slash', 'alpha/gamma'),
    ('normalize.backslash', 'alpha/gamma'), ('normalize.mixed', 'alpha/gamma'),
    ('basename.backslash', 'file.ny'), ('dirname.backslash', 'alpha/beta'),
    ('stem.backslash', 'file'), ('extension.backslash', '.ny'),
    ('relative.backslash', '../two/file.ny'), ('join.empty', ''),
    ('join.one', 'alpha/beta'), ('join.two', 'alpha/beta/file.ny'),
    ('dotfile.stem', '.config'), ('dotfile.extension', ''),
    ('multiext.stem', 'archive.tar'), ('multiext.extension', 'gz'),
    ('with_extension.mixed', 'alpha/beta/file.txt'),
    ('prefix.mixed', 'alpha/beta/pre-file.ny'), ('suffix.mixed', 'alpha/beta/file.ny.bak'),
    ('unicode.basename', '😀.txt'), ('unicode.dirname', 'café/日'),
    ('unicode.normalize', 'café/😀.txt'), ('unicode.nfd', 'e\u0301.txt'),
)
LEXICAL_STDOUT = (''.join('lexical.' + name + '=' + value + '\n' for name, value in LEXICAL_VALUES)
                  + 'lexical.ok=true\n').encode('utf-8')
ENVIRONMENT_KEYS = ('host_get', 'host_list', 'set', 'get', 'list', 'empty_set', 'empty_get',
                    'empty_list', 'cwd_set', 'cwd_get', 'cwd_restore', 'cwd_restored', 'unset', 'absent', 'ok')
ENVIRONMENT_STDOUT = ''.join('environment.' + key + '=true\n' for key in ENVIRONMENT_KEYS).encode()
FILES_STDOUT = (''.join(f'files.{index}.code=0\n' for index in range(6)) + 'files.ok=true\n').encode()


def require(value, message):
    if not value:
        raise AssertionError(message)


def sha(value):
    return hashlib.sha256(value).hexdigest()


def regular(path):
    mode = path.lstat()
    return stat.S_ISREG(mode.st_mode) and not (getattr(mode, 'st_file_attributes', 0) & 0x400)


def file_record(path, maximum=128 * 1024**2):
    require(regular(path), 'Expected a regular non-reparse input: ' + str(path))
    require(path.stat().st_size <= maximum, 'Input exceeds bound: ' + str(path))
    digest, size = hashlib.sha256(), 0
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(128 * 1024), b''):
            size += len(chunk)
            require(size <= maximum, 'Input grew beyond bound: ' + str(path))
            digest.update(chunk)
    return {'bytes': size, 'sha256': digest.hexdigest()}


def inventory(root, maximum_entries=4096, maximum_file=128 * 1024**2):
    """Never follow a fixture link/junction; record exact names independently."""
    result, pending, total_bytes = {}, [root], 0
    require(root.is_dir() and not root.is_symlink(), 'Expected a private directory')
    require(not getattr(root.lstat(), 'st_file_attributes', 0) & 0x400, 'Reparse fixture root')
    while pending:
        directory = pending.pop()
        for entry in sorted(directory.iterdir()):
            require(len(result) < maximum_entries, 'Namespace exceeds bounded entry count')
            name = entry.relative_to(root).as_posix()
            mode = entry.lstat()
            if stat.S_ISLNK(mode.st_mode) or getattr(mode, 'st_file_attributes', 0) & 0x400:
                result[name] = {'kind': 'link-or-reparse'}
            elif stat.S_ISDIR(mode.st_mode):
                result[name] = {'kind': 'directory'}
                pending.append(entry)
            elif stat.S_ISREG(mode.st_mode):
                total_bytes += mode.st_size
                require(total_bytes <= 64 * 1024**2, 'Namespace exceeds 64 MiB total byte budget')
                result[name] = {'kind': 'file', **file_record(entry, maximum_file)}
            else:
                result[name] = {'kind': 'special'}
    return dict(sorted(result.items()))


def expected_inventory(expected):
    return {name: {'kind': 'directory'} if value is None else
            {'kind': 'file', 'bytes': len(value), 'sha256': sha(value)}
            for name, value in sorted(expected.items())}


def check_namespace(root, expected):
    actual = inventory(root, maximum_entries=256, maximum_file=MAX_OUTPUT)
    wanted = expected_inventory(expected)
    differences = {'missing': sorted(set(wanted) - set(actual)),
                   'unexpected': sorted(set(actual) - set(wanted)),
                   'changed': [name for name in sorted(set(actual) & set(wanted))
                               if actual[name] != wanted[name]]}
    return {'status': 'PASS' if not any(differences.values()) else 'FAIL',
            'expected': wanted, 'actual': actual, **differences}


def prepare_files(root):
    root = root.resolve(strict=True)
    profiles = (('ascii-parent', 'plain'), ('parent-café', 'café'),
                ('parent-日本語', '日本語'), ('parent-🦊', '🦊'),
                ('normalization-nfc', 'café'), ('normalization-nfd', 'cafe\u0301'))
    expected, arguments, profiles_report = {}, [], []
    for index, (directory_name, basename) in enumerate(profiles):
        directory = root / directory_name
        directory.mkdir(exist_ok=True)
        expected[directory_name] = None
        seed, stem = directory / (basename + '.seed'), directory / basename
        payload = bytes((index, 10, 13, 128, 255))
        seed.write_bytes(payload)
        expected[seed.relative_to(root).as_posix()] = payload
        for suffix, content in (('.text', TEXT_BYTES), ('.bytes', payload),
                                ('.moved', payload), ('.stream', payload)):
            expected[(stem.with_name(stem.name + suffix)).relative_to(root).as_posix()] = content
        sentinel = stem.with_name(stem.name + '.sentinel')
        sentinel_bytes = b'independent untouched sentinel\0\xff' + bytes([index])
        sentinel.write_bytes(sentinel_bytes)
        expected[sentinel.relative_to(root).as_posix()] = sentinel_bytes
        absolute = bool(index % 2)
        arguments.extend(str(path if absolute else path.relative_to(root)) for path in (seed, stem))
        profiles_report.append({'index': index, 'seed': seed.relative_to(root).as_posix(),
                                'stem': stem.relative_to(root).as_posix(), 'absolute_argv': absolute,
                                'host_equivalent_spellings': []})
    # Seed plausible wrong spellings independently, but never overwrite the
    # intended file through a filesystem-equivalent lookup (for example APFS
    # NFC/NFD aliases). Separate parents keep the intended payloads distinct;
    # the inventory still requires the exact spelling of every stored entry.
    for index, (directory_name, basename) in enumerate(profiles):
        aliases = {unicodedata.normalize('NFC', basename), unicodedata.normalize('NFD', basename),
                   basename.encode('utf-8').decode('latin1')}
        aliases.discard(basename)
        for alias in sorted(aliases):
            seed = root / directory_name / (basename + '.seed')
            alias_seed = root / directory_name / (alias + '.seed')
            if alias_seed.exists() and alias_seed.samefile(seed):
                require(unicodedata.normalize('NFC', alias) == unicodedata.normalize('NFC', basename),
                        'Unexpected filesystem alias for independently named fixture')
                profiles_report[index]['host_equivalent_spellings'].append(alias)
                continue
            for suffix in ('.seed', '.text', '.bytes', '.moved', '.stream'):
                name = (Path(directory_name) / (alias + suffix)).as_posix()
                if name not in expected:
                    content = b'independent wrong-spelling sentinel\0' + bytes([index])
                    (root / name).write_bytes(content)
                    expected[name] = content
    return arguments, expected, profiles_report


def prepare_environment(root):
    root = root.resolve(strict=True)
    target = root / 'cwd-café-日本語-🦊-cafe\u0301'
    target.mkdir()
    sentinel = b'independent cwd sentinel\0\xff'
    (target / 'cwd-sentinel.bin').write_bytes(sentinel)
    expected = {target.name: None, target.name + '/cwd-sentinel.bin': sentinel}
    name = 'NIL_IO_PARITY_café_日本語_🦊'
    return [str(target), str(target.resolve()), name], expected, {name: HOST_ENV_VALUE}


def load_ownership(repository):
    path = repository / 'validation/ny-3d/scripts/debugger_process.py'
    # The exact current helper is recorded before/after with every other input;
    # do not pin a historical implementation into a repository regression.
    file_record(path)
    spec = importlib.util.spec_from_file_location('ordinary_io_owned_process', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_process(ownership, command, cwd, environment, logs, name, timeout):
    """Bounded raw pipes plus exact owned-tree cleanup on every path."""
    row = {'name': name, 'argv': [str(value) for value in command], 'cwd': str(cwd),
           'timeout_seconds': timeout, 'cleanup_verified': False, 'status': 'FAIL'}
    started = time.monotonic()
    owner, readers, streams = None, [], []
    failure = None
    try:
        for key in ('stdout', 'stderr'):
            row[key] = name + '.' + key
            streams.append((logs / row[key]).open('xb'))
        owner = ownership.OwnedProcess(row['argv'], cwd=cwd, env=environment,
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        budget = ownership.CaptureBudget(owner.terminate, maximum=MAX_OUTPUT)
        for pipe, sink in zip((owner.process.stdout, owner.process.stderr), streams):
            readers.append(ownership.PipeReader(pipe, budget, sink))
        deadline = started + timeout
        while owner.poll() is None or not all(reader.done.is_set() for reader in readers):
            budget.check()
            for reader in readers:
                if reader.error:
                    raise reader.error
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(row['argv'], timeout)
            time.sleep(.01)
        budget.check()
        owner.wait(max(.01, deadline - time.monotonic()))
        row['exit_code'] = owner.process.returncode
        if owner.job:
            owner.job.drain()
        owner.finish()
        row['cleanup_verified'] = True
        row['status'] = 'PASS' if row['exit_code'] == 0 else 'FAIL'
    except BaseException as error:
        failure = error
        row['error'] = str(error)
        row['timed_out'] = isinstance(error, subprocess.TimeoutExpired)
        if owner is None:
            # A constructor failure may itself be an unverifiable teardown.
            # Never invent cleanup proof when no retained owner was returned.
            row['cleanup_verified'] = False
        else:
            try:
                owner.kill()
                owner.finish()
                row['cleanup_verified'] = True
            except BaseException as cleanup:
                row['cleanup_error'] = str(cleanup)
    finally:
        for reader in readers:
            try:
                reader.close()
            except BaseException as error:
                row['reader_cleanup_error'] = str(error)
                row['cleanup_verified'] = False
                failure = failure or error
        if owner is not None:
            if owner.process.returncode is not None:
                row['exit_code'] = owner.process.returncode
            for pipe in (owner.process.stdout, owner.process.stderr):
                if pipe is not None and not pipe.closed:
                    pipe.close()
            if owner.job is not None:
                try:
                    owner.finish()
                except BaseException as error:
                    row['cleanup_error'] = str(error)
                    row['cleanup_verified'] = False
        for stream in streams:
            stream.close()
        row['elapsed_seconds'] = round(time.monotonic() - started, 6)
        for key in ('stdout', 'stderr'):
            path = logs / row.get(key, (name + '.' + key))
            if path.exists():
                row[key + '_record'] = file_record(path, MAX_OUTPUT)
        if failure is not None or not row['cleanup_verified']:
            row['status'] = 'FAIL'
    return row


def save_new(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as output:
        json.dump(value, output, indent=2, ensure_ascii=True)
        output.write('\n')


def allocate_output(repository, requested):
    """Create a new retained evidence directory, never reuse an existing one."""
    if requested is None:
        parent = Path(tempfile.gettempdir()).resolve()
        require(parent != repository and repository not in parent.parents,
                'Evidence temporary directory must be outside the repository')
        return Path(tempfile.mkdtemp(prefix='nil-ordinary-io-parity-', dir=parent))
    output = requested.absolute()
    require(output.resolve() != repository and repository not in output.resolve().parents,
            'Evidence output must be outside the repository')
    output.mkdir(exist_ok=False)
    return output


def run_contract(args):
    repository = args.repository.resolve(strict=True)
    source = (args.source or repository / 'tests/language/ordinary-io-parity.ny').resolve(strict=True)
    compiler, interpreter = (path.resolve(strict=True) for path in (args.compiler, args.interpreter))
    library = (args.nilstd or repository / 'nilstd/src').resolve(strict=True)
    output = allocate_output(repository, args.output)
    report = {'schema': 'nil.ordinary-io-parity.v1', 'scope': SCOPE, 'status': 'FAIL',
              'platform': sys.platform, 'evidence_directory': str(output), 'evidence_retained': True,
              'collection_complete': False,
              'observations_planned': [{'encoding': encoding, 'mode': mode}
                                       for encoding in ENCODINGS for mode in MODES],
              'commands': [], 'observations': [], 'shared_deadline_seconds': 300,
              'compile_timeout_seconds': 60, 'execution_timeout_seconds': 30,
              'source_stl_only': True, 'native_routes_evaluated': False}
    original_env = dict(os.environ)
    deadline = time.monotonic() + 300
    original_inputs = {'compiler': compiler, 'interpreter': interpreter, 'source': source,
                       'runner': Path(__file__).resolve(),
                       'ownership_helper': repository / 'validation/ny-3d/scripts/debugger_process.py'}
    try:
        report['inputs'] = {name: str(path) for name, path in original_inputs.items()}
        report['inputs']['source_stl'] = str(library)
        report['before'] = {name: file_record(path) for name, path in original_inputs.items()}
        report['before']['source_stl'] = inventory(library)
        require(all(item['kind'] in ('file', 'directory') for item in report['before']['source_stl'].values()),
                'Source STL includes linked or special entries')
        ownership = load_ownership(repository)
        private_library = output / 'nilstd'
        shutil.copytree(library, private_library)
        require(inventory(private_library) == report['before']['source_stl'], 'Private source STL copy differs')
        fixture = output / source.name
        shutil.copyfile(source, fixture)
        require(file_record(fixture) == report['before']['source'], 'Consumer source copy differs')
        runner_copy = output / 'retained-runner.py'
        shutil.copyfile(original_inputs['runner'], runner_copy)
        require(file_record(runner_copy) == report['before']['runner'], 'Runner source copy differs')
        logs, build, work = output / 'logs', output / 'build', output / 'work'
        for directory in (logs, build, work):
            directory.mkdir()
        environment = {key: value for key, value in original_env.items()
                       if not key.startswith(('NIL_', 'NY_'))}
        base = [compiler, '--quiet', '--no-stdlib', '--sdk-path', private_library, '--no-bundle']
        safe = True
        for encoding in ENCODINGS:
            nil = build / (encoding + '.nil')
            flags = [] if encoding == 'text' else ['--emit-binary-nil']
            budget = min(60, deadline - time.monotonic())
            require(safe and budget > 0, 'Unverified cleanup or shared deadline prevents another launch')
            compiled = run_process(ownership, [*base, *flags, '-o', nil, fixture], build,
                                   environment, logs, 'compile-' + encoding, budget)
            report['commands'].append(compiled)
            safe = compiled['cleanup_verified']
            compiled_ok = compiled['status'] == 'PASS'
            if compiled_ok:
                compiled_ok = nil.is_file() and (nil.read_bytes().startswith(b'NIL\0') == (encoding == 'binary'))
                compiled['nil_encoding_verified'] = compiled_ok
                compiled['nil_record'] = file_record(nil) if nil.is_file() else None
                compiled_ok = compiled_ok and (logs / compiled['stdout']).read_bytes() == b'' and (logs / compiled['stderr']).read_bytes() == b''
                compiled['status'] = 'PASS' if compiled_ok else 'FAIL'
            for mode in MODES:
                observation = {'encoding': encoding, 'mode': mode, 'status': 'NOT_RUN_COMPILE_FAILED'}
                report['observations'].append(observation)
                if not compiled_ok:
                    continue
                require(safe and time.monotonic() < deadline, 'Unsafe or exhausted regression continuation')
                cwd = work / (encoding + '-' + mode)
                cwd.mkdir()
                child_env = dict(environment)
                expected, arguments = {}, []
                if mode == 'files':
                    arguments, expected, profiles = prepare_files(cwd)
                    observation['profiles'] = profiles
                    expected_stdout = FILES_STDOUT
                elif mode == 'environment':
                    arguments, expected, injected = prepare_environment(cwd)
                    child_env.update(injected)
                    observation['injected_environment'] = injected
                    expected_stdout = ENVIRONMENT_STDOUT
                else:
                    expected_stdout = LEXICAL_STDOUT
                observation['namespace_before'] = inventory(cwd, maximum_entries=256, maximum_file=MAX_OUTPUT)
                row = run_process(ownership, [interpreter, '--no-ai', nil, '--', mode, *arguments],
                                  cwd, child_env, logs, encoding + '-' + mode,
                                  min(30, deadline - time.monotonic()))
                report['commands'].append(row)
                observation['command_name'] = row['name']
                safe = row['cleanup_verified']
                try:
                    observation['namespace'] = check_namespace(cwd, expected)
                except (OSError, AssertionError) as error:
                    observation['namespace'] = {'status': 'FAIL', 'error': str(error)}
                stdout, stderr = ((logs / row[key]).read_bytes() for key in ('stdout', 'stderr'))
                observation['stdout_expected'] = {'bytes': len(expected_stdout), 'sha256': sha(expected_stdout),
                                                   'text': expected_stdout.decode('utf-8')}
                observation['stdout_matches'] = stdout == expected_stdout
                observation['stderr_empty'] = stderr == b''
                observation['status'] = ('PASS' if row['status'] == 'PASS' and observation['stdout_matches']
                                         and observation['stderr_empty'] and observation['namespace']['status'] == 'PASS'
                                         else 'FAIL')
                require(safe, 'Owned cleanup unverified; no later observation is authorized')
        report['private_source_stl_after'] = inventory(private_library)
        report['private_source_after'] = file_record(fixture)
        report['retained_runner_after'] = file_record(runner_copy)
        require(report['private_source_stl_after'] == report['before']['source_stl'] and
                report['private_source_after'] == report['before']['source'] and
                report['retained_runner_after'] == report['before']['runner'], 'Private source/STL/runner mutated')
        report['collection_complete'] = len(report['observations']) == 6 and all(
            row['status'] != 'NOT_RUN_COMPILE_FAILED' for row in report['observations'])
        report['status'] = 'PASS' if report['collection_complete'] and all(
            row['status'] == 'PASS' for row in report['observations']) else 'FAIL'
    except BaseException as error:
        report['error'] = str(error)
    finally:
        try:
            report['after'] = {name: file_record(path) for name, path in original_inputs.items()}
            report['after']['source_stl'] = inventory(library)
            report['original_inputs_unchanged'] = report.get('before') == report['after']
            report['parent_environment_unchanged'] = original_env == dict(os.environ)
            if not report['original_inputs_unchanged'] or not report['parent_environment_unchanged']:
                report['status'] = 'FAIL'
        except BaseException as error:
            report['input_verification_error'] = str(error)
            report['status'] = 'FAIL'
        report['cleanup_verified'] = bool(report['commands']) and all(
            row['cleanup_verified'] for row in report['commands'])
        save_new(output / 'report.json', report)
    return report


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=REPOSITORY)
    for name in ('compiler', 'interpreter'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--output', type=Path,
                        help='New retained evidence directory (default: unique temporary directory)')
    parser.add_argument('--source', type=Path,
                        help='Consumer fixture (default: tests/language/ordinary-io-parity.ny in the selected repository)')
    parser.add_argument('--nilstd', type=Path,
                        help='Source STL (default: nilstd/src in the selected repository)')
    return parser


def main():
    args = argument_parser().parse_args()
    report = run_contract(args)
    print(json.dumps({'status': report['status'], 'scope': SCOPE, 'observations': len(report['observations']),
                      'cleanup_verified': report['cleanup_verified'], 'evidence': report['evidence_directory']}))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())

