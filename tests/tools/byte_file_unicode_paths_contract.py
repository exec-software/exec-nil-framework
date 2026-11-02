#!/usr/bin/env python3
"""Independent host filename/byte oracle for the portable STL byte-file APIs.

Python's Unicode filesystem interface is independent of Ny's narrow runtime
path conversion (and uses wide paths on Windows). Successful Ny write/read
round trips alone cannot prove that the intended filename was accessed.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests'))
sys.path.insert(0, str(REPOSITORY / 'validation/ny-3d/scripts'))
from debugger_process import OwnedProcess
from native_output_staging import bundle_output_path, linked_executable_path, native_output_path
from raw_byte_routes_contract import check_embedded_utf8_manifest

NAMES = ('caf\u00e9', '\u65e5\u672c\u8a9e', '\U0001f98a', 'cafe\u0301')
DIRECTORIES = ('relative-caf\u00e9-\u65e5\u672c\u8a9e-\U0001f98a',
               'absolute-caf\u00e9-\u65e5\u672c\u8a9e-\U0001f98a')
EXPECTED_STDOUT = (b'no-clobber\n' * 8 + b'malformed-paths-rejected\n' +
                   b'byte-file-unicode-paths-ok\n')


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@contextmanager
def private_workspace(report: dict):
    """Retain failures; remove only this newly allocated tree after success."""
    root = Path(tempfile.mkdtemp(prefix='nil-byte-file-paths-'))
    report['work_directory'] = str(root)
    report['work_directory_retained'] = True
    try:
        yield root
    except BaseException:
        # OwnedProcess has already completed bounded process-tree cleanup.
        # Preserve filenames and partial output for an independent inspection.
        raise
    else:
        shutil.rmtree(root)
        if root.exists():
            raise RuntimeError('private byte-file fixture survived successful cleanup')
        report['work_directory_retained'] = False


def prepare_namespace(root: Path) -> tuple[list[str], dict[str, bytes | None]]:
    """Return argv and final exact namespace, without any Ny-created names."""
    root.mkdir()
    arguments, expected = [], {}
    for kind, directory_name in enumerate(DIRECTORIES):
        directory = root / directory_name
        directory.mkdir()
        expected[directory_name] = None
        for index, name in enumerate(NAMES):
            item = kind * len(NAMES) + index
            payload = bytes((offset + item * 17) % 256 for offset in range(256))
            replacement = bytes([42]) + payload[1:255] + bytes([7])
            protected = b'independent no-clobber sentinel\0\xff' + bytes([item])
            paths = [directory / (name + suffix) for suffix in
                     ('.input.bin', '.new.bin', '.protected.bin', '.replace.bin')]
            paths[0].write_bytes(payload)
            paths[2].write_bytes(protected)
            paths[3].write_bytes(b'independent old replacement\0' + bytes([item]))
            for path, content in zip(paths, (payload, payload, protected, replacement)):
                expected[path.relative_to(root).as_posix()] = content
                arguments.append(str(path if kind else path.relative_to(root)))
    return arguments, expected


def check_namespace(root: Path, expected: dict[str, bytes | None]) -> dict:
    """Exact names catch mojibake, normalization, unexpected aliases and orphans."""
    entries = {path.relative_to(root).as_posix(): path for path in root.rglob('*')}
    if set(entries) != set(expected):
        raise AssertionError('namespace mismatch: missing={!r}, unexpected={!r}'.format(
            sorted(set(expected) - set(entries)), sorted(set(entries) - set(expected))))
    inventory = {}
    for name, content in expected.items():
        path = entries[name]
        mode = path.lstat().st_mode
        if content is None:
            if not stat.S_ISDIR(mode):
                raise AssertionError(f'expected a real directory: {name!r}')
            inventory[name] = {'kind': 'directory'}
        else:
            if not stat.S_ISREG(mode):
                raise AssertionError(f'expected a regular file: {name!r}')
            actual = path.read_bytes()
            if actual != content:
                raise AssertionError(f'file bytes differ: {name!r}; '
                                     f'actual={sha(actual)}, expected={sha(content)}')
            inventory[name] = {'kind': 'file', 'bytes': len(actual), 'sha256': sha(actual)}
    return inventory


def run_contract(args: argparse.Namespace, report: dict) -> None:
    tools = {name: getattr(args, name).resolve(strict=True) for name in
             ('compiler', 'interpreter', 'assembler', 'linker', 'runtime')}
    libraries = {'nilstd': args.nilstd.resolve(strict=True)}
    if args.nillib is not None:
        libraries['nillib'] = args.nillib.resolve(strict=True)
    source = args.source.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    report['inputs'] = {**{key: str(value) for key, value in tools.items()},
                        **{key: str(value) for key, value in libraries.items()},
                        'source': str(source), 'source_sha256': sha(source.read_bytes()),
                        'llvm': args.llvm,
                        'nillib_mode': 'supplied' if args.nillib else 'generated'}
    with private_workspace(report) as root:
        fixture = root / source.name
        shutil.copyfile(source, fixture)

        def run(command, cwd, output=b'', timeout=None):
            command = [str(part) for part in command]
            owner = OwnedProcess(command, cwd=cwd, env=environment,
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
            stdout, stderr = owner.communicate(timeout=args.timeout if timeout is None else timeout)
            report['commands'].append({'argv': command, 'cwd': str(cwd),
                                       'exit_code': owner.process.returncode,
                                       'stdout_sha256': sha(stdout), 'stderr_sha256': sha(stderr)})
            if owner.process.returncode != 0 or stderr or (output is not None and stdout != output):
                raise AssertionError((command, owner.process.returncode, stdout[:4096], stderr[:4096]))
            return stdout

        private_libraries = {}
        for name, library in libraries.items():
            private_libraries[name] = root / name
            shutil.copytree(library, private_libraries[name])
        if 'nillib' not in private_libraries:
            stage = root / 'prepared-library'
            stage.mkdir()
            # The packaging helper consumes/unlinks its input runtime after
            # publication. It must receive this disposable copy, never an SDK
            # or build-tree runtime. No SDK tools/manifest are repackaged here.
            runtime_copy = stage / tools['runtime'].name
            runtime_hash = sha(tools['runtime'].read_bytes())
            shutil.copyfile(tools['runtime'], runtime_copy)
            helper = REPOSITORY / 'scripts/package-sdk.py'
            code = (
                'import json, runpy, sys; from pathlib import Path; '
                'helper = runpy.run_path(sys.argv[1]); '
                'modules, count = helper["compile_standard_library"]('
                '*map(Path, sys.argv[2:])); '
                'print(json.dumps({"modules": len(modules), "sources": count}))')
            output = run([sys.executable, '-c', code, helper, tools['compiler'],
                          private_libraries['nilstd'], stage, runtime_copy],
                         root, None, args.prepare_timeout)
            if runtime_copy.exists() or sha(tools['runtime'].read_bytes()) != runtime_hash:
                raise AssertionError('library preparation did not preserve the original runtime')
            report['preparation'] = {**json.loads(output), 'helper': str(helper),
                                     'helper_sha256': sha(helper.read_bytes()),
                                     'runtime_sha256': runtime_hash,
                                     'runtime_source_preserved': True}
            private_libraries['nillib'] = stage / 'nillib'

        report['libraries'] = {}
        for library_name, private_library in private_libraries.items():
            # Isolate the dependency payload: binary-mode searches contain no
            # .ny/.matrix/.mat files, and the consumer is outside the source tree.
            if library_name == 'nillib':
                if not list(private_library.rglob('*.nil')):
                    raise AssertionError('binary STL has no NIL libraries')
                if any(path.suffix.lower() in ('.ny', '.matrix', '.mat')
                       for path in private_library.rglob('*') if path.is_file()):
                    raise AssertionError('binary STL proof contains source files')
            report['libraries'][library_name] = {
                'path': str(private_library),
                'files': {path.relative_to(private_library).as_posix(): sha(path.read_bytes())
                          for path in sorted(private_library.rglob('*')) if path.is_file()}}
            build = root / (library_name + '-build')
            build.mkdir()
            base = [tools['compiler'], '--quiet', '--no-stdlib', '--sdk-path', private_library]

            def execute(route, command):
                route_root = root / (library_name + '-' + route)
                argv, expected = prepare_namespace(route_root)
                run([*command, *argv], route_root, EXPECTED_STDOUT)
                inventory = check_namespace(route_root, expected)
                report['executions'].append({'library': library_name, 'route': route,
                                             'status': 'PASS', 'namespace': inventory})

            for encoding in ('text', 'binary'):
                flags = [] if encoding == 'text' else ['--emit-binary-nil']
                nil = build / (encoding + '.nil')
                run([*base, '--no-bundle', *flags, '-o', nil, fixture], build)
                if nil.read_bytes().startswith(b'NIL\0') != (encoding == 'binary'):
                    raise AssertionError('unexpected NIL encoding: ' + str(nil))
                execute(encoding + '-interpreter', [tools['interpreter'], '--no-ai', nil, '--'])
                bundle = build / (encoding + '-bundle')
                run([*base, *flags, '-o', bundle, fixture], build)
                check_embedded_utf8_manifest(bundle_output_path(bundle))
                execute('bundle-' + encoding, [bundle_output_path(bundle), '--no-ai', '--'])
                if args.llvm == 'ON':
                    obj = build / (encoding + ('.obj' if os.name == 'nt' else '.o'))
                    linked = build / (encoding + '-linked')
                    run([tools['assembler'], '-o', obj, nil], build, None)
                    run([tools['linker'], '--runtime', tools['runtime'], '-o', linked, obj], build, None)
                    check_embedded_utf8_manifest(linked_executable_path(linked))
                    execute('linked-' + encoding, [linked_executable_path(linked)])
            if args.llvm == 'ON':
                native = build / 'native'
                run([*base, '--native', '-o', native, fixture], build)
                check_embedded_utf8_manifest(native_output_path(native))
                execute('llvm-aot', [native_output_path(native)])
                execute('llvm-jit', [*base, '--native', '--jit', '-o', build / 'jit', fixture, '--'])

    expected_count = 16 if args.llvm == 'ON' else 8
    if len(report['executions']) != expected_count:
        raise AssertionError('incomplete byte-file path route matrix')
    report['status'] = 'PASS'


def summarize_report(report: dict) -> dict:
    return {'status': report['status'], 'executions': len(report['executions']),
            'work_directory': report.get('work_directory'),
            'work_directory_retained': report.get('work_directory_retained', False),
            **({'error': report['error']} if 'error' in report else {})}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime', 'nilstd'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--nillib', type=Path,
                        help='binary STL to copy; omitted builds a checked private STL from --nilstd')
    parser.add_argument('--source', type=Path,
                        default=REPOSITORY / 'tests/language/byte-file-unicode-paths.ny')
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--prepare-timeout', type=float, default=300)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if not 0 < args.timeout <= 300:
        parser.error('--timeout must be between 0 and 300 seconds')
    if not 0 < args.prepare_timeout <= 300:
        parser.error('--prepare-timeout must be between 0 and 300 seconds')
    report = {'schema': 'nil.byte-file-unicode-paths.v1', 'status': 'FAIL',
              'commands': [], 'executions': [],
              'boundary': 'Strict UTF-8 byte-file paths only; no normalization or release qualification.'}
    try:
        run_contract(args, report)
    except Exception as error:
        report['error'] = str(error)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
    print(json.dumps(summarize_report(report), indent=2))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
