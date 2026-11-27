#!/usr/bin/env python3
"""Exercise binary32 storage, arithmetic, imports and exact NIL metadata."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_output_staging import linked_executable_path
import tuple_array_nil_contract as wire


def annotated_module(body: str, wrappers, binary: bool) -> bytes:
    """Encode parameterized intrinsic declarations without source lowering.

    Source calls normally become direct INTEROP_CALLs; handwritten annotated
    functions must also validate their fixed operation's element kind.
    """
    functions = [("main", [], "i32", [], wire.instructions(body + "\nPUSH_CONST 0\nRET"))]
    functions += [(name, parameters, result, [f'@intrinsic("{operation}")'], [])
                  for name, parameters, result, operation in wrappers]
    if not binary:
        lines = ["module f32_annotations"]
        for name, parameters, result, annotations, instructions in functions:
            lines += annotations + [f"function {name}({', '.join(parameters)}) -> {result}:"]
            lines += [" ".join([opcode, *(json.dumps(argument) for argument in arguments)])
                      for opcode, arguments in instructions]
            lines += ["end function"]
        return ("\n".join(lines) + "\n").encode()

    def u32(value):
        return struct.pack("<I", value)

    def string(value):
        encoded = value.encode()
        return u32(len(encoded)) + encoded

    encoded_functions = u32(len(functions))
    for name, parameters, result, annotations, instructions in functions:
        encoded_functions += (string(name) + u32(len(parameters)) +
                              b"".join(map(string, parameters)) + u32(0) + string(result) +
                              u32(0) + u32(0) + u32(len(annotations)) +
                              b"".join(map(string, annotations)) + u32(len(instructions)))
        for opcode, arguments in instructions:
            encoded_functions += (string(opcode) + u32(len(arguments)) +
                                  b"".join(map(string, arguments)) + u32(0))
    sections = [(1, u32(0) + u32(0)), (2, encoded_functions),
                (3, u32(0)), (4, u32(0)), (5, string("f32_annotations") + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return b"NIL\0" + struct.pack("<III", 3, 2, len(sections)) + table + b"".join(
        payload for _, payload in sections)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "stdlib"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--runtime-tests", type=Path, action="append", default=[])
    args = parser.parse_args()
    compiler, interpreter, assembler, stdlib = (
        getattr(args, name).resolve(strict=True)
        for name in ("compiler", "interpreter", "assembler", "stdlib"))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_"))}
    native = args.llvm == "ON"
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-f32-contract-") as temporary:
        work = Path(temporary)

        def run(command, status=0):
            result = subprocess.run(list(map(str, command)), cwd=work, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", timeout=90)
            assert result.returncode == status, (command, result)
            return result

        base = [compiler, "--quiet", "--no-stdlib", "--sdk-path", stdlib]
        for name in ("scalar-f32-contract", "array-f32-contract"):
            report_path = work / (name + "-report.json")
            command = [sys.executable, ROOT / "tests/routes/ny_route_matrix.py",
                       "--compiler", compiler, "--interpreter", interpreter,
                       "--source", ROOT / "tests/language" / (name + ".ny"),
                       "--expected-stdout", ROOT / "tests/language" / (name + ".stdout"),
                       "--expected-exit", "0", "--routes",
                       "all" if native else "text-interpreter,binary-interpreter,bundle-text,bundle-binary",
                       "--compiler-arg=--no-stdlib", "--compiler-arg=--sdk-path",
                       "--compiler-arg=" + str(stdlib), "--work-dir", work / name,
                       "--report-json", report_path, "--strict"]
            run(command)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            assert all(row["status"] == "PASS" for row in report["results"]), report
            checks += len(report["results"])

        # First-class function values retain their existing interpreter-only
        # profile, while their f32 argument/result boundaries must still round.
        callable_source = ROOT / "tests/language/scalar-f32-callable.ny"
        callable_expected = callable_source.with_suffix(".stdout").read_text(encoding="utf-8")
        for binary in (False, True):
            artifact = work / ("callable-binary.nil" if binary else "callable-text.nil")
            run([*base, "--no-bundle", *(["--emit-binary-nil"] if binary else []),
                 "-o", artifact, callable_source])
            result = run([interpreter, "--no-ai", artifact])
            assert result.stdout == callable_expected and not result.stderr, result
            checks += 1

        # Import the same generic provider from source and a binary library.
        # Its implementation imports the current STL array intrinsics.
        provider_root = work / "provider"
        provider_root.mkdir()
        provider = provider_root / "f32_provider.ny"
        provider.write_text("""module f32_provider
import nil.std.collections
pub fn repeated<T>(value: T) -> array<T>
    return new_with_value<T>(2, value)
end fn
pub fn echo(value: f32) -> f32
    return value
end fn
""", encoding="utf-8", newline="\n")
        consumer = work / "consumer.ny"
        consumer.write_text("""import f32_provider.*
fn main() -> i32
    let values = repeated<f32>(16777217.0)
    println((echo(values[0]) as f64) - 16777216.0)
    println(echo(-0.0) as f64)
    return 0
end fn
""", encoding="utf-8", newline="\n")
        library_root = work / "libraries"
        library_root.mkdir()
        for dependency in ("source", "binary"):
            search = provider_root
            if dependency == "binary":
                run([*base, "--no-bundle", "--emit-library", "--emit-binary-nil",
                     "-o", library_root / "f32_provider.nil", provider])
                provider.unlink()
                search = library_root
            for encoding, flags in [("text", []), ("binary", ["--emit-binary-nil"]),
                                    *([("native", ["--native"])] if native else [])]:
                output = work / (dependency + "-" + encoding + ".nil")
                if encoding == "native":
                    output = linked_executable_path(output.with_suffix(""))
                result = run([*base, "--no-bundle", "-I", search, *flags, "-o", output, consumer])
                assert not result.stdout and not result.stderr, result
                result = run([output] if encoding == "native" else [interpreter, "--no-ai", output])
                assert result.stdout == "0\n-0\n" and not result.stderr, result
                checks += 1

        wire.ENUMS = []
        control = """PUSH_CONST 2
PUSH_CONST 16777217.0
CAST f32
INTEROP_CALL intrinsic array.new_f32 2 array<f32> f32
STORE_LOCAL values
LOAD_LOCAL values
PUSH_CONST 1
INTEROP_CALL intrinsic array.get_f32 2 f32 f32
CAST f64
PUSH_CONST 16777216.0
SUB f64
CALL println 1
LOAD_LOCAL values
PUSH_CONST 0
PUSH_CONST -0.0
CAST f32
INTEROP_CALL intrinsic array.set_f32 3 void f32
LOAD_LOCAL values
PUSH_CONST 0
GET_INDEX
CAST f64
CALL println 1
"""
        invalid = {
            "new-kind-mismatch": ("PUSH_CONST 1\nPUSH_CONST 1.5\n"
                                  "INTEROP_CALL intrinsic array.new_f32 2 array<f64> f64\nPOP", "metadata"),
            "get-result-mismatch": ("NEW_ARRAY_LITERAL 0 f32\nPUSH_CONST 0\n"
                                    "INTEROP_CALL intrinsic array.get_f32 2 f64 f32\nPOP", "metadata"),
            "set-result-mismatch": ("NEW_ARRAY_LITERAL 0 f32\nPUSH_CONST 0\nPUSH_CONST 1.5\nCAST f32\n"
                                    "INTEROP_CALL intrinsic array.set_f32 3 f32 f32\nPOP", "metadata"),
            "new-u8-mismatch": ("PUSH_CONST 1\nPUSH_CONST 1.5\nCAST f32\n"
                                "INTEROP_CALL intrinsic array.new_u8 2 array<f32> f32\nPOP", "metadata"),
            "integer-as-f32": ("PUSH_CONST 1\nNEW_ARRAY_LITERAL 1 f32\nPOP", "f32"),
            "f64-as-f32": ("PUSH_CONST 1.5\nNEW_ARRAY_LITERAL 1 f32\nPOP", "f32"),
            "local-f32-to-f64": ("PUSH_CONST 1.5\nCAST f32\nSTORE_LOCAL sample\n"
                                 "PUSH_CONST 16777217.0\nSTORE_LOCAL sample", "f32"),
            "local-f64-to-f32": ("PUSH_CONST 1.5\nSTORE_LOCAL sample\n"
                                 "PUSH_CONST 16777217.0\nCAST f32\nSTORE_LOCAL sample", "f32"),
            "f32-raw-pointer": ("PUSH_CONST 1\nALLOC f32\nPOP", "f32 raw-pointer storage is unsupported"),
            "boxed-f32-pointer": ("PUSH_CONST 0\nCAST ptr<f32>\nBOX_ANY ptr<f32>\nPOP", "ptr<f32>"),
            "foreign-f32-result": ("INTEROP_CALL native missing_provider 0 f32\nPOP",
                                   "f32 foreign-call ABI is unsupported"),
            "foreign-f32-parameter": ("PUSH_CONST 1.5\nCAST f32\n"
                                      "INTEROP_CALL native missing_provider 1 f64 f32\nPOP",
                                      "f32 foreign-call ABI is unsupported"),
        }
        for binary in (False, True):
            path = work / ("control-binary.nil" if binary else "control-text.nil")
            path.write_bytes(wire.encode(wire.instructions(control), [], binary))
            result = run([interpreter, "--no-ai", path])
            assert result.stdout == "0\n-0\n" and not result.stderr, result
            checks += 1
            if native:
                artifact = path.with_suffix(".o")
                run([assembler, "-o", artifact, path])
                assert artifact.is_file() and artifact.stat().st_size > 0
                checks += 1
            for name, (body, diagnostic) in invalid.items():
                path = work / (name + ("-binary.nil" if binary else "-text.nil"))
                path.write_bytes(wire.encode(wire.instructions(body), [], binary))
                result = run([interpreter, "--no-ai", path], status=1)
                assert not result.stdout and diagnostic in result.stderr, result
                checks += 1
                if native:
                    artifact = path.with_suffix(".o")
                    artifact.write_bytes(b"previous object\n")
                    result = run([assembler, "-o", artifact, path], status=1)
                    assert diagnostic in result.stderr and artifact.read_bytes() == b"previous object\n", result
                    checks += 1

        if native:
            for binary in (False, True):
                for element in ("f32", "u8"):
                    wrappers = [
                        ("fill", ["count:i32", "value:" + element], "array<" + element + ">", "array.new_" + element),
                        ("fetch", ["values:array<" + element + ">", "index:i32"], element, "array.get_" + element),
                        ("store", ["values:array<" + element + ">", "index:i32", "value:" + element], "void", "array.set_" + element),
                    ]
                    initial, replacement = ("1.5", "-0.0") if element == "f32" else ("1", "255")
                    body = (f"PUSH_CONST 2\nCAST i32\nPUSH_CONST {initial}\nCAST {element}\n"
                            "CALL fill 2\nSTORE_LOCAL values\nLOAD_LOCAL values\nPUSH_CONST 1\nCAST i32\n"
                            "CALL fetch 2\nCAST f64\nCALL println 1\n"
                            f"LOAD_LOCAL values\nPUSH_CONST 0\nCAST i32\nPUSH_CONST {replacement}\nCAST {element}\n"
                            "CALL store 3\nLOAD_LOCAL values\nPUSH_CONST 0\nCAST i32\n"
                            "CALL fetch 2\nCAST f64\nCALL println 1")
                    path = work / f"annotation-valid-{element}-{binary}.nil"
                    path.write_bytes(annotated_module(body, wrappers, binary))
                    artifact = path.with_suffix(".o")
                    run([assembler, "-o", artifact, path])
                    assert artifact.is_file() and artifact.stat().st_size > 0
                    checks += 1
                for fixed, actual in (("f32", "f64"), ("u8", "f32")):
                    value = "PUSH_CONST 1.5\n" + ("CAST f32\n" if actual == "f32" else "")
                    for operation in ("new", "get", "set"):
                        if operation == "new":
                            parameters, result_type = ["count:i32", "value:" + actual], "array<" + actual + ">"
                            body = "PUSH_CONST 1\nCAST i32\n" + value + "CALL wrapper 2\nPOP"
                        elif operation == "get":
                            parameters, result_type = ["values:array<" + actual + ">", "index:i32"], actual
                            body = "NEW_ARRAY_LITERAL 0 " + actual + "\nPUSH_CONST 0\nCAST i32\nCALL wrapper 2\nPOP"
                        else:
                            parameters, result_type = ["values:array<" + actual + ">", "index:i32", "value:" + actual], "void"
                            body = "NEW_ARRAY_LITERAL 0 " + actual + "\nPUSH_CONST 0\nCAST i32\n" + value + "CALL wrapper 3"
                        wrappers = [("wrapper", parameters, result_type, "array." + operation + "_" + fixed)]
                        path = work / f"annotation-invalid-{fixed}-{operation}-{binary}.nil"
                        path.write_bytes(annotated_module(body, wrappers, binary))
                        artifact = path.with_suffix(".o")
                        artifact.write_bytes(b"previous annotated object\n")
                        result = run([assembler, "-o", artifact, path], status=1)
                        assert "array intrinsic element metadata disagrees with its operation" in result.stderr, result
                        assert not result.stdout and artifact.read_bytes() == b"previous annotated object\n", result
                        checks += 1

        invalid_source = {
            "bitwise-f32": ("println((1.0 as f32) & (2.0 as f32))", "Bitwise"),
            "shift-f32": ("println((1.0 as f32) << 1)", "Bitwise"),
            "complement-f32": ("println(~(1.0 as f32))", "Bitwise"),
            "implicit-integer": ("let sample: f32 = 1.5\nlet value: i64 = sample", "f32"),
            "invariant-array": ("let samples: array<f32> = [1.5]\nlet values: array<f64> = samples", "array"),
        }
        for name, (body, diagnostic) in invalid_source.items():
            path = work / (name + ".ny")
            path.write_text("fn main() -> i32\n" + body + "\nreturn 0\nend fn\n",
                            encoding="utf-8", newline="\n")
            for flags in (["--no-bundle"], ["--no-bundle", "--emit-binary-nil"],
                          *([["--native"]] if native else [])):
                artifact = work / (name + ".nil")
                artifact.write_bytes(b"previous artifact\n")
                result = run([*base, *flags, "-o", artifact, path], status=1)
                assert not result.stdout and diagnostic in result.stderr, result
                assert artifact.read_bytes() == b"previous artifact\n", result
                checks += 1

        traps = {
            "division-zero": ("(1.0 as f32) / (0.0 as f32)", "Division by zero"),
            "modulo-zero": ("(1.0 as f32) % (-0.0 as f32)", "Modulo by zero"),
            "overflow-to-integer": ("(3.5e38 as f32) as i64", "outside i64 range"),
            "nonboolean": ("runtime_value(2.0) as bool", "0 or 1"),
        }
        for name, (expression, diagnostic) in traps.items():
            path = work / (name + ".ny")
            path.write_text("fn runtime_value(value: f32) -> f32\nreturn value\nend fn\n"
                            "fn main() -> i32\nprintln(" + expression +
                            ")\nreturn 0\nend fn\n", encoding="utf-8", newline="\n")
            for binary in (False, True):
                artifact = work / (name + ".nil")
                flags = ["--emit-binary-nil"] if binary else []
                run([*base, "--no-bundle", *flags, "-o", artifact, path])
                result = run([interpreter, "--no-ai", artifact], status=1)
                assert not result.stdout and diagnostic in result.stderr, result
                checks += 1
            if native:
                artifact = linked_executable_path(work / name)
                run([*base, "--native", "-o", artifact, path])
                result = run([artifact], status=1)
                assert not result.stdout and diagnostic in result.stderr, result
                checks += 1

        runtime_failures = {
            "unpack-high-bits": "f32 array word has nonzero high bits",
            "array-new-high-bits": "f32 array word has nonzero high bits",
            "array-set-high-bits": "f32 array word has nonzero high bits",
            "array-push-high-bits": "f32 array word has nonzero high bits",
            "box-unrounded": "not a canonical widened binary32",
            "box-array-word": "not a canonical widened binary32",
            "box-noncanonical-nan": "not a canonical widened binary32",
            "box-width-mismatch": "anything type mismatch",
            "nested-wrong-width": "exact child element type",
            "parallel-input": "invalid scalar array or worker",
            "parallel-output": "invalid scalar array or worker",
        }
        for runtime_tests in args.runtime_tests:
            executable = runtime_tests.resolve(strict=True)
            for mode, diagnostic in runtime_failures.items():
                result = run([executable, "--f32-failure", mode], status=1)
                assert not result.stdout and diagnostic in result.stderr, result
                checks += 1
    print(f"f32 contracts passed: {checks} checks")


if __name__ == "__main__":
    main()
