#!/usr/bin/env python3
"""Regress structural validation of NIL Framework SDK runtime and NIL artifacts."""

from __future__ import annotations

import argparse
import importlib.util
import struct
import tempfile
from pathlib import Path
from types import ModuleType
from typing import Sequence


REQUIRED_SYMBOLS = (
    b"nyrt_abi_version",
    b"nyrt_context_begin",
    b"nyrt_context_end",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-script", required=True, type=Path)
    parser.add_argument("--validator-script", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--shared-library", type=Path)
    return parser.parse_args()


def load_script(path: Path, label: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location(label, path)
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load Python script: {}".format(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ar_member(payload: bytes, name: bytes = b"runtime.o/") -> bytes:
    fields = (
        name.ljust(16, b" "),
        b"0".ljust(12, b" "),
        b"0".ljust(6, b" "),
        b"0".ljust(6, b" "),
        b"100644".ljust(8, b" "),
        str(len(payload)).encode("ascii").ljust(10, b" "),
        b"`\n",
    )
    header = b"".join(fields)
    if len(header) != 60:
        raise AssertionError("test generated an invalid ar member header")
    padding = b"\n" if len(payload) & 1 else b""
    return header + payload + padding


def require_result(
    modules: Sequence[ModuleType], path: Path, expected: bool, scenario: str
) -> None:
    results = [module.is_static_archive(path) for module in modules]
    if results != [expected] * len(modules):
        raise AssertionError(
            "{} archive validation mismatch: expected={} results={}".format(
                scenario, expected, results
            )
        )


def require_nil_result(
    modules: Sequence[ModuleType], path: Path, expected: bool, scenario: str,
    *, version: int = 2,
) -> None:
    results = [module.is_nil_library(path) for module in modules]
    if results != [expected] * len(modules):
        raise AssertionError(
            "{} NIL validation mismatch: expected={} results={}".format(
                scenario, expected, results
            )
        )
    for module in modules:
        if module.nil_library_version(path) != (version if expected else None):
            raise AssertionError(f"{scenario}: version did not follow full validation")
        if module.is_nil_v2_library(path) != (expected and version == 2):
            raise AssertionError(f"{scenario}: legacy exact-v2 predicate changed")


def encoded_unsigned(value: int, width: int, *, little_endian: bool) -> bytes:
    return value.to_bytes(
        width, "little" if little_endian else "big", signed=False
    )


def sysv_index_payload(
    member_offset: int, *, width: int = 4, decorated: bool = False
) -> bytes:
    symbols = [
        b"_" + symbol if decorated else symbol for symbol in REQUIRED_SYMBOLS
    ]
    return (
        encoded_unsigned(len(symbols), width, little_endian=False)
        + b"".join(
            encoded_unsigned(member_offset, width, little_endian=False)
            for _ in symbols
        )
        + b"".join(symbol + b"\0" for symbol in symbols)
    )


def sysv_archive(
    *, width: int = 4, decorated: bool = False, ordinary_name: bytes = b"runtime.o/"
) -> bytes:
    index_name = b"/" if width == 4 else b"/SYM64/"
    placeholder = ar_member(
        sysv_index_payload(0, width=width, decorated=decorated), index_name
    )
    member_offset = 8 + len(placeholder)
    index = ar_member(
        sysv_index_payload(
            member_offset, width=width, decorated=decorated
        ),
        index_name,
    )
    if len(index) != len(placeholder):
        raise AssertionError("symbol offsets unexpectedly changed archive framing")
    return b"!<arch>\n" + index + ar_member(b"object payload", ordinary_name)


def bsd_index_payload(
    member_offset: int, *, width: int = 4, little_endian: bool = True
) -> bytes:
    strings = b""
    entries = b""
    for symbol in REQUIRED_SYMBOLS:
        string_offset = len(strings)
        strings += b"_" + symbol + b"\0"
        entries += encoded_unsigned(
            string_offset, width, little_endian=little_endian
        )
        entries += encoded_unsigned(
            member_offset, width, little_endian=little_endian
        )
    return (
        encoded_unsigned(len(entries), width, little_endian=little_endian)
        + entries
        + encoded_unsigned(len(strings), width, little_endian=little_endian)
        + strings
    )


def bsd_archive() -> bytes:
    placeholder = ar_member(bsd_index_payload(0), b"__.SYMDEF/")
    member_offset = 8 + len(placeholder)
    index = ar_member(bsd_index_payload(member_offset), b"__.SYMDEF/")
    if len(index) != len(placeholder):
        raise AssertionError("BSD symbol offsets unexpectedly changed archive framing")
    return b"!<arch>\n" + index + ar_member(b"object payload")


def binary_string(value: bytes) -> bytes:
    return struct.pack("<I", len(value)) + value


def nil_library_bytes(
    *,
    source_path: bytes = b"ny/test/fixture.ny",
    source_count: int = 1,
    section_overrides: dict[int, bytes] | None = None,
    version: int = 2,
    flags: int = 1,
) -> bytes:
    source_payload = struct.pack("<I", source_count)
    if source_count:
        source_payload += binary_string(source_path)
        source_payload += binary_string(b"module ny.test.fixture\n")
    source_payload += struct.pack("<I", 0)
    sections = {
        1: struct.pack("<I", 0),
        2: struct.pack("<I", 0),
        3: struct.pack("<I", 0),
        4: struct.pack("<I", 0),
        5: binary_string(b"ny.test.fixture") + struct.pack("<I", 0),
        6: source_payload,
    }
    if version == 4:
        sections[1] = (struct.pack("<I", 1) + binary_string(b"Base")
                       + struct.pack("<II", 0, 0))
        sections[2] = function_section()
        sections[7] = class_section()
    if section_overrides:
        sections.update(section_overrides)
    section_count = len(sections)
    table_end = 16 + section_count * 20
    offset = table_end
    entries = []
    payload = b""
    for section_type in sorted(sections):
        section = sections[section_type]
        entries.append(struct.pack("<IQQ", section_type, offset, len(section)))
        payload += section
        offset += len(section)
    return (
        b"NIL\0"
        + struct.pack("<III", version, flags, section_count)
        + b"".join(entries)
        + payload
    )


def function_section(*, flags: int = 0, line: int = 1,
                     receiver: bytes = b"Base", result: bytes = b"i32") -> bytes:
    instructions = b""
    for opcode, operands in ((b"STORE_LOCAL", [b"self"]), (b"PUSH_CONST", [b"7"]), (b"RET", [])):
        instructions += binary_string(opcode) + struct.pack("<I", len(operands))
        instructions += b"".join(binary_string(value) for value in operands)
        instructions += struct.pack("<I", 1)
    return (struct.pack("<I", 1) + binary_string(b"Base::read") + struct.pack("<I", 1)
            + binary_string(b"self: " + receiver) + struct.pack("<I", 0)
            + binary_string(result) + struct.pack("<III", flags, line, 0)
            + struct.pack("<I", 3) + instructions)


def class_section(*, flags: int = 0, method_flags: int = 1,
                  implementation: bytes = b"Base::read", receiver: bytes = b"Base",
                  name: bytes = b"Base", bases: tuple[bytes, ...] = (),
                  duplicate: bool = False) -> bytes:
    record = (binary_string(name) + struct.pack("<II", flags, len(bases))
              + b"".join(binary_string(base) for base in bases)
              + struct.pack("<I", 1) + binary_string(b"read")
              + binary_string(implementation) + binary_string(receiver)
              + binary_string(b"i32") + struct.pack("<II", method_flags, 0))
    return struct.pack("<I", 2 if duplicate else 1) + record * (2 if duplicate else 1)


def main() -> int:
    args = arguments()
    package_script = args.package_script.resolve(strict=True)
    validator_script = args.validator_script.resolve(strict=True)
    archive = args.archive.resolve(strict=True)
    modules = (
        load_script(package_script, "ny_package_sdk_archive_test"),
        load_script(validator_script, "ny_validate_sdk_archive_test"),
    )

    require_result(modules, archive, True, "real Ny runtime")
    candidates = ([args.shared_library.resolve(strict=True)] if args.shared_library else
                  [directory / name for directory in (archive.parent, archive.parent.parent / "bin")
                   for name in ("libnil.so", "libnil.dylib", "nil.dll") if (directory / name).is_file()])
    if len(candidates) != 1:
        raise AssertionError("artifact regression requires exactly one shared runtime alongside the archive")
    shared = candidates[0]
    if not all(module.shared_library_valid(shared) for module in modules):
        raise AssertionError("real shared runtime failed framing validation")
    smoke = modules[1].shared_runtime_smoke(shared)
    if smoke:
        raise AssertionError(smoke)
    with tempfile.TemporaryDirectory(prefix="nil-shared-artifact-") as temporary:
        malformed = Path(temporary) / shared.name
        for payload in (b"", b"!<arch>\n", shared.read_bytes()[:32], bytes(128)):
            malformed.write_bytes(payload)
            if any(module.shared_library_valid(malformed) for module in modules):
                raise AssertionError("malformed shared runtime accepted")
    actual = archive.read_bytes()
    if not actual.startswith(b"!<arch>\n"):
        raise AssertionError(
            "focused archive regression currently requires a Unix/COFF ar fixture"
        )

    with tempfile.TemporaryDirectory(prefix="nil-sdk-archive-validation-") as raw:
        root = Path(raw)

        for index, magic in enumerate((b"!<arch>\n", b"<aiaff>\n", b"<bigaf>\n")):
            header_only = root / "header-only-{}.a".format(index)
            header_only.write_bytes(magic)
            require_result(modules, header_only, False, "header-only")

        missing_symbols = root / "missing-symbols.a"
        missing_symbols.write_bytes(b"!<arch>\n" + ar_member(b"not a Ny runtime"))
        require_result(modules, missing_symbols, False, "missing ABI symbols")

        payload_forgery = root / "payload-symbol-forgery.a"
        payload_forgery.write_bytes(
            b"!<arch>\n" + ar_member(b" ".join(REQUIRED_SYMBOLS), b"fake.txt/")
        )
        require_result(
            modules, payload_forgery, False, "payload strings without a symbol index"
        )

        for name, fixture in (
            ("gnu-index.a", sysv_archive()),
            (
                "coff-decorated-index.lib",
                sysv_archive(decorated=True, ordinary_name=b"runtime.obj/"),
            ),
            ("gnu-64-index.a", sysv_archive(width=8)),
            ("bsd-index.a", bsd_archive()),
        ):
            indexed = root / name
            indexed.write_bytes(fixture)
            require_result(modules, indexed, True, name)

        forged_reference = root / "forged-index-reference.a"
        forged_reference.write_bytes(
            b"!<arch>\n"
            + ar_member(sysv_index_payload(8), b"/")
            + ar_member(b"object payload")
        )
        require_result(
            modules,
            forged_reference,
            False,
            "symbol index referencing metadata instead of a non-metadata member",
        )

        truncated_member = root / "truncated-member.a"
        complete_member = ar_member(b"nyrt_abi_version nyrt_context_begin nyrt_context_end")
        truncated_member.write_bytes(b"!<arch>\n" + complete_member[:-1])
        require_result(modules, truncated_member, False, "truncated member")

        missing_abi = root / "real-archive-missing-abi.a"
        damaged = actual.replace(b"nyrt_context_begin", b"nyrt_context_begXX")
        if damaged == actual:
            raise AssertionError("real archive does not expose nyrt_context_begin")
        missing_abi.write_bytes(damaged)
        require_result(modules, missing_abi, False, "damaged ABI symbol")

        thin = root / "thin-runtime.a"
        thin.write_bytes(b"!<thin>\n" + complete_member)
        require_result(modules, thin, False, "thin archive")

        valid_nil_bytes = nil_library_bytes()
        valid_nil = root / "valid-library.nil"
        valid_nil.write_bytes(valid_nil_bytes)
        require_nil_result(modules, valid_nil, True, "complete library")

        header_only_nil = root / "header-only-library.nil"
        header_only_nil.write_bytes(b"NIL\0" + struct.pack("<III", 2, 1, 6))
        require_nil_result(modules, header_only_nil, False, "header-only library")

        truncated_nil = root / "truncated-library.nil"
        truncated_nil.write_bytes(valid_nil_bytes[:-1])
        require_nil_result(modules, truncated_nil, False, "truncated library")

        trailing_nil = root / "trailing-library.nil"
        trailing_nil.write_bytes(valid_nil_bytes + b"\0")
        require_nil_result(modules, trailing_nil, False, "unclaimed trailing byte")

        duplicate_type_bytes = bytearray(valid_nil_bytes)
        struct.pack_into("<I", duplicate_type_bytes, 16, 2)
        duplicate_type = root / "duplicate-section.nil"
        duplicate_type.write_bytes(duplicate_type_bytes)
        require_nil_result(modules, duplicate_type, False, "duplicate section")

        outside_bytes = bytearray(valid_nil_bytes)
        struct.pack_into("<Q", outside_bytes, 20, len(outside_bytes) + 1)
        outside = root / "outside-section.nil"
        outside.write_bytes(outside_bytes)
        require_nil_result(modules, outside, False, "out-of-bounds section")

        table_end = 16 + 6 * 20
        gap_bytes = bytearray(
            valid_nil_bytes[:table_end] + b"\0" + valid_nil_bytes[table_end:]
        )
        for index in range(6):
            offset_field = 16 + index * 20 + 4
            old_offset = struct.unpack_from("<Q", gap_bytes, offset_field)[0]
            struct.pack_into("<Q", gap_bytes, offset_field, old_offset + 1)
        gap = root / "section-gap.nil"
        gap.write_bytes(gap_bytes)
        require_nil_result(modules, gap, False, "unclaimed section gap")

        section_trailing = root / "section-trailing-data.nil"
        section_trailing.write_bytes(
            nil_library_bytes(section_overrides={1: struct.pack("<I", 0) + b"x"})
        )
        require_nil_result(
            modules, section_trailing, False, "unconsumed section payload"
        )

        nonempty_reserved = root / "nonempty-reserved.nil"
        nonempty_reserved.write_bytes(
            nil_library_bytes(section_overrides={4: struct.pack("<I", 1)})
        )
        require_nil_result(
            modules, nonempty_reserved, False, "nonempty reserved section"
        )

        unsafe_source = root / "unsafe-source.nil"
        unsafe_source.write_bytes(nil_library_bytes(source_path=b"../escape.ny"))
        require_nil_result(modules, unsafe_source, False, "unsafe source path")

        missing_source = root / "missing-source.nil"
        missing_source.write_bytes(nil_library_bytes(source_count=0))
        require_nil_result(modules, missing_source, False, "empty source metadata")

        valid_v4 = root / "valid-class-library.nil"
        valid_v4.write_bytes(nil_library_bytes(version=4))
        require_nil_result(modules, valid_v4, True, "complete class library", version=4)
        valid_v4_ref = root / "valid-reference-class-library.nil"
        valid_v4_ref.write_bytes(nil_library_bytes(version=4, section_overrides={
            2: function_section(receiver=b"ref<Base>"),
            7: class_section(receiver=b"ref<Base>"),
        }))
        require_nil_result(modules, valid_v4_ref, True, "exact reference receiver", version=4)
        abstract_v4 = root / "abstract-class-library.nil"
        abstract_v4.write_bytes(nil_library_bytes(version=4, section_overrides={
            2: struct.pack("<I", 0),
            7: class_section(flags=1, method_flags=3, implementation=b""),
        }))
        require_nil_result(modules, abstract_v4, True, "abstract class declaration", version=4)

        malformed_v4 = {
            "unknown-class-flags": {7: class_section(flags=2)},
            "unknown-method-flags": {7: class_section(method_flags=4)},
            "abstract-with-body": {7: class_section(method_flags=3)},
            "abstract-not-virtual": {7: class_section(flags=1, method_flags=2, implementation=b"")},
            "concrete-without-body": {7: class_section(implementation=b"")},
            "unknown-implementation": {7: class_section(implementation=b"missing")},
            "wrong-receiver": {7: class_section(receiver=b"Other")},
            "wrong-function-receiver": {2: function_section(receiver=b"Other")},
            "wrong-function-result": {2: function_section(result=b"f64")},
            "unknown-function-flags": {2: function_section(flags=4)},
            "invalid-source-line": {2: function_section(line=0xFFFFFFFF)},
            "truncated-function-metadata": {2: function_section()[:-1]},
            "missing-schema": {1: struct.pack("<II", 0, 0)},
            "truncated-schema": {1: struct.pack("<I", 1) + binary_string(b"Base")},
            "duplicate-class": {7: class_section(duplicate=True)},
            "empty-class-section": {7: struct.pack("<I", 0)},
            "truncated-class-section": {7: class_section()[:-1]},
            "trailing-class-section": {7: class_section() + b"x"},
            "nul-class-name": {7: class_section(name=b"Base\0")},
            "missing-base": {7: class_section(bases=(b"Missing",))},
            "duplicate-base": {7: class_section(bases=(b"Base", b"Base"))},
            "cyclic-base": {7: class_section(bases=(b"Base",))},
        }
        for name, overrides in malformed_v4.items():
            path = root / (name + ".nil")
            path.write_bytes(nil_library_bytes(version=4, section_overrides=overrides))
            require_nil_result(modules, path, False, name, version=4)
        for name, payload in {
            "v4-executable-not-library": nil_library_bytes(version=4, flags=0),
            "v4-empty-source": nil_library_bytes(version=4, source_count=0),
            "v4-unsafe-source": nil_library_bytes(version=4, source_path=b"../escape.ny"),
            "v4-header-only": b"NIL\0" + struct.pack("<III", 4, 1, 7),
            "v4-unclaimed-data": nil_library_bytes(version=4) + b"x",
            "v2-with-class-section": nil_library_bytes(section_overrides={7: class_section()}),
        }.items():
            path = root / (name + ".nil")
            path.write_bytes(payload)
            require_nil_result(modules, path, False, name, version=4)
        for module in modules:
            for versions, expected in (({2}, "NIL binary library version 2"),
                                       ({4}, "NIL binary library version 4"),
                                       ({2, 4}, "NIL binary library versions 2 and 4")):
                if module.nil_library_format(versions) != expected:
                    raise AssertionError("inaccurate NIL library format label")

        validator = modules[1]
        source = root / "fixture.ny"
        source.write_text("module ny.test.fixture\n", encoding="utf-8")

        def library_manifest(artifacts: list[tuple[str, Path]], label: str) -> tuple[dict, dict]:
            files = {}
            records = []

            def file_record(key: str, path: Path) -> dict:
                files[key] = path
                return {"path": key, "bytes": path.stat().st_size,
                        "sha256": modules[0].sha256(path)}

            for name, artifact in sorted(artifacts):
                stem = name.replace(".", "/")
                records.append({"name": name,
                                "sources": [file_record("nilstd/" + stem + ".ny", source)],
                                "artifact": file_record("nillib/" + stem + ".nil", artifact),
                                "aliases": []})
            return ({"format": label, "strictly_type_checked": True,
                     "source_file_count": len(records), "logical_module_count": len(records),
                     "compiled_module_count": len(records), "artifact_file_count": len(records),
                     "modules": records}, files)

        for label, artifacts in (
            ("NIL binary library version 2", [("ny.test.fixture", valid_nil)]),
            ("NIL binary library version 4", [("ny.test.fixture", valid_v4)]),
            ("NIL binary library versions 2 and 4", [("ny.test.fixture", valid_nil),
                                                     ("ny.test.classes", valid_v4)]),
        ):
            manifest, files = library_manifest(artifacts, label)
            errors = []
            validator.validate_standard_library(manifest, files, errors)
            if errors:
                raise AssertionError(f"correct library format rejected: {label}: {errors}")
            wrong_label = "NIL binary library version 4" if label.endswith("2") else "NIL binary library version 2"
            manifest["format"] = wrong_label
            errors = []
            validator.validate_standard_library(manifest, files, errors)
            if "standard_library.format does not match its artifact versions" not in errors:
                raise AssertionError("manifest library format misrepresented artifact versions")
        structural_message = validator.validation_success_message(True, 15, 17)
        full_message = validator.validation_success_message(False, 15, 17)
        if (
            "structural validation only passed" not in structural_message
            or "execution smoke skipped" not in structural_message
        ):
            raise AssertionError(
                "--skip-smoke success did not identify its structural-only scope"
            )
        if full_message != (
            "NIL Framework SDK contract valid: 8 tools, 15 logical library modules, "
            "17 documentation pages"
        ):
            raise AssertionError("full SDK validation success wording regressed")

    print("NIL Framework SDK artifact validation regressions passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(
            "NIL Framework SDK native-runtime archive validation regression failed: {}".format(
                error
            )
        )
        raise SystemExit(1)
