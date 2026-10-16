#!/usr/bin/env python3
"""Keep declaration, visibility and mutability contracts under lexical storage."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)

    def program(body: str) -> str:
        return "fn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    duplicate_diagnostic = "already declared in this scope"
    cases = {
        "match_statement_numeric_text_pattern": (
            program('match 12\n"12" -> println(1)\n_ -> println(0)\nend match'),
            "Pattern type 'String' incompatible with match target type",
        ),
        "match_expression_numeric_text_pattern": (
            program('println(match 12 { "12" -> 1; _ -> 0; })'),
            "Pattern type 'String' incompatible with match target type",
        ),
        "match_expression_text_boolean_pattern": (
            program('println(match "true" { true -> 1; _ -> 0; })'),
            "Pattern type 'bool' incompatible with match target type",
        ),
        "match_statement_boolean_text_pattern": (
            program('match true\n"true" -> println(1)\n_ -> println(0)\nend match'),
            "Pattern type 'String' incompatible with match target type",
        ),
        "match_expression_text_character_pattern": (
            program('println(match "1" { \'1\' -> 1; _ -> 0; })'),
            "Pattern type 'u8' incompatible with match target type",
        ),
        "if_expression_condition": (
            program('println(if "false" then 7 else 9 end if)'),
            "If expression condition must be boolean",
        ),
        "if_expression_dead_arm": (
            program('println(if true then 7 else "wrong" end if)'),
            "If expression branch types must be consistent",
        ),
        "if_expression_selected_arm": (
            program('println(if false then 7 else "wrong" end if)'),
            "If expression branch types must be consistent",
        ),
        "if_expression_boolean_integer": (
            program("println(if true then true else 7 end if)"),
            "If expression branch types must be consistent",
        ),
        "if_expression_void_arm": (
            "fn no_value()\nend fn\n" +
            program("println(if true then 7 else no_value() end if)"),
            "If expression branches must produce values",
        ),
        "match_expression_boolean_integer": (
            program("println(match 2 { 1 -> true; _ -> 7; })"),
            "Match case types must be consistent",
        ),
        "match_expression_text_integer": (
            program('println(match 2 { 1 -> 7; _ -> "wrong"; })'),
            "Match case types must be consistent",
        ),
        "function_scalar_cast": (
            "fn callback() -> i64\nreturn 7\nend fn\n" +
            program("println(callback as i64)"),
            "Cannot cast a function value to scalar type 'i64'",
        ),
        "typed_duplicate": (program("let value: i64 = 1\nlet mut value: i64 = 2"), duplicate_diagnostic),
        "inferred_duplicate": (program("let value = 1\nlet mut value = 2"), duplicate_diagnostic),
        "global_duplicate": ("let value: i64 = 1\nlet mut value: i64 = 2\n" + program(""), duplicate_diagnostic),
        "outer_mutability": (program("let value: i64 = 1\nif true then\nlet mut value: i64 = 2\nend if\nvalue = 3"), "Assignment to immutable variable"),
        "inner_mutability": (program("let mut value: i64 = 1\nif true then\nlet value: i64 = 2\nvalue = 3\nend if"), "Assignment to immutable variable"),
        "block_visibility": (program("if true then\nlet inner: i64 = 2\nend if\nprintln(inner)"), "Undefined identifier"),
        "loop_visibility": (program("for inner in range(2)\nprintln(inner)\nend for\nprintln(inner)"), "Undefined identifier"),
        "lambda_capture": (program("let value: i64 = 1\nlet callback = fn() -> i64\nreturn value\nend fn"), "lambda capture of local"),
        "lambda_capture_global_collision": ("let value: i64 = 99\n" + program("let value: i64 = 1\nlet callback = fn() -> i64\nreturn value\nend fn"), "lambda capture of local"),
        "match_expression_missing_default": (
            program("let selected = match 1 { 1 -> 10; }"),
            "match expression requires a final unguarded wildcard arm",
        ),
        "match_expression_guarded_default": (
            program("let selected = match 1 { _ when true -> 10; }"),
            "match expression requires a final unguarded wildcard arm",
        ),
        "match_expression_void_arm": (
            "fn no_value()\nend fn\n" +
            program("let selected = match 1 { _ -> no_value(); }"),
            "match expression arm does not produce a value",
        ),
        "match_expression_assignment_arm": (
            program("let mut value: i64 = 0\n"
                    "let selected = match 1 { _ -> value = 2; }"),
            "match expression arm does not produce a value",
        ),
        "match_expression_statement_arm": (
            program("let selected = match 1 { _ -> return 0; }"),
            "match expression arms require a single value expression",
        ),
        "asm_mutability_bypass": (
            program("let result: i64 = 1\nif true then\nlet mut result: i64 = 2\nend if\n"
                    'asm "x86_64", "xorq $0, $0" () -> result end asm'),
            "mutable i64 local",
        ),
    }
    valid = """let mut shared: i64 = 10

enum marker
    available
end enum

fn first() -> i64
    let value: i64 = 7
    return value
end fn

fn second() -> i64
    let value: i64 = 8
    return value
end fn

fn local_global_shadow() -> i64
    let mut shared: i64 = 20
    shared = shared + 1
    return shared
end fn

fn main() -> i32
    let immutable: i64 = 5
    if true then
        let inner: i64 = 6
        println(inner)
    end if
    let available: i64 = 9
    println(immutable)
    println(first())
    println(second())
    println(available)
    println(local_global_shadow())
    println(shared)
    if true then
        let sibling: i64 = 11
        println(sibling)
    end if
    if true then
        let sibling: i64 = 12
        println(sibling)
    end if
    for i in range(0, 2)
        println(i)
    end for
    for i in range(2, 4)
        println(i)
    end for
    return 0
end fn
"""
    interpreted_controls = {
        "references": ("struct Number\nvalue: i64\nend struct\n" + program(
            "let value = Number { value: 11 }\nlet alias = ref value\nprintln(alias.value)\n"
            "if true then\nlet value = Number { value: 22 }\nlet alias = ref value\n"
            "println(alias.value)\nend if\nprintln(alias.value)"), "11\n22\n11\n"),
        "function_values": (
            "fn first() -> i64\nreturn 11\nend fn\n"
            "fn second() -> i64\nreturn 22\nend fn\n" +
            program("let callback = first\nprintln(callback())\n"
                    "if true then\nlet callback = second\n"
                    "println(callback())\nend if\nprintln(callback())\n"
                    "let first = second\nprintln(first())"),
            "11\n22\n11\n22\n"),
        "lambda_parameters": (program(
            "let callback = fn(value: i64) -> i64\n"
            "let value: i64 = value + 1\nreturn value\nend fn\n"
            "println(callback(8 as i64))"), "9\n"),
        "void_function_value": (
            "fn no_value()\nend fn\n" +
            program("no_value;\nprintln(17)"), "17\n"),
        "tuple_loop_bindings": (program(
            "let left: i32 = 7\nlet right: i32 = 8\n"
            "for (left, right) in [[0, 0], [1, 1]]\n"
            "println(left + right)\nend for\nprintln(left)\nprintln(right)"),
            "0\n2\n7\n8\n"),
        "shadowed_iterator_helper": (program(
            "let range = fn(count: i32) -> array<i32>\n"
            "return [8, 9]\nend fn\n"
            "for value in range(1)\nprintln(value)\nend for"), "8\n9\n"),
    }

    with tempfile.TemporaryDirectory(prefix="ny-variable-scope-") as temporary:
        root = Path(temporary)

        def compile_source(name: str, source: str, extra=()):
            path = root / (name + ".ny")
            output = root / (name + ".nil")
            path.write_text(source, encoding="utf-8")
            output.write_bytes(b"previous artifact")
            process = subprocess.run(
                [str(compiler), "--no-stdlib", "--quiet", "--no-bundle",
                 *extra, "--output", str(output), str(path)],
                stdin=subprocess.DEVNULL, capture_output=True, text=True,
                timeout=60, check=False,
            )
            return process, output

        for name, (source, diagnostic) in cases.items():
            process, output = compile_source(name, source)
            if process.returncode <= 0 or diagnostic not in process.stderr:
                raise AssertionError(f"{name}: expected {diagnostic!r}: {process.stderr}")
            if output.read_bytes() != b"previous artifact":
                raise AssertionError(f"{name}: rejected declaration replaced prior artifact")

        process, output = compile_source("distinct_scopes", valid)
        if process.returncode:
            raise AssertionError("valid declarations were rejected: " + process.stderr)
        interpreted = subprocess.run(
            [str(interpreter), "--no-ai", str(output)], stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=60, check=False,
        )
        if interpreted.returncode or interpreted.stdout != "6\n5\n7\n8\n9\n21\n10\n11\n12\n0\n1\n2\n3\n":
            raise AssertionError(f"valid scopes: {interpreted.returncode}: {interpreted.stdout!r} {interpreted.stderr}")

        for name, (source, expected) in interpreted_controls.items():
            for binary in (False, True):
                process, output = compile_source(
                    name + ("_binary" if binary else "_text"), source,
                    ("--emit-binary-nil",) if binary else ())
                if process.returncode:
                    raise AssertionError(f"{name}: compile failed: {process.stderr}")
                result = subprocess.run(
                    [str(interpreter), "--no-ai", str(output)],
                    stdin=subprocess.DEVNULL, capture_output=True, text=True,
                    timeout=60, check=False)
                if result.returncode or result.stdout != expected or result.stderr:
                    raise AssertionError(f"{name}: {result.returncode}: {result.stdout!r} {result.stderr}")

        # Skipping type checking must not re-expose a completed lexical slot.
        for name in ("block_visibility", "loop_visibility"):
            process, output = compile_source(name + "_unchecked", cases[name][0],
                                             ("--no-typecheck",))
            if process.returncode <= 0 or "outside its lexical scope" not in process.stderr:
                raise AssertionError(f"{name}: unchecked visibility: {process.stderr}")
            if output.read_bytes() != b"previous artifact":
                raise AssertionError(f"{name}: unchecked rejection replaced prior artifact")

    print(f"variable scope contracts: {len(cases) + 2} rejected cases, "
          f"{1 + 2 * len(interpreted_controls)} accepted controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
