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

# What the same helper calls on a target with no libc. The host owns the
# memory and installs it; the runtime bumps through it. These are the only
# names allocation is allowed to add, and no libc name may appear beside them.
freestanding_concat_imports = {'nyrt_str_length', 'nyrt_mem_copy', 'nyrt_alloc'}


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
    exported: set[str] = set()
    for index in range(section_count):
        header = section(index)
        if text(names['offset'], header['name']) != '.symtab':
            continue
        strings = section(header['link'])
        if not header['entry_size']:
            raise SystemExit('symbol table has no entry size')
        for position in range(header['offset'], header['offset'] + header['size'],
                              header['entry_size']):
            name_index, info, _other, shndx = struct.unpack_from('<IBBH', data, position)
            if not name_index:
                continue
            name = text(strings['offset'], name_index)
            if shndx == 0:                     # SHN_UNDEF with a name
                undefined.add(name)
            elif info >> 4 == 1:               # STB_GLOBAL and defined here
                exported.add(name)
    return undefined, exported


def definitions_without_noredzone(ir: Path) -> list[str]:
    """Definitions in the IR whose attribute group lacks noredzone.

    Checked through the attribute groups the definitions actually reference,
    because the bare word appearing somewhere in the file would also be true
    if a single function carried it.
    """
    groups_with: set[str] = set()
    definitions: list[tuple[str, str]] = []
    for line in ir.read_text().splitlines():
        if line.startswith('attributes #'):
            name = line.split('=')[0].strip().removeprefix('attributes ')
            if 'noredzone' in line:
                groups_with.add(name)
        elif line.startswith('define'):
            head = line.split('{')[0].split()
            group = next((piece for piece in head if piece.startswith('#')), '')
            definitions.append((line.split('(')[0].split()[-1], group))
    assert definitions, f'no definitions found in {ir}'
    return [name for name, group in definitions if group not in groups_with]


def absolute_relocation_count(obj: Path) -> int:
    """Number of 32-bit absolute relocations in an ELF relocatable object.

    These are what confine an object to the low 4 GiB: the linker has nowhere
    to put a higher address, so the link fails rather than the program
    misbehaving. Read from the section headers directly, so the check needs no
    binutils.
    """
    data = obj.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise SystemExit(f'expected a little-endian 64-bit object: {obj}')
    section_offset, = struct.unpack_from('<Q', data, 0x28)
    section_size, section_count, _ = struct.unpack_from('<HHH', data, 0x3a)
    r_x86_64_32, r_x86_64_32s = 10, 11
    total = 0
    for index in range(section_count):
        base = section_offset + index * section_size
        kind, = struct.unpack_from('<I', data, base + 4)
        if kind != 4:                      # SHT_RELA
            continue
        offset, size = struct.unpack_from('<QQ', data, base + 0x18)
        entry_size, = struct.unpack_from('<Q', data, base + 0x38)
        if not entry_size:
            continue
        for position in range(offset, offset + size, entry_size):
            info, = struct.unpack_from('<Q', data, position + 8)
            if info & 0xffffffff in (r_x86_64_32, r_x86_64_32s):
                total += 1
    return total


def emit(compiler: Path, assembler: Path, root: Path, name: str, source: str,
         freestanding: bool = False, no_red_zone: bool = False,
         code_model: str | None = None,
         ir: Path | None = None):
    path = root / f'{name}.ny'
    path.write_text(source, newline='\n')
    nil = root / f'{name}.nil'
    obj = root / f'{name}.o'
    assemble = [assembler, '--unmanaged']
    if freestanding:
        assemble.append('--freestanding')
    if no_red_zone:
        assemble.append('--no-red-zone')
    if code_model is not None:
        assemble += ['--code-model', code_model]
    if ir is not None:
        assemble += ['--emit-llvm', str(ir)]
    for command in ([compiler, '--no-stdlib', '--no-bundle', '--unmanaged', '-o', nil, path],
                    assemble + ['-o', obj, nil]):
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
        trivial = 'fn main() -> i64 {\n    let total: i64 = 0;\n    return total;\n}\n'
        plain, plain_exports = emit(args.compiler, args.assembler, root, 'plain', trivial)
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
        concatenating = ('fn main() -> i64 {\n    let a: text = "left";\n'
                         '    let b: text = "right";\n'
                         '    let joined: text = a + b;\n'
                         '    return 0;\n}\n')
        concat, _ = emit(args.compiler, args.assembler, root, 'concat',
                         concatenating)
        assert concat_imports <= concat, (
            'a concatenating program must still import the helper dependencies; '
            f'missing: {sorted(concat_imports - concat)}')
        checks += 1
        assert concat <= runtime_abi | concat_imports, (
            'concatenation must not widen the import surface further; '
            f'unexpected: {sorted(concat - runtime_abi - concat_imports)}')
        checks += 1

        # The same program with no libc to call. Allocation is the one place
        # generated code needs memory, and on a freestanding target it asks
        # the runtime rather than the C library: the helper's three
        # dependencies become runtime hooks, and no libc name may survive --
        # a freestanding consumer satisfying calloc at all is the reverse-
        # engineered-shim problem this exists to remove.
        free_concat, _ = emit(args.compiler, args.assembler, root,
                              'free_concat', concatenating, freestanding=True)
        assert freestanding_concat_imports <= free_concat, (
            'a freestanding concatenating program must reach memory through the '
            'runtime hooks; missing: '
            f'{sorted(freestanding_concat_imports - free_concat)}')
        checks += 1
        assert not (free_concat & concat_imports), (
            'a freestanding object must import no libc symbol at all; '
            f'present: {sorted(free_concat & concat_imports)}')
        checks += 1
        assert free_concat <= runtime_abi | freestanding_concat_imports, (
            'freestanding concatenation must not widen the import surface '
            'further; unexpected: '
            f'{sorted(free_concat - runtime_abi - freestanding_concat_imports)}')
        checks += 1

        # A hosted object is entered through the C runtime's main; a
        # freestanding one is entered through a named symbol instead, because
        # a target with no C runtime has nothing to call main for it and
        # should not have to recognise a wrapper by its shape.
        assert 'main' in plain_exports and '__ny_entry' not in plain_exports, (
            f'a hosted object is entered through main; exports: {sorted(plain_exports)}')
        checks += 1
        free, free_exports = emit(args.compiler, args.assembler, root, 'free',
                                  trivial, freestanding=True)
        assert '__ny_entry' in free_exports and 'main' not in free_exports, (
            'a freestanding object is entered through __ny_entry and defines no C main; '
            f'exports: {sorted(free_exports)}')
        checks += 1
        assert free <= runtime_abi, (
            'the freestanding entry must not widen the import surface; '
            f'unexpected: {sorted(free - runtime_abi)}')
        checks += 1

        # A symbol the consumer supplies. The declared name is the symbol, so
        # it must appear in the import surface exactly as written and nothing
        # else may arrive with it -- and a declaration nobody calls must add
        # no import at all, or every consumer would owe a definition for a
        # capability the program did not use.
        host_symbol = 'anomalous_probe_emit'
        declaration = ('module host_probe\n\n'
                       '@interop("host")\n'
                       f'fn {host_symbol}(message: text) -> i64\n'
                       'end fn\n\n')
        unused, _ = emit(args.compiler, args.assembler, root, 'host_unused',
                         declaration + 'fn main() -> i64 {\n    return 0;\n}\n',
                         freestanding=True)
        assert host_symbol not in unused, (
            'a host symbol that is never called must not be imported; '
            f'imports: {sorted(unused)}')
        checks += 1
        called, _ = emit(args.compiler, args.assembler, root, 'host_called',
                         declaration + 'fn main() -> i64 {\n'
                         '    let greeting: text = "from ny";\n'
                         f'    return {host_symbol}(greeting);\n'
                         '}\n',
                         freestanding=True)
        assert host_symbol in called, (
            'a called host symbol must be imported under exactly its declared '
            f'name; imports: {sorted(called)}')
        checks += 1
        assert called <= runtime_abi | {host_symbol}, (
            'reaching a host symbol must not widen the import surface '
            f'further; unexpected: {sorted(called - runtime_abi - {host_symbol})}')
        checks += 1
        assert not (called & concat_imports), (
            'reaching a host symbol must not import a libc name; '
            f'present: {sorted(called & concat_imports)}')
        checks += 1

        # Where the object may be linked. The default small model puts every
        # address in a 32-bit absolute relocation, which a consumer placing
        # user code above 4 GiB cannot satisfy -- its link fails over the
        # string constants of a program that never asked to be low. The large
        # model removes them, and the first assertion keeps the second from
        # passing because the program had no addresses to materialize.
        emit(args.compiler, args.assembler, root, 'small', concatenating,
             freestanding=True)
        small_absolutes = absolute_relocation_count(root / 'small.o')
        assert small_absolutes > 0, (
            'the default code model is expected to use 32-bit absolute '
            'relocations; this check would otherwise prove nothing')
        checks += 1
        emit(args.compiler, args.assembler, root, 'large', concatenating,
             freestanding=True, code_model='large')
        large_absolutes = absolute_relocation_count(root / 'large.o')
        assert large_absolutes == 0, (
            'an object emitted for the large code model must have no 32-bit '
            f'absolute relocations; found {large_absolutes}')
        checks += 1

        # The red zone is a separate decision from the entry shape: a target
        # whose interrupt entry switches stacks keeps it, a kernel-reentrant
        # one cannot. So it is its own option and is checked on its own.
        hosted_ir = root / 'hosted.ll'
        emit(args.compiler, args.assembler, root, 'hosted_ir', trivial, ir=hosted_ir)
        assert definitions_without_noredzone(hosted_ir), (
            'the red zone stays available unless it is turned off')
        checks += 1
        guarded_ir = root / 'guarded.ll'
        emit(args.compiler, args.assembler, root, 'guarded_ir', trivial,
             freestanding=True, no_red_zone=True, ir=guarded_ir)
        missing = definitions_without_noredzone(guarded_ir)
        assert not missing, (
            'every generated definition must carry noredzone when it is requested; '
            f'without it: {missing}')
        checks += 1

    print(f'Native symbol surface: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
