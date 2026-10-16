#!/usr/bin/env python3
"""Keep source println unary on every compiler route, including unchecked lowering."""

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
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("NY_")}
    base = [compiler, "--quiet", "--no-stdlib"]
    sentinel = b"previous output must survive println rejection\n"
    rejected = executed = 0

    with tempfile.TemporaryDirectory(prefix="ny-println-contract-") as temporary:
        root = Path(temporary)

        def run(command: list[object]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(part) for part in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=30,
                                  check=False)

        def success(command: list[object], stdout: str = "") -> None:
            result = run(command)
            if (result.returncode, result.stdout, result.stderr) != (0, stdout, ""):
                raise AssertionError(
                    f"command failed: {command}\nexit={result.returncode}\n"
                    f"stdout={result.stdout!r}\nstderr={result.stderr!r}")

        def program(body: str, declarations: str = "") -> str:
            return ("module println_contract\n" + declarations +
                    "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n")

        control = root / "control.ny"
        control.write_text(program(""), encoding="utf-8")
        native = args.llvm == "ON"
        if native:
            probe = run([*base, "--native", "-o", root / "probe", control])
            if probe.returncode and any(marker in probe.stderr.lower() for marker in (
                    "no llvm backend", "llvm backend not available",
                    "llvm backend is not available")):
                native = False
            elif (probe.returncode, probe.stdout, probe.stderr) != (0, "", ""):
                raise AssertionError(f"native probe failed: {probe.stderr}")
            else:
                success([root / "probe"])

        routes = [
            ("text", ["--no-bundle"]),
            ("binary", ["--no-bundle", "--emit-binary-nil"]),
            ("debug-text", ["--no-bundle", "--debug-info"]),
            ("debug-binary", ["--no-bundle", "--debug-info", "--emit-binary-nil"]),
            ("library", ["--no-bundle", "--emit-binary-nil", "--emit-library"]),
        ]
        if os.name != "nt":
            routes.extend((("bundle-text", []), ("bundle-binary", ["--emit-binary-nil"])))
        if native:
            routes.extend((("native", ["--native"]), ("jit", ["--native", "--jit"])))

        cases = {
            "empty": (program("println()"), 0),
            "multiple": (program('println("first", "second")'), 2),
            "dead_branch": (program("if false then\nprintln()\nend if"), 0),
            "helper": (program("helper()", "fn helper()\nprintln(1, 2, 3)\nend fn\n"), 3),
        }
        for name, (contents, count) in cases.items():
            source = root / f"{name}.ny"
            source.write_text(contents, encoding="utf-8")
            diagnostic = f"println expects exactly 1 argument, got {count}"
            for mode, flags in routes:
                for unchecked in (False, True):
                    output = root / f"{name}-{mode}-{unchecked}.out"
                    output.write_bytes(sentinel)
                    continuation = ["--no-typecheck"] if unchecked else []
                    result = run([*base, *flags, *continuation, "-o", output, source])
                    phase = "Code generation failed" if unchecked else "Type checking failed:"
                    if (result.returncode != 1 or result.stdout or
                            diagnostic not in result.stderr or phase not in result.stderr):
                        raise AssertionError(
                            f"{name}/{mode}/unchecked={unchecked}: expected {diagnostic!r} "
                            f"during {phase!r}; exit={result.returncode}\n"
                            f"{result.stdout}\n{result.stderr}")
                    if output.read_bytes() != sentinel:
                        raise AssertionError(f"{name}/{mode}: rejection replaced existing output")
                    rejected += 1

        source = root / "positive.ny"
        source.write_text(program(
            'println("Ny λ")\nprintln("")\nprintln(42)\n'
            'println(10000000000 as i64)\nprintln(255 as u8)\n'
            'println(true)\nprintln(1.5)\nprintln(next())\nprintln(count)',
            "let mut count: i32 = 0\nfn next() -> i32\ncount = count + 1\n"
            "return count\nend fn\n"), encoding="utf-8")
        expected = "Ny λ\n\n42\n10000000000\n255\n1\n1.5\n1\n1\n"
        for mode, flags in routes:
            for unchecked in (False, True):
                output = root / f"positive-{mode}-{unchecked}.out"
                continuation = ["--no-typecheck"] if unchecked else []
                command = [*base, *flags, *continuation, "-o", output, source]
                success(command, expected if mode == "jit" else "")
                if mode == "library":
                    # A library is not executable; the exact positive source
                    # runs on every executable route in the other iterations.
                    if not output.is_file():
                        raise AssertionError("positive library output missing")
                    continue
                if mode != "jit":
                    success([output] if mode.startswith("bundle") or mode == "native"
                            else [interpreter, "--no-ai", output], expected)
                executed += 1

    print(f"println contract: {rejected} artifact-preserving rejections; "
          f"{executed} unary execution controls; " +
          ("native routes tested" if native else "native routes unavailable"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
