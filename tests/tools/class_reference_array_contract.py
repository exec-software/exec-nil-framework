#!/usr/bin/env python3
"""Class views and aggregate references retain exact array storage proofs."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_output_staging import linked_executable_path
import native_class_contract as wire
import tuple_array_nil_contract as record_wire

TYPES = """class Base
value: i32
pub virtual fn read(self) -> i32 { return self.value }
end class
class Derived inherits Base
extra: i32
pub override fn read(self) -> i32 { return self.value + self.extra }
end class
class Other
value: i32
end class
struct Record
value: i32
end struct
"""
BASE = "Base { value: 3 }"
DERIVED = "Derived { value: 5, extra: 7 }"


def source(body, declarations=TYPES):
    return "import nil.std.collections\n" + declarations + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "stdlib"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--runtime-tests", action="append", default=[], type=Path)
    args = parser.parse_args()
    compiler, interpreter, assembler, stdlib = (
        getattr(args, name).resolve(strict=True)
        for name in ("compiler", "interpreter", "assembler", "stdlib"))
    environment = {k: v for k, v in os.environ.items() if not k.startswith(("NY_", "NIL_"))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-class-reference-array-") as temporary:
        root = Path(temporary)

        def run(command, success=True):
            result = subprocess.run(list(map(str, command)), cwd=root, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=90)
            assert (result.returncode == 0 if success else result.returncode > 0), (command, result)
            return result

        base = [compiler, "--quiet", "--no-stdlib", "--no-bundle", "--sdk-path", stdlib]
        routes = [("text", []), ("binary", ["--emit-binary-nil"])]
        if args.llvm == "ON":
            routes += [("native", ["--native"])]
        gc_source = root / "object-roots.ny"
        gc_source.write_text("""import nil.std.runtime
class Reader
    value: i32
    pub virtual fn read(self) -> i32 { return self.value }
end class
class LabeledReader inherits Reader
    label: text
    values: array<i32>
    pub override fn read(self) -> i32 { return self.value + self.values[1] }
end class
struct ReferenceRow
    label: text
    values: array<i32>
end struct
fn make_readers() -> array<Reader>
    let readers: array<Reader> = []
    let mut i: i32 = 0
    while i < 1200
        readers.push(LabeledReader { value: i, label: $"reader-{i}", values: [i, i + 1] })
        i = i + 1
    end while
    return readers
end fn
fn make_refs() -> array<ref<ReferenceRow>>
    let refs: array<ref<ReferenceRow>> = []
    let mut i: i32 = 0
    while i < 1200
        let row = ReferenceRow { label: $"row-{i}", values: [i, i + 1] }
        refs.push(ref row)
        i = i + 1
    end while
    return refs
end fn
fn main() -> i32
    let readers = make_readers()
    let refs = make_refs()
    let retained = readers[513]
    let held = refs[513]
    readers[513] = Reader { value: 91 }
    let replacement = ReferenceRow { label: "replacement", values: [93] }
    refs[513] = ref replacement
    let mut i: i32 = 0
    while i < 3000
        let scratch: array<i32> = []
        scratch.reserve(512)
        i = i + 1
    end while
    collect(0)
    println(readers.len())
    println(readers[1199].read())
    println(retained.read())
    println(refs[1199].label)
    println(refs[1199].values[1])
    println(held.label)
    println(held.values[1])
    return 0
end fn
""", encoding="utf-8", newline="\n")
        for route, flags in routes:
            artifact = root / ("object-roots-" + route)
            source_path = gc_source
            if route == "native":
                artifact = linked_executable_path(artifact)
                source_path = root / "object-roots-native.ny"
                source_path.write_text(gc_source.read_text(encoding="utf-8").replace(
                    "import nil.std.runtime\n", "").replace("    collect(0)\n", ""), encoding="utf-8")
            run([*base, *flags, "-o", artifact, source_path])
            result = run([artifact] if route == "native" else
                         [interpreter, "--no-ai", "--memory-mode=safe", artifact])
            assert result.stdout == "1200\n2399\n1027\nrow-1199\n1200\nrow-513\n514\n" and not result.stderr, result
            checks += 1
        negatives = {
            "container-covariance": f"let children = [{DERIVED}]\nlet parents: array<Base> = children",
            "nested-container-covariance": f"let children = [{DERIVED}]\nlet parents: array<array<Base>> = [children]",
            "reference-covariance": f"let child = {DERIVED}\nlet refs: array<ref<Base>> = [ref child]",
            "plain-for-reference": f"let value = {BASE}\nlet refs: array<ref<Base>> = [value]",
            "reference-for-plain": f"let value = {BASE}\nlet items: array<Base> = [ref value]",
            "record-reference-wrapper": "let value = Record { value: 3 }\nlet items: array<ref<Record>> = [value]",
            "wrong-class-push": f"let items: array<Base> = [{BASE}]\nitems.push(Other {{ value: 7 }})",
            "wrong-class-set": f"let items: array<Base> = [{BASE}]\nitems[0] = Other {{ value: 7 }}",
            "wrong-class-intrinsic": f"let items: array<Base> = [{BASE}]\narray_set<Base>(items, 0, Other {{ value: 7 }})",
            "ref-push-covariance": f"let value = {BASE}\nlet child = {DERIVED}\nlet items = [ref value]\nitems.push(ref child)",
            "ref-set-covariance": f"let value = {BASE}\nlet child = {DERIVED}\nlet items = [ref value]\nitems[0] = ref child",
            "ref-intrinsic-covariance": f"let value = {BASE}\nlet child = {DERIVED}\nlet items = [ref value]\narray_set<ref<Base>>(items, 0, ref child)",
        }
        for name, body in negatives.items():
            path = root / (name + ".ny")
            path.write_text(source(body), encoding="utf-8", newline="\n")
            for route, flags in routes:
                artifact = root / (name + "-" + route)
                if route == "native":
                    artifact = linked_executable_path(artifact)
                sentinel = b"preserve prior artifact\n"
                artifact.write_bytes(sentinel)
                result = run([*base, *flags, "-o", artifact, path], success=False)
                assert result.stderr and artifact.read_bytes() == sentinel, result
                checks += 1

        for element, declaration in (("Base", f"let value = {BASE}"),
                                     ("ref<Base>", f"let value = {BASE}"),
                                     ("ref<Record>", "let value = Record { value: 3 }")):
            initial = "ref value" if element.startswith("ref<") else "value"
            for operation in ("items[-1]", "items[1]", f"get<{element}>(items, 1)"):
                path = root / "missing.ny"
                path.write_text(source(declaration + f"\nlet items: array<{element}> = [{initial}]\nlet missing = {operation}"), encoding="utf-8")
                for route, flags in routes:
                    artifact = root / ("missing-" + route)
                    if route == "native":
                        artifact = linked_executable_path(artifact)
                    run([*base, *flags, "-o", artifact, path])
                    result = run([artifact] if route == "native" else [interpreter, "--no-ai", artifact], success=False)
                    assert not result.stdout and "array" in result.stderr.lower(), result
                    checks += 1

        # Source class metadata is represented identically in raw text and binary
        # NIL. Include positive controls so rejecting all class arrays cannot pass.
        construction = wire.CONSTRUCTION
        parent = construction + "CLASS_UPCAST Base\n"
        reference = parent + "STORE_LOCAL parent\nREF_LOCAL parent\n"
        cases = {
            "class": (parent + "NEW_ARRAY_LITERAL 1 Base\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD value\nCALL println 1", "7\n"),
            "reference": (reference + "NEW_ARRAY_LITERAL 1 ref<Base>\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD value\nCALL println 1", "7\n"),
            "nested-reference": (reference + "NEW_ARRAY_LITERAL 1 ref<Base>\nNEW_ARRAY_LITERAL 1 array<ref<Base>>\nPUSH_CONST 0\nGET_INDEX\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD value\nCALL println 1", "7\n"),
        }
        invalid = {
            "missing-class-view": construction + "NEW_ARRAY_LITERAL 1 Base\nPOP",
            "plain-as-ref": parent + "NEW_ARRAY_LITERAL 1 ref<Base>\nPOP",
            "ref-as-plain": reference + "NEW_ARRAY_LITERAL 1 Base\nPOP",
            "ref-covariance": construction + "STORE_LOCAL child\nREF_LOCAL child\nNEW_ARRAY_LITERAL 1 ref<Base>\nPOP",
            "integer-as-ref": "PUSH_CONST 7\nNEW_ARRAY_LITERAL 1 ref<Base>\nPOP",
            "ref-unknown-schema": "NEW_ARRAY_LITERAL 0 ref<Missing>\nPOP",
            "ref-array-schema": "NEW_ARRAY_LITERAL 0 ref<array<i32>>\nPOP",
            "ref-scalar-schema": "NEW_ARRAY_LITERAL 0 ref<i32>\nPOP",
            "ref-wrapper-cast": parent + "NEW_ARRAY_LITERAL 1 Base\nCAST array<ref<Base>>\nPOP",
            "ref-child-cast": construction + "NEW_ARRAY_LITERAL 1 Derived\nCAST array<Base>\nPOP",
            "uninitialized-class": "NEW_STRUCT Derived\nCLASS_UPCAST Base\nNEW_ARRAY_LITERAL 1 Base\nPOP",
        }
        for binary in (False, True):
            encode = wire.binary_module if binary else wire.textual_module
            for name, body in [(name, body) for name, (body, _) in cases.items()] + list(invalid.items()):
                path = root / (name + ("-binary.nil" if binary else "-text.nil"))
                functions = [*wire.FUNCTIONS[:-1], ("main", [], "i32", wire.instructions(body + "\nPUSH_CONST 0\nRET"))]
                path.write_bytes(encode(functions))
                valid = name in cases
                # VM source defaults for ordinary class scalar fields are an
                # existing contract; definite required-field proof is native.
                if name != "uninitialized-class":
                    result = run([interpreter, "--no-ai", path], success=valid)
                    if valid:
                        assert result.stdout == cases[name][1] and not result.stderr, result
                    else:
                        assert not result.stdout and result.stderr, result
                    checks += 1
                if args.llvm == "ON":
                    output = root / "checked.o"
                    sentinel = b"preserve prior object\n"
                    output.write_bytes(sentinel)
                    result = run([assembler, "-o", output, path], success=valid)
                    if valid:
                        assert output.stat().st_size > len(sentinel), result
                    else:
                        assert output.read_bytes() == sentinel and result.stderr, result
                    checks += 1
        # A plain record-only module must activate reference provenance without
        # relying on class metadata elsewhere in the program.
        record_wire.ENUMS = []
        record = "NEW_STRUCT Record\nPUSH_CONST 7\nCAST i32\nSET_FIELD value\n"
        record_ref = record + "STORE_LOCAL value\nREF_LOCAL value\n"
        record_cases = {
            "record-ref": (record_ref + "NEW_ARRAY_LITERAL 1 ref<Record>\nPUSH_CONST 0\nGET_INDEX\nGET_FIELD value\nCALL println 1", True),
            "record-ref-erasure": (record_ref + "NEW_ARRAY_LITERAL 1 Record\nPOP", False),
            "record-ref-forgery": (record + "NEW_ARRAY_LITERAL 1 ref<Record>\nPOP", False),
            "record-ref-integer": ("PUSH_CONST 7\nNEW_ARRAY_LITERAL 1 ref<Record>\nPOP", False),
        }
        for binary in (False, True):
            for name, (body, valid) in record_cases.items():
                path = root / (name + ("-binary.nil" if binary else "-text.nil"))
                path.write_bytes(record_wire.encode(record_wire.instructions(body),
                    [("Record", [("value", "i32")], [])], binary))
                result = run([interpreter, "--no-ai", path], success=valid)
                assert result.stdout == ("7\n" if valid else ""), result
                checks += 1
                if args.llvm == "ON":
                    artifact = root / "record-ref.o"
                    artifact.write_bytes(b"preserve prior object\n")
                    result = run([assembler, "-o", artifact, path], success=valid)
                    if not valid:
                        assert artifact.read_bytes() == b"preserve prior object\n", result
                    checks += 1
        for runtime_tests in args.runtime_tests:
            modes = [kind + "-" + reason for kind in ("class", "reference")
                     for reason in ("null", "oob", "identity", "null-push", "null-set", "wrong-kind", "foreign", "context")]
            modes += ["nested-wrong-class", "nested-wrong-reference", "nested-reference-erasure"]
            for mode in modes:
                result = run([runtime_tests.resolve(), "--typed-array-failure", mode], success=False)
                assert not result.stdout and "NIL native runtime error:" in result.stderr, result
                checks += 1
    print(f"{checks} class/reference-array contract checks passed")


if __name__ == "__main__":
    main()
