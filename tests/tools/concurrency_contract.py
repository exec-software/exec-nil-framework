#!/usr/bin/env python3
"""Qualify isolated scalar workers, rejected effects, and joined runtime errors."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "linker", "runtime"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    tools = {name: getattr(args, name).resolve(strict=True)
             for name in ("compiler", "interpreter", "assembler", "linker", "runtime")}
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    sentinel = b"existing output must survive parallel contract rejection\n"
    rejected = executed = controls = 0
    with tempfile.TemporaryDirectory(prefix="ny-concurrency-contract-") as temporary:
        root = Path(temporary)

        def run(command: list[object]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(part) for part in command], cwd=root, env=environment,
                                  stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                  timeout=60, check=False)

        def good(command: list[object]) -> subprocess.CompletedProcess[str]:
            result = run(command)
            if result.returncode:
                raise AssertionError(f"command failed: {command}\n{result.stdout}\n{result.stderr}")
            return result

        base = [tools["compiler"], "--no-stdlib", "--no-bundle", "--quiet"]
        entry = "\nfn main() -> i32\nlet inputs: array<i64> = [1 as i64, 2 as i64]\nlet result = parallel_map(worker, inputs)\nreturn 0\nend fn\n"
        cases = {
            "global": "let counter: i64 = 7\nfn worker(value:i64)->i64\nreturn value + counter\nend fn\n" + entry,
            "console": "fn worker(value:i64)->i64\nprintln(value)\nreturn value\nend fn\n" + entry,
            "pointer": "fn worker(value:i64)->i64\nlet p=allocate<i64>(1)\n*p=value\nlet result=*p\ndeallocate(p)\nreturn result\nend fn\n" + entry,
            "transitive_console": "fn helper(value:i64)->i64\nprintln(value)\nreturn value\nend fn\nfn worker(value:i64)->i64\nreturn helper(value)\nend fn\n" + entry,
            "local_text": 'fn worker(value:i64)->i64\nlet secret:text="not copied"\nreturn value\nend fn\n' + entry,
            "nested_map": "fn helper(value:i64)->i64 { return value }\nfn worker(value:i64)->i64\nlet input:array<i64> = [value]\nlet output=parallel_map(helper,input)\nreturn output[0]\nend fn\n" + entry,
            "two_parameters": "fn worker(left:i64,right:i64)->i64 { return left+right }\n" + entry,
            "wrong_input": "fn worker(value:i32)->i64 { return value as i64 }\n" + entry,
            "text_result": 'fn worker(value:i64)->text { return "wrong" }\n' + entry,
            "local_worker": "fn worker(value:i64)->i64 { return value }\nfn main()->i32\nlet local=worker\nlet values:array<i64> = [1 as i64]\nlet result=parallel_map(local,values)\nreturn 0\nend fn\n",
            "non_function": "fn main()->i32\nlet values:array<i64> = [1 as i64]\nlet result=parallel_map(7,values)\nreturn 0\nend fn\n",
        }
        for name, contents in cases.items():
            source = root / f"{name}.ny"
            source.write_text(contents, encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / f"{name}-{mode}.nil"
                output.write_bytes(sentinel)
                result = run([*base, *flags, "-o", output, source])
                if result.returncode <= 0 or "parallel" not in result.stderr.lower():
                    raise AssertionError(f"{name}/{mode}: expected parallel semantic rejection\n{result.stdout}\n{result.stderr}")
                if "Parse error" in result.stderr or output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejected before checking or replaced output\n{result.stderr}")
                rejected += 1

        first = root / "parallel_first.ny"
        second = root / "parallel_second.ny"
        first.write_text("module parallel_first\npub fn worker(value:i64)->i64 { return value + 10 }\n"
                         "fn private_worker(value:i64)->i64 { return value }\n", encoding="utf-8")
        second.write_text("module parallel_second\npub fn worker(value:i64)->i64 { return value + 100 }\n", encoding="utf-8")
        for name, imports, body, expected in (
            ("qualified", "", "let first=parallel_map(parallel_first::worker,inputs)\n"
             "let second=parallel_map(parallel_second::worker,inputs)\nprintln(first[0])\nprintln(second[0])", "11\n101\n"),
            ("named", "import parallel_first.{worker}", "let result=parallel_map(worker,inputs)\nprintln(result[1])", "12\n"),
            ("glob", "import parallel_second.*", "let result=parallel_map(worker,inputs)\nprintln(result[1])", "102\n"),
            ("alias", "import parallel_first\nnamespace owner = parallel_first",
             "let result=parallel_map(owner::worker,inputs)\nprintln(result[1])", "12\n"),
            ("local_declaration", "import parallel_first.*\nfn worker(value:i64)->i64 { return value + 1000 }",
             "let result=parallel_map(worker,inputs)\nprintln(result[1])", "1002\n"),
        ):
            source = root / f"module-{name}.ny"
            source.write_text("module parallel_client\n" + imports + "\nfn main()->i32\n"
                              "let inputs:array<i64> = [1 as i64, 2 as i64]\n" + body +
                              "\nreturn 0\nend fn\n", encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                nil = root / f"module-{name}-{mode}.nil"
                good([*base, *flags, "-o", nil, first, second, source])
                result = good([tools["interpreter"], "--no-ai", nil])
                if result.stdout != expected or result.stderr:
                    raise AssertionError(f"module worker resolved incorrectly: {name}/{mode}\n{result.stdout}\n{result.stderr}")
                controls += 1
            if args.llvm == "ON":
                executable = root / f"module-{name}-native"
                good([*base, "--native", "-o", executable, first, second, source])
                result = good([executable])
                if result.stdout != expected or result.stderr:
                    raise AssertionError(f"native module worker resolved incorrectly: {name}\n{result.stdout}\n{result.stderr}")
                controls += 1
        for name, imports, worker in (
            ("ambiguous", "import parallel_first.*\nimport parallel_second.*", "worker"),
            ("private", "", "parallel_first::private_worker"),
            ("qualified_local_value", "", "local"),
        ):
            source = root / f"rejected-module-{name}.ny"
            body = "let local=parallel_first::worker\n" if name == "qualified_local_value" else ""
            source.write_text("module parallel_client\n" + imports + "\nfn main()->i32\n"
                              "let inputs:array<i64> = [1 as i64]\n" + body +
                              f"let result=parallel_map({worker},inputs)\nreturn 0\nend fn\n", encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / f"rejected-module-{name}-{mode}.nil"
                output.write_bytes(sentinel)
                result = run([*base, *flags, "-o", output, first, second, source])
                if result.returncode <= 0 or "parallel" not in result.stderr.lower() or output.read_bytes() != sentinel:
                    raise AssertionError(f"module worker rejection failed: {name}/{mode}\n{result.stdout}\n{result.stderr}")
                rejected += 1

        failure_source = root / "joined-failure.ny"
        failure_source.write_text("""fn divide(value:i64)->i64
return 100 / value
end fn
fn worker(value:i64)->i64
return divide(value) + 1
end fn
fn main()->i32
let inputs:array<i64> = [0 as i64, 2 as i64, 0 as i64, 4 as i64]
let output=parallel_map(worker,inputs)
println(output[1])
return 0
end fn
""", encoding="utf-8")

        def joined_failure(command: list[object]) -> None:
            nonlocal executed
            result = run(command)
            if result.returncode <= 0 or result.stdout or "PARALLEL_MAP: worker at input 0 failed" not in result.stderr:
                raise AssertionError(f"joined runtime error mismatch: {command}\n{result.stdout}\n{result.stderr}")
            executed += 1

        for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"])):
            nil = root / f"failure-{mode}.nil"
            good([*base, *flags, "-o", nil, failure_source])
            joined_failure([tools["interpreter"], "--no-ai", nil])
            if args.llvm == "ON":
                obj, executable = root / f"{mode}.o", root / f"{mode}.exe"
                good([tools["assembler"], "-o", obj, nil])
                good([tools["linker"], "--runtime", tools["runtime"], "-o", executable, obj])
                joined_failure([executable])
        if args.llvm == "ON":
            joined_failure([*base, "--native", "--jit", "-o", root / "jit-output", failure_source])
            executable = root / "failure-native"
            good([*base, "--native", "-o", executable, failure_source])
            joined_failure([executable])
        # A recursive clone must propagate failure before its caller resumes.
        # Without that edge check, the scalar zero sentinel enters this loop
        # and a join never completes. The original/clone identity differs for
        # LLVM self-recursive calls, so this is not covered by helper calls.
        recursive = root / "recursive-failure.ny"
        recursive.write_text("""fn worker(value:i64)->i64
if value == 0 then
return 100 / value
end if
let sentinel = worker(value - 1)
while sentinel == 0
end while
return sentinel
end fn
fn main()->i32
let inputs:array<i64> = [2 as i64, 3 as i64]
let output=parallel_map(worker,inputs)
println(output[0])
return 0
end fn
""", encoding="utf-8")
        for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"])):
            nil = root / f"recursive-{mode}.nil"
            good([*base, *flags, "-o", nil, recursive])
            joined_failure([tools["interpreter"], "--no-ai", nil])
            if args.llvm == "ON":
                obj, executable = root / f"recursive-{mode}.o", root / f"recursive-{mode}.exe"
                good([tools["assembler"], "-o", obj, nil])
                good([tools["linker"], "--runtime", tools["runtime"], "-o", executable, obj])
                joined_failure([executable])
        if args.llvm == "ON":
            joined_failure([*base, "--native", "--jit", "-o", root / "recursive-jit", recursive])
            executable = root / "recursive-native"
            good([*base, "--native", "-o", executable, recursive])
            joined_failure([executable])
    print(f"concurrency contract: {rejected} rejected compilations; {controls} qualified/imported execution controls; {executed} joined-failure execution routes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
