#!/usr/bin/env python3
"""Exercise versioned class NIL, native exact layouts, and virtual fault edges."""

from __future__ import annotations

import argparse
import copy
import os
from pathlib import Path
import struct
import subprocess
import tempfile


SCHEMAS = [
    ("Extra", [("padding", "i64")]),
    ("Base", [("value", "i64")]),
    ("Derived", [("padding", "i64"), ("value", "i64"), ("factor", "i64")]),
    ("Problem", [("code", "i64")]),
]
# name, abstract, bases, (name, implementation, receiver, result, flags, args)
CLASSES = [
    ("Extra", False, [], []),
    ("Base", True, [], [
        ("read", "Base::read", "Base", "i64", 1, []),
        ("refread", "Base::refread", "ref<Base>", "i64", 1, []),
        ("staticref", "Base::staticref", "ref<Base>", "i64", 0, []),
        ("explode", "", "Base", "void", 3, []),
    ]),
    ("Derived", False, ["Extra", "Base"], [
        ("explode", "Derived::explode", "Derived", "void", 1, []),
    ]),
]


def instructions(source: str) -> list[tuple[str, list[str]]]:
    return [(parts[0], parts[1:]) for line in source.strip().splitlines()
            if (parts := line.split())]


CONSTRUCTION = """
NEW_STRUCT Derived
PUSH_CONST 999
SET_FIELD padding
PUSH_CONST 7
SET_FIELD value
PUSH_CONST 3
SET_FIELD factor
"""
MAIN = instructions(CONSTRUCTION + """
STORE_LOCAL concrete
REF_LOCAL concrete
CALL_METHOD Derived staticref 1
CALL println 1
LOAD_LOCAL concrete
CLASS_UPCAST Base
STORE_LOCAL object
REF_LOCAL object
CALL_METHOD Base refread 1
CALL println 1
PUSH_CONST 17
LOAD_LOCAL object
FAULT_ENTER outer dispatch
PUSH_CONST 99
LOAD_LOCAL object
CALL_METHOD Base explode 1
POP
FAULT_LEAVE outer
POP
POP
PUSH_CONST 0
RET
LABEL dispatch
FAULT_SAVE saved
CALL_METHOD Base read 1
CALL println 1
CALL println 1
FAULT_BIND saved Problem
GET_FIELD code
CALL println 1
FAULT_DROP saved
PUSH_CONST 0
RET
""")
FUNCTIONS = [
    ("Base::read", ["self:Base"], "i64", instructions("STORE_LOCAL self\nLOAD_LOCAL self\nGET_FIELD value\nRET")),
    ("Base::refread", ["self:ref<Base>"], "i64", instructions("STORE_LOCAL self\nLOAD_LOCAL self\nGET_FIELD value\nRET")),
    ("Base::staticref", ["self:ref<Base>"], "i64", instructions("STORE_LOCAL self\nLOAD_LOCAL self\nGET_FIELD value\nPUSH_CONST 100\nADD\nRET")),
    ("Derived::explode", ["self:Derived"], "void", instructions("""
STORE_LOCAL self
NEW_STRUCT Problem
LOAD_LOCAL self
GET_FIELD factor
SET_FIELD code
FAULT_RAISE Problem
""")),
    ("main", [], "i32", MAIN),
]
EXPECTED = "107\n7\n7\n17\n3\n"


def textual_module(functions: list, classes: list = CLASSES) -> bytes:
    def quoted(value: str) -> str:
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\0", "\\0").replace(
            "\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + '"'

    lines = ["module native_classes"]
    for name, fields in SCHEMAS:
        lines += ["struct " + name, *(f"{field}: {kind}" for field, kind in fields), "end struct"]
    for name, abstract, bases, methods in classes:
        lines.append(f"class {name} {int(abstract)}")
        lines += ["base " + base for base in bases]
        for method, implementation, receiver, result, flags, parameters in methods:
            lines.append(" ".join(["method", method, implementation or "-", quoted(receiver), quoted(result),
                                   str(flags & 1), str((flags >> 1) & 1),
                                   str(len(parameters)), *(quoted(value) for value in parameters)]))
        lines.append("end class")
    for name, parameters, result, body in functions:
        lines.append(f"function {name}({', '.join(parameters)}) -> {result}:")
        lines += [" ".join([opcode, *arguments]) for opcode, arguments in body]
        lines.append("end function")
    return ("\n".join(lines) + "\n").encode()


def binary_module(functions: list, classes: list = CLASSES) -> bytes:
    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        encoded = value.encode()
        return u32(len(encoded)) + encoded

    types = u32(len(SCHEMAS))
    for name, fields in SCHEMAS:
        types += string(name) + u32(len(fields))
        types += b"".join(string(field) + string(kind) for field, kind in fields)
    types += u32(0)
    declarations = u32(len(classes))
    for name, abstract, bases, methods in classes:
        declarations += string(name) + u32(int(abstract)) + u32(len(bases))
        declarations += b"".join(string(base) for base in bases) + u32(len(methods))
        for method, implementation, receiver, result, flags, parameters in methods:
            declarations += b"".join(string(value) for value in (method, implementation, receiver, result))
            declarations += u32(flags) + u32(len(parameters))
            declarations += b"".join(string(parameter) for parameter in parameters)
    bodies = u32(len(functions))
    for name, parameters, result, body in functions:
        bodies += string(name) + u32(len(parameters))
        bodies += b"".join(string(parameter) for parameter in parameters)
        bodies += u32(0) + string(result) + u32(0) + u32(0) + u32(0) + u32(len(body))
        for opcode, arguments in body:
            bodies += string(opcode) + u32(len(arguments))
            bodies += b"".join(string(argument) for argument in arguments) + u32(0)
    sections = [(1, types), (2, bodies), (3, u32(0)), (4, u32(0)),
                (5, string("native_classes") + u32(0)), (7, declarations)]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return b"NIL\0" + struct.pack("<III", 4, 0, len(sections)) + table + b"".join(
        payload for _, payload in sections)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("assembler", "linker", "runtime", "interpreter"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    paths = {name: str(getattr(args, name).resolve(strict=True))
             for name in ("assembler", "linker", "runtime", "interpreter")}
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    with tempfile.TemporaryDirectory(prefix="ny-native-class-contract-") as directory:
        root = Path(directory)

        def run(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.run(command, cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60, check=False)

        def success(result: subprocess.CompletedProcess[str], label: str, expected: str | None = None) -> None:
            if result.returncode or result.stderr or (expected is not None and result.stdout != expected):
                raise AssertionError(f"{label}: {result}")

        for binary in (False, True):
            label = "binary" if binary else "text"
            encode = binary_module if binary else textual_module
            artifact = root / (label + ".nil")
            artifact.write_bytes(encode(FUNCTIONS))
            success(run([paths["interpreter"], str(artifact)]), label + " interpreter", EXPECTED)
            for unmanaged in (False, True):
                mode = label + ("-unmanaged" if unmanaged else "-managed")
                obj, exe = root / (mode + ".o"), root / (mode + (".exe" if os.name == "nt" else ""))
                assembled = run([paths["assembler"], *(["--unmanaged"] if unmanaged else []),
                                 "-o", str(obj), str(artifact)])
                message = (assembled.stdout + assembled.stderr).lower()
                if assembled.returncode and "llvm" in message and any(
                        marker in message for marker in ("not available", "not compiled", "unavailable")):
                    return 77
                success(assembled, mode + " assemble")
                success(run([paths["linker"], "--runtime", paths["runtime"], "-o", str(exe), str(obj)]),
                        mode + " link")
                success(run([str(exe)]), mode + " execution", EXPECTED)

            invalid = {
                "incomplete-upcast": "NEW_STRUCT Derived\nCLASS_UPCAST Base\nPOP\nPUSH_CONST 0\nRET",
                "scalar-upcast": "PUSH_CONST 7\nCLASS_UPCAST Base\nPOP\nPUSH_CONST 0\nRET",
                "class-cast-covariance": CONSTRUCTION + "CAST Base\nPOP\nPUSH_CONST 0\nRET",
                "class-arithmetic": CONSTRUCTION + "PUSH_CONST 1\nADD\nPOP\nPUSH_CONST 0\nRET",
                "abstract-allocation": "NEW_STRUCT Base\nPUSH_CONST 7\nSET_FIELD value\nPOP\nPUSH_CONST 0\nRET",
                "unknown-slot": CONSTRUCTION + "CALL_METHOD Base missing 1\nPUSH_CONST 0\nRET",
                "wrong-receiver-shape": CONSTRUCTION + "CALL_METHOD Base refread 1\nPOP\nPUSH_CONST 0\nRET",
                "class-ref-covariance": CONSTRUCTION + "STORE_LOCAL object\nREF_LOCAL object\nCLASS_UPCAST Base\nPOP\nPUSH_CONST 0\nRET",
            }
            for case, body in invalid.items():
                functions = copy.deepcopy(FUNCTIONS)
                functions[-1] = ("main", [], "i32", instructions(body))
                artifact.write_bytes(encode(functions))
                obj = root / (case + ".o")
                sentinel = b"previous valid object\n"
                obj.write_bytes(sentinel)
                rejected = run([paths["assembler"], "-o", str(obj), str(artifact)])
                if rejected.returncode != 1 or obj.read_bytes() != sentinel:
                    raise AssertionError(f"{label} {case} must reject without replacing output: {rejected}")
    print("native class contract: 2 interpreted, 4 managed/manual native routes and 16 output-preserving rejections passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
