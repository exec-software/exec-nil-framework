#!/usr/bin/env python3
"""Versioned class artifacts must be accepted completely or rejected strictly."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name: str, path: Path):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    sys.modules[name] = module
    specification.loader.exec_module(module)
    return module


routes = load("ny_route_artifact_matrix", ROOT / "tests/routes/ny_route_matrix.py")
fixture = load("ny_route_artifact_fixture", ROOT / "tests/tools/native_class_contract.py")


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def string(value: str) -> bytes:
    data = value.encode()
    return u32(len(data)) + data


def records(artifact: bytes) -> list[tuple[int, bytes]]:
    count = struct.unpack_from("<I", artifact, 12)[0]
    result = []
    for index in range(count):
        kind, offset, size = struct.unpack_from("<IQQ", artifact, 16 + index * 20)
        result.append((kind, artifact[offset:offset + size]))
    return result


def container(sections: list[tuple[int, bytes]], version: int = 4, flags: int = 0) -> bytes:
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return b"NIL\0" + struct.pack("<III", version, flags, len(sections)) + table + b"".join(
        payload for _, payload in sections)


class ClassArtifactTests(unittest.TestCase):
    def binary(self, data: bytes, valid: bool):
        actual, errors, metadata = routes.validate_binary_nil_bytes(data)
        self.assertEqual(actual, valid, errors)
        if not valid:
            self.assertTrue(errors)
        return metadata

    def text(self, data: bytes, valid: bool):
        actual, errors, metadata = routes.validate_text_nil_bytes(b"# NIL Textual Module\n" + data)
        self.assertEqual(actual, valid, errors)
        return metadata

    def test_valid_text_and_binary_classes(self):
        self.assertEqual(self.binary(fixture.binary_module(fixture.FUNCTIONS), True)["classes"],
                         ["Extra", "Base", "Derived"])
        self.assertEqual(self.text(fixture.textual_module(fixture.FUNCTIONS), True)["classes"],
                         ["Extra", "Base", "Derived"])

    def test_valid_version_four_library(self):
        sections = records(fixture.binary_module(fixture.FUNCTIONS[:-1]))
        source = u32(1) + string("module.ny") + string("module classes\n") + u32(0)
        metadata = self.binary(container([*sections, (6, source)], flags=1), True)
        self.assertEqual(metadata["artifact_kind"], "library")
        self.assertEqual(metadata["source_paths"], ["module.ny"])

    def test_quoted_reference_and_spaced_generic_method_types(self):
        self.text(b'''module generic_method
struct Pair<i32, i64>
left: i32
right: i64
end struct
struct Holder
pair: Pair<i32, i64>
end struct
class Holder 0
method identity Holder::identity "ref<Holder>" "Pair<i32, i64>" 1 0 1 "Pair<i32, i64>" ; exact types
end class
function Holder::identity(self: ref<Holder>, pair: Pair<i32, i64>) -> Pair<i32, i64>:
STORE_LOCAL pair
STORE_LOCAL self
LOAD_LOCAL pair
RET
end function
function main() -> i32:
PUSH_CONST 0
RET
end function
''', True)

    def test_quoted_operand_boundaries_and_escapes(self):
        self.assertEqual(routes._textual_record_operands(r'"ref<Base>" "Pair<i32, i64>", 0 ; comment'),
                         ["ref<Base>", "Pair<i32, i64>", "0"])
        self.assertEqual(routes._textual_record_operands(r'"a\\b\"c"'), ['a\\b"c'])
        valid = fixture.textual_module(fixture.FUNCTIONS)
        for replacement in (b'"Base', b'"Base"garbage', b'"Ba\\0se"', b'"Ba\\nse"'):
            with self.subTest(replacement=replacement):
                self.text(valid.replace(b'"Base"', replacement, 1), False)

    def test_previous_executable_versions_remain_readable(self):
        for version in (1, 3):
            function = u32(1) + string("main") + u32(0) + u32(0) + string("i32")
            if version == 3:
                function += u32(0) * 3
            function += u32(2) + string("PUSH_CONST") + u32(1) + string("0") + u32(0)
            function += string("RET") + u32(0) + u32(0)
            sections = [(1, u32(0) * (2 if version == 3 else 1)), (2, function),
                        (3, u32(0)), (4, u32(0)), (5, string("legacy") + u32(0))]
            with self.subTest(version=version):
                self.binary(container(sections, version=version), True)

    def debug_binary(self, debug_payload=None):
        function = (u32(1) + string("main") + u32(0) + u32(0) + string("i32") +
                    u32(0) * 3 + u32(2) + string("PUSH_CONST") + u32(1) + string("0") + u32(1) +
                    string("RET") + u32(0) + u32(2))
        if debug_payload is None:
            debug_payload = (u32(1) + u32(2) + string("/source/main.ny") + u32(0) +
                             string("/source/main.ny") + u32(1) + string("value") + string("slot") + string("i64") + u32(2))
        return container([(1, u32(0) * 2), (2, function), (3, u32(0)), (4, u32(0)),
                          (5, string("debug") + u32(0)), (7, u32(0)), (8, debug_payload)], version=5)

    def test_version_five_source_map(self):
        metadata = self.binary(self.debug_binary(), True)
        self.assertEqual(metadata["debug_source_paths"], ["/source/main.ny"])
        self.assertEqual(metadata["classes"], [])
        self.text(b'''module debug
function helper -> void:
# source_debug 0 "/source/helper.ny" 0
RET ; line 2
end function
function main -> i32:
# source_debug 0 "/source/main.ny" 0
PUSH_CONST 0 ; line 1
# source_debug 1 "/source/main.ny" 1 "value" "slot" "i64" 2
RET ; line 2
end function
''', True)

    def test_source_map_shape_bounds_and_unique_bindings(self):
        base = b'module debug\nfunction main:\n%s\nRET ; line 2\nend function\n'
        records_ = [b'# source_debug 1 "/source/main.ny" 0', b'# source_debug 0 "" 0',
                    b'# source_debug 0 "/source/main.ny" 1 "name" "slot" "i64" 1025',
                    b'# source_debug 0 "/source/main.ny" 1 "" "slot" "i64" 1',
                    b'# source_debug 0 "/source/main.ny" 2 "name" "slot" "i64" 1 "name" "other" "i64" 2',
                    b'# source_debug 0 "/source/main.ny" 2 "name" "slot" "i64" 1 "other" "slot" "i64" 2',
                    b'# source_debug 0 "/source/main.ny" 0\n# source_debug 0 "/source/main.ny" 0']
        for record_ in records_:
            with self.subTest(text_record=record_):
                self.text(base % record_, False)
        valid = records(self.debug_binary())[-1][1]
        for corrupt in (u32(2) + valid[4:], valid[:4] + u32(1) + valid[8:], valid[:-1], valid + b'x',
                        valid.replace(u32(2), u32(1025), 1)):
            with self.subTest(binary_record=corrupt[:20]):
                self.binary(self.debug_binary(corrupt), False)
        for path in ("/bad\n.ny", "/bad\0.ny", "a" * 16385):
            self.binary(self.debug_binary(u32(1) + u32(2) + string(path) + u32(0) + string("/ok.ny") + u32(0)), False)
        bad_utf8 = u32(1) + u32(2) + u32(1) + b'\xff' + u32(0) + string("/ok.ny") + u32(0)
        self.binary(self.debug_binary(bad_utf8), False)

    def test_container_version_flags_and_sections_are_exact(self):
        valid = fixture.binary_module(fixture.FUNCTIONS)
        sections = records(valid)
        invalid = [valid[:-1], valid + b"unclaimed", container(sections, version=5),
                   container(sections, flags=2), container(sections, version=3),
                   container(sections[:-1]), container([*sections, sections[-1]]),
                   container([*(item for item in sections if item[0] != 7), (8, sections[-1][1])])]
        for index, data in enumerate(invalid):
            with self.subTest(case=index):
                self.binary(data, False)

    def test_class_section_is_bounded_and_fully_consumed(self):
        sections = records(fixture.binary_module(fixture.FUNCTIONS))
        for payload in (u32(0), u32(routes.MAX_FUNCTION_COUNT + 1), sections[-1][1][:-1],
                        sections[-1][1] + b"trailing", u32(1) + string("Extra") + u32(2),
                        u32(1) + string("Extra") + u32(0) + u32(routes.MAX_PARAMETER_COUNT + 1)):
            with self.subTest(payload=payload[:30]):
                self.binary(container([*sections[:-1], (7, payload)]), False)

    def test_binary_method_flags_and_embedded_control_bytes_reject(self):
        for field, value in ((4, 8), (0, "bad\0name"), (2, "ref<Base\0>"), (3, "i64\n")):
            classes = copy.deepcopy(fixture.CLASSES)
            method = list(classes[1][3][0])
            method[field] = value
            classes[1][3][0] = tuple(method)
            with self.subTest(field=field, value=value):
                self.binary(fixture.binary_module(fixture.FUNCTIONS, classes), False)

    def test_invalid_metadata_rejected_equally_in_text_and_binary(self):
        cases = []
        for case in ("duplicate", "missing-schema", "unknown-base", "cycle", "duplicate-base",
                     "wrong-receiver", "missing-implementation", "abstract-with-implementation",
                     "abstract-not-virtual", "duplicate-method", "void-parameter"):
            classes = copy.deepcopy(fixture.CLASSES)
            if case == "duplicate":
                classes.append(classes[0])
            elif case == "missing-schema":
                classes[0] = ("Missing", False, [], [])
            elif case == "unknown-base":
                classes[2][2].append("Missing")
            elif case == "cycle":
                classes[0][2].append("Derived")
            elif case == "duplicate-base":
                classes[2][2].append("Base")
            elif case == "duplicate-method":
                classes[1][3].append(classes[1][3][0])
            else:
                method = list(classes[1][3][0])
                if case == "wrong-receiver": method[2] = "Derived"
                elif case == "missing-implementation": method[1] = "missing"
                elif case == "abstract-with-implementation": method[4] = 3
                elif case == "abstract-not-virtual": method[1], method[4] = "", 2
                elif case == "void-parameter": method[5] = ["void"]
                classes[1][3][0] = tuple(method)
            cases.append((case, classes))
        for case, classes in cases:
            with self.subTest(case=case):
                self.binary(fixture.binary_module(fixture.FUNCTIONS, classes), False)
                self.text(fixture.textual_module(fixture.FUNCTIONS, classes), False)

    def test_text_class_records_are_strict(self):
        valid = fixture.textual_module(fixture.FUNCTIONS)
        for malformed in (valid.replace(b"class Extra 0", b"class Extra 2"),
                          valid.replace(b"end class", b"end enum", 1),
                          valid.replace(b"base Extra", b"base Extra stray"),
                          valid.replace(b'"Base" "i64" 1 0 0', b'"Base" "i64" 7 0 0'),
                          valid.replace(b'"Base" "i64" 1 0 0', b'"Base" "i64" 1 0 1'),
                          valid.replace(b"end class", b"nonsense\nend class", 1),
                          valid + b"class Incomplete 0\n"):
            with self.subTest(malformed=malformed[:80]):
                self.text(malformed, False)


if __name__ == "__main__":
    unittest.main()
