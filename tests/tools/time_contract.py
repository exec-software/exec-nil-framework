#!/usr/bin/env python3
"""Reject noninteger sleep durations, including unchecked binary NIL routes."""

import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="ny-time-contract-") as temporary:
        root = Path(temporary)
        for name, value in (("fraction", "0.5"), ("text", '"invalid"'),
                            ("huge_float", "9223372036854775808.0")):
            source = root / (name + ".ny")
            source.write_text('@intrinsic("time.sleep")\nfn sleep_ms(value: i64)\nend fn\n'
                              'fn main() -> i32\nsleep_ms(' + value + ')\nreturn 0\nend fn\n',
                              encoding="utf-8")
            for binary in (False, True):
                output = root / (name + ("-binary" if binary else "-text") + ".nil")
                command = [str(compiler), "--no-stdlib", "--no-bundle", "--no-typecheck",
                           "--quiet", "--output", str(output), str(source)]
                if binary:
                    command.append("--emit-binary-nil")
                compiled = subprocess.run(command, cwd=root, capture_output=True,
                                          text=True, timeout=15, check=False)
                if compiled.returncode != 0:
                    raise AssertionError(f"{name}: unchecked fixture failed to compile: {compiled.stderr}")
                executed = subprocess.run([str(interpreter), str(output)], cwd=root,
                                          capture_output=True, text=True, timeout=15, check=False)
                if executed.returncode != 1 or executed.stdout or \
                        "TIME_SLEEP: expected an integer duration" not in executed.stderr:
                    raise AssertionError(f"{name}/{binary}: invalid duration not rejected: {executed}")
    print("time contracts: 6 invalid text/binary durations rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
