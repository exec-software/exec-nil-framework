#!/usr/bin/env python3
"""Check invalid RNG bounds on both frontends and every enabled execution route."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import tempfile


@dataclass(frozen=True)
class Case:
    name: str
    lower: str
    upper: str
    message: str
    integer: bool = False


FINITE = "random_range_f64 bounds must be finite"
CASES = (
    Case("integer-inverted", "2", "1", "random_range minimum exceeds maximum", True),
    Case("floating-inverted", "2.0", "1.0", "random_range_f64 minimum exceeds maximum"),
    Case("nan-lower", "nan", "1.0", FINITE),
    Case("nan-upper", "0.0", "nan", FINITE),
    Case("nan-both", "nan", "nan", FINITE),
    Case("positive-infinite-lower", "infinity", "1.0", FINITE),
    Case("positive-infinite-upper", "0.0", "infinity", FINITE),
    # These catch an equality shortcut placed before the finite-bound check.
    Case("positive-infinite-equal", "infinity", "infinity", FINITE),
    Case("negative-infinite-lower", "-infinity", "0.0", FINITE),
    Case("negative-infinite-upper", "-1.0", "-infinity", FINITE),
    Case("negative-infinite-equal", "-infinity", "-infinity", FINITE),
    Case("both-infinite", "-infinity", "infinity", FINITE),
    Case("reversed-infinite", "infinity", "-infinity", FINITE),
)


def source_for(language: str, instance: bool, case: Case) -> str:
    if language == "ny":
        function = ("stream.range_i32" if case.integer else "stream.range_f64") if instance else (
            "random_range" if case.integer else "random_range_f64")
        return f"""import nil.std.math
fn main() -> i32
    let nan = sqrt(-1.0)
    let infinity = exp(1000.0)
    let stream = random_generator.new(1)
    println({function}({case.lower}, {case.upper}))
    return 0
end fn
"""
    function = ("stream.range-i32" if case.integer else "stream.range-f64") if instance else (
        "random-range" if case.integer else "random-range-f64")
    lower, upper = (value.replace("-", "minus ") for value in (case.lower, case.upper))
    return f"""book validation.random-range-contract
read everything from nil.std.math
main returns override integer begin paragraph
    value nan is sqrt with x being minus 1.0
    value infinity is exp with x being 1000.0
    value stream is random-generator.new with seed-value being 1
    say {function} with min being {lower}, max being {upper}
    give 0
end paragraph
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    stdlib = args.stdlib.resolve(strict=True)
    environment = os.environ.copy()
    for name in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP",
                 "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(name, None)
    routes = [
        ("text-interpreter", ("--no-bundle",)),
        ("binary-interpreter", ("--no-bundle", "--emit-binary-nil")),
        ("bundle-text", ()),
        ("bundle-binary", ("--emit-binary-nil",)),
    ]
    if args.llvm == "ON":
        routes.extend((("llvm-aot", ("--native",)), ("llvm-jit", ("--native", "--jit"))))

    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-random-range-") as temporary:
        root = Path(temporary)

        def run(command: list[object]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(part) for part in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=30)

        for language in ("ny", "matrix"):
            for instance in (False, True):
                for case in CASES:
                    source = root / f"{case.name}-{instance}.{language}"
                    source.write_text(source_for(language, instance, case), encoding="utf-8")
                    for route, flags in routes:
                        output = root / f"{case.name}-{instance}-{language}-{route}"
                        command = [compiler, "--quiet", "--sdk-path", stdlib,
                                   *flags, "--output", output, source]
                        result = run(command)
                        if route != "llvm-jit":
                            assert (result.returncode, result.stdout, result.stderr) == (0, "", ""), (
                                language, instance, case.name, route, "compilation", result)
                            if route.endswith("interpreter"):
                                result = run([interpreter, "--no-ai", output])
                            else:
                                executable = output.with_suffix(".exe") if os.name == "nt" else output
                                result = run([executable])
                        runtime = "native" if route.startswith("llvm-") else "interpreter"
                        expected = (1, "", f"NIL {runtime} runtime error: panic: {case.message}\n")
                        assert (result.returncode, result.stdout, result.stderr) == expected, (
                            language, instance, case.name, route, expected, result)
                        checks += 1
    print(f"{checks} invalid RNG bound checks passed across both frontends and {len(routes)} routes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
