#!/usr/bin/env python3
"""Pin what a native object imports.

A relocatable object emitted for a program that uses no hosted facility must
import the native runtime ABI and nothing else. A freestanding or kernel
target supplies those hooks deliberately; anything beyond them it has to
satisfy with stubs it should never need, and a libc symbol arriving by
accident is exactly the kind of drift that is invisible on a hosted host.

The ELF symbol table is read directly rather than shelling out to nm, so the
contract does not depend on binutils being installed.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

# The native runtime ABI an emitted object is allowed to require. Adding a
# name here widens the boundary every freestanding consumer must implement,
# so it is a deliberate decision rather than a test fixup.
runtime_abi = {
    'nyrt_abi_version',
    'nyrt_context_begin',
    'nyrt_context_end',
    'nyrt_fail',
    'nyrt_profile_site',
}

# Emitted only for the string-concatenation helper. Their presence in a
# program that concatenates proves the helper is still generated; their
# absence elsewhere proves it is generated on demand.
concat_imports = {'strlen', 'memcpy', 'calloc'}


def undefined_symbols(path: Path) -> set[str]:
    data = path.read_bytes()
    if data[:4] != b'\x7fELF':
        raise SystemExit(f'not an ELF object: {path}')
    if data[4] != 2 or data[5] != 1:
        raise SystemExit('expected a little-endian 64-bit object')
    section_offset, = struct.unpack_from('<Q', data, 0x28)
    section_size, section_count, names_index = struct.unpack_from('<HHH', data, 0x3a)

    def section(index: int):
        base = section_offset + index * section_size
        name, kind = struct.unpack_from('<II', data, base)
        offset, size = struct.unpack_from('<QQ', data, base + 0x18)
        link, = struct.unpack_from('<I', data, base + 0x28)
        entry_size, = struct.unpack_from('<Q', data, base + 0x38)
        return {'name': name, 'kind': kind, 'offset': offset, 'size': size,
                'link': link, 'entry_size': entry_size}

    names = section(names_index)

    def text(table_offset: int, index: int) -> str:
        end = data.index(b'\0', table_offset + index)
        return data[table_offset + index:end].decode('utf-8', 'replace')

    undefined: set[str] = set()
    for index in range(section_count):
        header = section(index)
        if text(names['offset'], header['name']) != '.symtab':
            continue
        strings = section(header['link'])
        if not header['entry_size']:
            raise SystemExit('symbol table has no entry size')
        for position in range(header['offset'], header['offset'] + header['size'],
                              header['entry_size']):
            name_index, _info, _other, shndx = struct.unpack_from('<IBBH', data, position)
            if shndx != 0 or not name_index:   # SHN_UNDEF with a name
                continue
            undefined.add(text(strings['offset'], name_index))
    return undefined


def emit(compiler: Path, assembler: Path, root: Path, name: str, source: str) -> set[str]:
    path = root / f'{name}.ny'
    path.write_text(source, newline='\n')
    nil = root / f'{name}.nil'
    obj = root / f'{name}.o'
    for command in ([compiler, '--no-stdlib', '--no-bundle', '--unmanaged', '-o', nil, path],
                    [assembler, '--unmanaged', '-o', obj, nil]):
        result = subprocess.run(list(map(str, command)), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return undefined_symbols(obj)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'linux':
        print('SKIP: the native symbol surface is pinned on ELF hosts')
        return 77

    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-symbol-surface-') as directory:
        root = Path(directory)
        plain = emit(args.compiler, args.assembler, root, 'plain',
                     'fn main() -> i64 {\n    let total: i64 = 0;\n    return total;\n}\n')
        assert plain <= runtime_abi, (
            'an object that uses no hosted facility imports only the native runtime ABI; '
            f'unexpected: {sorted(plain - runtime_abi)}')
        checks += 1
        assert not (plain & concat_imports), (
            'the string concatenation helper must be emitted on demand, not in every object; '
            f'present: {sorted(plain & concat_imports)}')
        checks += 1

        # The helper still has to exist when a program actually concatenates,
        # or the first assertion would pass for the wrong reason.
        concat = emit(args.compiler, args.assembler, root, 'concat',
                      'fn main() -> i64 {\n    let a: text = "left";\n'
                      '    let b: text = "right";\n    let joined: text = a + b;\n'
                      '    return 0;\n}\n')
        assert concat_imports <= concat, (
            'a concatenating program must still import the helper dependencies; '
            f'missing: {sorted(concat_imports - concat)}')
        checks += 1
        assert concat <= runtime_abi | concat_imports, (
            'concatenation must not widen the import surface further; '
            f'unexpected: {sorted(concat - runtime_abi - concat_imports)}')
        checks += 1

    print(f'Native symbol surface: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
