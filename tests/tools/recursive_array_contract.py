#!/usr/bin/env python3
"""Finite recursive schemas preserve initialized-object and array contracts."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tests"))
from native_output_staging import linked_executable_path
import tuple_array_nil_contract as wire


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "stdlib"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--runtime-tests", action="append", default=[], type=Path)
    args = parser.parse_args()
    compiler, interpreter, assembler, stdlib = (
        getattr(args, name).resolve(strict=True)
        for name in ("compiler", "interpreter", "assembler", "stdlib"))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_LIBRARY_PATH"))
                   and key != "NIL_STRICT_BOUNDS"}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-recursive-array-contract-") as temporary:
        root = Path(temporary)

        def run(command, *, success=True):
            result = subprocess.run([str(part) for part in command], cwd=root,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", timeout=90)
            assert (result.returncode == 0 if success else result.returncode > 0), (command, result)
            return result

        def encoded(name, body, binary, schemas=(), enums=()):
            wire.ENUMS = list(enums)
            path = root / (name + (".binary.nil" if binary else ".text.nil"))
            path.write_bytes(wire.encode(wire.instructions(body), list(schemas), binary))
            return path

        base = [compiler, "--quiet", "--no-stdlib", "--no-bundle", "--sdk-path", stdlib]
        routes = [("text", []), ("binary", ["--emit-binary-nil"])]
        if args.llvm == "ON":
            routes += [("native", ["--native"]), ("jit", ["--native", "--jit"])]
        for name, extension in (("recursive-arrays", ".ny"),
                                ("recursive-arrays-matrix", ".matrix")):
            source = REPOSITORY / "tests/language" / (name + extension)
            expected = source.with_suffix(".stdout").read_text(encoding="utf-8")
            for route, flags in routes:
                native = route in ("native", "jit")
                output = (linked_executable_path(root / (name + "-" + route))
                          if native else root / (name + "-" + route + ".nil"))
                compiled = run([*base, *flags, "-o", output, source])
                result = compiled if route == "jit" else run([output] if native else
                             [interpreter, "--no-ai", "--memory-mode=safe", output])
                assert result.stdout == expected and not result.stderr, result
                checks += 1

        # Native stress above crosses its automatic safepoint. The interpreter
        # also supports explicit collection, whose intrinsic has no native ABI.
        source = root / "explicit-collection.ny"
        original = (REPOSITORY / "tests/language/recursive-arrays.ny").read_text(encoding="utf-8")
        source.write_text("import nil.std.runtime\n" + original.replace(
            "    println(total)\n", "    collect(0)\n    println(total)\n"),
            encoding="utf-8", newline="\n")
        expected = (REPOSITORY / "tests/language/recursive-arrays.stdout").read_text(encoding="utf-8")
        for binary in (False, True):
            output = root / ("explicit-collection-" + str(binary) + ".nil")
            run([*base, *(["--emit-binary-nil"] if binary else []), "-o", output, source])
            result = run([interpreter, "--no-ai", "--memory-mode=safe", output])
            assert result.stdout == expected and not result.stderr, result
            checks += 1

        # A long graph through array handles is distinct from a deeply nested
        # direct value: array storage checks the descriptor, not all descendants.
        source = root / "array-chain.ny"
        source.write_text("""struct Node
    value: i32
    children: array<Node>
end struct
fn main() -> i32
    let mut root = Node { value: 0, children: [] }
    let mut index: i32 = 1
    while index < 257
        root = Node { value: index, children: [root] }
        index = index + 1
    end while
    let roots: array<Node> = [root]
    let mut cursor = roots[0]
    let mut total: i64 = 0
    while cursor.children.len() > 0
        total = total + (cursor.value as i64)
        cursor = cursor.children[0]
    end while
    println(total)
    return 0
end fn
""", encoding="utf-8", newline="\n")
        for route, flags in routes:
            native = route in ("native", "jit")
            output = (linked_executable_path(root / ("chain-" + route))
                      if native else root / ("chain-" + route + ".nil"))
            compiled = run([*base, *flags, "-o", output, source])
            result = compiled if route == "jit" else run([output] if native else
                [interpreter, "--no-ai", "--memory-mode=safe", output])
            assert result.stdout == "32896\n" and not result.stderr, result
            checks += 1

        recursive = [("Node", [("value", "i32"), ("children", "array<Node>")], [])]
        required = [("RequiredSelf", [("next", "ref<RequiredSelf>")], [])]
        # An uninhabited schema can still describe an empty array. It grants no
        # permission to manufacture an initialized value of that schema.
        controls = [
            ("empty-required-self", "NEW_ARRAY_LITERAL 0 RequiredSelf\nLEN\nCALL println 1", required, []),
            ("empty-recursive-record", "NEW_ARRAY_LITERAL 0 Node\nLEN\nCALL println 1", recursive, []),
            ("empty-recursive-enum", "NEW_ARRAY_LITERAL 0 Choice\nLEN\nCALL println 1", [],
             [("Choice", [("Empty", ""), ("Next", "Choice")])]),
        ]
        incomplete = [
            ("missing-all-fields", "NEW_STRUCT Node\nNEW_ARRAY_LITERAL 1 Node\nPOP", recursive),
            ("missing-array-field", "NEW_STRUCT Node\nPUSH_CONST 1\nSET_FIELD value\nNEW_ARRAY_LITERAL 1 Node\nPOP", recursive),
            ("missing-scalar-field", "NEW_STRUCT Node\nNEW_ARRAY_LITERAL 0 Node\nSET_FIELD children\nNEW_ARRAY_LITERAL 1 Node\nPOP", recursive),
            ("required-self-construction", "NEW_STRUCT RequiredSelf\nNEW_ARRAY_LITERAL 1 RequiredSelf\nPOP", required),
        ]
        sentinel = b"preserve previous object\n"
        for binary in (False, True):
            for name, body, schemas, enums in controls:
                path = encoded(name, body, binary, schemas, enums)
                result = run([interpreter, "--no-ai", "--memory-mode=safe", path])
                assert result.stdout == "0\n" and not result.stderr, result
                checks += 1
                if args.llvm == "ON":
                    output = root / (path.name + ".o")
                    run([assembler, "-o", output, path])
                    assert output.is_file() and output.stat().st_size > 0
                    checks += 1
            for name, body, schemas in incomplete:
                path = encoded(name, body, binary, schemas)
                result = run([interpreter, "--no-ai", path], success=False)
                diagnostic = ("before all fields are initialized" if name == "required-self-construction"
                              else "fully initialized")
                assert diagnostic in result.stderr.lower(), result
                checks += 1
                if args.llvm == "ON":
                    output = root / (path.name + ".o")
                    output.write_bytes(sentinel)
                    result = run([assembler, "-o", output, path], success=False)
                    diagnostic = ("before all fields are initialized" if name == "required-self-construction"
                                  else "uninitialized")
                    assert diagnostic in result.stderr.lower(), result
                    assert output.read_bytes() == sentinel
                    checks += 1

            if args.llvm == "ON":
                # Native construction cannot first escape to a local and then
                # initialize its own required reference through that alias.
                path = encoded("self-alias-before-initialization",
                    "NEW_STRUCT RequiredSelf\nSTORE_LOCAL value\nLOAD_LOCAL value\n"
                    "LOAD_LOCAL value\nSET_FIELD next\nPOP", binary, required)
                output = root / (path.name + ".o")
                output.write_bytes(sentinel)
                result = run([assembler, "-o", output, path], success=False)
                assert "before all fields are initialized" in result.stderr.lower(), result
                assert output.read_bytes() == sentinel
                checks += 1
                unsupported = [
                    ("unknown-recursive-leaf", recursive + [("Bad", [("nodes", "array<Node>"), ("opaque", "Missing")], [])], [], "Bad"),
                    ("unsupported-recursive-enum-leaf", recursive, [("Bad", [("Next", "Node"), ("Opaque", "owned<Node>")])], "Bad"),
                ]
                for name, schemas, enums, kind in unsupported:
                    path = encoded(name, f"NEW_ARRAY_LITERAL 0 {kind}\nPOP", binary, schemas, enums)
                    output = root / (path.name + ".o")
                    output.write_bytes(sentinel)
                    result = run([assembler, "-o", output, path], success=False)
                    assert "[NativeCodegen]" in result.stderr, result
                    assert output.read_bytes() == sentinel
                    checks += 1

        if args.llvm == "ON":
            # Cycles terminate through a visited set, and distinct nominal
            # schemas retain a fixed admission budget even without any values.
            schemas = [(f"Node{index}", [("next", f"array<Node{index + 1}>")], [])
                       for index in range(4096)]
            schemas += [("Node4096", [("next", "array<Node0>")], [])]
            path = encoded("schema-budget", "NEW_ARRAY_LITERAL 0 Node0\nPOP", False, schemas)
            output = root / "schema-budget.o"
            output.write_bytes(sentinel)
            result = run([assembler, "-o", output, path], success=False)
            assert any(diagnostic in result.stderr for diagnostic in (
                "array element schema graph exceeds its bound",
                "NIL provenance type graph exceeds its bound")), result
            assert output.read_bytes() == sentinel
            checks += 1

    print(f"recursive array contract: {checks} checks passed")


if __name__ == "__main__":
    main()
