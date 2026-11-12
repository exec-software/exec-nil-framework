#!/usr/bin/env python3
"""Read freestanding archive imports without requiring nm on an SDK consumer.

Supported objects are ELF64LE, ordinary 64-bit COFF, and thin Mach-O64LE,
for x86_64/arm64. Unknown members (including bitcode/import objects), malformed
symbol tables, and unsupported encodings fail closed. This is an import-surface
check, not an executable loader or a replacement for native link/run contracts.
"""
from __future__ import annotations

import struct
from pathlib import Path

from sdk_artifact_validation import (
    ARCHIVE_MAGIC, MAXIMUM_ARCHIVE_BYTES, MAXIMUM_ARCHIVE_MEMBERS,
    MAXIMUM_ARCHIVE_SYMBOLS, is_static_archive,
)

ALLOWED_IMPORTS = frozenset({'__ny_host_fail'})
REQUIRED_DEFINITIONS = frozenset({'nyrt_abi_version', 'nyrt_context_begin', 'nyrt_context_end'})
INDEX_NAMES = frozenset({b'/', b'/SYM64/', b'__.SYMDEF', b'__.SYMDEF SORTED',
                        b'__.SYMDEF_64', b'__.SYMDEF_64 SORTED'})


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def extent(data: bytes, start: int, size: int) -> bytes:
    require(0 <= start <= len(data) and 0 <= size <= len(data) - start,
            'object table extends beyond its member')
    return data[start:start + size]


def name_at(table: bytes, index: int) -> str:
    require(0 <= index < len(table), 'symbol name offset outside its string table')
    end = table.find(b'\0', index)
    require(end != -1, 'unterminated symbol name')
    return table[index:end].decode('utf-8', 'strict')


def elf_symbols(data: bytes) -> tuple[set[str], set[str]]:
    require(len(data) >= 64 and data[:7] == b'\x7fELF\x02\x01\x01', 'unsupported ELF encoding')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    require(kind == 1 and machine in (62, 183) and version == 1, 'unsupported ELF object profile')
    offset, = struct.unpack_from('<Q', data, 40)
    header_size, = struct.unpack_from('<H', data, 52)
    stride, count = struct.unpack_from('<HH', data, 58)
    require(header_size == 64 and stride == 64 and 0 < count < 0xff00,
            'unsupported ELF section table')
    extent(data, offset, count * stride)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * stride) for i in range(count)]
    undefined, defined = set(), set()
    found = 0
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB, not names which an object may rename.
            continue
        found += 1
        start, size, link, entry_size = section[4], section[5], section[6], section[9]
        require(entry_size == 24 and size % entry_size == 0 and size // entry_size <= MAXIMUM_ARCHIVE_SYMBOLS,
                'invalid ELF symbol table size')
        extent(data, start, size)
        require(link < count and sections[link][1] == 3, 'ELF symbol table has no string table')
        strings = extent(data, sections[link][4], sections[link][5])
        for position in range(start, start + size, entry_size):
            index, info, _other, shndx, _value, _size = struct.unpack_from('<IBBHQQ', data, position)
            if info >> 4 not in (1, 2):  # STB_GLOBAL/STB_WEAK, including weak imports.
                continue
            require(shndx < count or shndx in (0xfff1, 0xfff2),
                    'unsupported or out-of-range external ELF symbol section')
            name = name_at(strings, index)
            require(bool(name), 'empty external ELF symbol')
            (undefined if shndx == 0 else defined).add(name)
    require(found == 1, 'expected exactly one ELF symbol table')
    return undefined, defined


def coff_symbols(data: bytes) -> tuple[set[str], set[str]]:
    require(len(data) >= 20, 'truncated COFF header')
    machine, sections, _time, start, count, optional, flags = struct.unpack_from('<HHIIIHH', data)
    require(machine in (0x8664, 0xaa64) and sections > 0 and optional == 0 and not flags & 2,
            'unsupported COFF object profile')
    extent(data, 20, sections * 40)
    require(start >= 20 + sections * 40 and 0 < count <= MAXIMUM_ARCHIVE_SYMBOLS,
            'invalid COFF symbol table')
    extent(data, start, count * 18 + 4)
    string_start = start + count * 18
    string_size, = struct.unpack_from('<I', data, string_start)
    require(string_size >= 4, 'invalid COFF string table')
    strings = extent(data, string_start, string_size)
    undefined, defined = set(), set()
    index = 0
    while index < count:
        position = start + index * 18
        raw_name, value, section, _type, storage, auxiliaries = struct.unpack_from('<8sIhHBB', data, position)
        require(auxiliaries < count - index, 'COFF auxiliary symbols exceed table')
        if storage in (2, 105):  # IMAGE_SYM_CLASS_EXTERNAL / WEAK_EXTERNAL.
            if raw_name[:4] == b'\0' * 4:
                name_offset, = struct.unpack_from('<I', raw_name, 4)
                require(name_offset >= 4, 'COFF symbol references string-table length')
                name = name_at(strings, name_offset)
            else:
                name = raw_name.split(b'\0', 1)[0].decode('utf-8', 'strict')
            require(bool(name) and -1 <= section <= sections, 'invalid external COFF symbol')
            # A nonzero value with section zero denotes common storage.
            (undefined if section == 0 and value == 0 else defined).add(name)
        index += 1 + auxiliaries
    return undefined, defined


def macho_symbols(data: bytes) -> tuple[set[str], set[str]]:
    require(len(data) >= 32, 'truncated Mach-O header')
    magic, cpu, _subtype, kind, commands, size, _flags, _reserved = struct.unpack_from('<IIIIIIII', data)
    require(magic == 0xfeedfacf and cpu in (0x1000007, 0x100000c) and kind == 1,
            'unsupported Mach-O object profile')
    require(0 < commands <= size // 8, 'invalid Mach-O load command count')
    extent(data, 32, size)
    end = 32 + size
    position = 32
    symtab = None
    sections = 0
    for _ in range(commands):
        require(position <= end - 8, 'truncated Mach-O load command')
        command, length = struct.unpack_from('<II', data, position)
        require(length >= 8 and length % 8 == 0 and length <= end - position,
                'invalid Mach-O load command size')
        if command == 2:  # LC_SYMTAB
            require(length == 24 and symtab is None, 'invalid or duplicate Mach-O symbol table')
            symtab = struct.unpack_from('<IIII', data, position + 8)
        elif command == 0x19:  # LC_SEGMENT_64; n_sect indexes these sections.
            require(length >= 72, 'truncated Mach-O segment command')
            count, = struct.unpack_from('<I', data, position + 64)
            require(count <= 255 and length == 72 + count * 80,
                    'invalid Mach-O segment section count')
            sections += count
            require(sections <= 255, 'too many Mach-O sections')
        position += length
    require(position == end and symtab is not None, 'missing Mach-O symbol table')
    start, count, string_start, string_size = symtab
    require(0 < count <= MAXIMUM_ARCHIVE_SYMBOLS, 'invalid Mach-O symbol count')
    extent(data, start, count * 16)
    strings = extent(data, string_start, string_size)
    undefined, defined = set(), set()
    for index in range(count):
        name_index, kind, section, _desc, value = struct.unpack_from('<IBBHQ', data, start + index * 16)
        if kind & 0xe0 or not kind & 1:  # N_STAB / N_EXT.
            continue
        symbol_type = kind & 0x0e
        require(symbol_type in (0, 2, 0x0e), 'unsupported external Mach-O symbol type')
        require(0 < section <= sections if symbol_type == 0x0e else section == 0,
                'invalid external Mach-O symbol section')
        raw_name = name_at(strings, name_index)
        require(raw_name.startswith('_') and len(raw_name) > 1, 'missing Mach-O C ABI underscore')
        name = raw_name[1:]  # Exactly one ABI underscore; preserve language underscores.
        (undefined if symbol_type == 0 and value == 0 else defined).add(name)
    return undefined, defined


def object_symbols(data: bytes) -> tuple[set[str], set[str]]:
    if data.startswith(b'\x7fELF'):
        return elf_symbols(data)
    if data.startswith(b'\xcf\xfa\xed\xfe'):
        return macho_symbols(data)
    if data[:2] in (b'\x64\x86', b'\x64\xaa'):
        return coff_symbols(data)
    raise ValueError('unsupported archive member object format')


def freestanding_archive_imports(archive: Path) -> set[str]:
    """Return unresolved external names, or raise on any uninspected member."""
    require(is_static_archive(archive), 'invalid archive framing or runtime symbol index')
    # Bound the read itself as well as the earlier stat, including a replaced file.
    with archive.open('rb') as stream:
        data = stream.read(MAXIMUM_ARCHIVE_BYTES + 1)
    require(len(data) <= MAXIMUM_ARCHIVE_BYTES and data.startswith(ARCHIVE_MAGIC), 'invalid archive size/magic')
    undefined, defined = set(), set()
    offset = len(ARCHIVE_MAGIC)
    members = 0
    objects = 0
    profile = None
    while offset < len(data):
        members += 1
        require(members <= MAXIMUM_ARCHIVE_MEMBERS, 'too many archive members')
        header = extent(data, offset, 60)
        require(header[58:] == b'`\n' and header[48:58].strip().isdigit(), 'invalid archive member header')
        size = int(header[48:58].strip())
        body = extent(data, offset + 60, size)
        name = header[:16].rstrip(b' ')
        if name.startswith(b'#1/'):
            require(name[3:].isdigit(), 'invalid BSD extended member name')
            length = int(name[3:])
            require(0 < length <= min(size, 65536), 'invalid BSD member name length')
            name, body = body[:length].rstrip(b'\0'), body[length:]
        if len(name) > 1 and name.endswith(b'/') and name not in (b'//', b'/SYM64/'):
            name = name[:-1]
        if name not in INDEX_NAMES and name != b'//':
            imports, exports = object_symbols(body)
            if body.startswith(b'\x7fELF'):
                member_profile = ('ELF', struct.unpack_from('<H', body, 18)[0])
                # ELF's GOT base is synthesized by its linker, not a host ABI
                # requirement. That exception does not apply to COFF/Mach-O.
                imports.discard('_GLOBAL_OFFSET_TABLE_')
            elif body.startswith(b'\xcf\xfa\xed\xfe'):
                member_profile = ('Mach-O', struct.unpack_from('<I', body, 4)[0])
            else:
                member_profile = ('COFF', struct.unpack_from('<H', body)[0])
            require(profile is None or profile == member_profile,
                    'mixed object format/architecture in freestanding archive')
            profile = member_profile
            undefined.update(imports)
            defined.update(exports)
            require(len(undefined) + len(defined) <= MAXIMUM_ARCHIVE_SYMBOLS,
                    'too many archive external symbols')
            objects += 1
        offset += 60 + size
        if size & 1:
            require(extent(data, offset, 1) == b'\n', 'invalid archive alignment padding')
            offset += 1
    require(objects > 0 and REQUIRED_DEFINITIONS <= defined,
            'archive members do not define the required runtime ABI')
    return undefined - defined
