#!/usr/bin/env python3
"""Bounded public ordinary-I/O route regression with an independent host oracle.

Tests source STL, and optionally a supplied binary STL, without asserting
native named-struct arrays or release-SDK qualification. All evidence survives.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import sys
import time

import ordinary_io_parity_contract as support

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests'))
from native_output_staging import bundle_output_path, linked_executable_path, native_output_path
from raw_byte_routes_contract import check_embedded_utf8_manifest

EXPECTED_STDOUT = b'paths-ok\nfiles-ok\nstreams-ok\nenvironment-ok\n'
PAYLOAD = bytes((0, 10, 13, 128, 255))
TEXT = 'café日本語🦊\r\ntail\n'.encode('utf-8')


def prepare(root):
    root.mkdir()
    root = root.resolve(strict=True)
    parent = root / 'parent-café-日本語-🦊'
    parent.mkdir()
    target = parent / 'cafe\u0301'
    seed = parent / 'café.seed'
    seed.write_bytes(PAYLOAD)
    cwd = root / 'cwd-café-日本語-🦊'
    cwd.mkdir()
    sentinel = b'untouched namespace sentinel\0\xff'
    # Host lookup may alias NFC/NFD spellings. Give the protected file its own
    # identity; the exact namespace oracle still rejects normalized outputs.
    protected = parent / 'sentinel-café.bytes'
    protected.write_bytes(sentinel)
    (cwd / 'sentinel').write_bytes(sentinel)
    expected = {
        parent.name: None, cwd.name: None,
        seed.relative_to(root).as_posix(): PAYLOAD,
        protected.relative_to(root).as_posix(): sentinel,
        (cwd / 'sentinel').relative_to(root).as_posix(): sentinel,
    }
    for suffix, data in (('.bytes', PAYLOAD), ('.text', TEXT), ('.stream', PAYLOAD)):
        expected[target.relative_to(root).as_posix() + suffix] = data
    name = 'NIL_NATIVE_IO_café_日本語_🦊'
    return [str(seed.relative_to(root)), str(target), str(cwd), name], expected, {name: 'host-café日本語🦊'}


def run_contract(args):
    repository = args.repository.resolve(strict=True)
    output = support.allocate_output(repository, args.output)
    tools = {name: getattr(args, name).resolve(strict=True)
             for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime')}
    source = repository / 'tests/language/ordinary-io-native.ny'
    inputs = {**tools, 'source': source, 'runner': Path(__file__).resolve(),
              'invalid_abi': repository / 'tests/language/ordinary-io-invalid-abi.ny',
              'support': repository / 'tests/tools/ordinary_io_parity_contract.py',
              'ownership': repository / 'validation/ny-3d/scripts/debugger_process.py',
              'output_paths': repository / 'tests/native_output_staging.py'}
    libraries = {'nilstd': args.nilstd.resolve(strict=True)}
    if args.nillib is not None:
        libraries['nillib'] = args.nillib.resolve(strict=True)
    report = {'schema': 'nil.ordinary-io-native.v1', 'status': 'FAIL',
              'scope': 'ORDINARY_IO_SCALAR_ROUTES_NOT_STRUCT_ARRAYS_OR_RELEASE_QUALIFICATION',
              'evidence_directory': str(output), 'commands': [], 'executions': [],
              'environment_enumeration_supported': False, 'shared_deadline_seconds': 600}
    deadline = time.monotonic() + 600
    try:
        before = {key: support.file_record(path) for key, path in inputs.items()}
        report['inputs'] = {key: str(path) for key, path in inputs.items()}
        report['before'] = before
        report['libraries_before'] = {name: support.inventory(path) for name, path in libraries.items()}
        support.require(all(entry['kind'] in ('file', 'directory') for inventory in
                            report['libraries_before'].values() for entry in inventory.values()),
                        'Library contains a linked or special entry')
        ownership = support.load_ownership(repository)
        logs = output / 'logs'
        logs.mkdir()
        fixture = output / source.name
        shutil.copyfile(source, fixture)
        environment = {key: value for key, value in os.environ.items() if not key.startswith(('NIL_', 'NY_'))}

        def run(name, command, cwd, expected=None, extra_environment=None, rejection=None):
            remaining = deadline - time.monotonic()
            support.require(remaining > 0, 'Shared deadline exhausted')
            row = support.run_process(ownership, command, cwd,
                        {**environment, **(extra_environment or {})}, logs, name, min(120, remaining))
            report['commands'].append(row)
            support.require(row['cleanup_verified'], 'Owned process cleanup failed')
            stdout, stderr = ((logs / row[key]).read_bytes() for key in ('stdout', 'stderr'))
            if rejection is not None:
                support.require(row.get('exit_code', 0) != 0 and not row.get('timed_out') and
                                rejection in stderr and not stdout, 'Expected ABI rejection: ' + name)
                row['expected_rejection'] = True
            else:
                support.require(row['status'] == 'PASS', 'Command failed: ' + name)
                support.require(not stderr, 'Unexpected stderr: ' + name)
            if expected is not None:
                support.require(stdout == expected, 'Unexpected stdout: ' + name)

        for library_name, library in libraries.items():
            private = output / library_name
            shutil.copytree(library, private)
            support.require(support.inventory(private) == report['libraries_before'][library_name],
                            'Private library copy differs')
            build = output / (library_name + '-build')
            build.mkdir()
            base = [tools['compiler'], '--quiet', '--no-stdlib', '--sdk-path', private]

            def execute(route, command):
                name = library_name + '-' + route
                cwd = output / name
                arguments, expected, extra = prepare(cwd)
                run(name, [*command, *arguments], cwd, EXPECTED_STDOUT, extra)
                namespace = support.check_namespace(cwd, expected)
                report['executions'].append({'library': library_name, 'route': route,
                                             'status': namespace['status'], 'namespace': namespace})
                support.require(namespace['status'] == 'PASS',
                                'Host namespace/byte oracle failed: ' + name +
                                '; missing=' + repr(namespace['missing']) +
                                '; unexpected=' + repr(namespace['unexpected']) +
                                '; changed=' + repr(namespace['changed']))

            for encoding in ('text', 'binary'):
                flags = [] if encoding == 'text' else ['--emit-binary-nil']
                nil = build / (encoding + '.nil')
                run(library_name + '-compile-' + encoding,
                    [*base, '--no-bundle', *flags, '-o', nil, fixture], build, b'')
                support.require(nil.read_bytes().startswith(b'NIL\0') == (encoding == 'binary'),
                                'Unexpected NIL encoding')
                execute(encoding + '-interpreter', [tools['interpreter'], '--no-ai', nil, '--'])
                bundle = build / (encoding + '-bundle')
                run(library_name + '-compile-bundle-' + encoding, [*base, *flags, '-o', bundle, fixture], build, b'')
                check_embedded_utf8_manifest(bundle_output_path(bundle))
                execute('bundle-' + encoding, [bundle_output_path(bundle), '--no-ai', '--'])
                if args.llvm == 'ON':
                    obj = build / (encoding + ('.obj' if os.name == 'nt' else '.o'))
                    linked = build / (encoding + '-linked')
                    run(library_name + '-assemble-' + encoding, [tools['assembler'], '-o', obj, nil], build)
                    run(library_name + '-link-' + encoding,
                        [tools['linker'], '--runtime', tools['runtime'], '-o', linked, obj], build)
                    check_embedded_utf8_manifest(linked_executable_path(linked))
                    execute('linked-' + encoding, [linked_executable_path(linked)])
            if args.llvm == 'ON':
                native = build / 'native'
                run(library_name + '-compile-aot', [*base, '--native', '-o', native, fixture], build, b'')
                check_embedded_utf8_manifest(native_output_path(native))
                execute('llvm-aot', [native_output_path(native)])
                execute('llvm-jit', [*base, '--native', '--jit', '-o', build / 'jit', fixture, '--'])
                rejected = build / 'invalid-abi'
                run(library_name + '-invalid-abi', [*base, '--native', '-o', rejected, inputs['invalid_abi']], build,
                    rejection=b'file.write_text requires its exact ordinary-I/O ABI and enum schema')
                support.require(not native_output_path(rejected).exists(), 'Rejected ABI produced an executable')
            support.require(support.inventory(private) == report['libraries_before'][library_name],
                            'Private library changed during execution')
        report['after'] = {key: support.file_record(path) for key, path in inputs.items()}
        report['libraries_after'] = {name: support.inventory(path) for name, path in libraries.items()}
        support.require(report['after'] == before and report['libraries_after'] == report['libraries_before'],
                        'Input changed during test')
        support.require(support.file_record(fixture) == before['source'], 'Private fixture changed')
        support.require(len(report['executions']) == len(libraries) * (8 if args.llvm == 'ON' else 4),
                        'Incomplete route matrix')
        report['status'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
    finally:
        support.save_new(output / 'report.json', report)
    print(report['status'] + ': ' + str(output / 'report.json'))
    if report['status'] != 'PASS':
        print(report.get('error', 'ordinary I/O failed'), file=sys.stderr)
    return 0 if report['status'] == 'PASS' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=REPOSITORY)
    for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime', 'nilstd'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--nillib', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    return run_contract(parser.parse_args())


if __name__ == '__main__':
    sys.exit(main())
