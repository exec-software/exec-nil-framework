#!/usr/bin/env python3
"""Tuple-array schemas and initialized values survive raw text/binary NIL."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


PAIR = "tuple<i64,text>"
SCHEMAS = [(PAIR, [("0", "i64"), ("1", "text")], ["@nil_tuple"])]


def instructions(source: str) -> list[tuple[str, list[str]]]:
    return [(parts[0], parts[1:]) for line in source.splitlines()
            if (parts := line.split())]


def encode(body, schemas, binary):
    body = body + instructions("PUSH_CONST 0\nRET")
    if not binary:
        lines = ['module tuple_array_nil', 'const word = "retained text"']
        for name, fields, annotations in schemas:
            lines += annotations + ["struct " + name]
            lines += [field + ": " + kind for field, kind in fields]
            lines += ["end struct"]
        lines += ["function main() -> i32:"]
        lines += [" ".join([opcode, *(json.dumps(argument) for argument in args)])
                  for opcode, args in body]
        return ("\n".join(lines + ["end function", ""])).encode()

    def u32(value):
        return struct.pack("<I", value)

    def string(value):
        raw = value.encode()
        return u32(len(raw)) + raw

    types = u32(len(schemas))
    for name, fields, annotations in schemas:
        types += string(name) + u32(len(fields))
        types += b"".join(string(field) + string(kind) for field, kind in fields)
        types += u32(len(annotations)) + b"".join(map(string, annotations))
    types += u32(0)
    function = (u32(1) + string("main") + u32(0) + u32(0) + string("i32") +
                u32(0) + u32(0) + u32(0) + u32(len(body)))
    for opcode, args in body:
        function += string(opcode) + u32(len(args))
        function += b"".join(map(string, args)) + u32(0)
    sections = [(1, types), (2, function),
                (3, u32(1) + string("word") + string("retained text")),
                (4, u32(0)), (5, string("tuple_array_nil") + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return b"NIL\0" + struct.pack("<III", 3, 2, len(sections)) + table + b"".join(
        payload for _, payload in sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("interpreter", "assembler", "linker", "runtime"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    paths = {name: getattr(args, name).resolve() for name in
             ("interpreter", "assembler", "linker", "runtime")}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_"))}
    checks = 0
    construct = instructions(f"NEW_STRUCT {PAIR}\nPUSH_CONST 42\nSET_FIELD 0\n"
                             "PUSH_CONST_STR word\nSET_FIELD 1")
    partial = instructions(f"NEW_STRUCT {PAIR}\nPUSH_CONST 42\nSET_FIELD 0")
    control = construct + instructions(f"NEW_ARRAY_LITERAL 1 {PAIR}\nSTORE_LOCAL values\n"
        "LOAD_LOCAL values\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD 0\nCALL println 1\n"
        "LOAD_LOCAL values\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD 1\nCALL println 1\n"
        f"PUSH_CONST 2\nLOAD_LOCAL values\nPUSH_CONST 0\nGET_INDEX\n"
        f"INTEROP_CALL intrinsic array.new_with_value 2 array<{PAIR}> {PAIR}\nSTORE_LOCAL filled\n"
        f"LOAD_LOCAL filled\nINTEROP_CALL intrinsic array.length 1 i32 {PAIR}\nCALL println 1\n"
        f"LOAD_LOCAL filled\nPUSH_CONST 1\nINTEROP_CALL intrinsic array.get 2 {PAIR} {PAIR}\n"
        "GET_FIELD 0\nCALL println 1")
    expected = "42\nretained text\n2\n42\n"
    nested_pair = "tuple<array<i64>,text>"
    nested_schemas = [(nested_pair, [("0", "array<i64>"), ("1", "text")], ["@nil_tuple"])]
    # The VM collects on allocation 1024. At that precise boundary the only
    # owner of allocation 1023 is the tuple initializer popped by the intrinsic.
    # Publishing the result before collecting must retain that nested array.
    gc_control = instructions("NEW_ARRAY_LITERAL 0 i64\nPOP") * 1022
    gc_control += instructions(f"PUSH_CONST 1\nNEW_STRUCT {nested_pair}\n"
        "PUSH_CONST 73\nNEW_ARRAY_LITERAL 1 i64\nSET_FIELD 0\n"
        "PUSH_CONST_STR word\nSET_FIELD 1\n"
        f"INTEROP_CALL intrinsic array.new_with_value 2 array<{nested_pair}> {nested_pair}\n"
        "PUSH_CONST 0\nGET_INDEX\nGET_FIELD 0\nPUSH_CONST 0\nGET_INDEX\nCALL println 1")
    invalid = [
        ("missing-schema", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}\nPOP"), []),
        ("unannotated-schema", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}\nPOP"),
         [(PAIR, SCHEMAS[0][1], [])]),
        ("wrong-field-order", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}\nPOP"),
         [(PAIR, [("1", "text"), ("0", "i64")], ["@nil_tuple"])]),
        ("wrong-field-type", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}\nPOP"),
         [(PAIR, [("0", "text"), ("1", "text")], ["@nil_tuple"])]),
        ("partial-literal", partial + instructions(f"NEW_ARRAY_LITERAL 1 {PAIR}\nPOP"), SCHEMAS),
        ("partial-push", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}") + partial +
         instructions("CALL array__push 2"), SCHEMAS),
        ("partial-set", construct + instructions(f"NEW_ARRAY_LITERAL 1 {PAIR}\nPUSH_CONST 0") +
         partial + instructions("SET_INDEX"), SCHEMAS),
        ("partial-create", instructions("PUSH_CONST 2") + partial +
         instructions(f"INTEROP_CALL intrinsic array.new_with_value 2 array<{PAIR}> {PAIR}\nPOP"), SCHEMAS),
        ("scalar-insertion", instructions(f"PUSH_CONST 42\nNEW_ARRAY_LITERAL 1 {PAIR}\nPOP"), SCHEMAS),
        ("intrinsic-result-shape", instructions("PUSH_CONST 2") + construct +
         instructions(f"INTEROP_CALL intrinsic array.new_with_value 2 array<tuple<text,i64>> {PAIR}\nPOP"), SCHEMAS),
    ]
    nominal_schemas = [
        ("Widget", [("value", "i64")], []),
        ("widget", [("value", "i64")], []),
        ("tuple<Widget>", [("0", "Widget")], ["@nil_tuple"]),
        ("tuple<widget>", [("0", "widget")], ["@nil_tuple"]),
    ]
    nominal_tuple = instructions("NEW_STRUCT tuple<Widget>\nNEW_STRUCT Widget\n"
                                 "PUSH_CONST 73\nSET_FIELD value\nSET_FIELD 0")
    invalid += [
        ("case-different-array", nominal_tuple +
         instructions("NEW_ARRAY_LITERAL 1 tuple<widget>\nPOP"), nominal_schemas),
        ("case-different-field", instructions("NEW_STRUCT tuple<Widget>\nNEW_STRUCT widget\n"
         "PUSH_CONST 73\nSET_FIELD value\nSET_FIELD 0\nNEW_ARRAY_LITERAL 1 tuple<Widget>\nPOP"), nominal_schemas),
    ]

    with tempfile.TemporaryDirectory(prefix="nil-tuple-array-wire-") as directory:
        root = Path(directory)

        def run(command):
            return subprocess.run([str(value) for value in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60)

        def success(result, output=None):
            assert result.returncode == 0 and not result.stderr, result
            assert output is None or result.stdout == output, result

        def source(name, body, schemas, binary):
            path = root / (name + (".binary.nil" if binary else ".text.nil"))
            path.write_bytes(encode(body, schemas, binary))
            return path

        def native(path, name, output):
            nonlocal checks
            obj, executable = root / (name + ".o"), root / name
            success(run([paths["assembler"], "-o", obj, path]))
            success(run([paths["linker"], "--runtime", paths["runtime"],
                         "-o", executable, obj]))
            success(run([executable]), output)
            checks += 1

        for binary in (False, True):
            path = source("valid", control, SCHEMAS, binary)
            success(run([paths["interpreter"], "--no-ai", path]), expected)
            checks += 1
            gc_path = source("initializer-gc", gc_control, nested_schemas, binary)
            success(run([paths["interpreter"], "--no-ai", gc_path]), "73\n")
            checks += 1
            if args.llvm == "ON":
                native(path, "valid", expected)
                native(gc_path, "initializer-gc", "73\n")
                identity = nominal_tuple + instructions(
                    "NEW_ARRAY_LITERAL 1 tuple<Widget>\nCAST array<tuple<Widget>>\n"
                    "PUSH_CONST 0\nGET_INDEX\nGET_FIELD 0\nGET_FIELD value\nCALL println 1")
                native(source("identity", identity, nominal_schemas, binary), "identity", "73\n")
            for name, body, schemas in invalid:
                path = source(name, body, schemas, binary)
                result = run([paths["interpreter"], "--no-ai", path])
                assert result.returncode > 0 and not result.stdout, (name, result)
                assert "tuple" in result.stderr.lower() or "array intrinsic" in result.stderr, (name, result)
                checks += 1
                if args.llvm == "ON":
                    obj = root / "protected.o"
                    sentinel = b"preserve existing object\n"
                    obj.write_bytes(sentinel)
                    result = run([paths["assembler"], "-o", obj, path])
                    assert result.returncode > 0 and not result.stdout, (name, result)
                    assert obj.read_bytes() == sentinel, name
                    checks += 1
            if args.llvm == "ON":
                cast = nominal_tuple + instructions(
                    "NEW_ARRAY_LITERAL 1 tuple<Widget>\nCAST array<tuple<widget>>\nPOP")
                path = source("cross-case-cast", cast, nominal_schemas, binary)
                result = run([paths["assembler"], "-o", root / "cast.o", path])
                assert result.returncode > 0 and "only exact compatible handle casts" in result.stderr, result
                checks += 1
            for name, body in [
                ("empty-get", instructions(f"NEW_ARRAY_LITERAL 0 {PAIR}\nPUSH_CONST 0\nGET_INDEX\nPOP")),
                ("negative-get", construct + instructions(f"NEW_ARRAY_LITERAL 1 {PAIR}\nPUSH_CONST -1\nGET_INDEX\nPOP")),
                ("intrinsic-get", construct + instructions(f"NEW_ARRAY_LITERAL 1 {PAIR}\nPUSH_CONST 5\n"
                    f"INTEROP_CALL intrinsic array.get 2 {PAIR} {PAIR}\nPOP")),
            ]:
                path = source(name, body, SCHEMAS, binary)
                result = run([paths["interpreter"], "--no-ai", path])
                assert result.returncode > 0 and not result.stdout and "tuple array read" in result.stderr, result
                checks += 1
                if args.llvm == "ON":
                    obj, executable = root / "bounds.o", root / "bounds"
                    success(run([paths["assembler"], "-o", obj, path]))
                    success(run([paths["linker"], "--runtime", paths["runtime"], "-o", executable, obj]))
                    result = run([executable])
                    assert result.returncode > 0 and not result.stdout and "tuple array read" in result.stderr, result
                    checks += 1
    print(f"tuple array NIL contract: {checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
