#!/usr/bin/env python3
"""Bounded structural validation for binary artifacts embedded in the NIL Framework SDK.

This module deliberately performs no subprocess calls.  Packaging and
post-package validation import the same implementation so their acceptance
criteria cannot drift.  Archive validation checks framing and symbol-index
offsets; it does not interpret ordinary member payloads as host objects.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import List, Optional, Tuple


ARCHIVE_MAGIC = b"!<arch>\n"
MAXIMUM_ARCHIVE_BYTES = 512 * 1024 * 1024
MAXIMUM_ARCHIVE_MEMBERS = 65_536
MAXIMUM_ARCHIVE_SYMBOLS = 1_000_000
MAXIMUM_SYMBOL_INDEX_BYTES = 64 * 1024 * 1024
MAXIMUM_SYMBOL_INDEX_INTERPRETATIONS = 16
ARCHIVE_MEMBER_HEADER_BYTES = 60
REQUIRED_NATIVE_RUNTIME_SYMBOLS = (
    b"nyrt_abi_version",
    b"nyrt_context_begin",
    b"nyrt_context_end",
)

NIL_MAGIC = b"NIL\0"
MAXIMUM_BINARY_NIL_BYTES = 256 * 1024 * 1024
MAXIMUM_BINARY_STRING_BYTES = 16 * 1024 * 1024
MAXIMUM_FUNCTION_COUNT = 100_000
MAXIMUM_CONSTANT_COUNT = 1_000_000
MAXIMUM_PARAMETER_COUNT = 65_536
MAXIMUM_INSTRUCTION_COUNT = 1_000_000
MAXIMUM_OPERAND_COUNT = 65_536
MAXIMUM_EMBEDDED_SOURCE_COUNT = 10_000
MAXIMUM_TYPE_COUNT = 100_000
MAXIMUM_CLASS_COUNT = 100_000


def nil_library_format(versions: set[int]) -> str:
    """Describe exactly the validated versions represented by an SDK."""
    if versions == {2}:
        return "NIL binary library version 2"
    if versions == {4}:
        return "NIL binary library version 4"
    if versions == {2, 4}:
        return "NIL binary library versions 2 and 4"
    raise ValueError("unsupported or empty NIL library version set")


def _parse_decimal_field(field: bytes) -> Optional[int]:
    value = field.strip(b" ")
    if not value or not value.isdigit():
        return None
    parsed = int(value)
    return parsed if parsed <= (1 << 64) - 1 else None


def _read_unsigned(
    data: bytes, offset: int, width: int, *, little_endian: bool
) -> int:
    return int.from_bytes(
        data[offset : offset + width],
        byteorder="little" if little_endian else "big",
        signed=False,
    )


def _required_symbol_index(symbol: bytes) -> Optional[int]:
    # Mach-O and 32-bit COFF decorate external C identifiers with one leading
    # underscore.  Archive indexes contain the decorated spelling.
    if symbol.startswith(b"_"):
        symbol = symbol[1:]
    try:
        return REQUIRED_NATIVE_RUNTIME_SYMBOLS.index(symbol)
    except ValueError:
        return None


ArchiveIndex = Tuple[List[int], List[int], List[int]]


def _empty_archive_index() -> ArchiveIndex:
    return ([], [], [])


def _parse_sysv_symbol_index(
    payload: bytes, integer_width: int
) -> Optional[ArchiveIndex]:
    if integer_width not in (4, 8) or len(payload) < integer_width:
        return None
    count = _read_unsigned(payload, 0, integer_width, little_endian=False)
    maximum_count = (len(payload) - integer_width) // integer_width
    if count > maximum_count or count > MAXIMUM_ARCHIVE_SYMBOLS:
        return None
    names_begin = integer_width + count * integer_width
    result = _empty_archive_index()
    cursor = names_begin
    for index in range(count):
        terminator = payload.find(b"\0", cursor)
        if terminator == -1:
            return None
        required = _required_symbol_index(payload[cursor:terminator])
        if required is not None:
            result[required].append(
                _read_unsigned(
                    payload,
                    integer_width + index * integer_width,
                    integer_width,
                    little_endian=False,
                )
            )
        cursor = terminator + 1
    return result


def _parse_bsd_symbol_index(
    payload: bytes, integer_width: int, *, little_endian: bool
) -> Optional[ArchiveIndex]:
    if integer_width not in (4, 8) or len(payload) < integer_width * 2:
        return None
    ranlib_bytes = _read_unsigned(
        payload, 0, integer_width, little_endian=little_endian
    )
    entry_bytes = integer_width * 2
    if ranlib_bytes % entry_bytes or ranlib_bytes > len(payload) - integer_width:
        return None
    string_size_offset = integer_width + ranlib_bytes
    if string_size_offset > len(payload) - integer_width:
        return None
    string_bytes = _read_unsigned(
        payload, string_size_offset, integer_width, little_endian=little_endian
    )
    strings_begin = string_size_offset + integer_width
    if string_bytes > len(payload) - strings_begin:
        return None
    strings_end = strings_begin + string_bytes
    entry_count = ranlib_bytes // entry_bytes
    if entry_count > MAXIMUM_ARCHIVE_SYMBOLS:
        return None

    result = _empty_archive_index()
    for index in range(entry_count):
        entry = integer_width + index * entry_bytes
        string_offset = _read_unsigned(
            payload, entry, integer_width, little_endian=little_endian
        )
        if string_offset >= string_bytes:
            return None
        symbol_begin = strings_begin + string_offset
        terminator = payload.find(b"\0", symbol_begin, strings_end)
        if terminator == -1:
            return None
        required = _required_symbol_index(payload[symbol_begin:terminator])
        if required is not None:
            result[required].append(
                _read_unsigned(
                    payload,
                    entry + integer_width,
                    integer_width,
                    little_endian=little_endian,
                )
            )
    return result


def _normalized_archive_member_name(raw_name: bytes) -> bytes:
    name = raw_name.rstrip(b" ")
    if len(name) > 1 and name.endswith(b"/") and name not in (b"//", b"/SYM64/"):
        name = name[:-1]
    return name


def _is_archive_index_name(name: bytes) -> bool:
    return name in (b"/", b"/SYM64/") or name.startswith(b"__.SYMDEF")


def _index_defines_runtime(index: ArchiveIndex, ordinary_offsets: set[int]) -> bool:
    return all(
        any(offset in ordinary_offsets for offset in definitions)
        for definitions in index
    )


def is_static_archive(path: Path) -> bool:
    """Validate bounded ar framing and required symbol-index member offsets."""
    try:
        archive_size = path.stat().st_size
        if archive_size <= len(ARCHIVE_MAGIC) or archive_size > MAXIMUM_ARCHIVE_BYTES:
            return False
        with path.open("rb") as stream:
            if stream.read(len(ARCHIVE_MAGIC)) != ARCHIVE_MAGIC:
                # AIX small/big archives use different containers and indexes.
                # Reject them until those formats have an equally strict parser.
                return False

            position = len(ARCHIVE_MAGIC)
            member_count = 0
            ordinary_offsets: set[int] = set()
            indexes: list[ArchiveIndex] = []
            while position < archive_size:
                member_count += 1
                if member_count > MAXIMUM_ARCHIVE_MEMBERS:
                    return False
                if archive_size - position < ARCHIVE_MEMBER_HEADER_BYTES:
                    return False
                stream.seek(position)
                header = stream.read(ARCHIVE_MEMBER_HEADER_BYTES)
                if (
                    len(header) != ARCHIVE_MEMBER_HEADER_BYTES
                    or header[58:60] != b"`\n"
                ):
                    return False
                member_size = _parse_decimal_field(header[48:58])
                if member_size is None:
                    return False
                data_position = position + ARCHIVE_MEMBER_HEADER_BYTES
                if member_size > archive_size - data_position:
                    return False

                member_name = _normalized_archive_member_name(header[:16])
                name_bytes = 0
                if member_name.startswith(b"#1/"):
                    name_bytes = _parse_decimal_field(member_name[3:]) or 0
                    if (
                        name_bytes == 0
                        or name_bytes > member_size
                        or name_bytes > 65_536
                    ):
                        return False
                    stream.seek(data_position)
                    extended_name = stream.read(name_bytes)
                    if len(extended_name) != name_bytes:
                        return False
                    member_name = extended_name.rstrip(b"\0")
                    if len(member_name) > 1 and member_name.endswith(b"/"):
                        member_name = member_name[:-1]

                payload_position = data_position + name_bytes
                payload_size = member_size - name_bytes
                is_index = _is_archive_index_name(member_name)
                if not is_index and member_name != b"//" and payload_size:
                    ordinary_offsets.add(position)

                if is_index and payload_size <= MAXIMUM_SYMBOL_INDEX_BYTES:
                    stream.seek(payload_position)
                    payload = stream.read(payload_size)
                    if len(payload) != payload_size:
                        return False
                    parsed_indexes: list[ArchiveIndex] = []
                    if member_name == b"/":
                        parsed = _parse_sysv_symbol_index(payload, 4)
                        if parsed is not None:
                            parsed_indexes.append(parsed)
                    elif member_name == b"/SYM64/":
                        parsed = _parse_sysv_symbol_index(payload, 8)
                        if parsed is not None:
                            parsed_indexes.append(parsed)
                    else:
                        for width in (4, 8):
                            for little_endian in (True, False):
                                parsed = _parse_bsd_symbol_index(
                                    payload, width, little_endian=little_endian
                                )
                                if parsed is not None:
                                    parsed_indexes.append(parsed)
                    if (
                        len(indexes) + len(parsed_indexes)
                        > MAXIMUM_SYMBOL_INDEX_INTERPRETATIONS
                    ):
                        return False
                    indexes.extend(parsed_indexes)

                position = data_position + member_size
                if member_size & 1:
                    if position >= archive_size:
                        return False
                    stream.seek(position)
                    if stream.read(1) != b"\n":
                        return False
                    position += 1
    except (OSError, OverflowError, ValueError):
        return False

    return bool(ordinary_offsets) and any(
        _index_defines_runtime(index, ordinary_offsets) for index in indexes
    )


class _NilReadError(ValueError):
    pass


class _NilCursor:
    def __init__(self, data: bytes, begin: int, end: int):
        if begin > end or end > len(data):
            raise _NilReadError("invalid cursor bounds")
        self._data = data
        self.position = begin
        self.end = end

    def take(self, count: int) -> bytes:
        if count < 0 or count > self.end - self.position:
            raise _NilReadError("truncated field")
        result = self._data[self.position : self.position + count]
        self.position += count
        return result

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def string(self) -> bytes:
        size = self.u32()
        if size > MAXIMUM_BINARY_STRING_BYTES:
            raise _NilReadError("string exceeds 16 MiB")
        return self.take(size)

    def count(self, maximum: int) -> int:
        value = self.u32()
        if value > maximum:
            raise _NilReadError("count exceeds safety limit")
        return value

    def require_consumed(self) -> None:
        if self.position != self.end:
            raise _NilReadError("section contains trailing data")


def _unsafe_source_path(path: bytes) -> bool:
    if not path or b"\0" in path or b"\\" in path:
        return True
    if (
        path.startswith(b"/")
        or b"//" in path
        or path.endswith(b"/")
        or (len(path) >= 2 and path[1:2] == b":")
    ):
        return True
    return any(component in (b"", b".", b"..") for component in path.split(b"/"))


def _nil_name(cursor: _NilCursor, subject: str, *, empty: bool = False) -> bytes:
    value = cursor.string()
    if (not value and not empty) or b"\0" in value:
        raise _NilReadError("invalid " + subject)
    return value


def _parse_nil_library_section(
    section_type: int, cursor: _NilCursor, version: int, records: dict
) -> None:
    if section_type == 1:
        if version == 2:
            if cursor.u32() != 0:
                raise _NilReadError("reserved type records are nonempty")
        else:
            names: set[bytes] = set()
            total_members = 0
            for _ in range(cursor.count(MAXIMUM_TYPE_COUNT)):
                name = _nil_name(cursor, "struct name")
                if name in names:
                    raise _NilReadError("duplicate schema name")
                names.add(name)
                fields: set[bytes] = set()
                field_count = cursor.count(MAXIMUM_PARAMETER_COUNT)
                total_members += field_count
                if total_members > MAXIMUM_INSTRUCTION_COUNT:
                    raise _NilReadError("total type member count exceeds safety limit")
                for _ in range(field_count):
                    field = _nil_name(cursor, "field name")
                    _nil_name(cursor, "field type")
                    if field in fields:
                        raise _NilReadError("duplicate field name")
                    fields.add(field)
                records["structs"].add(name)
            for _ in range(cursor.count(MAXIMUM_TYPE_COUNT)):
                name = _nil_name(cursor, "enum name")
                if name in names:
                    raise _NilReadError("duplicate schema name")
                names.add(name)
                variants: set[bytes] = set()
                variant_count = cursor.count(MAXIMUM_PARAMETER_COUNT)
                total_members += variant_count
                if total_members > MAXIMUM_INSTRUCTION_COUNT:
                    raise _NilReadError("total type member count exceeds safety limit")
                for _ in range(variant_count):
                    variant = _nil_name(cursor, "variant name")
                    _nil_name(cursor, "variant type", empty=True)
                    if variant in variants:
                        raise _NilReadError("duplicate variant name")
                    variants.add(variant)
    elif section_type == 2:
        function_count = cursor.count(MAXIMUM_FUNCTION_COUNT)
        total_instructions = 0
        total_metadata_records = 0
        for _ in range(function_count):
            name = _nil_name(cursor, "function name")
            if name in records["functions"]:
                raise _NilReadError("duplicate function name")
            parameter_count = cursor.count(MAXIMUM_PARAMETER_COUNT)
            total_metadata_records += parameter_count
            if total_metadata_records > MAXIMUM_INSTRUCTION_COUNT:
                raise _NilReadError("total function metadata exceeds safety limit")
            parameters = [cursor.string() for _ in range(parameter_count)]
            generic_count = cursor.count(MAXIMUM_PARAMETER_COUNT)
            total_metadata_records += generic_count
            if total_metadata_records > MAXIMUM_INSTRUCTION_COUNT:
                raise _NilReadError("total function metadata exceeds safety limit")
            for _ in range(generic_count):
                cursor.string()
            result = cursor.string()
            records["functions"][name] = (parameters, result)
            if version == 4:
                if cursor.u32() & ~3:
                    raise _NilReadError("unknown function metadata flags")
                if cursor.u32() > 0x7FFFFFFF:
                    raise _NilReadError("source line exceeds INT_MAX")
                annotation_count = cursor.count(MAXIMUM_PARAMETER_COUNT)
                total_metadata_records += annotation_count
                if total_metadata_records > MAXIMUM_INSTRUCTION_COUNT:
                    raise _NilReadError("total function metadata exceeds safety limit")
                for _ in range(annotation_count):
                    cursor.string()
            instruction_count = cursor.count(MAXIMUM_INSTRUCTION_COUNT)
            total_instructions += instruction_count
            if total_instructions > MAXIMUM_INSTRUCTION_COUNT:
                raise _NilReadError("total instruction count exceeds safety limit")
            for _ in range(instruction_count):
                cursor.string()
                operand_count = cursor.count(MAXIMUM_OPERAND_COUNT)
                for _ in range(operand_count):
                    cursor.string()
                if cursor.u32() > 0x7FFFFFFF:
                    raise _NilReadError("debug line exceeds INT_MAX")
    elif section_type == 3:
        constant_count = cursor.count(MAXIMUM_CONSTANT_COUNT)
        for _ in range(constant_count):
            if not cursor.string():
                raise _NilReadError("empty constant name")
            cursor.string()
    elif section_type == 4:
        if cursor.u32() != 0:
            raise _NilReadError("reserved symbol records are nonempty")
    elif section_type == 5:
        if not cursor.string():
            raise _NilReadError("empty module name")
        test_count = cursor.count(MAXIMUM_FUNCTION_COUNT)
        for _ in range(test_count):
            cursor.string()
    elif section_type == 6:
        source_count = cursor.count(MAXIMUM_EMBEDDED_SOURCE_COUNT)
        source_paths: set[bytes] = set()
        for _ in range(source_count):
            source_path = cursor.string()
            source_contents = cursor.string()
            if (
                _unsafe_source_path(source_path)
                or b"\0" in source_contents
                or source_path in source_paths
            ):
                raise _NilReadError("unsafe or duplicate source entry")
            source_paths.add(source_path)
        dependency_count = cursor.count(MAXIMUM_FUNCTION_COUNT)
        dependencies: set[bytes] = set()
        for _ in range(dependency_count):
            dependency = cursor.string()
            if not dependency or dependency in dependencies:
                raise _NilReadError("invalid or duplicate dependency")
            dependencies.add(dependency)
        if not source_paths:
            raise _NilReadError("source metadata is empty")
    elif section_type == 7 and version == 4:
        class_count = cursor.count(MAXIMUM_CLASS_COUNT)
        if class_count == 0:
            raise _NilReadError("v4 library has no class records")
        total_members = 0
        for _ in range(class_count):
            name = _nil_name(cursor, "class name")
            if name in records["classes"]:
                raise _NilReadError("duplicate class name")
            abstract = cursor.u32()
            if abstract > 1:
                raise _NilReadError("unknown class flags")
            bases = [_nil_name(cursor, "base class")
                     for _ in range(cursor.count(MAXIMUM_CLASS_COUNT))]
            total_members += len(bases)
            if len(bases) != len(set(bases)):
                raise _NilReadError("duplicate base class")
            methods = {}
            method_count = cursor.count(MAXIMUM_FUNCTION_COUNT)
            total_members += method_count
            if total_members > MAXIMUM_INSTRUCTION_COUNT:
                raise _NilReadError("total class member count exceeds safety limit")
            for _ in range(method_count):
                method = _nil_name(cursor, "method name")
                implementation = _nil_name(cursor, "method implementation", empty=True)
                receiver = _nil_name(cursor, "method receiver")
                result = _nil_name(cursor, "method result")
                flags = cursor.u32()
                parameters = [_nil_name(cursor, "method parameter")
                              for _ in range(cursor.count(MAXIMUM_PARAMETER_COUNT))]
                total_members += len(parameters)
                if total_members > MAXIMUM_INSTRUCTION_COUNT:
                    raise _NilReadError("total class member count exceeds safety limit")
                if method in methods or flags & ~3:
                    raise _NilReadError("duplicate method or unknown method flags")
                is_abstract = bool(flags & 2)
                if is_abstract and (not flags & 1 or implementation) or not is_abstract and not implementation:
                    raise _NilReadError("inconsistent abstract method metadata")
                if receiver not in (name, b"ref<" + name + b">"):
                    raise _NilReadError("method receiver is not its declaring owner")
                methods[method] = (implementation, receiver, result, parameters)
            records["classes"][name] = (bases, methods)
    else:
        raise _NilReadError("unknown section type")
    cursor.require_consumed()


def nil_library_version(path: Path) -> Optional[int]:
    """Return 2 or 4 only after complete bounded library-container validation.

    This is structural validation, not a replacement for the compiler's NIL
    instruction/provenance verifier or execution qualification.
    """
    try:
        size = path.stat().st_size
        if size < 16 or size > MAXIMUM_BINARY_NIL_BYTES:
            return None
        with path.open("rb") as stream:
            data = stream.read(MAXIMUM_BINARY_NIL_BYTES + 1)
        if (
            len(data) != size
            or len(data) > MAXIMUM_BINARY_NIL_BYTES
            or data[:4] != NIL_MAGIC
        ):
            return None
        version, flags, section_count = struct.unpack_from("<III", data, 4)
        if version not in (2, 4) or flags != 1 or section_count != (7 if version == 4 else 6):
            return None
        table_end = 16 + section_count * 20
        if table_end > len(data):
            return None

        sections: dict[int, tuple[int, int]] = {}
        ranges: list[tuple[int, int]] = []
        for index in range(section_count):
            section_type, offset, section_size = struct.unpack_from(
                "<IQQ", data, 16 + index * 20
            )
            if section_type not in range(1, section_count + 1) or section_type in sections:
                return None
            if (
                section_size == 0
                or offset < table_end
                or offset > len(data)
                or section_size > len(data) - offset
            ):
                return None
            sections[section_type] = (offset, section_size)
            ranges.append((offset, offset + section_size))
        if set(sections) != set(range(1, section_count + 1)):
            return None
        ranges.sort()
        if ranges[0][0] != table_end or ranges[-1][1] != len(data):
            return None
        if any(
            previous[1] != current[0]
            for previous, current in zip(ranges, ranges[1:])
        ):
            return None

        records: dict = {"structs": set(), "functions": {}, "classes": {}}
        for section_type in range(1, section_count + 1):
            offset, section_size = sections[section_type]
            _parse_nil_library_section(
                section_type, _NilCursor(data, offset, offset + section_size), version, records
            )
        if version == 4:
            # Check declaration references without attempting to reimplement
            # the shared C++ instruction/provenance validator in Python.
            class_names = set(records["classes"])
            descendants: dict[bytes, list[bytes]] = {name: [] for name in class_names}
            unresolved_bases: dict[bytes, int] = {}
            for name, (bases, methods) in records["classes"].items():
                if name not in records["structs"] or any(base not in class_names for base in bases):
                    raise _NilReadError("class refers to a missing schema or base")
                unresolved_bases[name] = len(bases)
                for base in bases:
                    descendants[base].append(name)
                for implementation, receiver, result, parameters in methods.values():
                    if not implementation:
                        continue
                    signature = records["functions"].get(implementation)
                    if signature is None:
                        raise _NilReadError("class implementation function is missing")
                    actual_parameters, actual_result = signature
                    types = []
                    for parameter in actual_parameters:
                        # Split the declaration colon, not a module qualifier.
                        delimiter = parameter.find(b": ")
                        if delimiter < 0:
                            raise _NilReadError("class implementation lacks exact parameter types")
                        types.append(parameter[delimiter + 2:].strip())
                    if types != [receiver, *parameters] or actual_result != result:
                        raise _NilReadError("class implementation signature mismatch")
            ready = [name for name, count in unresolved_bases.items() if count == 0]
            resolved_count = 0
            while ready:
                name = ready.pop()
                resolved_count += 1
                for descendant in descendants[name]:
                    unresolved_bases[descendant] -= 1
                    if unresolved_bases[descendant] == 0:
                        ready.append(descendant)
            if resolved_count != len(class_names):
                raise _NilReadError("cyclic class ancestry")
    except (OSError, OverflowError, struct.error, _NilReadError, ValueError):
        return None
    return version


def is_nil_library(path: Path) -> bool:
    return nil_library_version(path) is not None


def is_nil_v2_library(path: Path) -> bool:
    """Legacy exact-version predicate retained for existing SDK tooling."""
    return nil_library_version(path) == 2
