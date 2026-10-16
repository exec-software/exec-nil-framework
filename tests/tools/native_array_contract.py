#!/usr/bin/env python3
"""Check exact native scalar-array metadata and intrinsic transport."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, timeout=60, check=False)


def module(body: str, declarations: str = "") -> str:
    return ('module scalar_array_contract\nconst word = "owned text"\n'
            + declarations + '\nfunction main() -> i32:\n' + body
            + '\nPUSH_CONST 0\nRET\nend function\n')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assembler", type=Path, required=True)
    parser.add_argument("--linker", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path)
    args = parser.parse_args()
    assembler = str(args.assembler.resolve(strict=True))
    linker = str(args.linker.resolve(strict=True))
    runtime = str(args.runtime.resolve(strict=True))
    interpreter = str(args.interpreter.resolve(strict=True)) if args.interpreter else None
    invalid = {
        "method-set-wrong-f64": (
            "NEW_ARRAY_LITERAL 0 f64\nPUSH_CONST 0\nPUSH_CONST 1\nCALL array__set 3",
            "requires an exact f64 element"),
        "method-set-wrong-text": (
            "NEW_ARRAY_LITERAL 0 text\nPUSH_CONST 0\nPUSH_CONST 1\nCALL array__set 3",
            "requires an exact text element"),
        "method-get-pointer": (
            "PUSH_CONST 1\nALLOC i32\nPUSH_CONST 0\nCALL array__get 2\nPOP",
            "GET_INDEX cannot index native handle"),
        "method-set-pointer": (
            "PUSH_CONST 1\nALLOC i32\nPUSH_CONST 0\nPUSH_CONST 1\nCALL array__set 3",
            "SET_INDEX cannot index native handle"),
        "method-length-pointer": (
            "PUSH_CONST 1\nALLOC i32\nCALL array__length 1\nPOP",
            "LEN requires a typed array handle"),
        "method-get-wrong-count": (
            "NEW_ARRAY_LITERAL 0 i32\nCALL array__get 1\nPOP", "expected 2"),
        "method-set-wrong-count": (
            "NEW_ARRAY_LITERAL 0 i32\nPUSH_CONST 0\nCALL array__set 2", "expected 3"),
        "method-length-wrong-count": (
            "NEW_ARRAY_LITERAL 0 i32\nPUSH_CONST 0\nCALL array__length 2\nPOP", "expected 1"),
        "text-substr-wrong-count": (
            "PUSH_CONST_STR word\nPUSH_CONST 0\nCALL String__substr 2\nPOP", "expected 3"),
        "text-substr-forged-text": (
            "PUSH_CONST 1\nPUSH_CONST 0\nPUSH_CONST 1\nCALL String__substr 3\nPOP",
            "String__substr requires text and two integer bounds"),
        "text-substr-floating-bound": (
            "PUSH_CONST_STR word\nPUSH_CONST 0.5\nPUSH_CONST 1\nCALL String__substr 3\nPOP",
            "String__substr requires text and two integer bounds"),
        "panic-nontext-message": ("PUSH_CONST 1\nCALL panic 1", "panic requires a typed text message"),
        "panic-wrong-count": ("CALL panic 0", "expected 1"),
        "unrecognized-array-method": (
            "NEW_ARRAY_LITERAL 0 i32\nCALL array__unknown 1", "names undefined callee"),
        "integer-as-f64": ("PUSH_CONST 1\nNEW_ARRAY_LITERAL 1 f64\nPOP",
                           "requires an exact f64 element"),
        "integer-as-text": ("PUSH_CONST 1\nNEW_ARRAY_LITERAL 1 text\nPOP",
                            "requires an exact text element"),
        "text-as-integer": ("PUSH_CONST_STR word\nNEW_ARRAY_LITERAL 1 i64\nPOP",
                            "requires an integer element"),
        "float-as-integer": ("PUSH_CONST 1.5\nNEW_ARRAY_LITERAL 1 i64\nPOP",
                             "requires an integer element"),
        "wrong-f64-set": ("NEW_ARRAY_LITERAL 0 f64\nPUSH_CONST 0\n"
                          "PUSH_CONST 1\nSET_INDEX", "requires an exact f64 element"),
        "wrong-text-set": ("NEW_ARRAY_LITERAL 0 text\nPUSH_CONST 0\n"
                           "PUSH_CONST 1\nSET_INDEX", "requires an exact text element"),
        "wrong-f64-push": ("NEW_ARRAY_LITERAL 0 f64\nPUSH_CONST 1\n"
                           "CALL array__push 2", "requires an exact f64 element"),
        "wrong-text-push": ("NEW_ARRAY_LITERAL 0 text\nPUSH_CONST 1\n"
                            "CALL array__push 2", "requires an exact text element"),
        "nested-array": ("NEW_ARRAY_LITERAL 0 array<text>\nPOP",
                         "pointer-valued array elements are not implemented"),
        "unsupported-width": ("NEW_ARRAY_LITERAL 0 f32\nPOP",
                              "floating-point array elements are not implemented"),
        "direct-result-mismatch": (
            "PUSH_CONST 1\nPUSH_CONST 1.25\n"
            "INTEROP_CALL intrinsic array.new_with_value 2 i64 f64\nPOP",
            "array intrinsic arity/result metadata mismatch"),
        "direct-count-mismatch": (
            "PUSH_CONST 1\nPUSH_CONST 1.25\nPUSH_CONST 0\n"
            "INTEROP_CALL intrinsic array.new_with_value 3 array<f64> f64\nPOP",
            "array intrinsic arity/result metadata mismatch"),
        "direct-fixed-kind-mismatch": (
            "PUSH_CONST 1\nPUSH_CONST 1.25\n"
            "INTEROP_CALL intrinsic array.new_u8 2 array<f64> f64\nPOP",
            "array intrinsic element metadata disagrees"),
        "direct-unsupported-kind": (
            "PUSH_CONST 1\nPUSH_CONST 1.25\n"
            "INTEROP_CALL intrinsic array.new_f32 2 array<f32> f32\nPOP",
            "array intrinsic has unsupported element type"),
        "direct-forged-handle": (
            "PUSH_CONST 1\nPUSH_CONST 0\n"
            "INTEROP_CALL intrinsic array.get 2 f64 f64\nPOP",
            "array intrinsic requires a matching typed array handle"),
        "direct-wrong-kind": (
            "NEW_ARRAY_LITERAL 0 f64\nPUSH_CONST 0\n"
            "INTEROP_CALL intrinsic array.get 2 i64 i64\nPOP",
            "array intrinsic requires a matching typed array handle"),
        "direct-boolean-size": (
            "PUSH_CONST 1\nCAST bool\nPUSH_CONST 1.25\n"
            "INTEROP_CALL intrinsic array.new_with_value 2 array<f64> f64\nPOP",
            "array intrinsic requires an integer size/index"),
    }
    declarations = '\n'.join((
        '@intrinsic("array.new_with_value")\n'
        'function fill_float(count: i32, value: f64) -> array<f64>:\nend function',
        '@intrinsic("array.get")\n'
        'function get_float(values: array<f64>, index: i32) -> f64:\nend function',
        '@intrinsic("array.set")\n'
        'function set_float(values: array<f64>, index: i32, value: f64) -> void:\nend function',
        '@intrinsic("array.new_with_value")\n'
        'function fill_text(count: i32, value: text) -> array<text>:\nend function',
        '@intrinsic("array.get")\n'
        'function get_text(values: array<text>, index: i32) -> text:\nend function',
        '@intrinsic("array.set")\n'
        'function set_text(values: array<text>, index: i32, value: text) -> void:\nend function',
        '@intrinsic("array.length")\n'
        'function length_text(values: array<text>) -> i32:\nend function',
    ))
    control = module(
        'PUSH_CONST 2\nPUSH_CONST 1.25\nCALL fill_float 2\nSTORE_LOCAL floats\n'
        'LOAD_LOCAL floats\nPUSH_CONST 1\nCALL get_float 2\n'
        'PUSH_CONST 1.25\nCMP_EQ\nCALL println 1\n'
        'LOAD_LOCAL floats\nPUSH_CONST 0\nPUSH_CONST -2.5\nCALL set_float 3\n'
        'LOAD_LOCAL floats\nPUSH_CONST 0\nCALL get_float 2\n'
        'PUSH_CONST -2.5\nCMP_EQ\nCALL println 1\n'
        'LOAD_LOCAL floats\nPUSH_CONST 99\nCALL get_float 2\n'
        'PUSH_CONST 0.0\nCMP_EQ\nCALL println 1\n'
        'PUSH_CONST 2\nPUSH_CONST_STR word\nCALL fill_text 2\nSTORE_LOCAL texts\n'
        'LOAD_LOCAL texts\nPUSH_CONST 1\nCALL get_text 2\nCALL println 1\n'
        'LOAD_LOCAL texts\nPUSH_CONST 0\nPUSH_CONST_STR word\nCALL set_text 3\n'
        'LOAD_LOCAL texts\nPUSH_CONST 0\nCALL get_text 2\nCALL println 1\n'
        'LOAD_LOCAL texts\nCALL length_text 1\nCALL println 1\n'
        'LOAD_LOCAL texts\nPUSH_CONST 99\nCALL get_text 2\nCALL println 1',
        declarations)
    direct_control = module(
        'PUSH_CONST 2\nPUSH_CONST 1.25\n'
        'INTEROP_CALL intrinsic array.new_with_value 2 array<f64> f64\nSTORE_LOCAL floats\n'
        'LOAD_LOCAL floats\nINTEROP_CALL intrinsic array.length 1 i32 f64\nCALL println 1\n'
        'LOAD_LOCAL floats\nPUSH_CONST 1\nINTEROP_CALL intrinsic array.get 2 f64 f64\n'
        'PUSH_CONST 1.25\nCMP_EQ\nCALL println 1\n'
        'LOAD_LOCAL floats\nPUSH_CONST 0\nPUSH_CONST -2.5\n'
        'INTEROP_CALL intrinsic array.set 3 void f64\n'
        'LOAD_LOCAL floats\nPUSH_CONST 0\nINTEROP_CALL intrinsic array.get 2 f64 f64\n'
        'PUSH_CONST -2.5\nCMP_EQ\nCALL println 1\n'
        'PUSH_CONST 2\nPUSH_CONST_STR word\n'
        'INTEROP_CALL intrinsic array.new_with_value 2 array<text> text\nSTORE_LOCAL texts\n'
        'LOAD_LOCAL texts\nPUSH_CONST 1\nINTEROP_CALL intrinsic array.get 2 text text\nCALL println 1\n'
        'LOAD_LOCAL texts\nINTEROP_CALL intrinsic array.length 1 i64\nCALL println 1')
    direct_expected = "2\n1\n1\nowned text\n2\n"
    runtime_errors = {
        "negative-size": ("PUSH_CONST -1\nPUSH_CONST 1.25\n"
                          "INTEROP_CALL intrinsic array.new_with_value 2 array<f64> f64\nPOP",
                          "array size must be between 0 and 2147483647"),
        "overwide-size": ("PUSH_CONST 2147483648\nPUSH_CONST 1.25\n"
                          "INTEROP_CALL intrinsic array.new_with_value 2 array<f64> f64\nPOP",
                          "array size must be between 0 and 2147483647"),
        "overwide-index": ("NEW_ARRAY_LITERAL 0 text\nPUSH_CONST 2147483648\n"
                           "INTEROP_CALL intrinsic array.get 2 text text\nPOP",
                           "array index is outside the i32 range"),
    }
    with tempfile.TemporaryDirectory(prefix="ny-native-array-") as temporary:
        directory = Path(temporary)
        source, output = directory / "probe.nil", directory / "probe.o"
        if interpreter:
            for name, (body, diagnostic) in {**invalid, **runtime_errors}.items():
                if not name.startswith("direct-") and name not in runtime_errors:
                    continue
                # The VM intentionally represents all integer widths/bool as
                # int64_t. Source typing and native metadata reject bool sizes;
                # the erased raw VM value cannot distinguish true from one.
                if name == "direct-boolean-size":
                    continue
                source.write_text(module(body), encoding="utf-8")
                result = run([interpreter, str(source)])
                if result.returncode <= 0 or result.stdout or diagnostic not in result.stderr:
                    raise AssertionError(f"{name}: interpreter accepted invalid array ABI: {result}")
            source.write_text(direct_control, encoding="utf-8")
            result = run([interpreter, str(source)])
            if result.returncode or result.stdout != direct_expected or result.stderr:
                raise AssertionError(f"direct interpreter control failed: {result}")
        source.write_text(module(""), encoding="utf-8")
        result = run([assembler, "-o", str(output), str(source)])
        if result.returncode and "llvm" in result.stderr.lower() and any(
                marker in result.stderr.lower()
                for marker in ("not available", "unavailable", "no llvm backend")):
            print("skip: nilassembler has no LLVM backend")
            return 77
        if result.returncode:
            raise AssertionError(f"valid NIL rejected: {result.stderr}")
        for name, (body, diagnostic) in invalid.items():
            source.write_text(module(body), encoding="utf-8")
            sentinel = b"preserve existing object\n"
            output.write_bytes(sentinel)
            result = run([assembler, "-o", str(output), str(source)])
            if result.returncode <= 0 or diagnostic not in result.stderr:
                raise AssertionError(f"{name}: missing rejection: {result}")
            if output.read_bytes() != sentinel:
                raise AssertionError(f"{name}: damaged existing object")
        source.write_text(control, encoding="utf-8")
        result = run([assembler, "-o", str(output), str(source)])
        if result.returncode:
            raise AssertionError(f"intrinsic control assembly failed: {result.stderr}")
        executable = directory / "control"
        result = run([linker, "--runtime", runtime, "-o", str(executable), str(output)])
        if result.returncode:
            raise AssertionError(f"intrinsic control link failed: {result.stderr}")
        result = run([str(executable)])
        expected = "1\n1\n1\nowned text\nowned text\n2\n\n"
        if result.returncode or result.stdout != expected or result.stderr:
            raise AssertionError(f"intrinsic control result mismatch: {result}")
        for name, (body, diagnostic) in runtime_errors.items():
            source.write_text(module(body), encoding="utf-8")
            result = run([assembler, "-o", str(output), str(source)])
            if result.returncode:
                raise AssertionError(f"{name}: assembly failed: {result.stderr}")
            result = run([linker, "--runtime", runtime, "-o", str(executable), str(output)])
            if result.returncode:
                raise AssertionError(f"{name}: link failed: {result.stderr}")
            result = run([str(executable)])
            if result.returncode <= 0 or result.stdout or diagnostic not in result.stderr:
                raise AssertionError(f"{name}: native runtime guard failed: {result}")
        source.write_text(direct_control, encoding="utf-8")
        result = run([assembler, "-o", str(output), str(source)])
        if result.returncode:
            raise AssertionError(f"direct control assembly failed: {result.stderr}")
        result = run([linker, "--runtime", runtime, "-o", str(executable), str(output)])
        if result.returncode:
            raise AssertionError(f"direct control link failed: {result.stderr}")
        result = run([str(executable)])
        if result.returncode or result.stdout != direct_expected or result.stderr:
            raise AssertionError(f"direct native control failed: {result}")
    print(f"native scalar arrays: {len(invalid)} rejected shapes, intrinsic transport passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
