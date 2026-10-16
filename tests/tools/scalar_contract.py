#!/usr/bin/env python3
"""Check scalar errors across source/NIL/bundle/native execution boundaries."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    base = [args.compiler.resolve(), "--quiet", "--no-stdlib"]
    environment = os.environ.copy()
    for key in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(key, None)

    with tempfile.TemporaryDirectory(prefix="ny-scalar-contract-") as temporary:
        root = Path(temporary)

        def run(argv):
            return subprocess.run([str(part) for part in argv], cwd=root,
                                  stdin=subprocess.DEVNULL, capture_output=True,
                                  text=True, timeout=60, env=environment)

        def success(result):
            assert result.returncode == 0 and not result.stdout and not result.stderr, (
                result.args, result.returncode, result.stdout, result.stderr)

        def rejection(result, diagnostic):
            # A signal, timeout, or unrelated parse error is not an expected trap.
            assert result.returncode == 1 and not result.stdout and diagnostic in result.stderr, (
                result.args, result.returncode, result.stdout, result.stderr, diagnostic)

        def source(name, expression):
            path = root / (name + ".ny")
            path.write_text("fn main() -> i32\nprintln(" + expression +
                            ")\nreturn 0\nend fn\n", encoding="utf-8")
            return path

        control = root / "control.ny"
        control.write_text("fn main() -> i32\nreturn 0\nend fn\n", encoding="utf-8")
        native = args.llvm == "ON"
        if native:
            probe = run([*base, "--native", "--output", root / "control", control])
            if probe.returncode and any(text in probe.stderr.lower() for text in (
                    "no llvm backend", "llvm backend not available", "llvm backend is not available")):
                native = False
            else:
                success(probe)
                success(run([root / "control"]))

        invalid_source = {
            "bool_add": ("true + false", "Boolean arithmetic is unsupported"),
            "bool_div": ("true / false", "Boolean arithmetic is unsupported"),
            "bool_neg": ("-true", "Boolean arithmetic is unsupported"),
            "bool_bitwise": ("true & false", "Boolean arithmetic is unsupported"),
            "float_bitwise": ("1.5 & 2.5", "Bitwise and shift operands must be integers"),
            "float_shift": ("1 << 2.5", "Bitwise and shift operands must be integers"),
            "bool_complement": ("~true", "Bitwise complement requires"),
            "float_complement": ("~1.5", "Bitwise complement requires"),
        }
        for name, (expression, diagnostic) in invalid_source.items():
            path = source(name, expression)
            for flags in (["--no-bundle"], ["--no-bundle", "--emit-binary-nil"],
                          *([["--native"]] if native else [])):
                output = root / "protected-output"
                output.write_bytes(b"previous artifact")
                rejection(run([*base, *flags, "--output", output, path]), diagnostic)
                assert output.read_bytes() == b"previous artifact", name

        runtime_errors = {
            "shift_i32_width": ("(1 as i32) << 32", "Shift count out of range"),
            "shift_i64_width": ("(1 as i64) << 64", "Shift count out of range"),
            "shift_negative": ("(8 as i64) >> -1", "Shift count out of range"),
            "shift_u8_width": ("(1 as u8) << 8", "Shift count out of range"),
            "shift_wide_count": ("(1 as u8) << (256 as i64)", "Shift count out of range"),
            "divide_zero": ("(9 as i32) / 0", "Division by zero"),
            "divide_byte_zero": ("(9 as u8) / (0 as u8)", "Division by zero"),
            "divide_float_zero": ("9.0 / 0.0", "Division by zero"),
            "modulo_zero": ("(9 as i64) % 0", "Modulo by zero"),
            "modulo_float_zero": ("9.0 % 0.0", "Modulo by zero"),
            "divide_i32_overflow": ("((-2147483647 - 1) as i32) / (-1 as i32)", "Integer division overflow"),
            "modulo_i32_overflow": ("((-2147483647 - 1) as i32) % (-1 as i32)", "Integer modulo overflow"),
            "divide_i64_overflow": ("(-(9223372036854775807 as i64) - (1 as i64)) / (-1 as i64)", "Integer division overflow"),
            "modulo_i64_overflow": ("(-(9223372036854775807 as i64) - (1 as i64)) % (-1 as i64)", "Integer modulo overflow"),
        }
        for name, (expression, diagnostic) in runtime_errors.items():
            path = source(name, expression)
            for binary in (False, True):
                flags = ["--emit-binary-nil"] if binary else []
                nil = root / (name + ".nil")
                success(run([*base, *flags, "--no-bundle", "--output", nil, path]))
                rejection(run([args.interpreter.resolve(), "--no-ai", nil]), diagnostic)
                if os.name != "nt":
                    bundle = root / (name + ".bundle")
                    success(run([*base, *flags, "--output", bundle, path]))
                    rejection(run([bundle]), diagnostic)
            if native:
                rejection(run([*base, "--native", "--jit", "--output", root / "jit.nil", path]), diagnostic)
                executable = root / (name + ".native")
                success(run([*base, "--native", "--output", executable, path]))
                rejection(run([executable]), diagnostic)

        for width in ("f32", "u64"):
            path = source("unsupported_" + width, "1 as " + width)
            diagnostic = "CAST: unsupported scalar target '" + width + "'"
            source_diagnostic = "Unsupported scalar cast from"
            for binary in (False, True):
                nil = root / "unsupported.nil"
                nil.write_bytes(b"previous artifact")
                flags = ["--emit-binary-nil"] if binary else []
                rejection(run([*base, "--no-bundle", *flags,
                               "--output", nil, path]), source_diagnostic)
                assert nil.read_bytes() == b"previous artifact"
                compiled = run([*base, "--no-typecheck", "--no-bundle", *flags,
                                "--output", nil, path])
                assert compiled.returncode == 0 and not compiled.stdout, (
                    compiled.args, compiled.returncode, compiled.stdout, compiled.stderr)
                rejection(run([args.interpreter.resolve(), "--no-ai", nil]), diagnostic)
            if native:
                protected = root / "protected-native"
                protected.write_bytes(b"previous artifact")
                rejection(run([*base, "--no-typecheck", "--native",
                               "--output", protected, path]), diagnostic)
                assert protected.read_bytes() == b"previous artifact"

    print("scalar contracts passed: 8 source rejections, 14 runtime traps, 2 unsupported casts" +
          ("; native routes tested" if native else "; native routes unavailable"))


if __name__ == "__main__":
    main()
