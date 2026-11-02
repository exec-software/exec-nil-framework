#!/usr/bin/env python3
"""Native safe-mode allocation loops must complete under an enforced memory cap."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from native_memory_support import memory_metric, run_memory_bounded, verify_memory_limit


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--memory-launcher", type=Path)
    args = parser.parse_args()
    if sys.platform not in ("linux", "win32") or args.llvm == "OFF":
        return 77

    with tempfile.TemporaryDirectory(prefix="ny-managed-lifetime café ") as temporary:
        root = Path(temporary)
        source = root / "sustained.ny"
        output = root / ("sustained.exe" if os.name == "nt" else "sustained")
        source.write_text('''import ny.lang.collections
fn main() -> i32
    let mut index: i32 = 0
    let mut total: i64 = 0
    while index < 40000
        let values: array<i32> = new_with_value<i32>(4096, index)
        total = total + (get<i32>(values, 0) as i64)
        index = index + 1
    end while
    println(total)
    return 0
end fn
''', encoding="utf-8", newline='\n')
        environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
        verify_memory_limit(args.memory_launcher, cwd=root, environment=environment)
        compiled = subprocess.run([str(args.compiler.resolve()), "--quiet", "--no-stdlib",
            "--sdk-path", str(args.stdlib.resolve()), "--native", "--output", str(output), str(source)],
            cwd=root, env=environment, text=True, capture_output=True, timeout=90)
        if compiled.returncode != 0 or compiled.stdout != "" or compiled.stderr != "":
            raise AssertionError(compiled)

        # Apply to the compiled consumer only, never compiler/JIT host.
        # The unchanged loop allocates >625 MiB cumulatively.
        result = run_memory_bounded(output, cwd=root, environment=environment,
                                    launcher=args.memory_launcher)
        if result.returncode != 0 or result.stdout != "799980000\n" or result.stderr != "":
            raise AssertionError(result)
    print("native managed lifetime: sustained allocation fits " + memory_metric())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
