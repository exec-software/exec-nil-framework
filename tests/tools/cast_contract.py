#!/usr/bin/env python3
"""Check cast boundaries, text Boolean parity, and interpreted numeric text conversions."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    base = [compiler, "--quiet", "--no-stdlib"]
    environment = os.environ.copy()
    for key in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(key, None)
    sentinel = b"previous artifact must survive rejected conversion\n"

    with tempfile.TemporaryDirectory(prefix="ny-cast-contract-") as temporary:
        root = Path(temporary)

        def run(argv):
            return subprocess.run([str(part) for part in argv], cwd=root,
                                  stdin=subprocess.DEVNULL, capture_output=True,
                                  text=True, timeout=60, env=environment)

        def success(result, expected="", *, allow_diagnostics=False):
            assert (result.returncode == 0 and result.stdout == expected and
                    (allow_diagnostics or not result.stderr)), (
                result.args, result.returncode, result.stdout, result.stderr, expected)

        def rejection(result, diagnostic):
            assert (result.returncode == 1 and not result.stdout and
                    diagnostic in result.stderr), (
                result.args, result.returncode, result.stdout, result.stderr, diagnostic)

        def source(name, body, declarations=""):
            path = root / (name + ".ny")
            path.write_text(declarations + "\nfn main() -> i32\n" + body +
                            "\nreturn 0\nend fn\n", encoding="utf-8")
            return path

        native = args.llvm == "ON"
        if native:
            control = source("control", "")
            result = run([*base, "--native", "--output", root / "control", control])
            if result.returncode and any(text in result.stderr.lower() for text in (
                    "no llvm backend", "llvm backend not available", "llvm backend is not available")):
                native = False
            else:
                success(result)
                success(run([root / "control"]))

        record = "struct Record\nvalue: i32\nend struct\n"
        other = "struct Other\nvalue: i32\nend struct\n"
        generic = "struct Box<T>\nvalue: T\nend struct\n"
        invalid_source = {
            "integer_pointer": ("let value = 7 as ptr<i32>", ""),
            "text_pointer": ('let value = "wrong" as ptr<i32>', ""),
            "pointer_integer": ("let p = allocate<i32>(1)\nlet value = p as i64", ""),
            "pointer_retype": ("let p = allocate<i32>(1)\nlet value = p as ptr<i64>", ""),
            "array_retype": ("let value = [1, 2] as array<i64>", ""),
            "array_integer": ("let value = [1, 2] as i64", ""),
            "record_integer": ("let value = Record { value: 7 } as i64", record),
            "record_text": ("let value = Record { value: 7 } as text", record),
            "record_other": ("let value = Record { value: 7 } as Other", record + other),
            "generic_retype": ("let value = Box<i32> { value: 7 } as Box<i64>", generic),
            "enum_retype": ("let value = Choice.One as OtherChoice",
                            "enum Choice\nOne\nend enum\nenum OtherChoice\nOne\nend enum\n"),
            "enum_integer": ("let value = Choice.One as i32", "enum Choice\nOne\nend enum\n"),
            "unknown_target": ("let value = 1 as NotAType", ""),
            "unknown_identity": ("", "fn identity(value: NotAType) -> NotAType\n"
                                  "return value as NotAType\nend fn\n"),
            "any_erasure": ("let value = 1 as any", ""),
            "any_recovery": ("let value: any = 1\nlet scalar = value as i32", ""),
            "any_identity": ("let value: any = 1\nlet erased = value as any", ""),
        }
        for name, (body, declarations) in invalid_source.items():
            path = source(name, body, declarations)
            modes = [["--no-bundle"], ["--no-bundle", "--emit-binary-nil"]]
            if native:
                modes.append(["--native"])
            for flags in modes:
                output = root / "protected-output"
                output.write_bytes(sentinel)
                rejection(run([*base, *flags, "--output", output, path]), "Cannot cast '")
                assert output.read_bytes() == sentinel, name

        function = "fn number() -> i64\nreturn 7\nend fn\n"
        static_function = (record + "impl Record\npub fn number() -> i64\n"
                           "return 7\nend fn\nend impl\ntype Alias = Record\n")
        invalid_values = {
            "function_bool": ("let value = number as bool", function,
                              "Cannot cast a function value to scalar type 'bool'"),
            "aliased_static_function_bool": ("let value = Alias::number as bool", static_function,
                                             "Cannot cast a function value to scalar type 'bool'"),
            "builtin_function_bool": ("let value = read_i32 as bool", "",
                                      "Cannot cast a function value to scalar type 'bool'"),
            "not_function": ("println(not number)", function,
                             "Logical 'not' requires a boolean operand"),
            "not_integer": ("println(not 1)", "", "Logical 'not' requires a boolean operand"),
            "not_float": ("println(not 1.5)", "", "Logical 'not' requires a boolean operand"),
            "not_text": ('println(not "text")', "", "Logical 'not' requires a boolean operand"),
            "not_record": ("println(not Record { value: 7 })", record,
                           "Logical 'not' requires a boolean operand"),
        }
        for name, (body, declarations, diagnostic) in invalid_values.items():
            path = source(name, body, declarations)
            for flags in modes:
                output = root / "protected-output"
                output.write_bytes(sentinel)
                rejection(run([*base, *flags, "--output", output, path]), diagnostic)
                assert output.read_bytes() == sentinel, name

        # A receiver's concrete generic arguments must reach the method return
        # and then the matched payload. A bare Maybe<T> return would make both
        # valid casts below fail, or could conceal the array-type mismatch.
        provider = root / "generic_member_provider.ny"
        provider.write_text(
            "module cast_members\npub enum Maybe<T>\nSome(T)\nNone\nend enum\n"
            "pub struct Box<T>\npub value: T\nend struct\n"
            "impl<T> Box<T>\npub fn get(self) -> Maybe<T>\n"
            "return Maybe.Some(self.value)\nend fn\nend impl\n", encoding="utf-8")
        consumer = root / "generic_member_consumer.ny"
        consumer.write_text(
            "module cast_member_client\nimport cast_members.{Box, Maybe}\n"
            "fn byte_value(value: Box<u8>) -> i32\nmatch value.get()\n"
            "Maybe.Some(item) -> return item as i32\n"
            "Maybe.None -> return 0\nend match\nend fn\n"
            "fn text_value(value: Box<text>) -> text\nmatch value.get()\n"
            "Maybe.Some(item) -> return item as text\n"
            'Maybe.None -> return ""\nend match\nend fn\n'
            "fn main() -> i32\n"
            "println(byte_value(Box<u8> { value: 251 as u8 }))\n"
            'println(text_value(Box<text> { value: "concrete" }))\n'
            "return 0\nend fn\n", encoding="utf-8")
        rejected_consumer = root / "generic_member_retype.ny"
        rejected_consumer.write_text(
            "module cast_member_retype\nimport cast_members.{Box, Maybe}\n"
            "fn retype(value: Box<array<u8>>) -> array<i32>\n"
            "match value.get()\nMaybe.Some(item) -> return item as array<i32>\n"
            "Maybe.None -> return []\nend match\nend fn\n"
            "fn main() -> i32\nreturn 0\nend fn\n", encoding="utf-8")
        for binary in (False, True):
            flags = ["--emit-binary-nil"] if binary else []
            output = root / "generic_member.nil"
            success(run([*base, "--no-bundle", *flags, "--output", output,
                         provider, consumer]))
            success(run([interpreter, "--no-ai", output]), "251\nconcrete\n")
            output.write_bytes(sentinel)
            rejection(run([*base, "--no-bundle", *flags, "--output", output,
                           provider, rejected_consumer]),
                      "Cannot cast 'array<u8>' to 'array<i32>'")
            assert output.read_bytes() == sentinel

        valid = source("valid_text", '\n'.join([
            'println("+42" as i64)',
            'println("-9223372036854775808" as i64)',
            'println("4294967297" as i32)',
            'println("-1" as u8)',
            'println(("1.25e2" as f64) == 125.0)',
            'println("TRUE" as bool)',
            'println("yes" as bool)',
            'println("on" as bool)',
            'println("1" as bool)',
            'println("FALSE" as bool)',
            'println("no" as bool)',
            'println("off" as bool)',
            'println("0" as bool)',
            'println(42 as text)',
            'println((-42) as text)',
            'println(2.5 as text)',
            'println(true as text)',
        ]))
        expected = "42\n-9223372036854775808\n1\n255\n1\n1\n1\n1\n1\n0\n0\n0\n0\n42\n-42\n2.500000\n1\n"

        def interpreted(path, expected="", diagnostic=None, unchecked=False):
            for binary in (False, True):
                flags = ["--emit-binary-nil"] if binary else []
                if unchecked:
                    flags.append("--no-typecheck")
                nil = root / "conversion.nil"
                success(run([*base, *flags, "--no-bundle", "--output", nil, path]),
                        allow_diagnostics=unchecked)
                result = run([interpreter, "--no-ai", nil])
                if diagnostic:
                    rejection(result, diagnostic)
                else:
                    success(result, expected)
                if os.name != "nt":
                    bundle = root / "conversion.bundle"
                    success(run([*base, *flags, "--output", bundle, path]),
                            allow_diagnostics=unchecked)
                    result = run([bundle])
                    if diagnostic:
                        rejection(result, diagnostic)
                    else:
                        success(result, expected)

        interpreted(valid, expected)
        invalid_text = []
        for text in ('""', '"12tail"', '" 12"', '"12 "', '"0x10"',
                     '"9223372036854775808"', '"-9223372036854775809"', '"+"'):
            invalid_text.append((text + " as i64", "CAST: text is not a complete decimal i64 value"))
        for text in ('""', '"1.2tail"', '" 1.2"', '"1.2 "', '"NaN"',
                     '"inf"', '"1e999"'):
            invalid_text.append((text + " as f64", "CAST: text is not a complete finite f64 value"))
        for text in ('""', '"truthy"', '"2"', '" true"', '"false "'):
            invalid_text.append((text + " as bool", "CAST: text is not a recognized bool value"))
        for index, (expression, diagnostic) in enumerate(invalid_text):
            invalid = source(f"invalid_text_{index}", "println(" + expression + ")")
            interpreted(invalid, diagnostic=diagnostic)
            if native and expression.endswith(" as bool"):
                executable = root / f"invalid_text_native_{index}"
                success(run([*base, "--native", "--output", executable, invalid]))
                rejection(run([executable]), "text is not a recognized bool value")

        boolean_spellings = ("true", "TRUE", "Yes", "1", "on",
                             "false", "FALSE", "No", "0", "off")
        boolean_text = source("valid_boolean_text", "\n".join(
            f'println("{text}" as bool)' for text in boolean_spellings))
        boolean_expected = "1\n" * 5 + "0\n" * 5
        interpreted(boolean_text, boolean_expected)
        if native:
            executable = root / "valid_boolean_text_native"
            success(run([*base, "--native", "--output", executable, boolean_text]))
            success(run([executable]), boolean_expected)

        # Bypass source checking deliberately to exercise the VM's independent
        # fail-closed behavior in textual, binary, and bundled executable NIL.
        runtime_rejections = [
            ("let value = 1 as any", "", "CAST: unsupported target 'any'"),
            ("let value = 1 as Missing", "", "CAST: unsupported target 'Missing'"),
            ("let value = 1 as ptr<i32>", "", "CAST: unsupported target 'ptr<i32>'"),
            ("let value = 0 as array<i32>", "", "CAST: unsupported target 'array<i32>'"),
        ]
        for target in ("i64", "i32", "u8", "f64", "bool", "text"):
            runtime_rejections.append(("let value = Record { value: 7 } as " + target,
                                       record, "CAST: value cannot be converted to '"))
        for index, (body, declarations, diagnostic) in enumerate(runtime_rejections):
            interpreted(source(f"unchecked_{index}", body, declarations),
                        diagnostic=diagnostic, unchecked=True)

        for name, instruction, diagnostic in (
                ("missing", "CAST", "expects 1 operand(s)"),
                ("extra", "CAST i32 i64", "expects 1 operand(s)"),
                ("empty", 'CAST ""', "CAST: exactly one nonempty target type is required")):
            nil = root / ("raw_" + name + ".nil")
            nil.write_text("function main() -> i32:\nPUSH_CONST 1\n" + instruction +
                           "\nRET\nend function\n", encoding="utf-8")
            rejection(run([interpreter, "--no-ai", nil]), diagnostic)

        if native:
            for name, expression in (("text_to_scalar", '"12" as i64'),
                                     ("scalar_to_text", "12 as text")):
                path = source(name, "println(" + expression + ")")
                output = root / "protected-native"
                output.write_bytes(sentinel)
                rejection(run([*base, "--native", "--output", output, path]),
                          "ordinary CAST cannot convert")
                assert output.read_bytes() == sentinel

    print(f"cast contracts passed: {len(invalid_source)} source rejections, "
          f"{len(invalid_values)} function/boolean source rejections, "
          f"{len(invalid_text)} malformed text conversions, "
          f"{len(runtime_rejections)} unchecked runtime rejections, "
          "3 raw NIL operand rejections, concrete generic member controls, "
          "valid Boolean text controls, and interpreted numeric text controls" +
          ("; native boundaries tested" if native else "; native routes unavailable"))


if __name__ == "__main__":
    main()
