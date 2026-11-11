#!/usr/bin/env python3
"""Compare real file metadata with the public VM/JIT/AOT timestamp operation."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time

import ordinary_io_parity_contract as support

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from native_output_staging import native_output_path

ERROR = 'file.modified_at: the file has no readable timestamp'


def literal(value: str) -> str:
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def source_for(paths: list[Path], nul_case: bool = False) -> str:
    calls = '\n'.join('    emit(' + literal(path.name) + ')' for path in paths)
    if nul_case:
        calls = '    emit(' + literal(paths[0].name)[:-1] + '\\0ignored")'
    return '''import nil.std.core
import nil.std.io.file
fn emit(path: text)
    match nil::std::io::modified_at(path)
        Result.Ok(value) -> println(value)
        Result.Error(message) -> println(message)
    end match
end fn
fn main() -> i32
''' + ('' if nul_case else '    for index in range(100)\n        emit(' + literal(paths[0].name) + ')\n    end for\n') + calls + '''
    return 0
end fn
'''


def signature(paths: list[Path]) -> dict:
    result = {}
    for path in paths:
        if not path.exists():
            result[path.name] = {'missing': True}
            continue
        info = path.stat()
        result[path.name] = {'modified_ns': info.st_mtime_ns,
                             'milliseconds': info.st_mtime_ns // 1_000_000,
                             'size': info.st_size,
                             'directory': path.is_dir()}
        if path.is_file():
            result[path.name]['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def expected(paths: list[Path]) -> bytes:
    values = [str(path.stat().st_mtime_ns // 1_000_000) if path.exists() else ERROR
              for path in paths]
    return ('\n'.join([values[0]] * 100 + values) + '\n').encode('utf-8')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--nilstd', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument('--output', type=Path,
                             help='Fresh exact evidence directory')
    destination.add_argument('--output-parent', type=Path,
                             help='Keep each run in a fresh child of this directory')
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    nilstd = args.nilstd.resolve(strict=True)
    if args.output_parent:
        parent = args.output_parent.resolve()
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='nil-file-time-contract-', dir=parent))
    elif args.output:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
    else:
        output = Path(tempfile.mkdtemp(prefix='nil-file-time-contract-'))
    work = output / 'work'
    work.mkdir()
    logs = output / 'logs'
    logs.mkdir()
    report = {'schema': 'nil.file-modified-at.v1', 'status': 'FAIL',
              'llvm_enabled': args.llvm == 'ON', 'commands': [], 'routes': [],
              'native_routes': 'enabled' if args.llvm == 'ON' else 'disabled by build configuration',
              'native_text_scope': 'NUL-free; embedded NUL checked on interpreter routes',
              'epoch_rule': 'floor native last-write timestamp to Unix milliseconds',
              'whole_seconds': 160}
    deadline = time.monotonic() + 160
    ownership = support.load_ownership(ROOT)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NIL_', 'NY_'))}

    def run(name, command, wanted=b'', limit=30):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError('whole timestamp contract deadline exhausted')
        row = support.run_process(ownership, [str(value) for value in command], work,
                                  environment, logs, name, min(limit, remaining))
        report['commands'].append(row)
        actual = (logs / row['stdout']).read_bytes()
        error = (logs / row['stderr']).read_bytes()
        if row['status'] != 'PASS' or not row['cleanup_verified'] or actual != wanted or error:
            raise AssertionError(name + ': exact timestamp/exit/stderr/cleanup contract failed')

    try:
        paths = [work / 'fraction-café-日本語-🦊.txt', work / 'epoch.txt',
                 work / 'boundary.txt', work / 'before-epoch.txt',
                 work / 'directory-café', work / 'absent.txt']
        requested = [1600000000123456700, 0, 1600000000999999900, -1000100]
        for path, nanoseconds in zip(paths[:4], requested):
            path.write_bytes(b'unchanged metadata fixture\n')
            os.utime(path, ns=(nanoseconds, nanoseconds))
        paths[4].mkdir()
        os.utime(paths[4], ns=(requested[0], requested[0]))
        report['requested_file_times_ns'] = dict(zip((p.name for p in paths[:4]), requested))
        report['before'] = signature(paths)
        source = work / 'metadata.ny'
        source.write_text(source_for(paths), encoding='utf-8', newline='\n')
        base = [compiler, '--quiet', '--no-stdlib', '--sdk-path', nilstd]
        commands = []
        for encoding in ('text', 'binary'):
            nil = work / (encoding + '.nil')
            flags = [] if encoding == 'text' else ['--emit-binary-nil']
            run('compile-' + encoding, [*base, '--no-bundle', *flags, '-o', nil, source])
            if nil.read_bytes().startswith(b'NIL\0') != (encoding == 'binary'):
                raise AssertionError('compiled NIL encoding differs')
            commands.append((encoding + '-interpreter', [interpreter, '--no-ai', nil]))
        if args.llvm == 'ON':
            native = work / 'native'
            run('compile-native', [*base, '--native', '-o', native, source], limit=60)
            commands.append(('llvm-aot', [native_output_path(native)]))
            commands.append(('llvm-jit', [*base, '--native', '--jit', '-o', work / 'jit', source]))
        for stage in ('initial', 'updated'):
            if stage == 'updated':
                old = paths[0].stat().st_mtime_ns
                os.utime(paths[0], ns=(old + 2345678900, old + 2345678900))
                if paths[0].stat().st_mtime_ns // 1_000_000 == old // 1_000_000:
                    raise AssertionError('host did not change the fixture timestamp')
            before = signature(paths)
            for name, command in commands:
                run(stage + '-' + name, command, expected(paths), limit=60)
                report['routes'].append({'stage': stage, 'route': name, 'status': 'PASS',
                                         'stable_repetitions': 100})
            if signature(paths) != before:
                raise AssertionError('timestamp reads changed the fixtures')
        # NUL is a reserved DOS device name, even with a file extension.
        nul = work / 'embedded-zero-path.ny'
        nul.write_text(source_for(paths, nul_case=True), encoding='utf-8', newline='\n')
        for encoding in ('text', 'binary'):
            output_nil = work / ('nul-' + encoding + '.nil')
            flags = [] if encoding == 'text' else ['--emit-binary-nil']
            run('compile-nul-' + encoding, [*base, '--no-bundle', *flags, '-o', output_nil, nul])
            run('nul-' + encoding, [interpreter, '--no-ai', output_nil], (ERROR + '\n').encode())
        report['after'] = signature(paths)
        report['status'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
    report['cleanup_verified'] = all(row.get('cleanup_verified') is True for row in report['commands'])
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(report['status'] + ': ' + str(output / 'report.json'))
    if report['status'] != 'PASS':
        print(report.get('error', 'timestamp contract failed'), file=sys.stderr)
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
