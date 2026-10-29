#!/usr/bin/env python3
"""Reject invalid stores and struct construction before publishing NIL."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)

    def program(body: str, declarations: str = "") -> str:
        return declarations + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    record = "struct Record\nvalue: i64\nend struct\n"
    generic = "struct Box<T>\nvalue: T\nend struct\n"
    cases = {
        "field_type": (program('let value = Record { value: 7 }\nvalue.value = "wrong"', record),
                       "Type mismatch in field assignment. Expected 'i64'"),
        "generic_field_type": (program('let value = Box<i64> { value: 7 }\nvalue.value = "wrong"', generic),
                               "Type mismatch in field assignment. Expected 'i64'"),
        "generic_field_array_type": (program('let value = Box<array<i32>> { value: [] }\nvalue.value = ["wrong"]', generic),
                                     "Type mismatch in field assignment. Expected 'array<i32>'"),
        "unknown_field": (program('let value = Record { value: 7 }\nvalue.missing = 8', record),
                          "has no assignable field 'missing'"),
        "method_field": (program('let value = Record { value: 7 }\nvalue.read = 8', record +
                                 "impl Record\nfn read(self: ref<Self>) -> i64\nreturn self.value\nend fn\nend impl\n"),
                         "has no assignable field 'read'"),
        "synthetic_array_property": (program('let values = [1, 2]\nvalues.len = 9'),
                                     "has no assignable field 'len'"),
        "static_field": (program('Record.value = 9', record),
                         "has no assignable field 'value'"),
        "enum_variant": (program('Choice.ready = Choice.ready', "enum Choice\nready\nend enum\n"),
                         "has no assignable field 'ready'"),
        "literal_target": (program('1 = 2'), "Invalid assignment target"),
        "struct_literal_type": (program('let value = Record { value: "wrong" }', record),
                                "Type mismatch in struct field 'value'. Expected 'i64'"),
        "generic_literal_type": (program('let value = Box<i64> { value: "wrong" }', generic),
                                 "Type mismatch in struct field 'value'. Expected 'i64'"),
        "struct_literal_pointer_type": (program('let p = allocate<i32>(1)\nlet value = Record { value: p }', record),
                                        "Type mismatch in struct field 'value'. Expected 'i64'"),
        "index_type": (program('let values = [1, 2]\nvalues[0] = "wrong"'),
                       "Type mismatch in indexed assignment"),
        "pointer_index_type": (program('let p = allocate<i32>(1)\np[0] = "wrong"'),
                               "Type mismatch in indexed assignment"),
        "pointer_deref_type": (program('let p = allocate<i32>(1)\n*p = "wrong"'),
                               "Type mismatch in dereference assignment"),
        "byte_pointer_value": (program('let p = allocate<u8>(1)\nlet q = allocate<i32>(1)\n*p = q'),
                               "Type mismatch in dereference assignment"),
        "byte_index_pointer_value": (program('let p = allocate<u8>(1)\nlet q = allocate<i32>(1)\np[0] = q'),
                                     "Type mismatch in indexed assignment"),
        "generic_pointer_field": (program('let p = allocate<i32>(1)\nlet q = allocate<i64>(1)\nlet value = Box<ptr<i32>> { value: p }\nvalue.value = q', generic),
                                  "Type mismatch in field assignment. Expected 'ptr<i32>'"),
        "array_push_float_from_integer": (program('let mut values: array<f64> = []\nvalues.push(4)'),
                                           "Array.push element type mismatch. Expected 'f64'"),
        "array_push_text_from_integer": (program('let mut values: array<text> = []\nvalues.push(4)'),
                                          "Array.push element type mismatch. Expected 'String'"),
        "array_push_float_from_text": (program('let mut values: array<f64> = []\nvalues.push("wrong")'),
                                        "Array.push element type mismatch. Expected 'f64'"),
        "array_push_missing_argument": (program('let mut values: array<f64> = []\nvalues.push()'),
                                         "Array.push expects 1 argument, got 0"),
        "array_push_extra_argument": (program('let mut values: array<f64> = []\nvalues.push(1.0, 2.0)'),
                                       "Array.push expects 1 argument, got 2"),
    }
    environment = os.environ.copy()
    for variable in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(variable, None)
    sentinel = b"previous artifact must survive rejected source\n"
    with tempfile.TemporaryDirectory(prefix="ny-assignment-contract-") as temporary:
        root = Path(temporary)
        for name, (source, diagnostic) in cases.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                output.write_bytes(sentinel)
                result = subprocess.run(
                    [str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                     *extra, "--output", str(output), str(path)],
                    cwd=root, env=environment, stdin=subprocess.DEVNULL,
                    capture_output=True, text=True, timeout=60, check=False,
                )
                if result.returncode <= 0 or diagnostic not in result.stderr:
                    raise AssertionError(
                        f"{name}/{mode}: expected {diagnostic!r}; "
                        f"exit={result.returncode}\n{result.stdout}\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejected source replaced prior artifact")
        control = root / "array-push-valid.ny"
        control.write_text(program(
            'let mut floats: array<f64> = []\nfloats.push(4 as f64)\n'
            'let mut texts: array<text> = []\ntexts.push("four")\n'
            'let mut bytes: array<u8> = []\nbytes.push(257)\n'
            'let mut flags: array<bool> = []\nflags.push(1)'
        ), encoding="utf-8")
        for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
            output = root / ("array-push-valid-" + mode + ".nil")
            result = subprocess.run(
                [str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                 *extra, "--output", str(output), str(control)],
                cwd=root, env=environment, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, timeout=60, check=False,
            )
            if result.returncode or not output.is_file():
                raise AssertionError(f"valid array.push/{mode} rejected: {result.stderr}")
    print(f"assignment contracts: {len(cases) * 2} rejected compilations, 2 accepted controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
