#!/usr/bin/env python3
"""Bounded public snapshot/byte-conversion routes with independent host oracles.

This is a source-library regression, not installed SDK or release qualification.
No server is started. Raw process evidence and the owned fixture are retained.
"""
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

SNAPSHOT_OUTPUT = b'snapshot-content-ok\nsnapshot-errors-ok\nsnapshot-links-ok\n'
CONVERSION_OUTPUT = b'from-bytes-ok\n'
NATIVE_REJECTION = b"unsupported native annotation '@intrinsic(\"text.from_bytes\")'"
SNAPSHOT_REJECTION = b'file.read_rooted_snapshot requires (text,text,i64) -> Result<FileSnapshot,text>'
SOURCES = ('rooted-file-snapshot.ny', 'text-from-bytes.ny', 'text-from-bytes-native-reject.ny',
           'rooted-file-snapshot-invalid-schema.ny', 'rooted-file-snapshot-invalid-signature.ny')


def snapshot_signature(root: Path) -> dict:
    result = support.inventory(root, maximum_entries=32, maximum_file=1024)
    for name, entry in result.items():
        path = root / name
        if entry['kind'] == 'file':
            entry['modified_ns'] = path.stat().st_mtime_ns
        elif entry['kind'] == 'link-or-reparse':
            entry['target'] = os.readlink(path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'nilstd'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    destination = parser.add_mutually_exclusive_group()
    destination.add_argument('--output', type=Path, help='Fresh exact evidence directory')
    destination.add_argument('--output-parent', type=Path, help='Retain a fresh child here')
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    nilstd = args.nilstd.resolve(strict=True)
    if args.output_parent:
        parent = args.output_parent.resolve()
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='rooted-file-snapshot-', dir=parent))
    elif args.output:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
    else:
        output = Path(tempfile.mkdtemp(prefix='rooted-file-snapshot-'))
    work, logs = output / 'work', output / 'logs'
    work.mkdir()
    logs.mkdir()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    ownership = support.load_ownership(ROOT)
    deadline = time.monotonic() + 300
    inputs = {name: {'path': str(path), **support.file_record(path)} for name, path in
              [('compiler', compiler), ('interpreter', interpreter),
               *[(name, ROOT / 'tests/language' / name) for name in SOURCES],
               ('file_library', nilstd / 'io/file.ny'),
               ('text_library', nilstd / 'text/string_utils.ny'),
               ('contract', Path(__file__).resolve()),
               ('process_owner', ROOT / 'validation/ny-3d/scripts/debugger_process.py'),
               ('process_support', ROOT / 'tests/tools/ordinary_io_parity_contract.py')]}
    report = {'schema': 'nil.rooted-file-snapshot.v1', 'status': 'FAIL',
              'inputs': inputs, 'nilstd': str(nilstd), 'commands': [],
              'snapshot_routes': [], 'conversion_routes': [], 'native_rejections': [],
              'unbox_rejections': [],
              'llvm_enabled': args.llvm == 'ON', 'whole_seconds': 300,
              'scope': 'Source-library interpreter/native regression. FileSnapshot content is a byte array on every route; text.from_bytes is interpreter-only.',
              'link_cases': 'required; POSIX file/directory symlinks, Windows directory junctions (no symlink privilege required)',
              'work_directory_retained': True}

    def run(name, command, expected=b'', limit=60, rejection=None):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError('whole snapshot contract deadline exhausted')
        row = support.run_process(ownership, [str(value) for value in command],
                                  work, environment, logs, name, min(limit, remaining))
        report['commands'].append(row)
        stdout = (logs / row['stdout']).read_bytes()
        stderr = (logs / row['stderr']).read_bytes()
        if not row['cleanup_verified'] or row.get('timed_out') or row.get('error'):
            raise AssertionError(name + ': bounded process cleanup failed')
        if rejection:
            if row.get('exit_code', 0) <= 0 or stdout or rejection not in stderr:
                raise AssertionError(name + ': native compilation did not fail for the exact expected contract')
        elif row['status'] != 'PASS' or stderr or (expected is not None and stdout != expected):
            raise AssertionError(name + ': exit/stdout/stderr contract failed')
        return row

    try:
        fixture = work / 'fixtures'
        fixture.mkdir()
        rooted = fixture / 'root café λ'
        rooted.mkdir()
        (rooted / 'nested').mkdir()
        outside = fixture / 'outside'
        outside.mkdir()
        sentinel = outside / 'sentinel.bin'
        sentinel.write_bytes(b'outside fixture must never be returned\x00\xff')
        if os.name == 'nt':
            for name in ('linked.bin', 'via-link'):
                link = rooted / name
                run('create-junction-' + name,
                    ['cmd.exe', '/d', '/c', 'mklink', '/J', link, outside], expected=None)
                if not getattr(link.lstat(), 'st_file_attributes', 0) & 0x400:
                    raise AssertionError('Windows fixture is not a reparse point')
            report['link_fixture_kind'] = 'directory junctions: final reparse entry and intermediate directory escape'
        else:
            (rooted / 'linked.bin').symlink_to(sentinel)
            (rooted / 'via-link').symlink_to(outside, target_is_directory=True)
            report['link_fixture_kind'] = 'final file symlink and intermediate directory symlink'
        payload = rooted / 'nested/café-λ.bin'
        empty = rooted / 'empty.bin'
        empty.write_bytes(b'')
        fixed_time = 1_600_000_000_123_000_000
        os.utime(empty, ns=(fixed_time, fixed_time))
        sources = {}
        for name in SOURCES:
            source = work / name
            source.write_bytes((ROOT / 'tests/language' / name).read_bytes())
            sources[name] = source
        base = [compiler, '--quiet', '--no-stdlib', '--sdk-path', nilstd]
        routes = []
        for encoding in ('text', 'binary'):
            flags = [] if encoding == 'text' else ['--emit-binary-nil']
            nil = work / ('snapshot-' + encoding + '.nil')
            run('compile-snapshot-' + encoding, [*base, '--no-bundle', *flags, '-o', nil,
                                                sources['rooted-file-snapshot.ny']])
            if nil.read_bytes().startswith(b'NIL\0') != (encoding == 'binary'):
                raise AssertionError('compiled snapshot encoding differs')
            routes.append((encoding + '-interpreter', [interpreter, '--no-ai', nil, '--']))
        if args.llvm == 'ON':
            native = work / 'snapshot-native'
            run('compile-snapshot-native', [*base, '--native', '-o', native,
                                            sources['rooted-file-snapshot.ny']])
            routes.append(('llvm-aot', [native_output_path(native)]))
            routes.append(('llvm-jit', [*base, '--native', '--jit', '-o', work / 'snapshot-jit',
                                        sources['rooted-file-snapshot.ny'], '--']))

        initial_digest = None
        for stage, seed in (('initial', 19), ('same-size-time-replacement', 21)):
            data = bytes((index * 73 + seed) % 256 for index in range(256))
            payload.write_bytes(data)
            os.utime(payload, ns=(fixed_time, fixed_time))
            stamp = payload.stat().st_mtime_ns // 1_000_000
            if empty.stat().st_mtime_ns // 1_000_000 != stamp:
                raise AssertionError('host did not preserve equal fixture timestamps')
            digest = hashlib.sha256(data).hexdigest()
            if initial_digest is None:
                initial_digest = digest
            elif digest == initial_digest:
                raise AssertionError('same-size/time replacement failed to change content digest')
            before = snapshot_signature(fixture)
            arguments = [rooted, str(stamp), digest, str(seed)]
            for route, command in routes:
                row = run(stage + '-' + route, [*command, *arguments], SNAPSHOT_OUTPUT)
                if snapshot_signature(fixture) != before:
                    raise AssertionError('snapshot read changed the owned fixture')
                report['snapshot_routes'].append({'stage': stage, 'route': route, 'status': 'PASS',
                    'bytes': len(data), 'sha256': digest, 'modified_ms': stamp,
                    'exact_payload_bytes_checked': 256, 'empty_file_checked': True,
                    'descendant_and_final_links_rejected': True, 'namespace_unchanged': True,
                    'elapsed_seconds': row['elapsed_seconds']})
            report[stage + '_namespace'] = before

        for route, command in routes:
            expected = (b"anything type mismatch: expected 'array<i64>', got 'array<u8>'"
                        if route.startswith('llvm-') else
                        b"anything contains 'array<u8>', cannot extract 'array<i64>'")
            run('wrong-element-' + route, [*command, *arguments, 'wrong-element'], rejection=expected)
            if snapshot_signature(fixture) != before:
                raise AssertionError('rejected extraction changed the owned fixture')
            report['unbox_rejections'].append({'route': route, 'status': 'PASS',
                                              'reason': expected.decode()})

        for encoding in ('text', 'binary'):
            flags = [] if encoding == 'text' else ['--emit-binary-nil']
            nil = work / ('conversion-' + encoding + '.nil')
            run('compile-conversion-' + encoding, [*base, '--no-bundle', *flags, '-o', nil,
                                                  sources['text-from-bytes.ny']])
            if nil.read_bytes().startswith(b'NIL\0') != (encoding == 'binary'):
                raise AssertionError('compiled conversion encoding differs')
            row = run('conversion-' + encoding, [interpreter, '--no-ai', nil], CONVERSION_OUTPUT)
            report['conversion_routes'].append({'route': encoding + '-interpreter', 'status': 'PASS',
                'all_256_byte_values': True, 'copy_independent_of_source_mutation': True,
                'empty_and_1MiB_payloads': True, 'elapsed_seconds': row['elapsed_seconds']})
        if args.llvm == 'ON':
            for route, flags in (('llvm-aot', []), ('llvm-jit', ['--jit'])):
                target = work / ('conversion-rejected-' + route)
                run('conversion-rejected-' + route, [*base, '--native', *flags, '-o', target,
                                                      sources['text-from-bytes-native-reject.ny']],
                    rejection=NATIVE_REJECTION)
                if target.exists() or native_output_path(target).exists():
                    raise AssertionError('rejected binary text conversion published an executable')
                report['native_rejections'].append({'route': route, 'status': 'PASS',
                    'case': 'text.from_bytes',
                    'reason': NATIVE_REJECTION.decode()})
            for case in ('invalid-schema', 'invalid-signature'):
                for route, flags in (('llvm-aot', []), ('llvm-jit', ['--jit'])):
                    target = work / ('snapshot-rejected-' + case + '-' + route)
                    run('snapshot-rejected-' + case + '-' + route,
                        [*base, '--native', *flags, '-o', target,
                         sources['rooted-file-snapshot-' + case + '.ny']],
                        rejection=SNAPSHOT_REJECTION)
                    if target.exists() or native_output_path(target).exists():
                        raise AssertionError('invalid snapshot ABI published an executable')
                    report['native_rejections'].append({'route': route, 'case': case,
                        'status': 'PASS', 'reason': SNAPSHOT_REJECTION.decode()})
        if len(report['snapshot_routes']) != (8 if args.llvm == 'ON' else 4):
            raise AssertionError('snapshot route inventory incomplete')
        if len(report['unbox_rejections']) != (4 if args.llvm == 'ON' else 2):
            raise AssertionError('wrong-element extraction route inventory incomplete')
        if len(report['conversion_routes']) != 2 or len(report['native_rejections']) != (6 if args.llvm == 'ON' else 0):
            raise AssertionError('conversion route inventory incomplete')
        for item in inputs.values():
            if support.file_record(Path(item['path'])) != {key: item[key] for key in ('bytes', 'sha256')}:
                raise AssertionError('input changed during contract')
        report['status'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
    report['cleanup_verified'] = all(row.get('cleanup_verified') is True for row in report['commands'])
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(report['status'] + ': ' + str(output / 'report.json'))
    if report['status'] != 'PASS':
        print(report.get('error', 'snapshot contract failed'), file=sys.stderr)
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
