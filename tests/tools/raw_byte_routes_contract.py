#!/usr/bin/env python3
"""UTF-8 argv and untranslated stdout through every executable-producing route.

Never enable subprocess text mode here: universal-newline decoding would hide
the Windows CRLF regression this contract exists to catch. Native programs
must carry their own UTF-8 process manifest, not inherit the compiler's code
page or rely on the host's optional system-wide UTF-8 setting.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_output_staging import bundle_output_path, linked_executable_path, native_output_path


def check_embedded_utf8_manifest(executable: Path) -> None:
    if os.name != 'nt':
        return
    # Do not let a system-wide UTF-8 ACP setting mask a missing manifest.
    # Map resources only: this does not execute the image's startup code.
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel32.LoadLibraryExW.restype = wintypes.HMODULE
    kernel32.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel32.FindResourceW.restype = wintypes.HRSRC
    kernel32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
    kernel32.SizeofResource.restype = wintypes.DWORD
    kernel32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
    kernel32.LoadResource.restype = wintypes.HGLOBAL
    kernel32.LockResource.argtypes = [wintypes.HGLOBAL]
    kernel32.LockResource.restype = ctypes.c_void_p
    kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
    kernel32.FreeLibrary.restype = wintypes.BOOL
    handle = kernel32.LoadLibraryExW(str(executable), None, 0x02 | 0x20)
    assert handle, (executable, ctypes.get_last_error())
    try:
        # CREATEPROCESS_MANIFEST_RESOURCE_ID=1, RT_MANIFEST=24.
        resource = kernel32.FindResourceW(handle, 1, 24)
        assert resource, (executable, 'missing embedded process manifest')
        size = kernel32.SizeofResource(handle, resource)
        loaded = kernel32.LoadResource(handle, resource)
        pointer = kernel32.LockResource(loaded) if loaded else None
        assert size and pointer, (executable, 'unreadable embedded manifest')
        manifest = ET.fromstring(ctypes.string_at(pointer, size))
        code_page = manifest.find(
            './/{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage')
        assert code_page is not None and code_page.text == 'UTF-8', (
            executable, 'native executable did not declare UTF-8 ACP')
    finally:
        kernel32.FreeLibrary(handle)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    tools = {name: getattr(args, name).resolve(strict=True)
             for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime')}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    # One scalar from each major UTF-8 width, including an astral scalar.
    # Quotes, a trailing backslash, empty argv, and shell syntax are ordinary
    # data too; subprocess supplies a vector, never a shell command string.
    program_args = ['café', '日本語', '🦊', '', 'a "quoted" word', 'tail\\', 'literal\r',
                    '%PATH% & literal! $value']
    marker = 'UTF-8 café 日本語 🦊'
    expected = (marker + '\n' + str(len(program_args)) + '\n' +
                ''.join(value + '\n' for value in program_args)).encode('utf-8')
    executions = 0

    with tempfile.TemporaryDirectory(prefix='nil raw bytes ') as temporary:
        # Exercise Unicode and shell metacharacters in *link input paths*,
        # independently of the Unicode arguments supplied to the executable.
        root = Path(temporary) / 'café 日本語 & %PATH%'
        root.mkdir()

        def run(command: list[object], output: bytes | None = None) -> None:
            result = subprocess.run(list(map(str, command)), cwd=root,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, timeout=120)
            assert result.returncode == 0 and not result.stderr, (command, result)
            if output is not None:
                assert result.stdout == output, (command, result.stdout, output)

        def execute(command: list[object]) -> None:
            nonlocal executions
            run(command, expected)
            executions += 1

        for language in ('ny', 'matrix'):
            if language == 'ny':
                source_text = (
                    '@interop("std_sys")\nfn arg_count() -> i64\nend fn\n'
                    '@interop("std_sys")\nfn arg(index: i64) -> text\nend fn\n'
                    'fn main() -> i32\n'
                    f'println("{marker}")\nprintln(arg_count())\n' +
                    ''.join(f'println(arg({index}))\n' for index in range(len(program_args))) +
                    'return 0\nend fn\n')
            else:
                source_text = (
                    'book raw_bytes\n'
                    'note interop "std_sys"\narg_count returns number begin paragraph\nend paragraph\n'
                    'note interop "std_sys"\narg receives index as number returns text begin paragraph\nend paragraph\n'
                    'main returns override integer begin paragraph\n'
                    f'say "{marker}"\nsay arg_count with nothing\n' +
                    ''.join(f'say arg with index being {index}\n' for index in range(len(program_args))) +
                    'give 0\nend paragraph\n')
            source = root / ('source café.' + language)
            source.write_bytes(source_text.encode('utf-8'))
            base = [tools['compiler'], '--quiet', '--no-stdlib']
            for encoding in ('text', 'binary'):
                flags = [] if encoding == 'text' else ['--emit-binary-nil']
                stem = language + '-' + encoding
                nil = root / (stem + '.nil')
                run([*base, '--no-bundle', *flags, '-o', nil, source], b'')
                execute([tools['interpreter'], '--no-ai', nil, '--', *program_args])

                bundle = root / (stem + '-bundle')
                run([*base, *flags, '-o', bundle, source], b'')
                check_embedded_utf8_manifest(bundle_output_path(bundle))
                execute([bundle_output_path(bundle), '--no-ai', '--', *program_args])

                if args.llvm == 'ON':
                    obj = root / (stem + ('.obj' if os.name == 'nt' else '.o'))
                    linked = root / (stem + '-linked')
                    run([tools['assembler'], '-o', obj, nil])
                    run([tools['linker'], '--runtime', tools['runtime'], '-o', linked, obj])
                    check_embedded_utf8_manifest(linked_executable_path(linked))
                    execute([linked_executable_path(linked), *program_args])

            if args.llvm == 'ON':
                native = root / (language + '-native')
                run([*base, '--native', '-o', native, source], b'')
                check_embedded_utf8_manifest(native_output_path(native))
                execute([native_output_path(native), *program_args])
                execute([*base, '--native', '--jit', '-o', root / (language + '-jit'),
                         source, '--', *program_args])

    expected_executions = 16 if args.llvm == 'ON' else 8
    assert executions == expected_executions, executions
    print(f'raw-byte execution contracts passed: {executions} (Ny and Matrix)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
