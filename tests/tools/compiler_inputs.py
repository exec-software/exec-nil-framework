#!/usr/bin/env python3
"""Invalid source paths/options are diagnostics, not host exceptions or waits."""

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
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    with tempfile.TemporaryDirectory(prefix="ny-compiler-inputs-") as directory:
        root = Path(directory)
        source = root / "valid.ny"
        source.write_text("fn main() -> i32\nreturn 0\nend fn\n", encoding="utf-8")
        output = root / "previous.nil"
        sentinel = b"previous output must remain intact\n"
        cases = [
            ([str(root)], "Error: input is not a regular file:"),
            ([str(root / "missing.ny")], "Error: cannot open input file:"),
            (["--stdlib-path", str(root), str(source)], "Error: unknown option: --stdlib-path"),
            (["--misspelled-option", str(source)], "Error: unknown option: --misspelled-option"),
        ]
        if hasattr(os, "mkfifo"):
            fifo = root / "not-source.ny"
            os.mkfifo(fifo)
            cases.append(([str(fifo)], "Error: input is not a regular file:"))
        for inputs, diagnostic in cases:
            output.write_bytes(sentinel)
            result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                     "--output", str(output), *inputs],
                                    cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=10, check=False)
            if result.returncode != 1 or result.stdout or diagnostic not in result.stderr:
                raise AssertionError((inputs, result.returncode, result.stdout, result.stderr))
            if output.read_bytes() != sentinel:
                raise AssertionError("invalid source request replaced previous output")
    print(f"compiler input contract: {len(cases)} rejection checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
