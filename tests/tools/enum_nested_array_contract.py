#!/usr/bin/env python3
"""Validate enum/nested-array identity, initialization and encoded NIL admission."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tests"))
from native_output_staging import linked_executable_path

# Reuse the exact text/binary NIL fixture encoder already used by tuple-array
# tests. Each call supplies its own enum list; no checked-in fixture is changed.
import tuple_array_nil_contract as wire

ENUMS = [
    ("Signal", [("Idle", ""), ("Number", "i32"), ("Text", "text")]),
    ("signal", [("Idle", ""), ("Number", "i32")]),
    ("Other", [("Idle", ""), ("Number", "i32")]),
    ("Generic<i32>", [("None", ""), ("Some", "i32")]),
    ("Generic<i64>", [("None", ""), ("Some", "i64")]),
    ("left::State", [("Idle", "")]),
    ("right::State", [("Idle", "")]),
    ("Boxed", [("Empty", ""), ("Value", "anything")]),
    ("Carrier", [("Numbers", "array<i32>")]),
    ("Ordered", [("Pair", "(i32,text)")]),
]
SCHEMAS = [("Leaf", [("value", "i32")], []),
           ("OtherLeaf", [("value", "i32")], [])]
SOURCE_TYPES = """enum Signal<T>
Idle
Value(T)
end enum
enum Other<T>
Idle
Value(T)
end enum
enum PatternPayload
    Unit
    Pair((i64, text))
    Tagged((i64, text), i32)
end enum
struct Leaf
value: i32
end struct
struct OtherLeaf
value: i32
end struct
fn take_rows_i64(values: array<array<i64>>) -> i32
    return values[0].len()
end fn
"""


def source(body: str) -> str:
    return ("import nil.std.collections\n" + SOURCE_TYPES + "\nfn main() -> i32\n"
            + body + "\nreturn 0\nend fn\n")


def imported_arrays(run, root, compiler, interpreter, llvm):
    """Cross-frontend nominal identity survives source and binary imports."""
    checks = 0
    for language in ("ny", "matrix"):
        provider_root = root / ("provider-" + language)
        provider_root.mkdir()
        provider = provider_root / ("provider." + language)
        provider.write_text("""module provider
pub enum Packet<T>
    Idle
    Data(T)
end enum
pub fn make() -> array<Packet<i64>>
    let item: Packet<i64> = Packet.Data(73)
    return [item]
end fn
pub fn identity<T>(rows: array<array<T>>) -> array<array<T>>
    return rows
end fn
""" if language == "ny" else """book provider
public Packet of type T begin choice
    when Idle
    when Data holding T
end choice
public make returns array of Packet of number begin paragraph
    value item as Packet of number is Packet.Data holding (73 becomes number)
    give [item]
end paragraph
public identity of type T receives rows as array of array of T returns array of array of T begin paragraph
    give rows
end paragraph
""", encoding="utf-8", newline="\n")
        consumers = []
        for qualified in (False, True):
            prefix = "provider::" if qualified else ""
            nominal = "provider::Packet" if qualified else "Packet"
            consumer_language = "matrix" if language == "ny" else "ny"
            consumer = root / f"consumer-{language}-{qualified}.{consumer_language}"
            if consumer_language == "ny":
                content = f"""import provider{'' if qualified else '.*'}
fn main() -> i32
    let items: array<{nominal}<i64>> = {prefix}make()
    let rows: array<array<{nominal}<i64>>> = [items]
    let alias = {prefix}identity<{nominal}<i64>>(rows)
    let saved = alias[0][0]
    let replacement: {nominal}<i64> = {nominal}::Data(79)
    alias[0][0] = replacement
    match saved
        {nominal}::Data(value) -> println(value)
        {nominal}::Idle -> println(0)
    end match
    match rows[0][0]
        {nominal}::Data(value) -> println(value)
        {nominal}::Idle -> println(0)
    end match
    return 0
end fn
"""
            else:
                nominal = "provider.Packet" if qualified else "Packet"
                content = f"""book consumer
read everything from provider
main returns override integer begin paragraph
    value items as array of {nominal} of number is {prefix}make with nothing
    value rows as array of array of {nominal} of number is [items]
    value alias is {prefix}identity of {nominal} of number with rows being rows
    value saved is (alias at 0) at 0
    value replacement as {nominal} of number is {prefix}Packet.Data holding (79 becomes number)
    set (alias at 0) at 0 to replacement
    consider saved
        when {nominal}.Data holding amount then say amount
        when {nominal}.Idle then say 0
    end consider
    consider (rows at 0) at 0
        when {nominal}.Data holding amount then say amount
        when {nominal}.Idle then say 0
    end consider
    give 0
end paragraph
"""
            consumer.write_text(content, encoding="utf-8", newline="\n")
            consumers.append((qualified, consumer))
        libraries = root / ("libraries-" + language)
        libraries.mkdir()
        for dependency_mode in ("source", "binary"):
            search = provider_root
            if dependency_mode == "binary":
                run([compiler, "--quiet", "--no-stdlib", "--no-bundle", "--emit-library",
                     "--emit-binary-nil", "-o", libraries / "provider.nil", provider])
                provider.unlink()
                assert not provider.exists()
                search = libraries
            for qualified, consumer in consumers:
                for encoding, flags in [("text", []), ("binary", ["--emit-binary-nil"]),
                                        *([("native", ["--native"])] if llvm else [])]:
                    name = f"consumer-{language}-{dependency_mode}-{qualified}-{encoding}"
                    output = root / (name + (".nil" if encoding != "native" else ""))
                    if encoding == "native":
                        output = linked_executable_path(output)
                    run([compiler, "--quiet", "--no-stdlib", "--no-bundle", "-I", search,
                         *flags, "-o", output, consumer])
                    result = run([output] if encoding == "native" else
                                 [interpreter, "--no-ai", output])
                    assert result.stdout == "73\n79\n" and not result.stderr, result
                    checks += 1
    return checks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "stdlib"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--runtime-tests", action="append", default=[], type=Path)
    args = parser.parse_args()
    compiler, interpreter, assembler, stdlib = (
        getattr(args, name).resolve(strict=True)
        for name in ("compiler", "interpreter", "assembler", "stdlib"))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_LIBRARY_PATH"))
                   and key != "NIL_STRICT_BOUNDS"}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-enum-nested-array-contract-") as temporary:
        root = Path(temporary)

        def run(command, *, success=True):
            result = subprocess.run([str(part) for part in command], cwd=root,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", timeout=90)
            if success:
                assert result.returncode == 0, (command, result)
            else:
                assert result.returncode > 0, (command, result)
            return result

        def encoded(name, body, binary, *, schemas=SCHEMAS, enums=ENUMS):
            path = root / (name + (".binary.nil" if binary else ".text.nil"))
            wire.ENUMS = enums
            path.write_bytes(wire.encode(wire.instructions(body), schemas, binary))
            return path

        base = [compiler, "--quiet", "--no-stdlib", "--no-bundle", "--sdk-path", stdlib]
        encodings = [("text", []), ("binary", ["--emit-binary-nil"])]
        for name in ("enum-arrays", "enum-arrays-matrix", "nested-arrays", "nested-arrays-matrix"):
            path = REPOSITORY / "tests/language" / (name + (".matrix" if name.endswith("-matrix") else ".ny"))
            expected = path.with_suffix(".stdout").read_text(encoding="utf-8")
            for encoding, flags in encodings:
                artifact = root / (name + "-" + encoding + ".nil")
                result = run([*base, *flags, "-o", artifact, path])
                assert not result.stdout and not result.stderr, result
                result = run([interpreter, "--no-ai", "--memory-mode=safe", artifact])
                assert result.stdout == expected and not result.stderr, result
                checks += 1

        # Explicit collections verify ownership after returning the only roots,
        # replacing outer slots, and retaining a child and an extracted enum.
        gc_source = root / "enum-nested-roots.ny"
        gc_source.write_text("""import nil.std.runtime
struct Leaf
    value: i32
end struct
enum Packet
    Text(text)
    Record(Leaf)
    Numbers(array<i32>)
end enum
fn make() -> array<array<Packet>>
    let result: array<array<Packet>> = []
    let mut i: i32 = 0
    while i < 1200
        let word: Packet = Packet.Text($"owned-{i}")
        let object: Packet = Packet.Record(Leaf { value: i })
        let numbers: Packet = Packet.Numbers([i, i + 1])
        result.push([word, object, numbers])
        i = i + 1
    end while
    return result
end fn
fn show(packet: Packet) -> void
    match packet
        Packet.Text(word) -> println(word)
        Packet.Record(object) -> println(object.value)
        Packet.Numbers(items) -> println(items[1])
    end match
end fn
fn main() -> i32
    let roots = make()
    let retained = roots[513]
    let retained_text = retained[0]
    let retained_record = retained[1]
    collect(0)
    println(roots.len())
    show(roots[0][0])
    show(roots[1199][0])
    show(roots[1199][2])
    roots[513] = []
    retained[0] = Packet.Text("replacement")
    retained[1] = Packet.Record(Leaf { value: 91 })
    collect(0)
    show(retained_text)
    show(retained_record)
    show(retained[2])
    match retained_record
        Packet.Record(object) -> object.value = 97
        Packet.Text(_) -> panic("wrong variant")
        Packet.Numbers(_) -> panic("wrong variant")
    end match
    collect(0)
    show(retained_record)
    println(roots[513].len())
    return 0
end fn
""", encoding="utf-8", newline="\n")
        for encoding, flags in encodings:
            artifact = root / ("enum-nested-roots-" + encoding + ".nil")
            run([*base, *flags, "-o", artifact, gc_source])
            result = run([interpreter, "--no-ai", "--memory-mode=safe", artifact])
            assert result.stdout == "1200\nowned-0\nowned-1199\n1200\nowned-513\n513\n514\n97\n0\n", result
            assert not result.stderr, result
            checks += 1

        checks += imported_arrays(run, root, compiler, interpreter, args.llvm == "ON")

        invalid_source = {
            "enum-owner": "let first: Signal<i32> = Signal.Value(7)\nlet wrong: Other<i32> = Other.Value(9)\nlet items: array<Signal<i32>> = [wrong]",
            "enum-generic": "let first: Signal<i32> = Signal.Value(7)\nlet wrong: Signal<i64> = Signal.Value(9 as i64)\nlet items = [first]\nitems.push(wrong)",
            "enum-set": "let first: Signal<i32> = Signal.Value(7)\nlet wrong: Other<i32> = Other.Idle\nlet items = [first]\nitems[0] = wrong",
            "enum-intrinsic": "let first: Signal<i32> = Signal.Value(7)\nlet wrong: Other<i32> = Other.Idle\nlet items = [first]\narray_set<Signal<i32>>(items, 0, wrong)",
            "nested-kind": "let rows: array<array<i32>> = [[1]]\nrows.push([\"wrong\"])",
            "nested-width": "let rows: array<array<i32>> = [[1]]\nlet wide: array<i64> = [2 as i64]\nrows[0] = wide",
            "nested-rank": "let rows: array<array<i32>> = [[1]]\nrows.push([[2]])",
            "nested-record": "let rows: array<array<Leaf>> = [[Leaf { value: 1 }]]\nrows.push([OtherLeaf { value: 2 }])",
            "nested-enum": "let first: Signal<i32> = Signal.Value(7)\nlet wrong: Other<i32> = Other.Idle\nlet rows: array<array<Signal<i32>>> = [[first]]\nrows.push([wrong])",
            "nested-intrinsic": "let rows: array<array<i32>> = [[1]]\narray_set<array<i32>>(rows, 0, [\"wrong\"])",
            "nested-context-call": "let rejected = take_rows_i64([[], [1 as i32]])",
            "nested-context-handle": "let narrow: array<i32> = [1]\nlet rows: array<array<i64>> = [[], narrow]",
            "enum-pattern-tuple-arity": "let item: PatternPayload = PatternPayload.Pair((1 as i64, \"x\"))\nmatch item\nPatternPayload.Pair(first, second) -> println(0)\nend match",
            "enum-pattern-tagged-short": "let item: PatternPayload = PatternPayload.Tagged((1 as i64, \"x\"), 3)\nmatch item\nPatternPayload.Tagged(first) -> println(0)\nend match",
            "enum-pattern-tagged-long": "let item: PatternPayload = PatternPayload.Tagged((1 as i64, \"x\"), 3)\nmatch item\nPatternPayload.Tagged(first, second, third) -> println(0)\nend match",
            "enum-pattern-unit": "let item: PatternPayload = PatternPayload.Unit\nmatch item\nPatternPayload.Unit(value) -> println(0)\nend match",
        }
        for name, body in invalid_source.items():
            path = root / (name + ".ny")
            path.write_text(source(body), encoding="utf-8", newline="\n")
            for encoding, flags in [*encodings, *([("native", ["--native"])] if args.llvm == "ON" else [])]:
                artifact = root / (name + "-" + encoding)
                if encoding == "native":
                    artifact = linked_executable_path(artifact)
                artifact.write_bytes(b"preserve previous artifact\n")
                result = run([*base, *flags, "-o", artifact, path], success=False)
                assert "Type checking failed:" in result.stderr, result
                if name.startswith("enum-pattern-"):
                    assert "payload pattern" in result.stderr.lower(), result
                assert artifact.read_bytes() == b"preserve previous artifact\n"
                checks += 1
            analysis = json.loads(run([compiler, "--no-stdlib", "--sdk-path", stdlib,
                                       "--analyze", path], success=False).stdout)
            assert not analysis["success"] and analysis["diagnostics"], analysis
            if name.startswith("enum-pattern-"):
                assert any("payload pattern" in diagnostic["message"].lower()
                           for diagnostic in analysis["diagnostics"]), analysis
            checks += 1
            if args.llvm == "ON" and name.startswith("nested-context-"):
                # Suppressing the source checker must not silently recast an
                # already concrete i32 child to the enclosing i64 array ABI.
                artifact = linked_executable_path(root / (name + "-unchecked"))
                artifact.write_bytes(b"preserve previous artifact\n")
                result = run([*base, "--no-typecheck", "--native", "-o", artifact, path], success=False)
                assert (("[NativeCodegen]" in result.stderr and "array" in result.stderr.lower()) or
                        ("Array element type mismatch" in result.stderr and
                         "Code generation failed" in result.stderr)), result
                assert artifact.read_bytes() == b"preserve previous artifact\n"
                checks += 1

        enum_setup = "let initial: Signal<i32> = Signal.Value(7)\nlet values: array<Signal<i32>> = [initial]\n"
        nested_setup = "let values: array<array<i32>> = [[7]]\n"
        invalid_reads = {
            "enum-negative": enum_setup + "let missing = values[-1]",
            "enum-end": enum_setup + "let missing = values[1]",
            "enum-empty": "let empty: array<Signal<i32>> = []\nlet missing = empty[0]",
            "enum-intrinsic-read": enum_setup + "let missing = get<Signal<i32>>(values, 1)",
            "nested-negative": nested_setup + "let missing = values[-1]",
            "nested-end": nested_setup + "let missing = values[1]",
            "nested-empty": "let empty: array<array<i32>> = []\nlet missing = empty[0]",
            "nested-intrinsic-read": nested_setup + "let missing = get<array<i32>>(values, 1)",
        }
        for name, body in invalid_reads.items():
            path = root / (name + ".ny")
            path.write_text(source(body), encoding="utf-8", newline="\n")
            for encoding, flags in [*encodings, *([("native", ["--native"])] if args.llvm == "ON" else [])]:
                artifact = root / (name + "-" + encoding)
                if encoding == "native":
                    artifact = linked_executable_path(artifact)
                run([*base, *flags, "-o", artifact, path])
                result = run([artifact] if encoding == "native" else
                             [interpreter, "--no-ai", artifact], success=False)
                assert not result.stdout and "array" in result.stderr.lower(), result
                checks += 1

        enum_word = "PUSH_CONST 7\nCAST i32\nNEW_VARIANT Signal::Number 1 Signal i32\n"
        scalar_array = "PUSH_CONST 7\nCAST i32\nNEW_ARRAY_LITERAL 1 i32\n"
        controls = {
            "enum-payload-order": ("PUSH_CONST 7\nCAST i32\nPUSH_CONST_STR word\nNEW_VARIANT Ordered::Pair 2 Ordered (i32,text)\nNEW_ARRAY_LITERAL 1 Ordered\nPUSH_CONST 0\nGET_INDEX\nUNWRAP_VARIANT 2 Ordered (i32,text)\nCALL println 1\nCALL println 1", "retained text\n7\n"),
            "enum-control": (enum_word + "NEW_ARRAY_LITERAL 1 Signal\nPUSH_CONST 0\nGET_INDEX\nUNWRAP_VARIANT 1 Signal i32\nCALL println 1", "7\n"),
            "nested-control": (scalar_array + "NEW_ARRAY_LITERAL 1 array<i32>\nPUSH_CONST 0\nGET_INDEX\nPUSH_CONST 0\nGET_INDEX\nCALL println 1", "7\n"),
        }
        # At allocation 1024 an automatic VM collection must retain the array
        # initializer just popped from the operand stack by either constructor.
        garbage = "NEW_ARRAY_LITERAL 0 i32\nPOP\n" * 1022
        child = "PUSH_CONST 73\nCAST i32\nNEW_ARRAY_LITERAL 1 i32\n"
        for intrinsic in (False, True):
            suffix = "intrinsic" if intrinsic else "literal"
            count = "PUSH_CONST 1\n" if intrinsic else ""
            for enum in (False, True):
                kind = "Carrier" if enum else "array<i32>"
                wrap = "NEW_VARIANT Carrier::Numbers 1 Carrier array<i32>\n" if enum else ""
                create = (f"INTEROP_CALL intrinsic array.new_with_value 2 array<{kind}> {kind}\n"
                          if intrinsic else f"NEW_ARRAY_LITERAL 1 {kind}\n")
                unwrap = "UNWRAP_VARIANT 1 Carrier array<i32>\n" if enum else ""
                controls[f"gc-{'enum' if enum else 'nested'}-{suffix}"] = (
                    garbage + count + child + wrap + create + "PUSH_CONST 0\nGET_INDEX\n" +
                    unwrap + "PUSH_CONST 0\nGET_INDEX\nCALL println 1", "73\n")
        invalid_nil = {
            "enum-wrong-owner": ("NEW_VARIANT Other::Idle 0 Other void\nNEW_ARRAY_LITERAL 1 Signal\nPOP", SCHEMAS, ENUMS),
            "enum-wrong-case": ("NEW_VARIANT signal::Idle 0 signal void\nNEW_ARRAY_LITERAL 1 Signal\nPOP", SCHEMAS, ENUMS),
            "enum-wrong-generic": ("NEW_VARIANT Generic::None 0 Generic<i64> void\nNEW_ARRAY_LITERAL 1 Generic<i32>\nPOP", SCHEMAS, ENUMS),
            "enum-wrong-namespace": ("NEW_VARIANT right::State::Idle 0 right::State void\nNEW_ARRAY_LITERAL 1 left::State\nPOP", SCHEMAS, ENUMS),
            "enum-integer": ("PUSH_CONST 0\nNEW_ARRAY_LITERAL 1 Signal\nPOP", SCHEMAS, ENUMS),
            "enum-array-handle": (scalar_array + "NEW_ARRAY_LITERAL 1 Signal\nPOP", SCHEMAS, ENUMS),
            "enum-wrong-payload": ("PUSH_CONST_STR word\nNEW_VARIANT Signal::Number 1 Signal i32\nNEW_ARRAY_LITERAL 1 Signal\nPOP", SCHEMAS, ENUMS),
            "enum-unknown": ("NEW_ARRAY_LITERAL 0 MissingEnum\nPOP", SCHEMAS, ENUMS),
            "enum-duplicate": ("NEW_ARRAY_LITERAL 0 Signal\nPOP", SCHEMAS, ENUMS + [ENUMS[0]]),
            "enum-record-collision": ("NEW_ARRAY_LITERAL 0 Signal\nPOP", SCHEMAS + [("Signal", [("value", "i32")], [])], ENUMS),
            "enum-missing-owner": ("NEW_ARRAY_LITERAL 0 State\nPOP", SCHEMAS, [("pkg::State", [("Idle", "")])]),
            "enum-symbol-spelling": ("NEW_ARRAY_LITERAL 0 pkg__State\nPOP", SCHEMAS, [("pkg::State", [("Idle", "")])]),
            "nested-wrong-kind": ("NEW_ARRAY_LITERAL 0 text\nNEW_ARRAY_LITERAL 1 array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-wrong-width": ("NEW_ARRAY_LITERAL 0 i64\nNEW_ARRAY_LITERAL 1 array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-wrong-rank": ("NEW_ARRAY_LITERAL 0 array<i32>\nNEW_ARRAY_LITERAL 1 array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-wrong-record": ("NEW_ARRAY_LITERAL 0 OtherLeaf\nNEW_ARRAY_LITERAL 1 array<Leaf>\nPOP", SCHEMAS, ENUMS),
            "nested-wrong-enum": ("NEW_ARRAY_LITERAL 0 Other\nNEW_ARRAY_LITERAL 1 array<Signal>\nPOP", SCHEMAS, ENUMS),
            "nested-forged-integer": ("PUSH_CONST 1\nNEW_ARRAY_LITERAL 1 array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-null": ("PUSH_CONST 0\nNEW_ARRAY_LITERAL 1 array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-unknown-leaf": ("NEW_ARRAY_LITERAL 0 array<MissingLeaf>\nPOP", SCHEMAS, ENUMS),
            "nested-wrong-push": ("NEW_ARRAY_LITERAL 0 array<i32>\nNEW_ARRAY_LITERAL 0 text\nCALL array__push 2", SCHEMAS, ENUMS),
            "nested-wrong-set": (scalar_array + "NEW_ARRAY_LITERAL 1 array<i32>\nPUSH_CONST 0\nNEW_ARRAY_LITERAL 0 text\nSET_INDEX", SCHEMAS, ENUMS),
            "enum-forged-intrinsic": ("PUSH_CONST 1\nPUSH_CONST 0\nINTEROP_CALL intrinsic array.get 2 Signal Signal\nPOP", SCHEMAS, ENUMS),
            "nested-forged-intrinsic": ("PUSH_CONST 1\nPUSH_CONST 0\nINTEROP_CALL intrinsic array.get 2 array<i32> array<i32>\nPOP", SCHEMAS, ENUMS),
            "nested-sensitive-cast": ("NEW_ARRAY_LITERAL 0 array<anything>\nCAST array<array<i64>>\nPOP", SCHEMAS, ENUMS),
            "nested-sensitive-empty-launder": ("NEW_ARRAY_LITERAL 0 anything\nCAST array<array<i64>>\nPOP", SCHEMAS, ENUMS),
            "nested-sensitive-merge": ("PUSH_CONST 1\nJMP_IF_NOT scalar\nNEW_ARRAY_LITERAL 0 array<anything>\nJMP merged\nLABEL scalar\nPUSH_CONST 0\nLABEL merged\nSTORE_LOCAL mixed\nLOAD_LOCAL mixed\nCAST array<array<i64>>\nPOP", SCHEMAS, ENUMS),
            "enum-sensitive-unknown-call": ("CALL missing_provider 0\nNEW_ARRAY_LITERAL 1 array<Boxed>\nPOP", SCHEMAS, ENUMS),
            "nested-sensitive-unknown-call": ("CALL missing_provider 0\nNEW_ARRAY_LITERAL 1 array<anything>\nPOP", SCHEMAS, ENUMS),
            "enum-sensitive-opaque": ("INTEROP_CALL native missing_provider 0 array<Boxed>\nPOP", SCHEMAS, ENUMS),
            "nested-sensitive-opaque": ("INTEROP_CALL native missing_provider 0 array<array<anything>>\nPOP", SCHEMAS, ENUMS),
        }
        for binary in (False, True):
            for name, (body, expected) in controls.items():
                path = encoded(name, body, binary)
                result = run([interpreter, "--no-ai", path])
                assert result.stdout == expected and not result.stderr, result
                checks += 1
                if args.llvm == "ON":
                    artifact = root / (name + ".o")
                    run([assembler, "-o", artifact, path])
                    assert artifact.is_file() and artifact.stat().st_size > 0
                    checks += 1
            for name, (body, schemas, enums) in invalid_nil.items():
                path = encoded(name, body, binary, schemas=schemas, enums=enums)
                result = run([interpreter, "--no-ai", path], success=False)
                assert not result.stdout and result.stderr, (name, result)
                if "sensitive-opaque" in name:
                    assert "provenance" in result.stderr.lower(), result
                if "sensitive-unknown-call" in name:
                    assert "supported exact result signature" in result.stderr.lower(), result
                checks += 1
                if args.llvm == "ON":
                    artifact = root / (name + ".o")
                    artifact.write_bytes(b"preserve previous object\n")
                    result = run([assembler, "-o", artifact, path], success=False)
                    assert result.stderr and artifact.read_bytes() == b"preserve previous object\n", result
                    checks += 1
            if args.llvm == "ON":
                recursive = [
                    ("recursive-enum", "NEW_ARRAY_LITERAL 0 Recursive\nPOP", [],
                     [("Recursive", [("Idle", ""), ("Next", "array<Recursive>")])]),
                    ("recursive-enum-short-payload", "NEW_ARRAY_LITERAL 0 pkg::Recursive\nPOP", [],
                     [("pkg::Recursive", [("Idle", ""), ("Next", "Recursive")])]),
                    ("recursive-nested-record", "NEW_ARRAY_LITERAL 0 Recursive\nPOP",
                     [("Recursive", [("children", "array<array<Recursive>>")], [])], []),
                ]
                for name, body, schemas, enums in recursive:
                    path = encoded(name, body, binary, schemas=schemas, enums=enums)
                    artifact = root / (name + ".o")
                    run([assembler, "-o", artifact, path])
                    assert artifact.is_file() and artifact.stat().st_size > 0
                    checks += 1

        if args.llvm == "ON":
            # Dynamic subclasses remain reachable through the enum's Base
            # payload. The finite graph may contain a nominal back edge.
            for payload in ("ReviewBase", "ref<ReviewBase>"):
                path = root / ("recursive-derived-" + ("reference" if payload.startswith("ref") else "plain") + ".ny")
                path.write_text("""class ReviewBase
    value: i32
end class
class ReviewDerived inherits ReviewBase
    child: ReviewEnum
end class
enum ReviewEnum
    Item(""" + payload + """)
end enum
fn main() -> i32
    let items: array<ReviewEnum> = []
    return 0
end fn
""", encoding="utf-8", newline="\n")
                for jit in (False, True):
                    artifact = linked_executable_path(root / (path.stem + ("-jit" if jit else "-aot")))
                    compiled = run([*base, "--native", *(["--jit"] if jit else []),
                                   "-o", artifact, path])
                    result = compiled if jit else run([artifact])
                    assert not result.stdout and not result.stderr, result
                    checks += 1

        # The same direct ABI rejection cases run with static and shared runtime
        # linkage; subprocesses must fail through nyrt_fail, never a signal.
        for runtime_tests in args.runtime_tests:
            for mode in ("enum-identity-case", "enum-identity-generic", "enum-wrong-kind",
                         "enum-default", "enum-empty-read", "enum-null-push",
                         "nested-default", "nested-empty-read", "nested-null-push",
                         "nested-forged-child", "nested-pointer-child", "nested-wrong-width",
                         "nested-wrong-record", "nested-wrong-enum", "nested-wrong-depth",
                         "nested-stale-child", "nested-foreign-parent"):
                result = run([runtime_tests.resolve(), "--typed-array-failure", mode], success=False)
                assert not result.stdout and "NIL native runtime error:" in result.stderr, result
                checks += 1
    print(f"enum and nested array contract: {checks} checks passed")


if __name__ == "__main__":
    main()
