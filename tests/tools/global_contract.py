#!/usr/bin/env python3
"""Check typed global metadata and initialization across NIL consumers."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import struct
import subprocess
import tempfile
import textwrap
import shlex

# The toolchain appends .exe to extensionless executables on Windows.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_output_staging import linked_executable_path


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
    parser.add_argument("--nilstd", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    environment = os.environ.copy()
    for key in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(key, None)
    for name in ("compiler", "interpreter", "assembler", "linker", "runtime", "nilstd"):
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
                                + '\nend function\n', encoding="utf-8", newline='\n')
            return path

        # Nominal globals retain their exact declared identities, while legacy
        # scalar-array globals continue to use the existing interpreted profile.
        aggregate_source = root / "aggregate-globals.ny"
        aggregate_source.write_text(
            "struct Record\nvalue: i32\nend struct\n"
            "enum Choice\nOne\nend enum\n"
            "let values: array<i32> = [2, 3]\n"
            "let record: Record = Record { value: 17 }\n"
            "let choice: Choice = Choice.One\n"
            "fn main() -> i32\nprintln(values[1])\nprintln(record.value)\n"
            "return 0\nend fn\n", encoding="utf-8", newline='\n')
        for binary in (False, True):
            output = root / ("aggregates-binary.nil" if binary else "aggregates-text.nil")
            require(run([args.compiler, "--quiet", "--no-stdlib", "--no-bundle",
                         *(["--emit-binary-nil"] if binary else []),
                         "--output", output, aggregate_source]))
            require(run([args.interpreter, "--no-ai", output]), output="3\n17\n")

        # Real imported Matrix classes flow through private Ny module globals,
        # concrete Option methods and record fields before dispatching methods.
        # No cast, fake owner, erased global or compiler-only acceptance can
        # satisfy these execution controls.
        package = root / "probe"
        package.mkdir()
        (package / "foreign_types.matrix").write_text(textwrap.dedent("""\
            book probe.foreign_types
            read everything from nil.std.core
            read everything from nil.std.nildata
            public record_owner begin class
                field identity as number
                public read_identity returns number begin paragraph
                    give my identity
                end paragraph
                public echo_result receives candidate as Result of `Value`, text returns Result of `Value`, text begin paragraph
                    give candidate
                end paragraph
            end class
            public make_value returns Result of `Value`, text begin paragraph
                value content is "nominal"
                give string_value with content
            end paragraph
            public pass_value receives candidate as Result of `Value`, text returns Result of `Value`, text begin paragraph
                give candidate
            end paragraph
            public record_client begin class
                field identity as number
                public read_identity returns number begin paragraph
                    give my identity
                end paragraph
            end class
            """), encoding="utf-8", newline='\n')
        (package / "adapter.ny").write_text(textwrap.dedent("""\
            module probe.adapter
            import nil.std.core
            import nil.std.nildata
            import probe.foreign_types
            struct OwnerSlot
                owner: record_owner
            end struct
            let mut stored_owner: Option<record_owner> = Option<record_owner>.none()
            let mut stored_client: Option<record_client> = Option<record_client>.none()
            let mut direct_owner: record_owner = record_owner { identity: 7 }
            let mut wrapped_owner: OwnerSlot = OwnerSlot { owner: record_owner { identity: 8 } }
            fn bind(owner: record_owner, client: record_client)
                stored_owner = Option<record_owner>.some(owner)
                stored_client = Option<record_client>.some(client)
                direct_owner = owner
                wrapped_owner = OwnerSlot { owner: owner }
            end fn
            fn reset()
                stored_owner = Option<record_owner>.none()
                stored_client = Option<record_client>.none()
            end fn
            pub fn exercise()
                println(direct_owner.read_identity())
                println(wrapped_owner.owner.read_identity())
                println(stored_owner.is_none())
                println(stored_client.is_none())
                let owner = record_owner { identity: 41 }
                let client = record_client { identity: 42 }
                let materialized = pass_value(owner.echo_result(make_value()))
                println(materialized.is_error())
                println(nil::std::nildata::to_json(materialized.unwrap()).unwrap())
                bind(owner, client)
                println(stored_owner.unwrap().read_identity())
                println(stored_client.unwrap().read_identity())
                println(direct_owner.read_identity())
                println(wrapped_owner.owner.read_identity())
                reset()
                println(stored_owner.is_none())
                println(stored_client.is_none())
            end fn
            """), encoding="utf-8", newline='\n')
        nominal_expected = '7\n8\n1\n1\n0\n"nominal"\n41\n42\n41\n41\n1\n1\n'
        nominal_types = {
            "probe::adapter::stored_owner": "nil::std::core::Option<probe::foreign_types::record_owner>",
            "probe::adapter::stored_client": "nil::std::core::Option<probe::foreign_types::record_client>",
            "probe::adapter::direct_owner": "probe::foreign_types::record_owner",
            "probe::adapter::wrapped_owner": "probe::adapter::OwnerSlot",
        }
        nominal_inputs = {
            "ny": "module choice_client\nimport probe.adapter\n"
                  "fn main() -> i32\nexercise()\nreturn 0\nend fn\n",
            "matrix": "book choice_client\nread everything from probe.adapter\n"
                      "main returns number begin paragraph\n"
                      "evaluate exercise with nothing\ngive 0\nend paragraph\n",
        }
        nominal_options = ["--no-stdlib", "--sdk-path", args.nilstd, "-I", root]
        for language, contents in nominal_inputs.items():
            source = root / ("nominal-globals." + language)
            source.write_text(contents, encoding="utf-8", newline='\n')
            analysis = run([args.compiler, *nominal_options, "--analyze", source])
            assert analysis.returncode == 0 and not analysis.stderr, analysis
            for binary in (False, True):
                output = root / ("nominal-" + language + ("-binary.nil" if binary else "-text.nil"))
                require(run([args.compiler, "--quiet", *nominal_options, "--no-bundle",
                             *(["--emit-binary-nil"] if binary else []), "--output", output, source]))
                if not binary:
                    serialized = output.read_text(encoding="utf-8")
                    resolved_result = "nil::std::core::Result<nil::std::nildata::Value,text>"
                    for name in ("probe__foreign_types__make_value", "probe__foreign_types__pass_value",
                                 "probe::foreign_types::record_owner::echo_result"):
                        signature = next(line for line in serialized.splitlines()
                                         if line.startswith(("function " + name + "(",
                                                             "function " + name + " ->")))
                        assert signature.endswith(" -> " + resolved_result + ":"), signature
                        if "make_value" not in name:
                            assert "candidate: " + resolved_result in signature, signature
                    method = next(line for line in serialized.splitlines()
                                  if line.strip().startswith("method echo_result "))
                    assert shlex.split(method)[4] == resolved_result, method
                    assert shlex.split(method)[8] == resolved_result, method
                    accesses = []
                    for line in serialized.splitlines():
                        if not line.strip().startswith(("LOAD_GLOBAL ", "STORE_GLOBAL ")):
                            continue
                        lexer = shlex.shlex(line, posix=True)
                        lexer.whitespace_split = True
                        lexer.commenters = ";"
                        accesses.append(list(lexer))
                    observed = set()
                    for access in accesses:
                        if access[1] in nominal_types:
                            assert access == [access[0], access[1], nominal_types[access[1]]], access
                            observed.add(access[1])
                    assert observed == set(nominal_types), observed
                # A new interpreter instance must begin with its own globals.
                for _ in range(2):
                    require(run([args.interpreter, "--no-ai", output]), output=nominal_expected)
            if args.llvm == "ON":
                protected = linked_executable_path(root / ("nominal-native-" + language))
                protected.write_bytes(b"prior native artifact\n")
                require(run([args.compiler, "--quiet", *nominal_options, "--native",
                             "--output", protected, source]),
                        diagnostic="nominal aggregate globals are not supported by native compilation")
                assert protected.read_bytes() == b"prior native artifact\n"

        forward_nominal = root / "forward-nominal.ny"
        forward_nominal.write_text(
            "module forward_nominal\nimport probe.foreign_types\n"
            "let first: record_owner = later\n"
            "let later: record_owner = record_owner { identity: 9 }\n"
            "fn main() -> i32\nprintln(first.read_identity())\nreturn 0\nend fn\n",
            encoding="utf-8", newline='\n')
        for binary in (False, True):
            output = root / ("forward-nominal-binary.nil" if binary else "forward-nominal-text.nil")
            require(run([args.compiler, "--quiet", *nominal_options, "--no-bundle",
                         *(["--emit-binary-nil"] if binary else []), "--output", output, forward_nominal]))
            require(run([args.interpreter, "--no-ai", output]), diagnostic="Global not found")

        source_modes = [["--no-bundle"], ["--no-bundle", "--emit-binary-nil"]]
        if args.llvm == "ON":
            source_modes.append(["--native"])

        def source_control(path, expected, providers=(), extra=()):
            for flags in source_modes:
                output = root / "source-control"
                require(run([args.compiler, "--quiet", "--no-stdlib", *flags, *extra,
                             "--output", output, *providers, path]))
                require(run([linked_executable_path(output)] if "--native" in flags else
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
                         'return private_value + private_constant\nend fn\n', encoding="utf-8", newline='\n')
        second.write_text('module global_second\npub let mut shared: text = "second"\n',
                          encoding="utf-8", newline='\n')
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
                            body + "\nreturn 0\nend fn\n", encoding="utf-8", newline='\n')
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
                            encoding="utf-8", newline='\n')
            for flags in source_modes:
                output = root / "protected-global-output"
                output.write_bytes(b"previous global artifact\n")
                require(run([args.compiler, "--quiet", "--no-stdlib", *flags, *extra,
                             "--output", output, first, second, path]), diagnostic=diagnostic)
                assert output.read_bytes() == b"previous global artifact\n"

        forward = root / "forward-global.ny"
        forward.write_text('module forward\nlet early: text = later\nlet later: text = "late"\n'
                           'fn main() -> i32\nprintln(early)\nreturn 0\nend fn\n', encoding="utf-8", newline='\n')
        for flags in source_modes:
            output = root / "forward-output"
            require(run([args.compiler, "--quiet", "--no-stdlib", *flags,
                         "--output", output, forward]))
            native = "--native" in flags
            require(run([linked_executable_path(output)] if native
                        else [args.interpreter, "--no-ai", output]),
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
                obj, executable = root / "control.o", linked_executable_path(root / "control")
                require(run([args.assembler, "-o", obj, path]), output=None)
                require(run([args.linker, "--runtime", args.runtime, "-o", executable, obj]), output=None)
                require(run([executable]), output=output,
                        diagnostic="read before initialization" if uninitialized else "")
        print("global contracts passed: 9 negative and 3 control cases in text/binary NIL")
        print("nominal global contracts passed: 8 VM executions, 2 initialization rejections, "
              + ("2" if args.llvm == "ON" else "0") + " native rejections")


if __name__ == "__main__":
    main()
