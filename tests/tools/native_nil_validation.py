#!/usr/bin/env python3
"""Reject malformed native NIL without replacing a previously valid output."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assembler", type=Path, required=True)
    parser.add_argument("--linker", type=Path)
    parser.add_argument("--runtime", type=Path)
    args = parser.parse_args()
    assembler = args.assembler.resolve(strict=True)
    sentinel = b"previous object must survive rejected NIL\n"

    def module(body: str, signature: str = "main() -> i32") -> str:
        return f"module native_validation\nfunction {signature}:\n{body}\nend function\n"

    enum_schema = "enum Example\nSome: text\nOther: i64\nPair: (i64,text)\nNone\nend enum\n"
    floating_enum_schema = (
        "enum FloatingChoice\nReal: f64\nInteger: i64\nText: text\nNone\nend enum\n"
    )
    aggregate_schema = (
        "struct Left\nvalue: i64\nend struct\n"
        "struct Right\nvalue: i64\nend struct\n"
        "struct Outer\nitem: Left\nend struct\n"
    )
    scalar_field_schema = (
        "struct Floating\nvalue: f64\nend struct\n"
        "struct Byte\nvalue: u8\nend struct\n"
    )

    invalid = {
        "integer-as-f64-enum-payload": (
            floating_enum_schema +
            module("PUSH_CONST 7\nNEW_VARIANT FloatingChoice::Real 1 FloatingChoice f64\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "requires exact type 'f64', got 'i64'",
        ),
        "text-as-f64-enum-payload": (
            floating_enum_schema + 'const message = "wrong"\n' +
            module("PUSH_CONST_STR message\nNEW_VARIANT FloatingChoice::Real 1 FloatingChoice f64\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "requires exact type 'f64', got 'text'",
        ),
        "f64-as-integer-enum-payload": (
            floating_enum_schema +
            module("PUSH_CONST 7.5\nNEW_VARIANT FloatingChoice::Integer 1 FloatingChoice i64\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "requires exact type 'i64', got 'f64'",
        ),
        "f64-as-text-enum-payload": (
            floating_enum_schema +
            module("PUSH_CONST 7.5\nNEW_VARIANT FloatingChoice::Text 1 FloatingChoice text\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "requires exact type 'text', got 'f64'",
        ),
        "f64-enum-forged-payload-metadata": (
            floating_enum_schema +
            module("PUSH_CONST 7.5\nNEW_VARIANT FloatingChoice::Real 1 FloatingChoice i64\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "payload metadata 'i64' disagrees with concrete enum schema 'f64'",
        ),
        "integer-as-f64-field": (
            scalar_field_schema +
            module("NEW_STRUCT Floating\nPUSH_CONST 7\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "requires an exact f64 field value",
        ),
        "text-as-f64-field": (
            scalar_field_schema + 'const message = "wrong"\n' +
            module("NEW_STRUCT Floating\nPUSH_CONST_STR message\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "requires an exact f64 field value",
        ),
        "pointer-as-f64-field": (
            scalar_field_schema +
            module("NEW_STRUCT Floating\nPUSH_CONST 1\nALLOC i32\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "requires an exact f64 field value",
        ),
        "f64-as-u8-field": (
            scalar_field_schema +
            module("NEW_STRUCT Byte\nPUSH_CONST 7.5\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "expects scalar word type 'u8'",
        ),
        "pointer-as-u8-field": (
            scalar_field_schema +
            module("NEW_STRUCT Byte\nPUSH_CONST 1\nALLOC i32\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "expects scalar word type 'u8'",
        ),
        "enum-as-u8-field": (
            scalar_field_schema + enum_schema +
            module("NEW_STRUCT Byte\nNEW_VARIANT Example::None 0\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "expects scalar word type 'u8'",
        ),
        "integer-reassigns-f64-field": (
            scalar_field_schema +
            module("NEW_STRUCT Floating\nPUSH_CONST 1.5\nSET_FIELD value\nSTORE_LOCAL item\n"
                   "LOAD_LOCAL item\nPUSH_CONST 2\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET"),
            "requires an exact f64 field value",
        ),
        "integer-as-f64-field-parameter": (
            scalar_field_schema +
            module("NEW_STRUCT Floating\nLOAD_LOCAL value\nSET_FIELD value\nRET", "make(value: f64) -> Floating") +
            "function main() -> i32:\nPUSH_CONST 7\nCALL make 1\nPOP\nPUSH_CONST 0\nRET\nend function\n",
            "requires an exact f64 argument",
        ),
        "conditional-nested-field-initialization": (
            aggregate_schema +
            module("NEW_STRUCT Outer\nPUSH_CONST 0\nJMP_IF_NOT use\n"
                   "NEW_STRUCT Left\nPUSH_CONST 7\nSET_FIELD value\nSET_FIELD item\n"
                   "LABEL use\nGET_FIELD item\nGET_FIELD value\nRET"),
            "uninitialized on an incoming path",
        ),
        "merged-distinct-incomplete-construction": (
            aggregate_schema +
            module("PUSH_CONST 0\nJMP_IF_NOT other\nNEW_STRUCT Left\n"
                   "PUSH_CONST 7\nSET_FIELD value\nJMP use\nLABEL other\n"
                   "NEW_STRUCT Left\nLABEL use\nGET_FIELD value\nRET"),
            "uninitialized on an incoming path",
        ),
        "incomplete-reference-call": (
            aggregate_schema +
            module("LOAD_LOCAL value\nGET_FIELD value\nRET", "read(value: ref<Left>) -> i64") +
            "function main() -> i32:\nNEW_STRUCT Left\nCALL read 1\nRET\nend function\n",
            "uninitialized on an incoming path",
        ),
        "incomplete-aggregate-return": (
            aggregate_schema +
            module("NEW_STRUCT Left\nRET", "make() -> Left") +
            "function main() -> i32:\nCALL make 0\nGET_FIELD value\nRET\nend function\n",
            "uninitialized on an incoming path",
        ),
        "integer-as-f64-argument": (
            module("LOAD_LOCAL value\nRET", "accept(value: f64) -> f64") +
            "function main() -> i32:\nPUSH_CONST 1\nCALL accept 1\n"
            "POP\nPUSH_CONST 0\nRET\nend function\n",
            "requires an exact f64 argument",
        ),
        "integer-as-f64-return": (
            module("PUSH_CONST 1\nRET", "helper() -> f64") +
            "function main() -> i32:\nCALL helper 0\nPOP\nPUSH_CONST 0\nRET\nend function\n",
            "requires an exact f64 return value",
        ),
        "f64-as-integer-return": (
            module("PUSH_CONST 1.5\nRET", "helper() -> i64") +
            "function main() -> i32:\nCALL helper 0\nRET\nend function\n",
            "cannot return f64 through",
        ),
        "global-integer-as-text": (
            module("PUSH_CONST 7\nSTORE_GLOBAL value text\nLOAD_GLOBAL value text\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "value does not match storage type 'text'",
        ),
        "global-conflicting-types": (
            module("PUSH_CONST 7\nSTORE_GLOBAL value i64\nLOAD_GLOBAL value text\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "conflicting or missing storage type metadata",
        ),
        "global-mixed-typed-and-legacy": (
            'const message = "text"\n' +
            module("PUSH_CONST_STR message\nSTORE_GLOBAL value text\nLOAD_GLOBAL value\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "conflicting or missing storage type metadata",
        ),
        "global-text-as-integer": (
            'const message = "text"\n' +
            module("PUSH_CONST_STR message\nSTORE_GLOBAL value i64\nLOAD_GLOBAL value i64\nRET"),
            "value does not match storage type 'i64'",
        ),
        "global-array-as-text": (
            module("NEW_ARRAY_LITERAL 0 u8\nSTORE_GLOBAL value text\nLOAD_GLOBAL value text\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "value does not match storage type 'text'",
        ),
        "global-unsupported-storage-type": (
            module("PUSH_CONST 7\nSTORE_GLOBAL value ptr<i32>\n"
                   "LOAD_GLOBAL value ptr<i32>\nPOP\nPUSH_CONST 0\nRET"),
            "unsupported storage type",
        ),
        "empty-time-sleep": (
            module("TIME_SLEEP\nPUSH_CONST 0\nRET"),
            "stack underflow at TIME_SLEEP",
        ),
        "floating-time-sleep": (
            module("PUSH_CONST 1.5\nTIME_SLEEP\nPUSH_CONST 0\nRET"),
            "TIME_SLEEP requires one integer duration",
        ),
        "extra-time-sleep-operand": (
            module("PUSH_CONST 1\nTIME_SLEEP extra\nPUSH_CONST 0\nRET"),
            "expects 0 operand(s)",
        ),
        "nested-field-nominal-mismatch": (
            aggregate_schema +
            module("NEW_STRUCT Outer\nNEW_STRUCT Right\nPUSH_CONST 1\n"
                   "SET_FIELD value\nSET_FIELD item\nPOP\nPUSH_CONST 0\nRET"),
            "expects aggregate type 'left', got 'right'",
        ),
        "reference-argument-nominal-mismatch": (
            aggregate_schema +
            module("LOAD_LOCAL value\nGET_FIELD value\nRET", "read(value: ref<Right>) -> i64") +
            "function main() -> i32:\nNEW_STRUCT Left\nPUSH_CONST 1\n"
            "SET_FIELD value\nSTORE_LOCAL item\nREF_LOCAL item\nCALL read 1\nRET\nend function\n",
            "to aggregate parameter 'value' of type 'ref<Right>'",
        ),
        "reference-return-nominal-mismatch": (
            aggregate_schema +
            module("LOAD_LOCAL value\nRET", "echo(value: ref<Left>) -> ref<Right>") +
            "function main() -> i32:\nNEW_STRUCT Left\nPUSH_CONST 1\n"
            "SET_FIELD value\nSTORE_LOCAL item\nREF_LOCAL item\nCALL echo 1\n"
            "POP\nPUSH_CONST 0\nRET\nend function\n",
            "requires aggregate type 'ref<Right>'",
        ),
        "nested-generic-field-mismatch": (
            "struct Box<i32>\nvalue: i32\nend struct\n"
            "struct Box<i64>\nvalue: i64\nend struct\n"
            "struct Outer\nitem: Box<i32>\nend struct\n" +
            module("NEW_STRUCT Outer\nNEW_STRUCT Box<i64>\nPUSH_CONST 1\n"
                   "SET_FIELD value\nSET_FIELD item\nPOP\nPUSH_CONST 0\nRET"),
            "expects aggregate type 'box<i32>', got 'box<i64>'",
        ),
        "mutually-recursive-reference-fields": (
            "struct Left\nnext: ref<Right>\nend struct\n"
            "struct Right\nnext: ref<Left>\nend struct\n" +
            module("NEW_STRUCT Left\nPOP\nPUSH_CONST 0\nRET"),
            "is a recursive aggregate",
        ),
        "primitive-reference-local": (
            module("PUSH_CONST 7\nSTORE_LOCAL item\nREF_LOCAL item\nPOP\nPUSH_CONST 0\nRET"),
            "requires a concrete struct local",
        ),
        "nested-reference-local": (
            aggregate_schema +
            module("NEW_STRUCT Left\nPUSH_CONST 1\nSET_FIELD value\nSTORE_LOCAL item\n"
                   "REF_LOCAL item\nSTORE_LOCAL reference\nREF_LOCAL reference\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "requires a concrete struct local",
        ),
        "conditionally-initialized-reference-local": (
            aggregate_schema +
            module("PUSH_CONST 0\nJMP_IF_NOT use\nNEW_STRUCT Left\nPUSH_CONST 1\n"
                   "SET_FIELD value\nSTORE_LOCAL item\nLABEL use\nREF_LOCAL item\n"
                   "POP\nPUSH_CONST 0\nRET"),
            "not initialized on every incoming path",
        ),
        "arithmetic-type-bool": (module("PUSH_CONST 1\nPUSH_CONST 2\nADD bool\nRET"),
                                 "unsupported scalar arithmetic type 'bool'"),
        "bitwise-type-float": (module("PUSH_CONST 1\nPUSH_CONST 2\nBIT_AND f64\nRET"),
                               "unsupported scalar arithmetic type 'f64'"),
        "extra-arithmetic-type": (module("PUSH_CONST 1\nNEG i32 i64\nRET"),
                                  "expects 0 to 1 operand(s)"),
        "empty-pop": (module("POP\nPUSH_CONST 0\nRET"), "stack underflow at POP"),
        "empty-add": (module("ADD\nPUSH_CONST 0\nRET"), "stack underflow at ADD"),
        "one-operand-add": (module("PUSH_CONST 1\nADD\nRET"), "stack underflow at ADD"),
        "empty-sub": (module("SUB\nPUSH_CONST 0\nRET"), "stack underflow at SUB"),
        "empty-compare": (module("CMP_EQ\nPUSH_CONST 0\nRET"), "stack underflow at CMP_EQ"),
        "one-operand-compare": (module("PUSH_CONST 1\nCMP_LT\nRET"), "stack underflow at CMP_LT"),
        "empty-store": (module("STORE_LOCAL value\nPUSH_CONST 0\nRET"), "stack underflow at STORE_LOCAL"),
        "float-to-integer-local": (
            module("PUSH_CONST 0\nSTORE_LOCAL value\nPUSH_CONST 1.5\n"
                   "STORE_LOCAL value\nLOAD_LOCAL value\nRET"),
            "cannot implicitly convert between integer and floating-point storage",
        ),
        "out-of-range-float-to-integer-local": (
            module("PUSH_CONST 0\nSTORE_LOCAL value\nPUSH_CONST 9223372036854775808.0\n"
                   "STORE_LOCAL value\nLOAD_LOCAL value\nRET"),
            "cannot implicitly convert between integer and floating-point storage",
        ),
        "integer-to-floating-local": (
            module("PUSH_CONST 0.0\nSTORE_LOCAL value\nPUSH_CONST 1\n"
                   "STORE_LOCAL value\nPUSH_CONST 0\nRET"),
            "cannot implicitly convert between integer and floating-point storage",
        ),
        "floating-main-return": (
            module("PUSH_CONST 1.5\nRET"),
            "main return value must be an integer scalar",
        ),
        "out-of-range-floating-main-return": (
            module("PUSH_CONST 9223372036854775808.0\nRET"),
            "main return value must be an integer scalar",
        ),
        "nan-main-return": (
            module("PUSH_CONST -1.0\nMATH_SQRT\nRET"),
            "main return value must be an integer scalar",
        ),
        "undefined-local": (module("LOAD_LOCAL missing\nRET"), "Undefined local missing"),
        "conditional-target": (module("PUSH_CONST 1\nJMP_IF_NOT missing\nPUSH_CONST 0\nRET"), "target 'missing'"),
        "duplicate-label": (module("LABEL repeated\nLABEL repeated\nPUSH_CONST 0\nRET"), "duplicate label"),
        "missing-return-value": (module("RET"), "invalid RET stack"),
        "extra-return-value": (module("PUSH_CONST 1\nPUSH_CONST 2\nRET"), "invalid RET stack"),
        "void-return-value": (module("PUSH_CONST 1\nRET", "main() -> void"), "invalid RET stack"),
        "missing-return": (module("PUSH_CONST 0"), "falls through without RET"),
        "conditional-fallthrough": (module("PUSH_CONST 1\nJMP_IF_NOT last\nPUSH_CONST 0\nRET\nLABEL last"), "falls through without RET"),
        "scalar-helper-return": (
            module("RET", "helper() -> i32")
            + "function main() -> i32:\nCALL helper 0\nRET\nend function\n",
            "invalid RET stack",
        ),
        "conditionally-initialized-local": (
            module("PUSH_CONST 0\nJMP_IF_NOT use\nPUSH_CONST 7\nSTORE_LOCAL value\nLABEL use\nLOAD_LOCAL value\nRET"),
            "not initialized on every incoming path",
        ),
        "first-iteration-uninitialized-local": (
            module("JMP test\nLABEL body\nPUSH_CONST 7\nSTORE_LOCAL value\nLABEL test\nLOAD_LOCAL value\nPOP\nPUSH_CONST 0\nJMP_IF body\nPUSH_CONST 0\nRET"),
            "not initialized on every incoming path",
        ),
        "is-variant-scalar": (
            enum_schema + module("PUSH_CONST 0\nIS_VARIANT Example::Some\nRET"),
            "IS_VARIANT requires a typed enum",
        ),
        "unwrap-scalar": (
            enum_schema + module("PUSH_CONST 0\nUNWRAP_VARIANT 1\nRET"),
            "UNWRAP_VARIANT requires a typed enum",
        ),
        "integer-as-text-payload": (
            enum_schema + module("PUSH_CONST 42\nNEW_VARIANT Example::Some 1 Example text\nPOP\nPUSH_CONST 0\nRET"),
            "payload 0 requires exact type 'text'",
        ),
        "unwrap-impossible-count": (
            enum_schema + module("NEW_VARIANT Example::None 0\nUNWRAP_VARIANT 3\nPUSH_CONST 0\nRET"),
            "payload count/type does not match enum schema",
        ),
        "unwrap-invented-type": (
            enum_schema + module("NEW_VARIANT Example::None 0\nUNWRAP_VARIANT 1 Example array<u8>\nPOP\nPUSH_CONST 0\nRET"),
            "payload count/type does not match enum schema",
        ),
        "unwrap-ambiguous-legacy": (
            enum_schema + module("NEW_VARIANT Example::None 0\nUNWRAP_VARIANT 1\nPOP\nPUSH_CONST 0\nRET"),
            "exact payload type metadata",
        ),
        "is-variant-invented-payload": (
            enum_schema + module("NEW_VARIANT Example::None 0\nIS_VARIANT Example::Some Example i64\nRET"),
            "incompatible enum schema metadata",
        ),
        "constructed-enum-owner-mismatch": (
            enum_schema + "enum Other\nNone\nend enum\n" +
            module("NEW_VARIANT Other::None 0 Example void\nPOP\nPUSH_CONST 0\nRET"),
            "tag owner disagrees with enum schema",
        ),
        "matched-enum-owner-mismatch": (
            enum_schema + "enum Other\nNone\nend enum\n" +
            module("NEW_VARIANT Example::None 0\nIS_VARIANT Other::None Example void\nRET"),
            "tag owner disagrees with enum schema",
        ),
        "join-stack-height": (
            module("PUSH_CONST 1\nJMP_IF_NOT other\nPUSH_CONST 7\nJMP joined\n"
                   "LABEL other\nLABEL joined\nPUSH_CONST 0\nRET"),
            "incompatible operand-stack height",
        ),
        "join-scalar-type": (
            module("PUSH_CONST 1\nJMP_IF_NOT other\nPUSH_CONST 7\nJMP joined\n"
                   "LABEL other\nPUSH_CONST 2.5\nLABEL joined\nPOP\nPUSH_CONST 0\nRET"),
            "incompatible operand-stack type",
        ),
        "join-handle-identity": (
            'const message = "text"\n' +
            module("PUSH_CONST 1\nJMP_IF_NOT other\nPUSH_CONST_STR message\nJMP joined\n"
                   "LABEL other\nNEW_ARRAY_LITERAL 0 u8\nLABEL joined\nPOP\nPUSH_CONST 0\nRET"),
            "incompatible operand-stack type",
        ),
        "join-enum-identity": (
            enum_schema + "enum Other\nNone\nend enum\n" +
            module("PUSH_CONST 1\nJMP_IF_NOT other\nNEW_VARIANT Example::None 0\nJMP joined\n"
                   "LABEL other\nNEW_VARIANT Other::None 0\nLABEL joined\nPOP\nPUSH_CONST 0\nRET"),
            "incompatible operand-stack type",
        ),
        "backedge-stack-height": (
            module("PUSH_CONST 3\nLABEL loop\nPUSH_CONST 1\nJMP loop"),
            "incompatible operand-stack height",
        ),
        "backedge-stack-type": (
            'const message = "text"\n' +
            module("PUSH_CONST 3\nLABEL loop\nPOP\nPUSH_CONST_STR message\nJMP loop"),
            "incompatible operand-stack type",
        ),
    }

    # Every comparison keeps its exact text provenance check. A pointer-shaped
    # value must never reach strcmp just because LLVM gives it the same type.
    for opcode in ("CMP_EQ", "CMP_NE", "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE"):
        for name, value in (
            ("integer", "PUSH_CONST 1"),
            ("floating", "PUSH_CONST 1.5"),
            ("raw-pointer", "PUSH_CONST 1\nALLOC u8"),
            ("array", "NEW_ARRAY_LITERAL 0 text"),
            ("aggregate", "NEW_STRUCT Left\nPUSH_CONST 1\nSET_FIELD value"),
            ("enum", "NEW_VARIANT Example::None 0"),
        ):
            for text_first in (True, False):
                values = ("PUSH_CONST_STR message", value)
                if not text_first:
                    values = tuple(reversed(values))
                invalid[f"{opcode}-text-{name}-{text_first}"] = (
                    aggregate_schema + enum_schema + 'const message = "text"\n' +
                    module("\n".join(values) + f"\n{opcode}\nPOP\nPUSH_CONST 0\nRET"),
                    f"{opcode} cannot compare text with a non-text value",
                )
        for name, value, diagnostic in (
            ("array", "NEW_ARRAY_LITERAL 0 text", "pointer-valued comparisons"),
            ("aggregate", "NEW_STRUCT Left\nPUSH_CONST 1\nSET_FIELD value", "pointer-valued comparisons"),
            ("enum", "NEW_VARIANT Example::None 0", "enum-valued comparisons"),
        ):
            invalid[f"{opcode}-{name}-pair"] = (
                aggregate_schema + enum_schema +
                module(f"{value}\n{value}\n{opcode}\nPOP\nPUSH_CONST 0\nRET"),
                diagnostic,
            )
        invalid[f"{opcode}-mismatched-pointer-pair"] = (
            module(f"PUSH_CONST 1\nALLOC i32\nPUSH_CONST 1\nALLOC u8\n{opcode}\nPOP\nPUSH_CONST 0\nRET"),
            "raw-pointer operands must have identical exact",
        )

    with tempfile.TemporaryDirectory(prefix="ny-native-nil-validation-") as temporary:
        root = Path(temporary)

        def assemble(source: Path, output: Path) -> subprocess.CompletedProcess[str]:
            return subprocess.run(
                [str(assembler), "-o", str(output), str(source)],
                cwd=root,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
                check=False,
            )

        probe = root / "valid.nil"
        probe.write_text(module("PUSH_CONST 0\nRET"), encoding="utf-8")
        process = assemble(probe, root / "valid.o")
        diagnostic = process.stderr.lower()
        if process.returncode and "llvm" in diagnostic and (
            "not available" in diagnostic or "unavailable" in diagnostic
            or "no llvm backend" in diagnostic
        ):
            print("skip: nilassembler has no LLVM backend")
            return 77
        if process.returncode:
            raise AssertionError(f"valid NIL was rejected: {process.stderr}")

        # A void CALL produces no operand and preserves the caller's pending
        # expression. POP always consumes one real operand; it has no special
        # post-CALL cleanup exemption.
        valid_void = root / "void-call.nil"
        valid_void.write_text(
            module("RET", "helper() -> void")
            + "function main() -> i32:\nPUSH_CONST 7\nCALL helper 0\nRET\nend function\n",
            encoding="utf-8",
        )
        process = assemble(valid_void, root / "void-call.o")
        if process.returncode:
            raise AssertionError(f"valid void call was rejected: {process.stderr}")
        scalar_field_controls = {
            "scalar-field-f64-roundtrip": (
                scalar_field_schema +
                module("NEW_STRUCT Floating\nLOAD_LOCAL value\nSET_FIELD value\nGET_FIELD value\nRET",
                       "roundtrip(value: f64) -> f64") +
                "function main() -> i32:\nPUSH_CONST -2.375\nCALL roundtrip 1\n"
                "PUSH_CONST -2.375\nCMP_EQ\nNOT\nRET\nend function\n",
                0,
            ),
        }
        for value, expected in ((-1, 255), (256, 0), (511, 255)):
            scalar_field_controls[f"scalar-field-u8-{value}"] = (
                scalar_field_schema +
                module("NEW_STRUCT Byte\nLOAD_LOCAL value\nSET_FIELD value\nRET", "make(value: u8) -> Byte") +
                "function main() -> i32:\n" + f"PUSH_CONST {value}\n" +
                "CALL make 1\nGET_FIELD value\n" + f"PUSH_CONST {expected}\n" +
                "CMP_EQ\nNOT\nRET\nend function\n",
                0,
            )
        for name, (payload, _) in scalar_field_controls.items():
            source = root / f"{name}.nil"
            source.write_text(payload, encoding="utf-8")
            process = assemble(source, root / f"{name}.o")
            if process.returncode:
                raise AssertionError(f"{name}: valid scalar field was rejected: {process.stderr}")
        for name, body in {
            "explicit-float-to-integer-store": (
                "PUSH_CONST 0\nCAST i32\nSTORE_LOCAL value\n"
                "PUSH_CONST 7.5\nCAST i32\nSTORE_LOCAL value\nLOAD_LOCAL value\nRET"
            ),
            "explicit-integer-to-floating-store": (
                "PUSH_CONST 0.0\nSTORE_LOCAL value\n"
                "PUSH_CONST 7\nCAST f64\nSTORE_LOCAL value\n"
                "LOAD_LOCAL value\nPUSH_CONST 7.0\nCMP_EQ\nNOT\nRET"
            ),
            "explicit-floating-main-return": (
                "PUSH_CONST 7.5\nCAST i32\nRET"
            ),
            "scalar-i32-wrap": (
                "PUSH_CONST 2147483647\nPUSH_CONST 1\nADD i32\n"
                "PUSH_CONST -2147483648\nCMP_EQ\nNOT\nRET"
            ),
            "scalar-u8-wrap": (
                "PUSH_CONST 255\nPUSH_CONST 1\nADD u8\n"
                "PUSH_CONST 0\nCMP_EQ\nNOT\nRET"
            ),
            "legacy-scalar-width": (
                "PUSH_CONST 2147483647\nCAST i32\nPUSH_CONST 1\nCAST i32\nADD\n"
                "PUSH_CONST 2147483648\nCMP_EQ\nNOT\nRET"
            ),
            "legacy-comparison-width": (
                "PUSH_CONST 1\nPUSH_CONST 2\nCMP_LT\n"
                "PUSH_CONST 3\nPUSH_CONST 4\nCMP_LT\nADD\n"
                "PUSH_CONST 2\nCMP_EQ\nNOT\nRET"
            ),
            "both-branches-initialize": (
                "PUSH_CONST 0\nJMP_IF_NOT other\nPUSH_CONST 7\nSTORE_LOCAL value\nJMP use\n"
                "LABEL other\nPUSH_CONST 9\nSTORE_LOCAL value\nLABEL use\nLOAD_LOCAL value\nRET"
            ),
            "initialized-before-loop": (
                "PUSH_CONST 7\nSTORE_LOCAL value\nLABEL body\nLOAD_LOCAL value\nPOP\n"
                "PUSH_CONST 0\nJMP_IF body\nLOAD_LOCAL value\nRET"
            ),
            "fixed-size-array": (
                "PUSH_CONST 100\nNEW_ARRAY 2\nSTORE_LOCAL values\n"
                "LOAD_LOCAL values\nPUSH_CONST 1\nPUSH_CONST 42\nSET_INDEX\n"
                "LOAD_LOCAL values\nPUSH_CONST 1\nGET_INDEX\n"
                "LOAD_LOCAL values\nPUSH_CONST 0\nGET_INDEX\nADD\nADD\nRET"
            ),
            "stack-diamond-true": (
                "PUSH_CONST 100\nPUSH_CONST 1\nJMP_IF_NOT other\n"
                "PUSH_CONST 20\nJMP joined\nLABEL other\nPUSH_CONST 30\n"
                "LABEL joined\nADD\nRET"
            ),
            "stack-diamond-false": (
                "PUSH_CONST 100\nPUSH_CONST 0\nJMP_IF_NOT other\n"
                "PUSH_CONST 20\nJMP joined\nLABEL other\nPUSH_CONST 30\n"
                "LABEL joined\nADD\nRET"
            ),
            "stack-jmp-if": (
                "PUSH_CONST 100\nPUSH_CONST 1\nJMP_IF other\n"
                "PUSH_CONST 20\nJMP joined\nLABEL other\nPUSH_CONST 30\n"
                "LABEL joined\nADD\nRET"
            ),
            "stack-backedge": (
                "PUSH_CONST 100\nPUSH_CONST 3\nLABEL loop\nSTORE_LOCAL remaining\n"
                "LOAD_LOCAL remaining\nPUSH_CONST 1\nSUB\nSTORE_LOCAL remaining\n"
                "LOAD_LOCAL remaining\nLOAD_LOCAL remaining\nPUSH_CONST 0\nCMP_GT\n"
                "JMP_IF loop\nADD\nRET"
            ),
        }.items():
            source = root / f"{name}.nil"
            source.write_text(module(body), encoding="utf-8")
            process = assemble(source, root / f"{name}.o")
            if process.returncode:
                raise AssertionError(f"{name}: valid CFG was rejected: {process.stderr}")
        invalid["empty-pop-after-void-call"] = (
            module("RET", "helper() -> void")
            + "function main() -> i32:\nCALL helper 0\nPOP\nPUSH_CONST 0\nRET\nend function\n",
            "stack underflow at POP",
        )
        invalid["void-call-pop-with-pending-value"] = (
            module("RET", "helper() -> void")
            + "function main() -> i32:\nPUSH_CONST 7\nCALL helper 0\nPOP\nRET\nend function\n",
            "invalid RET stack",
        )

        for name, (payload, diagnostic) in invalid.items():
            source = root / f"{name}.nil"
            output = root / f"{name}.o"
            source.write_text(payload, encoding="utf-8")
            output.write_bytes(sentinel)
            process = assemble(source, output)
            if process.returncode <= 0:
                raise AssertionError(f"{name}: expected ordinary rejection, got {process.returncode}: {process.stderr}")
            if diagnostic.lower() not in process.stderr.lower():
                raise AssertionError(f"{name}: missing diagnostic {diagnostic!r}: {process.stderr}")
            if output.read_bytes() != sentinel:
                raise AssertionError(f"{name}: failure replaced prior object")

        if args.linker and args.runtime:
            for name, expected_exit in (
                ("explicit-float-to-integer-store", 7),
                ("explicit-integer-to-floating-store", 0),
                ("explicit-floating-main-return", 7),
                ("scalar-i32-wrap", 0), ("scalar-u8-wrap", 0),
                ("legacy-scalar-width", 0), ("legacy-comparison-width", 0),
                ("void-call", 7), ("fixed-size-array", 142),
                ("stack-diamond-true", 120), ("stack-diamond-false", 130),
                ("stack-jmp-if", 130), ("stack-backedge", 100),
            ) + tuple((name, expected) for name, (_, expected) in scalar_field_controls.items()):
                executable = root / f"{name}.exe"
                process = subprocess.run(
                    [str(args.linker.resolve()), "--runtime", str(args.runtime.resolve()),
                     "-o", str(executable), str(root / f"{name}.o")],
                    capture_output=True, text=True, timeout=60, check=False,
                )
                if process.returncode:
                    raise AssertionError(f"{name}: linking failed: {process.stderr}")
                process = subprocess.run(
                    [str(executable)], capture_output=True, text=True,
                    timeout=30, check=False,
                )
                if process.returncode != expected_exit or process.stdout or process.stderr:
                    raise AssertionError(f"{name}: operand-stack result was not preserved: {process}")

            for name, creation, unwrap, should_fail in (
                ("valid-text-unwrap", 'PUSH_CONST_STR message\nNEW_VARIANT Example::Some 1',
                 "UNWRAP_VARIANT 1 Example text", False),
                ("wrong-count-runtime", "NEW_VARIANT Example::None 0",
                 "UNWRAP_VARIANT 1 Example text", True),
                ("wrong-type-runtime", "PUSH_CONST 42\nNEW_VARIANT Example::Other 1",
                 "UNWRAP_VARIANT 1 Example text", True),
            ):
                source = root / f"{name}.nil"
                source.write_text(
                    enum_schema + 'const message = "enum payload"\n'
                    + module(f"{creation}\n{unwrap}\nCALL println 1\nPUSH_CONST 0\nRET"),
                    encoding="utf-8",
                )
                output = root / f"{name}.o"
                process = assemble(source, output)
                if process.returncode:
                    raise AssertionError(f"{name}: assembly failed: {process.stderr}")
                executable = root / f"{name}.exe"
                process = subprocess.run(
                    [str(args.linker.resolve()), "--runtime", str(args.runtime.resolve()),
                     "-o", str(executable), str(output)],
                    capture_output=True, text=True, timeout=60, check=False,
                )
                if process.returncode:
                    raise AssertionError(f"{name}: linking failed: {process.stderr}")
                process = subprocess.run(
                    [str(executable)], capture_output=True, text=True,
                    timeout=30, check=False,
                )
                if should_fail:
                    if process.returncode <= 0 or process.stdout or (
                        "enum payload shape mismatch" not in process.stderr
                    ):
                        raise AssertionError(f"{name}: expected checked runtime failure: {process}")
                elif process.returncode or process.stdout != "enum payload\n" or process.stderr:
                    raise AssertionError(f"{name}: invalid runtime result: {process}")

            source = root / "tuple-payload.nil"
            source.write_text(
                enum_schema + 'const message = "enum payload"\n'
                + module("PUSH_CONST 42\nPUSH_CONST_STR message\n"
                         "NEW_VARIANT Example::Pair 2\nUNWRAP_VARIANT 2\n"
                         "CALL println 1\nCALL println 1\nPUSH_CONST 0\nRET"),
                encoding="utf-8",
            )
            output = root / "tuple-payload.o"
            process = assemble(source, output)
            if process.returncode:
                raise AssertionError(f"tuple-payload: assembly failed: {process.stderr}")
            executable = root / "tuple-payload.exe"
            process = subprocess.run(
                [str(args.linker.resolve()), "--runtime", str(args.runtime.resolve()),
                 "-o", str(executable), str(output)],
                capture_output=True, text=True, timeout=60, check=False,
            )
            if process.returncode:
                raise AssertionError(f"tuple-payload: linking failed: {process.stderr}")
            process = subprocess.run(
                [str(executable)], capture_output=True, text=True, timeout=30, check=False,
            )
            if process.returncode or process.stdout != "42\nenum payload\n" or process.stderr:
                raise AssertionError(f"tuple-payload: invalid runtime result: {process}")

        print(f"native NIL validation: {len(invalid)} rejected cases, "
              f"{16 + len(scalar_field_controls)} accepted controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
