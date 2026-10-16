#!/usr/bin/env python3
"""Prove exact 0..255 file bytes through checked Ny and nilinterpreter."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--interpreter", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--stdlib", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = arguments()
    expected = bytes(range(256))
    with tempfile.TemporaryDirectory(prefix="ny-byte-io-") as temporary:
        root = Path(temporary)
        input_path = root / "input.bin"
        output_path = root / "output.bin"
        artifact = root / "byte-io.nil"
        input_path.write_bytes(expected)

        compile_process = subprocess.run(
            [
                str(args.compiler),
                "--no-stdlib",
                "--sdk-path",
                str(args.stdlib),
                "--no-bundle",
                "--emit-binary-nil",
                "--quiet",
                "--output",
                str(artifact),
                str(args.source),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
        )
        if compile_process.returncode != 0:
            print("Ny byte-I/O fixture did not compile:")
            print(compile_process.stdout + compile_process.stderr, end="")
            return 1

        run_process = subprocess.run(
            [
                str(args.interpreter),
                "--no-ai",
                str(artifact),
                "--",
                str(input_path),
                str(output_path),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
        )
        expected_stdout = (
            "256\n0\n127\n128\n255\n42\n256\n"
            "copied\nmoved\ndeleted\nmissing-file-error\n"
        )
        if (
            run_process.returncode != 0
            or run_process.stdout != expected_stdout
            or run_process.stderr
        ):
            print(
                "Ny byte-I/O runtime mismatch: exit={} stdout={!r} stderr={!r}".format(
                    run_process.returncode,
                    run_process.stdout,
                    run_process.stderr,
                )
            )
            return 1
        if not output_path.is_file() or output_path.read_bytes() != expected:
            print("Ny byte-I/O output did not preserve all octets exactly")
            return 1
    print("Ny byte-I/O round trip passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
