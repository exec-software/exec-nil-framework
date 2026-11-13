#!/usr/bin/env python3
"""Cross-format parser controls and actual built freestanding archive contract."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_freestanding_imports import (ALLOWED_IMPORTS, REQUIRED_DEFINITIONS,
                                     freestanding_archive_imports, object_symbols)
from sdk_native_archive_validation import ar_member, sysv_index_payload, bsd_index_payload


def symbols(imports):
    return [(name, True) for name in sorted(REQUIRED_DEFINITIONS)] + [(name, False) for name in imports]


def elf(imports=(), *, machine=62, weak=False):
    strings = bytearray(b'\0')
    entries = bytearray(24)
    for name, defined in symbols(imports):
        index = len(strings)
        strings.extend(name.encode() + b'\0')
        entries.extend(struct.pack('<IBBHQQ', index, (2 if weak and not defined else 1) << 4,
                                   0, 1 if defined else 0, 0, 0))
    data = bytearray(64)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHI', data, 16, 1, machine, 1)
    data.extend(b'\xc3')
    string_start = len(data)
    data.extend(strings)
    symbol_start = len(data)
    data.extend(entries)
    section_start = len(data)
    data.extend(bytes(64))
    data.extend(struct.pack('<IIQQQQIIQQ', 0, 1, 6, 0, 64, 1, 0, 0, 1, 0))
    data.extend(struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, string_start, len(strings), 0, 0, 1, 0))
    data.extend(struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, symbol_start, len(entries), 2, 1, 8, 24))
    struct.pack_into('<Q', data, 40, section_start)
    struct.pack_into('<HHHHHH', data, 52, 64, 0, 0, 64, 4, 0)
    return bytes(data)


def coff(imports=(), *, machine=0x8664, weak=False):
    strings = bytearray(4)
    entries = bytearray()
    count = 0
    for name, defined in symbols(imports):
        encoded = name.encode()
        if len(encoded) <= 8:
            raw = encoded.ljust(8, b'\0')
        else:
            raw = struct.pack('<II', 0, len(strings))
            strings.extend(encoded + b'\0')
        is_weak = weak and not defined
        entries.extend(struct.pack('<8sIhHBB', raw, 0, 1 if defined else 0, 0,
                                   105 if is_weak else 2, 1 if is_weak else 0))
        count += 1
        if is_weak:
            entries.extend(bytes(18))
            count += 1
    struct.pack_into('<I', strings, 0, len(strings))
    data = bytearray(struct.pack('<HHIIIHH', machine, 1, 0, 61, count, 0, 0))
    data.extend(struct.pack('<8sIIIIIIHHI', b'.text\0\0\0', 0, 0, 1, 60, 0, 0, 0, 0, 0x60000020))
    data.extend(b'\xc3' + entries + strings)
    return bytes(data)


def macho(imports=(), *, machine=0x1000007, weak=False):
    strings = bytearray(b'\0')
    entries = bytearray()
    for name, defined in symbols(imports):
        index = len(strings)
        strings.extend(b'_' + name.encode() + b'\0')
        entries.extend(struct.pack('<IBBHQ', index, 3 if defined else 1, 0,
                                   0x40 if weak and not defined else 0, 0))
    data = struct.pack('<IIIIIIII', 0xfeedfacf, machine, 0, 1, 1, 24, 0, 0)
    data += struct.pack('<IIIIII', 2, 24, 56, len(entries) // 16, 56 + len(entries), len(strings))
    return data + entries + strings


def archive(obj, *, bsd=False, gnu=False, extra=b''):
    make_index = bsd_index_payload if bsd else sysv_index_payload
    index_name = b'__.SYMDEF' if bsd else b'/'
    placeholder = ar_member(make_index(0), index_name)
    names = ar_member(b'very-long-runtime-object-name.o/\n', b'//') if gnu else b''
    index = ar_member(make_index(8 + len(placeholder) + len(names)), index_name)
    if bsd:
        member_name = b'extended-runtime.o\0\0'
        member = ar_member(member_name + obj, b'#1/' + str(len(member_name)).encode())
    else:
        member = ar_member(obj, b'/0' if gnu else b'runtime.o/')
    return b'!<arch>\n' + index + names + member + extra


class Imports(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nil-freestanding-imports-')
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / 'runtime.a'

    def read(self, value):
        self.path.write_bytes(value)
        return freestanding_archive_imports(self.path)

    def test_all_six_profiles_and_archive_names(self):
        for make, machine in ((elf, 62), (elf, 183), (coff, 0x8664), (coff, 0xaa64),
                              (macho, 0x1000007), (macho, 0x100000c)):
            for naming in ({}, {'bsd': True}, {'gnu': True}):
                with self.subTest(format=make.__name__, machine=machine, naming=naming):
                    self.assertEqual(self.read(archive(make(machine=machine), **naming)), set())
                    self.assertEqual(self.read(archive(make(['memcpy', 'memset', '__ny_host_fail'], machine=machine),
                                                       **naming)), {'memcpy', 'memset', '__ny_host_fail'})

    def test_weak_import_is_not_silently_ignored(self):
        for make in (elf, coff, macho):
            with self.subTest(format=make.__name__):
                self.assertEqual(self.read(archive(make(['memcpy'], weak=True))), {'memcpy'})

    def test_definitions_resolve_imports(self):
        for make in (elf, coff, macho):
            self.assertEqual(self.read(archive(make(['nyrt_abi_version']))), set())

    def test_got_exception_applies_only_to_elf(self):
        self.assertEqual(self.read(archive(elf(['_GLOBAL_OFFSET_TABLE_']))), set())
        for make in (coff, macho):
            self.assertEqual(self.read(archive(make(['_GLOBAL_OFFSET_TABLE_']))), {'_GLOBAL_OFFSET_TABLE_'})
            self.assertNotIn('_GLOBAL_OFFSET_TABLE_', ALLOWED_IMPORTS)

    def test_incompatible_member_cannot_resolve_an_import(self):
        for second in (elf(machine=183), coff(), macho()):
            # An incompatible second member is rejected even when it does not
            # happen to resolve an import; cross-target archives are unsupported.
            with self.assertRaises(ValueError):
                self.read(archive(elf(['memcpy']), extra=ar_member(second, b'second.o/')))

    def test_external_definitions_require_real_sections(self):
        e = bytearray(elf(['memcpy']))
        section, = struct.unpack_from('<Q', e, 40)
        sym, = struct.unpack_from('<Q', e, section + 3 * 64 + 24)
        struct.pack_into('<H', e, sym + 4 * 24 + 6, 99)
        with self.assertRaises(ValueError):
            self.read(archive(e))
        m = bytearray(macho(['memcpy']))
        struct.pack_into('<BB', m, 56 + 3 * 16 + 4, 0x0f, 99)
        with self.assertRaises(ValueError):
            self.read(archive(m))

    def test_macho_removes_exactly_one_abi_underscore(self):
        self.assertEqual(self.read(archive(macho(['_memcpy', '__ny_host_fail']))), {'_memcpy', '__ny_host_fail'})

    def test_unknown_or_empty_member_is_never_ignored(self):
        for unknown in (b'', b'BC\xc0\xde' + bytes(32), b'\x00\x00\xff\xff' + bytes(32),
                        b'ordinary text', b'\xca\xfe\xba\xbe' + bytes(32)):
            with self.subTest(member=unknown[:8]):
                with self.assertRaises(ValueError):
                    self.read(archive(elf(), extra=ar_member(unknown, b'unknown.o/')))

    def test_similarly_named_index_member_cannot_hide_object(self):
        name = b'__.SYMDEF-hidden.o'
        extra = ar_member(name + b'unknown object', b'#1/' + str(len(name)).encode())
        with self.assertRaises(ValueError):
            self.read(archive(elf(), extra=extra))

    def test_truncated_objects_fail_at_every_byte_boundary(self):
        for make in (elf, coff, macho):
            obj = make(['memcpy'])
            for length in range(len(obj)):
                with self.subTest(format=make.__name__, length=length):
                    with self.assertRaises(ValueError):
                        object_symbols(obj[:length])

    def test_invalid_framing_and_empty_archive(self):
        valid = archive(elf())
        for value in (b'', b'!<arch>\n', valid[:-1], valid + b'x', b'!<thin>\n' + valid[8:]):
            with self.assertRaises(ValueError):
                self.read(value)

    def test_malformed_symbol_and_string_tables(self):
        mutations = []
        e = bytearray(elf(['memcpy']))
        section, = struct.unpack_from('<Q', e, 40)
        sym, = struct.unpack_from('<Q', e, section + 3 * 64 + 24)
        for offset, fmt, value in ((60, '<H', 0xff00), (section + 3 * 64 + 56, '<Q', 0), (section + 3 * 64 + 40, '<I', 99),
                                   (sym + 24, '<I', 0xffffffff), (sym + 24 + 6, '<H', 0xffff),
                                   (sym + 4 * 24 + 4, '<B', 0xa0)):
            changed = bytearray(e); struct.pack_into(fmt, changed, offset, value); mutations.append(changed)
        c = bytearray(coff(['memcpy']))
        for offset, fmt, value in ((8, '<I', 0xffffffff), (12, '<I', 0xffffffff), (61 + 17, '<B', 255),
                                   (61 + 4, '<I', 0xffffffff), (61 + 12, '<h', 9)):
            changed = bytearray(c); struct.pack_into(fmt, changed, offset, value); mutations.append(changed)
        m = bytearray(macho(['memcpy']))
        for offset, fmt, value in ((20, '<I', 16), (36, '<I', 8), (40, '<I', 0xffffffff),
                                   (44, '<I', 0xffffffff), (48, '<I', 0xffffffff),
                                   (56, '<I', 0xffffffff), (60, '<B', 0x0b)):
            changed = bytearray(m); struct.pack_into(fmt, changed, offset, value); mutations.append(changed)
        for index, changed in enumerate(mutations):
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.read(archive(changed))

    def test_unsupported_profiles_fail_closed(self):
        for obj in (elf(machine=3), coff(machine=0x14c), macho(machine=7)):
            with self.assertRaises(ValueError):
                self.read(archive(obj))

    def test_required_definitions_are_in_objects_not_just_index(self):
        obj = elf().replace(b'nyrt_abi_version', b'fake_abi_version')
        with self.assertRaises(ValueError):
            self.read(archive(obj))

    def test_sdk_validator_rejects_crt_imports_and_unknown_members(self):
        spec = importlib.util.spec_from_file_location('nil_freestanding_validator', ROOT / 'scripts/validate-sdk.py')
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)
        key = 'nillib/native/libnilruntime_freestanding.a'
        for make in (elf, coff, macho):
            for imports in ((), ('__ny_host_fail',), ('memcpy', 'memset')):
                self.path.write_bytes(archive(make(imports)))
                record = {'path': key, 'bytes': self.path.stat().st_size,
                          'sha256': hashlib.sha256(self.path.read_bytes()).hexdigest()}
                errors = []
                validator.validate_freestanding_runtime(record, {key: self.path}, errors)
                self.assertEqual(bool(errors), 'memcpy' in imports, errors)
            self.path.write_bytes(archive(b'unknown member'))
            record = {'path': key, 'bytes': self.path.stat().st_size,
                      'sha256': hashlib.sha256(self.path.read_bytes()).hexdigest()}
            errors = []
            validator.validate_freestanding_runtime(record, {key: self.path}, errors)
            self.assertTrue(any('import validation failed' in error for error in errors), errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Imports))
    if not result.wasSuccessful():
        return 1
    imports = freestanding_archive_imports(args.archive)
    unexpected = imports - ALLOWED_IMPORTS
    if unexpected:
        raise AssertionError(f'actual built freestanding archive imports hosted symbols: {sorted(unexpected)}')
    print(f'PASS actual built archive import surface: {sorted(imports)}; sha256={hashlib.sha256(args.archive.read_bytes()).hexdigest()}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
