#!/usr/bin/env python3
"""Reject unsupported binding references while preserving aggregate handle aliases."""

from __future__ import annotations

import argparse
import os
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
    declarations = """struct Counter
value: i32
end struct
enum Choice
One
end enum
"""

    def program(body: str, extra: str = "") -> str:
        return declarations + extra + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    cases = {
        "scalar_integer": program("let value: i32 = 3\nlet alias = ref value"),
        "scalar_integer_wide": program("let value: i64 = 3\nlet alias = ref value"),
        "scalar_float": program("let value: f64 = 3.5\nlet alias = ref value"),
        "scalar_bool": program("let value: bool = true\nlet alias = ref value"),
        "scalar_byte": program("let value: u8 = 3 as u8\nlet alias = ref value"),
        "scalar_text": program('let value: text = "text"\nlet alias = ref value'),
        "scalar_alias": program("let value: Word = 3\nlet alias = ref value", "type Word = i32\n"),
        "array": program("let value: array<i32> = [1, 2]\nlet alias = ref value"),
        "raw_pointer": program("let value = allocate<i32>(1)\nlet alias = ref value\ndeallocate(value)"),
        "enum": program("let value = Choice.One\nlet alias = ref value"),
        "function": program("let value = increment\nlet alias = ref value", "fn increment(value: i32) -> i32 { return value + 1 }\n"),
        "erased_any": program("let value: any = 3\nlet alias = ref value"),
        "nested_reference": program("let value = Counter { value: 3 }\nlet alias = ref value\nlet nested = ref alias"),
        "temporary_aggregate": program("let alias = ref Counter { value: 3 }"),
        "field_aggregate": program("let value = Box { item: Counter { value: 3 } }\nlet alias = ref value.item", "struct Box\nitem: Counter\nend struct\n"),
        "global_aggregate": program("let alias = ref value", "let value = Counter { value: 3 }\n"),
        "read_scalar_reference": program("let mut value: i32 = 3\nlet alias = ref value\nprintln(*alias)"),
        "write_scalar_reference": program("let mut value: i32 = 3\nlet alias = ref value\n*alias = 9\nprintln(value)"),
        "return_scalar_reference": program("let alias = create()\nprintln(*alias)", "fn create() -> ref<i32>\nlet value: i32 = 3\nreturn ref value\nend fn\n"),
        "read_aggregate_reference": program("let value = Counter { value: 3 }\nlet alias = ref value\nlet dereferenced = *alias\nprintln(dereferenced.value)"),
        "write_aggregate_reference": program("let value = Counter { value: 3 }\nlet alias = ref value\n*alias = Counter { value: 9 }"),
        "write_dereferenced_field": program("let value = Counter { value: 3 }\nlet alias = ref value\n(*alias).value = 9"),
        "parameter_reference_read": program("", "fn read(value: ref<Counter>) -> Counter\nreturn *value\nend fn\n"),
        "parameter_reference_write": program("", "fn write(value: ref<Counter>)\n*value = Counter { value: 9 }\nend fn\n"),
        "invalid_proof_after_error": program('let value = Counter { value: 3 }\nlet alias = ref value\nlet wrong: i32 = "not an integer"'),
        "scalar_named_like_type": program("let Counter: i32 = 3\nlet alias = ref Counter"),
        "generic_parameter_shadows_nominal": program("let alias = unproven<i32>(3)", "fn unproven<Counter>(value: Counter) -> ref<Counter>\nreturn ref value\nend fn\n"),
    }
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    sentinel = b"existing output must survive reference rejection\n"
    with tempfile.TemporaryDirectory(prefix="ny-reference-contract-") as temporary:
        root = Path(temporary)
        for name, source in cases.items():
            path = root / f"{name}.ny"
            path.write_text(source, encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / f"{name}-{mode}.nil"
                output.write_bytes(sentinel)
                result = subprocess.run(
                    [str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *flags,
                     "--output", str(output), str(path)],
                    cwd=root, env=environment, stdin=subprocess.DEVNULL,
                    capture_output=True, text=True, timeout=30, check=False)
                if result.returncode <= 0 or not any(marker in result.stderr for marker in (
                    "Type checking failed:", "Code generation failed", "codegen", "error:", "Error:"
                )):
                    raise AssertionError(f"{name}/{mode}: expected semantic rejection; exit={result.returncode}\n{result.stdout}\n{result.stderr}")
                if "Parse error" in result.stderr or "Syntax error" in result.stderr:
                    raise AssertionError(f"{name}/{mode}: did not reach semantic checking\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejection replaced existing output")

        fixture = Path(__file__).resolve().parent.parent / "language" / "reference-handles.ny"
        expected = fixture.with_suffix(".stdout").read_text(encoding="utf-8")
        for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                            ("unchecked-text", ["--no-typecheck"]),
                            ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
            output = root / f"positive-{mode}.nil"
            compiled = subprocess.run(
                [str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *flags,
                 "--output", str(output), str(fixture)],
                cwd=root, env=environment, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, timeout=30, check=False)
            if compiled.returncode != 0:
                raise AssertionError(f"positive/{mode}: compile failed\n{compiled.stdout}\n{compiled.stderr}")
            executed = subprocess.run(
                [str(interpreter), "--no-ai", str(output)], cwd=root, env=environment,
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30, check=False)
            if (executed.returncode, executed.stdout, executed.stderr) != (0, expected, ""):
                raise AssertionError(f"positive/{mode}: execution mismatch\n{executed.stdout}\n{executed.stderr}")
    print(f"reference contract: {len(cases) * 4} rejected compilations; 4 aggregate-handle execution controls passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
