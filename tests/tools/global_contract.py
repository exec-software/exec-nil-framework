#!/usr/bin/env python3
"""Check typed global metadata and initialization across NIL consumers."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def binary_module(body: list[tuple[str, list[str]]]) -> bytes:
    """Encode the documented executable-v3 sections without a source checker."""
    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        data = value.encode("utf-8")
        return u32(len(data)) + data

    function = (u32(1) + string("main") + u32(0) + u32(0) + string("i32")
                + u32(0) + u32(0) + u32(0) + u32(len(body)))
    for opcode, operands in body:
        function += (string(opcode) + u32(len(operands))
                     + b"".join(string(value) for value in operands) + u32(0))
    sections = [(1, u32(0) + u32(0)), (2, function),
                (3, u32(1) + string("message") + string("global text")),
                (4, u32(0)), (5, string("global_contract") + u32(0))]
    table = b""
    offset = 16 + 20 * len(sections)
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return (b"NIL\0" + struct.pack("<III", 3, 0, len(sections)) + table
            + b"".join(payload for _, payload in sections))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--assembler", type=Path, required=True)
    parser.add_argument("--linker", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    environment = os.environ.copy()
    for key in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(key, None)
    for name in ("compiler", "interpreter", "assembler", "linker", "runtime"):
        setattr(args, name, getattr(args, name).resolve())

    with tempfile.TemporaryDirectory(prefix="ny-global-contract-") as temporary:
        root = Path(temporary)

        def run(command):
            return subprocess.run([str(value) for value in command], cwd=root,
                                  capture_output=True, text=True, timeout=60,
                                  stdin=subprocess.DEVNULL, env=environment)

        def require(result, *, diagnostic="", output=""):
            assert (result.returncode > 0 if diagnostic else result.returncode == 0), result
            assert output is None or result.stdout == output, result
            assert (diagnostic in result.stderr if diagnostic else not result.stderr), result

        def write(name, body, binary):
            path = root / (name + (".binary.nil" if binary else ".text.nil"))
            if binary:
                path.write_bytes(binary_module(body))
            else:
                instructions = "\n".join(
                    " ".join([opcode, *(json.dumps(value) for value in operands)])
                    for opcode, operands in body)
                path.write_text('module global_contract\nconst message = "global text"\n'
                                'function main() -> i32:\n' + instructions
                                + '\nend function\n', encoding="utf-8")
            return path

        # Non-scalar globals keep their established interpreted representation;
        # the new optional scalar/text contract must not reject existing Ny.
        aggregate_source = root / "aggregate-globals.ny"
        aggregate_source.write_text(
            "struct Record\nvalue: i32\nend struct\n"
            "enum Choice\nOne\nend enum\n"
            "let values: array<i32> = [2, 3]\n"
            "let record: Record = Record { value: 17 }\n"
            "let choice: Choice = Choice.One\n"
            "fn main() -> i32\nprintln(values[1])\nprintln(record.value)\n"
            "return 0\nend fn\n", encoding="utf-8")
        for binary in (False, True):
            output = root / ("aggregates-binary.nil" if binary else "aggregates-text.nil")
            require(run([args.compiler, "--quiet", "--no-stdlib", "--no-bundle",
                         *(["--emit-binary-nil"] if binary else []),
                         "--output", output, aggregate_source]))
            require(run([args.interpreter, "--no-ai", output]), output="3\n17\n")

        source_modes = [["--no-bundle"], ["--no-bundle", "--emit-binary-nil"]]
        if args.llvm == "ON":
            source_modes.append(["--native"])

        def source_control(path, expected, providers=(), extra=()):
            for flags in source_modes:
                output = root / "source-control"
                require(run([args.compiler, "--quiet", "--no-stdlib", *flags, *extra,
                             "--output", output, *providers, path]))
                require(run([output] if "--native" in flags else
                            [args.interpreter, "--no-ai", output]), output=expected)

        fixture = Path(__file__).resolve().parents[1] / "language" / "module-global-contract.ny"
        source_control(fixture, fixture.with_suffix(".stdout").read_text(encoding="utf-8"))
        first = root / "global_first.ny"
        second = root / "global_second.ny"
        first.write_text('module global_first\npub let mut shared: text = "first"\n'
                         'let mut private_value: i32 = 7\n'
                         'const private_constant: i32 = 11\n'
                         'pub const public_constant: i32 = 13\n'
                         'pub fn read_private() -> i32\n'
                         'return private_value + private_constant\nend fn\n', encoding="utf-8")
        second.write_text('module global_second\npub let mut shared: text = "second"\n',
                          encoding="utf-8")
        for name, imports, body, expected in (
                ("named", "import global_first.{shared}",
                 'shared = "named"\nprintln(shared)\nprintln(global_second::shared)', "named\nsecond\n"),
                ("glob", "import global_first.*",
                 'shared = "glob"\nprintln(shared)\nprintln(global_second::shared)', "glob\nsecond\n"),
                ("alias", "import global_first\nnamespace owner = global_first",
                 'owner::shared = "alias"\nprintln(owner::shared)\nprintln(global_second::shared)', "alias\nsecond\n"),
                ("visibility", "import global_first.{public_constant}",
                 'println(public_constant)\nprintln(global_first::read_private())', "13\n18\n"),
                ("shadow", "import global_first.*\nimport global_second.*",
                 'let mut shared: text = "local"\nshared = "local changed"\nprintln(shared)\n'
                 'println(global_first::shared)\nprintln(global_second::shared)',
                 "local changed\nfirst\nsecond\n")):
            path = root / (name + "-globals.ny")
            path.write_text("module global_client\n" + imports + "\nfn main() -> i32\n" +
                            body + "\nreturn 0\nend fn\n", encoding="utf-8")
            source_control(path, expected, (first, second))
            if name == "named":
                # This flag continues after checker errors; it does not skip
                # successful resolution of an unambiguous imported identity.
                source_control(path, expected, (first, second), ("--no-typecheck",))

        rejected_sources = {
            "ambiguous_read": ("import global_first.*\nimport global_second.*",
                               "println(shared)", "Ambiguous imported global 'shared'", []),
            "ambiguous_write": ("import global_first.*\nimport global_second.*",
                                'shared = "wrong"', "Ambiguous imported global 'shared'", []),
            "ambiguous_named": ("import global_first.{shared}\nimport global_second.{shared}",
                                "println(shared)", "Ambiguous imported global 'shared'", []),
            "private_read": ("", "println(global_first::private_value)", "is private to module", []),
            "private_write": ("", "global_first::private_value = 9", "is private to module", []),
            "private_const_read": ("", "println(global_first::private_constant)",
                                   "is private to module", []),
            "private_import": ("import global_first.{private_value}", "println(private_value)",
                               "is private to module", []),
            "unchecked_ambiguous_read": ("import global_first.*\nimport global_second.*",
                                         "println(shared)", "has no exact global storage identity",
                                         ["--no-typecheck"]),
            "unchecked_ambiguous_write": ("import global_first.*\nimport global_second.*",
                                          'shared = "wrong"', "has no exact global storage identity",
                                          ["--no-typecheck"]),
            "unchecked_ambiguous_function": (
                "import global_first.*\nimport global_second.*\n"
                "fn shared() -> i32\nreturn 42\nend fn",
                "let callback = shared", "has no exact global storage identity", ["--no-typecheck"]),
            "unchecked_unknown": ("", "println(missing::shared)",
                                  "has no exact global storage identity", ["--no-typecheck"]),
            "duplicate_module": ("let shared: i32 = 1\nlet shared: i32 = 2", "",
                                 "duplicate global storage name", ["--no-typecheck"]),
        }
        for name, (declarations, body, diagnostic, extra) in rejected_sources.items():
            path = root / (name + ".ny")
            path.write_text("module global_client\n" + declarations +
                            "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n",
                            encoding="utf-8")
            for flags in source_modes:
                output = root / "protected-global-output"
                output.write_bytes(b"previous global artifact\n")
                require(run([args.compiler, "--quiet", "--no-stdlib", *flags, *extra,
                             "--output", output, first, second, path]), diagnostic=diagnostic)
                assert output.read_bytes() == b"previous global artifact\n"

        forward = root / "forward-global.ny"
        forward.write_text('module forward\nlet early: text = later\nlet later: text = "late"\n'
                           'fn main() -> i32\nprintln(early)\nreturn 0\nend fn\n', encoding="utf-8")
        for flags in source_modes:
            output = root / "forward-output"
            require(run([args.compiler, "--quiet", "--no-stdlib", *flags,
                         "--output", output, forward]))
            native = "--native" in flags
            require(run([output] if native else [args.interpreter, "--no-ai", output]),
                    diagnostic="read before initialization" if native else "Global not found")

        finish = [("PUSH_CONST", ["0"]), ("RET", [])]
        bad = {
            "conflicting": ([("PUSH_CONST_STR", ["message"]),
                             ("STORE_GLOBAL", ["value", "text"]),
                             ("LOAD_GLOBAL", ["value", "i64"]), ("POP", [])],
                            "conflicting or missing storage type metadata"),
            "erased": ([("PUSH_CONST_STR", ["message"]),
                        ("STORE_GLOBAL", ["value", "text"]),
                        ("LOAD_GLOBAL", ["value"]), ("POP", [])],
                       "conflicting or missing storage type metadata"),
            "unsupported": ([("LOAD_GLOBAL", ["value", "ptr<i32>"]), ("POP", [])],
                            "unsupported storage type"),
            "empty": ([("LOAD_GLOBAL", ["value", ""]), ("POP", [])],
                      "unsupported storage type"),
            "extra": ([("LOAD_GLOBAL", ["value", "text", "extra"]), ("POP", [])],
                      "expects 1 to 2 operand(s)"),
            "integer-as-text": ([("PUSH_CONST", ["7"]),
                                 ("STORE_GLOBAL", ["value", "text"]),
                                 ("LOAD_GLOBAL", ["value", "text"]), ("POP", [])],
                                "does not match storage type 'text'"),
            "array-as-text": ([("NEW_ARRAY_LITERAL", ["0", "u8"]),
                               ("STORE_GLOBAL", ["value", "text"]),
                               ("LOAD_GLOBAL", ["value", "text"]), ("POP", [])],
                              "does not match storage type 'text'"),
            "text-as-integer": ([("PUSH_CONST_STR", ["message"]),
                                 ("STORE_GLOBAL", ["value", "i64"]),
                                 ("LOAD_GLOBAL", ["value", "i64"]), ("POP", [])],
                                "does not match storage type 'i64'"),
            "integer-as-float": ([("PUSH_CONST", ["7"]),
                                  ("STORE_GLOBAL", ["value", "f64"]),
                                  ("LOAD_GLOBAL", ["value", "f64"]), ("POP", [])],
                                 "does not match storage type 'f64'"),
        }
        sentinel = b"preserve existing object\n"
        for binary in (False, True):
            for name, (body, diagnostic) in bad.items():
                path = write(name, body + finish, binary)
                require(run([args.interpreter, "--no-ai", path]), diagnostic=diagnostic)
                if args.llvm == "ON":
                    obj = root / "protected.o"
                    obj.write_bytes(sentinel)
                    require(run([args.assembler, "-o", obj, path]), diagnostic=diagnostic)
                    assert obj.read_bytes() == sentinel, name

            controls = {
                "typed-text": ([("PUSH_CONST_STR", ["message"]),
                                ("STORE_GLOBAL", ["value", "text"]),
                                ("LOAD_GLOBAL", ["value", "text"]),
                                ("CALL", ["println", "1"])], "global text\n"),
                "legacy-integer": ([("PUSH_CONST", ["37"]),
                                    ("STORE_GLOBAL", ["value"]),
                                    ("LOAD_GLOBAL", ["value"]),
                                    ("CALL", ["println", "1"])], "37\n"),
                "uninitialized": ([("LOAD_GLOBAL", ["value", "text"]),
                                   ("POP", [])], ""),
            }
            for name, (body, output) in controls.items():
                path = write(name, body + finish, binary)
                uninitialized = name == "uninitialized"
                require(run([args.interpreter, "--no-ai", path]), output=output,
                        diagnostic="Global not found" if uninitialized else "")
                if args.llvm != "ON":
                    continue
                obj, executable = root / "control.o", root / "control"
                require(run([args.assembler, "-o", obj, path]), output=None)
                require(run([args.linker, "--runtime", args.runtime, "-o", executable, obj]), output=None)
                require(run([executable]), output=output,
                        diagnostic="read before initialization" if uninitialized else "")
        print("global contracts passed: 9 negative and 3 control cases in text/binary NIL")


if __name__ == "__main__":
    main()
