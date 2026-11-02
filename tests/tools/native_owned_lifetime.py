#!/usr/bin/env python3
"""Compiled owned scopes must physically release storage on every control-flow exit."""
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
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--memory-launcher", type=Path)
    args = parser.parse_args()
    if sys.platform not in ("linux", "win32") or args.llvm == "OFF":
        return 77
    source_text = """struct LifetimeProblem
code:i32
end struct
fn early(value:i32)->i32
let mut buffer=owned<i32>(16384)
*buffer=value
return *buffer
end fn
fn fail(value:i32)->i32
let mut buffer=owned<i32>(16384)
*buffer=value
raise LifetimeProblem { code:*buffer }
return 0
end fn
fn main()->i32
let mut total:i64=0
for index in range(6000)
let local=owned<i32>(16384)
total=total+(early(index) as i64)
attempt
let pending=owned<i32>(16384)
fail(index)
fault LifetimeProblem as error
let caught=owned<i32>(16384)
total=total+(error.code as i64)
recover
let cleanup=owned<i32>(16384)
end attempt
if index%2==0 then
continue
end if
end for
println(total)
return 0
end fn
"""
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    with tempfile.TemporaryDirectory(prefix="ny-owned-lifetime café ") as temporary:
        root = Path(temporary)
        verify_memory_limit(args.memory_launcher, cwd=root, environment=environment)
        source = root / "sustained.ny"
        source.write_text(source_text, encoding="utf-8", newline='\n')
        for name, flags in (("managed", []), ("unmanaged", ["--unmanaged"])):
            output = root / (name + (".exe" if os.name == "nt" else ""))
            compiled = subprocess.run([str(args.compiler.resolve()), "--no-stdlib", "--quiet",
                "--native", *flags, "--output", str(output), str(source)], cwd=root,
                env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
            if compiled.returncode != 0 or compiled.stderr:
                raise AssertionError(f"{name} compile: {compiled}")
            ran = run_memory_bounded(output, cwd=root, environment=environment,
                                     launcher=args.memory_launcher)
            if ran.returncode != 0 or ran.stderr or ran.stdout != "35994000\n":
                raise AssertionError(f"{name} lifetime: {ran}")
    print("Owned normal/return/continue/fault/recover scopes: >2 GiB cumulative storage fits " +
          memory_metric() + " in both memory modes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
