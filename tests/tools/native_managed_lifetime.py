#!/usr/bin/env python3
"""Native safe-mode allocation loops must complete under a bounded address space."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or args.llvm == "OFF":
        return 77
    import resource

    with tempfile.TemporaryDirectory(prefix="ny-managed-lifetime-") as temporary:
        root = Path(temporary)
        source, output = root / "sustained.ny", root / "sustained"
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
''', encoding="utf-8")
        environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
        compiled = subprocess.run([str(args.compiler.resolve()), "--quiet", "--no-stdlib",
            "--sdk-path", str(args.stdlib.resolve()), "--native", "--output", str(output), str(source)],
            cwd=root, env=environment, text=True, capture_output=True, timeout=90)
        if compiled.returncode != 0 or compiled.stdout != "" or compiled.stderr != "":
            raise AssertionError(compiled)

        def memory_limit():
            # Apply to the already compiled consumer only, never the compiler
            # or JIT host. The loop allocates >625 MiB cumulatively.
            resource.setrlimit(resource.RLIMIT_AS, (96 * 1024 * 1024, 96 * 1024 * 1024))

        result = subprocess.run([str(output)], cwd=root, env=environment, text=True,
            capture_output=True, timeout=45, preexec_fn=memory_limit)
        if result.returncode != 0 or result.stdout != "799980000\n" or result.stderr != "":
            raise AssertionError(result)
    print("native managed lifetime: sustained allocation fits 96 MiB address space")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
