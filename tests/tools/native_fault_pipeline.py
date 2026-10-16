#!/usr/bin/env python3
"""Preserve exact nonempty ENTER prefixes through faulting native calls."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def instruction(name: str, *arguments: str, line: int = 0) -> tuple:
    return name, arguments, line


EXPLODE = [
    instruction("NEW_STRUCT", "Problem"),
    instruction("PUSH_CONST", "42"),
    instruction("SET_FIELD", "value"),
    instruction("FAULT_RAISE", "Problem", line=71),
]
MAIN = [
    instruction("PUSH_CONST", "17"),
    instruction("PUSH_CONST_STR", "prefix"),
    instruction("FAULT_ENTER", "outer", "dispatch"),
    instruction("PUSH_CONST", "99"),  # Incomplete expression, not ENTER prefix.
    instruction("CALL", "explode", "0"),
    instruction("POP"),
    instruction("FAULT_LEAVE", "outer"),
    instruction("POP"),
    instruction("POP"),
    instruction("PUSH_CONST", "0"),
    instruction("RET"),
    instruction("LABEL", "dispatch"),
    instruction("FAULT_SAVE", "saved"),
    instruction("CALL", "println", "1"),  # Retained text must not become 99.
    instruction("CALL", "println", "1"),  # Retained scalar remains below it.
    instruction("FAULT_BIND", "saved", "Problem"),
    instruction("GET_FIELD", "value"),
    instruction("CALL", "println", "1"),
    instruction("FAULT_DROP", "saved"),
    instruction("PUSH_CONST", "0"),
    instruction("RET"),
]
FUNCTIONS = [("explode", "void", EXPLODE), ("main", "i32", MAIN)]
EXPECTED = "retained-prefix\n17\n42\n"
OUT_OF_ORDER = [
    instruction("JMP", "setup"),
    instruction("LABEL", "body"),
    instruction("CALL", "explode", "0"),
    instruction("FAULT_LEAVE", "outer"),
    instruction("PUSH_CONST", "0"),
    instruction("RET"),
    instruction("LABEL", "setup"),
    instruction("FAULT_ENTER", "outer", "dispatch"),
    instruction("JMP", "body"),
    instruction("LABEL", "dispatch"),
    instruction("FAULT_SAVE", "saved"),
    instruction("FAULT_BIND", "saved", "Problem"),
    instruction("GET_FIELD", "value"),
    instruction("CALL", "println", "1"),
    instruction("FAULT_DROP", "saved"),
    instruction("PUSH_CONST", "0"),
    instruction("RET"),
]


def textual_module(functions: list) -> str:
    result = ('module fault_prefix\nconst prefix = "retained-prefix"\n'
              'struct Problem\nvalue: i64\nend struct\n')
    for name, result_type, body in functions:
        result += f"function {name} -> {result_type}:\n"
        for opcode, arguments, line in body:
            result += "    " + " ".join((opcode, *arguments)) + f" ; line {line}\n"
        result += "end function\n"
    return result


def binary_module(definitions: list) -> bytes:
    # The stable executable NIL v3 container, matching global_contract.py.
    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        encoded = value.encode("utf-8")
        return u32(len(encoded)) + encoded

    types = u32(1) + string("Problem") + u32(1) + string("value") + string("i64") + u32(0)
    functions = u32(len(definitions))
    for name, result_type, body in definitions:
        functions += string(name) + u32(0) + u32(0) + string(result_type)
        functions += u32(0) + u32(0) + u32(0) + u32(len(body))
        for opcode, arguments, line in body:
            functions += string(opcode) + u32(len(arguments))
            functions += b"".join(string(argument) for argument in arguments) + u32(line)
    constants = u32(1) + string("prefix") + string("retained-prefix")
    sections = [(1, types), (2, functions), (3, constants),
                (4, u32(0)), (5, string("fault_prefix") + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return (b"NIL\0" + struct.pack("<III", 3, 0, len(sections)) + table
            + b"".join(payload for _, payload in sections))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("assembler", "linker", "runtime", "interpreter"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    paths = {name: str(getattr(args, name).resolve(strict=True))
             for name in ("assembler", "linker", "runtime", "interpreter")}
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith("NY_")}

    with tempfile.TemporaryDirectory(prefix="ny-native-fault-prefix-") as directory:
        root = Path(directory)

        def run(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.run(command, cwd=root, env=environment, check=False,
                                  stdin=subprocess.DEVNULL, capture_output=True,
                                  text=True, timeout=60)

        def require_success(result: subprocess.CompletedProcess[str], label: str,
                            expected: str | None = None) -> None:
            if result.returncode != 0 or result.stderr or (
                    expected is not None and result.stdout != expected):
                raise AssertionError(f"{label}: exit={result.returncode}\n"
                                     f"stdout={result.stdout!r}\nstderr={result.stderr!r}")

        cases = [("nonempty", FUNCTIONS, EXPECTED),
                 ("out-of-order", [("explode", "void", EXPLODE),
                                   ("main", "i32", OUT_OF_ORDER)], "42\n")]
        for case, functions, expected in cases:
            for binary in (False, True):
                label = case + ("-binary" if binary else "-text")
                artifact = root / (label + ".nil")
                artifact.write_bytes(binary_module(functions) if binary else
                                     textual_module(functions).encode("utf-8"))
                require_success(run([paths["interpreter"], str(artifact)]),
                                label + " interpreter prefix", expected)
                for unmanaged in (False, True):
                    mode = label + ("-unmanaged" if unmanaged else "-managed")
                    obj = root / (mode + ".o")
                    exe = root / (mode + (".exe" if os.name == "nt" else ""))
                    assembled = run([paths["assembler"], *(["--unmanaged"] if unmanaged else []),
                                     "-o", str(obj), str(artifact)])
                    message = (assembled.stdout + assembled.stderr).lower()
                    if assembled.returncode != 0 and "llvm" in message and any(
                            marker in message for marker in ("not available", "not compiled", "unavailable")):
                        print("SKIP: native fault prefix requires LLVM")
                        return 77
                    require_success(assembled, mode + " assemble")
                    require_success(run([paths["linker"], "--runtime", paths["runtime"],
                                         "-o", str(exe), str(obj)]), mode + " link")
                    require_success(run([str(exe)]), mode + " native prefix", expected)
    print("native fault prefixes: 4 text/binary interpreter and 8 managed/manual native routes passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
