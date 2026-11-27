#!/usr/bin/env python3
"""Exercise nominal record-array identity, bounds, and untrusted NIL storage."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tests"))
from native_output_staging import linked_executable_path


RECORDS = """struct Counter
    value: i32
end struct
struct Other
    value: i32
end struct
"""


def source(body: str, declarations: str = RECORDS) -> str:
    return ("import nil.std.collections\n" + declarations + "\nfn main() -> i32\n"
            + body + "\nreturn 0\nend fn\n")


def nil_module(body: str, declarations: str = RECORDS) -> str:
    return ('module record_array_contract\nconst word = "owned"\n' + declarations
            + "\nfunction main() -> i32:\n" + body
            + "\nPUSH_CONST 0\nRET\nend function\n")


def make_record(name: str = "Counter", value: int = 7) -> str:
    return f"NEW_STRUCT {name}\nPUSH_CONST {value}\nCAST i32\nSET_FIELD value\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("compiler", "interpreter", "stdlib", "assembler"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), default="OFF")
    parser.add_argument("--runtime-tests", type=Path, action="append", default=[],
                        help="runtime ABI test executable (repeat for static/shared linkage)")
    args = parser.parse_args()
    compiler, interpreter, stdlib, assembler = (
        getattr(args, name).resolve() for name in ("compiler", "interpreter", "stdlib", "assembler"))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_LIBRARY_PATH"))
                   and key != "NIL_STRICT_BOUNDS"}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-record-array-contract-") as directory:
        root = Path(directory)

        def run(command, *, success: bool = True):
            result = subprocess.run([str(item) for item in command], cwd=root, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=90)
            if success:
                assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
            else:
                # A signal/crash is not a successful validation rejection.
                assert result.returncode > 0, (
                    command, result.returncode, result.stdout, result.stderr)
            return result

        base = [compiler, "--quiet", "--no-stdlib", "--no-bundle", "--sdk-path", stdlib]
        encodings = [("text", []), ("binary", ["--emit-binary-nil"])]

        # The language-route tests exercise normal VM/bundle/JIT/AOT execution.
        # Here safe mode additionally collects while shared record handles and
        # nested arrays are live. The separate hashmap-buckets route fixture
        # covers monomorphized vec<record> storage without a Default record.
        for name in ("record-arrays",):
            path = REPOSITORY / "tests/language" / (name + ".ny")
            expected = path.with_suffix(".stdout").read_text(encoding="utf-8")
            for encoding, flags in encodings:
                artifact = root / (name + "-" + encoding + ".nil")
                run([*base, *flags, "-o", artifact, path])
                result = run([interpreter, "--no-ai", "--memory-mode=safe", artifact])
                assert result.stdout == expected and not result.stderr, result
                checks += 1

        gc_source = root / "record-roots.ny"
        gc_source.write_text("""import nil.std.runtime
struct Child
    value: i32
end struct
struct Holder
    child: Child
    name: text
    values: array<i32>
end struct
fn make() -> array<Holder>
    let result: array<Holder> = []
    let mut i: i32 = 0
    while i < 1200
        result.push(Holder { child: Child { value: i }, name: $"owned-{i}", values: [i, i + 1] })
        i = i + 1
    end while
    return result
end fn
fn main() -> i32
    let roots = make()
    let retained = roots[513]
    collect(0)
    println(roots.len())
    println(roots[0].name)
    println(roots[1199].name)
    println(roots[1199].values[1])
    roots[513] = Holder { child: Child { value: 91 }, name: "replacement", values: [93] }
    collect(0)
    println(retained.name)
    println(retained.child.value)
    println(retained.values[1])
    return 0
end fn
""", encoding="utf-8", newline="\n")
        for encoding, flags in encodings:
            artifact = root / ("record-roots-" + encoding + ".nil")
            run([*base, *flags, "-o", artifact, gc_source])
            result = run([interpreter, "--no-ai", "--memory-mode=safe", artifact])
            assert result.stdout == "1200\nowned-0\nowned-1199\n1200\nowned-513\n513\n514\n", result
            assert not result.stderr, result.stderr
            checks += 1

        invalid_source = {
            "literal-nominal": "let items: array<Counter> = [Other { value: 3 }]",
            "mixed-literal": "let items = [Counter { value: 3 }, Other { value: 5 }]",
            "set-nominal": "let items = [Counter { value: 3 }]\nitems[0] = Other { value: 5 }",
            "push-nominal": "let items = [Counter { value: 3 }]\nitems.push(Other { value: 5 })",
            "array-assignment": "let mut items: array<Counter> = []\nlet wrong = [Other { value: 3 }]\nitems = wrong",
            "intrinsic-set-nominal": "let items = [Counter { value: 3 }]\narray_set<Counter>(items, 0, Other { value: 5 })",
        }
        source_cases = [(name, body, RECORDS) for name, body in invalid_source.items()]
        source_cases.extend([
            ("case-sensitive-nominal", "let items: array<Counter> = [counter { value: 3 }]",
             RECORDS + "struct counter\nvalue: i32\nend struct\n"),
            ("generic-nominal", "let items = [Holder<i32> { value: 3 }]\nitems.push(Holder<i64> { value: 5 })",
             "struct Holder<T>\nvalue: T\nend struct\n"),
        ])
        for name, body, declarations in source_cases:
            path = root / (name + ".ny")
            path.write_text(source(body, declarations), encoding="utf-8", newline="\n")
            routes = [*encodings, *([("native", ["--native"])] if args.llvm == "ON" else [])]
            for encoding, flags in routes:
                artifact = root / (name + "-" + encoding)
                if encoding == "native":
                    artifact = linked_executable_path(artifact)
                artifact.write_bytes(b"preserve previous artifact\n")
                result = run([*base, *flags, "-o", artifact, path], success=False)
                assert any(word in result.stderr.lower() for word in ("counter", "array", "holder")), result.stderr
                assert artifact.read_bytes() == b"preserve previous artifact\n"
                checks += 1
            analysis = json.loads(run([compiler, "--no-stdlib", "--sdk-path", stdlib,
                                       "--analyze", path], success=False).stdout)
            assert not analysis["success"] and analysis["diagnostics"], analysis
            checks += 1

        # A missing record cannot be represented by zero or a fabricated object.
        # Do not project a field: the read itself must reject the invalid result.
        invalid_reads = {
            "negative": "let items = [Counter { value: 3 }]\nlet missing = items[-1]",
            "past-end": "let items = [Counter { value: 3 }]\nlet missing = items[1]",
            "empty": "let items: array<Counter> = []\nlet missing = items[0]",
            "intrinsic": "let items = [Counter { value: 3 }]\nlet missing = get<Counter>(items, 1)",
        }
        for name, body in invalid_reads.items():
            path = root / ("read-" + name + ".ny")
            path.write_text(source(body), encoding="utf-8", newline="\n")
            routes = [*encodings, *([("native", ["--native"])] if args.llvm == "ON" else [])]
            for encoding, flags in routes:
                artifact = root / ("read-" + name + "-" + encoding)
                if encoding == "native":
                    artifact = linked_executable_path(artifact)
                run([*base, *flags, "-o", artifact, path])
                result = run([artifact] if encoding == "native" else
                             [interpreter, "--no-ai", artifact], success=False)
                assert not result.stdout and "array" in result.stderr.lower(), result
                checks += 1

        valid_record = make_record()
        wrong_record = make_record("Other")
        valid_array = valid_record + "NEW_ARRAY_LITERAL 1 Counter\n"
        invalid_nil = {
            "unknown-schema": "NEW_ARRAY_LITERAL 0 MissingRecord\nPOP",
            "uninitialized-record": "NEW_STRUCT Counter\nNEW_ARRAY_LITERAL 1 Counter\nPOP",
            "uninitialized-push": valid_array + "NEW_STRUCT Counter\nCALL array__push 2",
            "uninitialized-set": valid_array + "PUSH_CONST 0\nNEW_STRUCT Counter\nSET_INDEX",
            "uninitialized-create": "PUSH_CONST 2\nNEW_STRUCT Counter\nINTEROP_CALL intrinsic array.new_with_value 2 array<Counter> Counter\nPOP",
            "wrong-nominal-literal": wrong_record + "NEW_ARRAY_LITERAL 1 Counter\nPOP",
            "integer-as-record": "PUSH_CONST 1\nNEW_ARRAY_LITERAL 1 Counter\nPOP",
            "array-as-record": "NEW_ARRAY_LITERAL 0 i32\nNEW_ARRAY_LITERAL 1 Counter\nPOP",
            "wrong-field-value": 'NEW_STRUCT Counter\nPUSH_CONST_STR word\nSET_FIELD value\nNEW_ARRAY_LITERAL 1 Counter\nPOP',
            "wrong-nominal-set": valid_array + "PUSH_CONST 0\n" + wrong_record + "SET_INDEX",
            "wrong-nominal-push": valid_array + wrong_record + "CALL array__push 2",
            "intrinsic-forged-handle": "PUSH_CONST 1\nPUSH_CONST 0\nINTEROP_CALL intrinsic array.get 2 Counter Counter\nPOP",
            "intrinsic-wrong-array": wrong_record + "NEW_ARRAY_LITERAL 1 Other\nPUSH_CONST 0\nINTEROP_CALL intrinsic array.get 2 Counter Counter\nPOP",
        }
        nil_cases = [(name, body, RECORDS) for name, body in invalid_nil.items()]
        nil_cases.extend([
            ("duplicate-empty-schema", "NEW_ARRAY_LITERAL 0 Counter\nPOP",
             RECORDS + "struct Counter\nvalue: i32\nend struct\n"),
            ("enum-record-name-collision", "NEW_ARRAY_LITERAL 0 Counter\nPOP",
             RECORDS + "enum Counter\nReady\nend enum\n"),
            ("case-sensitive-schema", make_record("counter") + "NEW_ARRAY_LITERAL 1 Counter\nPOP",
             RECORDS + "struct counter\nvalue: i32\nend struct\n"),
            ("generic-schema", make_record("Holder<i64>") + "NEW_ARRAY_LITERAL 1 Holder<i32>\nPOP",
             "struct Holder<i32>\nvalue: i32\nend struct\nstruct Holder<i64>\nvalue: i32\nend struct\n"),
            ("namespace-schema", make_record("pkg__Counter") + "NEW_ARRAY_LITERAL 1 pkg::Counter\nPOP",
             "struct pkg::Counter\nvalue: i32\nend struct\nstruct pkg__Counter\nvalue: i32\nend struct\n"),
        ])
        # This positive NIL control prevents a blanket aggregate-array rejection
        # from satisfying every negative assembler check.
        control = root / "control.nil"
        control.write_text(nil_module(valid_array + "PUSH_CONST 0\nGET_INDEX\nGET_FIELD value\nCALL println 1"),
                           encoding="utf-8", newline="\n")
        result = run([interpreter, "--no-ai", control])
        assert result.stdout == "7\n" and not result.stderr, result
        checks += 1
        if args.llvm == "ON":
            control_object = root / "control.o"
            run([assembler, "-o", control_object, control])
            assert control_object.is_file() and control_object.stat().st_size > 0
            checks += 1
        for name, body, declarations in nil_cases:
            path = root / ("invalid-" + name + ".nil")
            path.write_text(nil_module(body, declarations), encoding="utf-8", newline="\n")
            # The VM currently initializes a declared scalar record field to
            # zero. Native definite initialization is stricter, so this case
            # belongs to the assembler contract rather than changing VM defaults.
            if not name.startswith("uninitialized-"):
                result = run([interpreter, "--no-ai", path], success=False)
                assert not result.stdout, result
                assert any(word in result.stderr.lower() for word in
                           ("array", "record", "schema", "struct", "counter")), result.stderr
                checks += 1
            if args.llvm == "ON":
                artifact = root / (name + ".o")
                artifact.write_bytes(b"preserve previous object\n")
                result = run([assembler, "-o", artifact, path], success=False)
                assert "[NativeCodegen]" in result.stderr, result.stderr
                assert artifact.read_bytes() == b"preserve previous object\n"
                checks += 1
        if args.llvm == "ON":
            # Recursive nominal field layouts are outside this first native
            # record-array contract, including arrays with zero elements.
            path = root / "recursive-schema.nil"
            path.write_text(nil_module("NEW_ARRAY_LITERAL 0 Recursive\nPOP",
                            "struct Recursive\nchild: Recursive\nend struct\n"),
                            encoding="utf-8", newline="\n")
            artifact = root / "recursive-schema.o"
            artifact.write_bytes(b"preserve previous object\n")
            result = run([assembler, "-o", artifact, path], success=False)
            assert "[NativeCodegen]" in result.stderr and "recursive" in result.stderr.lower(), result.stderr
            assert artifact.read_bytes() == b"preserve previous object\n"
            checks += 1
        for runtime_tests in args.runtime_tests:
            for mode in ("identity-case", "identity-generic", "identity-namespace", "wrong-kind",
                         "foreign", "stale", "default", "empty-read", "negative-read", "null-push"):
                result = run([runtime_tests.resolve(), "--record-array-failure", mode], success=False)
                assert not result.stdout and "NIL native runtime error:" in result.stderr, result
                checks += 1
    print(f"record array contract: {checks} checks passed")


if __name__ == "__main__":
    main()
