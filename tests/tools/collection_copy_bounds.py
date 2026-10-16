#!/usr/bin/env python3
"""Check collection copy offsets and contents on the VM, JIT, and native routes.

Dictionary copies use arrays of key_value_pair records, and HashSet uses arrays
of map_entry records internally. The native array ABI does not support those
yet. They are deliberately covered on the VM only; List, Queue, and Stack must
pass every route when LLVM is enabled.
"""

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
    length: int
    offset: int
    count: int
    valid: bool


CASES = (
    Case("empty_array", 0, 0, 0, True),
    Case("empty_start", 3, 0, 0, True),
    Case("empty_end", 3, 3, 0, True),
    Case("front", 3, 0, 2, True),
    Case("middle", 4, 1, 2, True),
    Case("exact_end", 3, 1, 2, True),
    Case("full", 2, 0, 2, True),
    Case("single_end", 3, 2, 1, True),
    Case("negative", 3, -1, 1, False),
    Case("negative_empty", 3, -1, 0, False),
    Case("minimum_empty", 3, -2147483648, 0, False),
    Case("past_end_empty", 3, 4, 0, False),
    Case("past_end", 3, 4, 1, False),
    Case("at_end", 3, 3, 1, False),
    Case("insufficient_room", 3, 2, 2, False),
    # The old offset + count guard wrapped on these positive offsets and
    # silently attempted out-of-bounds array writes instead of rejecting.
    Case("overflow_one", 3, 2147483647, 1, False),
    Case("overflow_two", 3, 2147483647, 2, False),
    Case("overflow_near_max", 3, 2147483646, 2, False),
    Case("maximum_empty", 3, 2147483647, 0, False),
)

COLLECTIONS = (
    ("list", "List"),
    ("queue", "Queue"),
    ("stack", "Stack"),
    ("hash_set", "HashSet"),
    ("dictionary", "Dictionary"),
)


def source_for(collection: str) -> str:
    """Compile once per collection; choose offsets at runtime, as callers do."""
    choices = "\n".join(
        f'    if scenario == "{case.name}" then\n'
        f"        size = {case.length}\n"
        f"        offset = {case.offset}\n"
        f"        count = {case.count}\n"
        "    end if"
        for case in CASES
    )
    dictionary = collection == "dictionary"
    element = "key_value_pair<i32, i32>" if dictionary else "i32"
    collection_type = "dictionary<i32, i32>" if dictionary else f"{collection}<i32>"
    seed = "key_value_pair<i32, i32> { key: -1, value: 99 }" if dictionary else "99"
    add = "source.add_key_value(index + 1, (index + 1) * 11)" if dictionary else "source.add((index + 1) * 11)"
    inspect = ("println(destination.get(index).key)\n        println(destination.get(index).value)"
               if dictionary else "println(destination.get(index))")
    return f"""import ny.lang.collections
import ny.lang.system.std_sys

fn main() -> i32
    let scenario: text = ny::lang::system::arg(0)
    let mut size: i32 = 0
    let mut offset: i32 = 0
    let mut count: i32 = 0
{choices}
    let mut source: {collection_type} = {collection_type}.new()
    let mut index: i32 = 0
    while index < count
        {add}
        index = index + 1
    end while
    let mut destination: array<{element}> = []
    index = 0
    while index < size
        destination.push({seed})
        index = index + 1
    end while
    source.copy_to(destination, offset)
    println(source.count())
    println(destination.len())
    index = 0
    while index < destination.len()
        {inspect}
        index = index + 1
    end while
    return 0
end fn
"""


def expected_stdout(case: Case, dictionary: bool) -> str:
    values = [99] * case.length
    keys = [-1] * case.length
    for index in range(case.count):
        values[case.offset + index] = (index + 1) * 11
        keys[case.offset + index] = index + 1
    lines = [case.count, case.length]
    for key, value in zip(keys, values):
        if dictionary:
            lines.append(key)
        lines.append(value)
    return "".join(f"{line}\n" for line in lines)


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
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("NY_")}
    base = [compiler, "--quiet", "--sdk-path", stdlib]
    passed = 0

    with tempfile.TemporaryDirectory(prefix="ny-collection-copy-") as temporary:
        root = Path(temporary)

        def run(command: list[object]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(part) for part in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60,
                                  check=False)

        def compile_ok(command: list[object]) -> None:
            result = run(command)
            if (result.returncode, result.stdout, result.stderr) != (0, "", ""):
                raise AssertionError(f"compile failed: {command}\n{result}")

        for collection, label in COLLECTIONS:
            source = root / f"{collection}.ny"
            source.write_text(source_for(collection), encoding="utf-8")
            artifact = root / f"{collection}.nil"
            compile_ok([*base, "--no-bundle", "-o", artifact, source])
            routes: list[tuple[str, list[object]]] = [
                ("VM", [interpreter, "--no-ai", artifact, "--"]),
            ]
            if args.llvm == "ON" and collection not in ("hash_set", "dictionary"):
                executable = root / f"{collection}.native"
                compile_ok([*base, "--native", "-o", executable, source])
                routes.extend((
                    ("AOT", [executable]),
                    ("JIT", [*base, "--native", "--jit", "-o",
                             root / f"{collection}-jit.nil", source, "--"]),
                ))
            for route, command in routes:
                for case in CASES:
                    result = run([*command, case.name])
                    if case.valid:
                        expected = (0, expected_stdout(case, collection == "dictionary"), "")
                    else:
                        runtime = "interpreter" if route == "VM" else "native"
                        expected = (1, "", f"NIL {runtime} runtime error: panic: "
                                    f"{label} copy_to destination is too small\n")
                    actual = (result.returncode, result.stdout, result.stderr)
                    if actual != expected:
                        raise AssertionError(
                            f"{collection}/{route}/{case.name}:\n"
                            f"expected={expected!r}\nactual={actual!r}")
                    passed += 1

    native = "VM, JIT, and AOT" if args.llvm == "ON" else "VM (LLVM disabled)"
    print(f"collection copy bounds: {passed} cases passed; list/queue/stack: {native}; "
          "hash_set/dictionary: VM only (native record arrays unsupported)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
