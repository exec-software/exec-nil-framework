#!/usr/bin/env python3
"""Verify concrete generic-enum metadata in textual and binary NIL."""

from __future__ import annotations

import argparse
import re
import shlex
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


@dataclass(frozen=True)
class Instruction:
    function_name: str
    opcode: str
    arguments: Tuple[str, ...]
    debug_line: int


def run_checked(argv: Sequence[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(argument) for argument in argv],
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            "command failed ({}):\nargv: {}\nstdout:\n{}\nstderr:\n{}".format(
                result.returncode, list(argv), result.stdout, result.stderr
            )
        )
    return result


def parse_text_instructions(path: Path) -> List[Instruction]:
    instructions: List[Instruction] = []
    function_name = ""
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        function_match = re.match(r"^function\s+([^:(\s]+)", raw_line)
        if function_match:
            function_name = function_match.group(1)
            continue
        if raw_line == "end function":
            function_name = ""
            continue
        if not function_name or not raw_line.startswith("    "):
            continue

        instruction_text = raw_line.strip()
        debug_line = 0
        debug_match = re.search(r"\s+;\s+line\s+(\d+)\s*$", instruction_text)
        if debug_match:
            debug_line = int(debug_match.group(1))
            instruction_text = instruction_text[: debug_match.start()].rstrip()
        tokens = shlex.split(instruction_text)
        if tokens:
            instructions.append(
                Instruction(function_name, tokens[0], tuple(tokens[1:]), debug_line)
            )
    return instructions


class BinaryCursor:
    def __init__(self, data: bytes, start: int, end: int):
        self.data = data
        self.position = start
        self.end = end

    def take(self, size: int) -> bytes:
        if size < 0 or self.position + size > self.end:
            raise AssertionError("truncated binary NIL field")
        value = self.data[self.position : self.position + size]
        self.position += size
        return value

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def string(self) -> str:
        size = self.u32()
        return self.take(size).decode("utf-8")


def parse_binary_instructions(path: Path) -> List[Instruction]:
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"NIL\0":
        raise AssertionError("artifact is not binary NIL")
    version, flags = struct.unpack_from("<II", data, 4)
    if version not in (1, 3) or flags != 0:
        raise AssertionError("artifact is not a supported executable binary NIL")
    section_count = struct.unpack_from("<I", data, 12)[0]
    if section_count == 0 or section_count > 64:
        raise AssertionError("invalid binary NIL section count")

    instructions: List[Instruction] = []
    for section_index in range(section_count):
        table_offset = 16 + section_index * 20
        if table_offset + 20 > len(data):
            raise AssertionError("truncated binary NIL section table")
        section_type = struct.unpack_from("<I", data, table_offset)[0]
        section_offset = struct.unpack_from("<Q", data, table_offset + 4)[0]
        section_size = struct.unpack_from("<Q", data, table_offset + 12)[0]
        if section_offset + section_size > len(data):
            raise AssertionError("binary NIL section lies outside the artifact")
        if section_type != 2:
            continue

        cursor = BinaryCursor(data, section_offset, section_offset + section_size)
        function_count = cursor.u32()
        for _ in range(function_count):
            function_name = cursor.string()
            for _ in range(cursor.u32()):
                cursor.string()
            for _ in range(cursor.u32()):
                cursor.string()
            cursor.string()  # return type
            if version == 3:
                cursor.u32()  # function flags
                cursor.u32()  # declaration source line
                for _ in range(cursor.u32()):
                    cursor.string()  # preserved function annotation
            for _ in range(cursor.u32()):
                opcode = cursor.string()
                arguments = tuple(cursor.string() for _ in range(cursor.u32()))
                debug_line = cursor.u32()
                instructions.append(
                    Instruction(function_name, opcode, arguments, debug_line)
                )
        if cursor.position != cursor.end:
            raise AssertionError("unconsumed binary NIL function section")
    if not instructions:
        raise AssertionError("binary NIL has no function instructions")
    return instructions


def variant_instructions(instructions: Iterable[Instruction]) -> List[Instruction]:
    return [
        instruction
        for instruction in instructions
        if instruction.opcode in {"NEW_VARIANT", "IS_VARIANT", "UNWRAP_VARIANT"}
    ]


def require_instruction(
    instructions: Iterable[Instruction],
    function_suffix: str,
    opcode: str,
    arguments: Tuple[str, ...],
    debug_line: int,
) -> None:
    for instruction in instructions:
        if (
            instruction.function_name.endswith(function_suffix)
            and instruction.opcode == opcode
            and instruction.arguments == arguments
            and instruction.debug_line == debug_line
        ):
            return
    raise AssertionError(
        "missing {} {} at line {} in function *{}".format(
            opcode, arguments, debug_line, function_suffix
        )
    )


def verify_metadata(instructions: List[Instruction]) -> None:
    result_bytes = "nil::std::core::Result<array<u8>,text>"
    result_bool = "nil::std::core::Result<bool,text>"
    option_text = "nil::std::core::Option<text>"

    required = (
        ("__byte_result", "NEW_VARIANT", ("Result::Ok", "1", result_bytes, "array<u8>"), 6),
        ("__byte_result", "NEW_VARIANT", ("Result::Error", "1", result_bytes, "text"), 8),
        ("__bool_result", "NEW_VARIANT", ("Result::Ok", "1", result_bool, "bool"), 13),
        ("__bool_result", "NEW_VARIANT", ("Result::Error", "1", result_bool, "text"), 15),
        ("__text_option", "NEW_VARIANT", ("Option::Some", "1", option_text, "text"), 20),
        ("__text_option", "NEW_VARIANT", ("Option::None", "0", option_text, "void"), 22),
        ("main", "IS_VARIANT", ("Result::Ok", result_bytes, "array<u8>"), 27),
        ("main", "UNWRAP_VARIANT", ("1", result_bytes, "array<u8>"), 27),
        # Line 33 is the wildcard arm: it must retain payload metadata even
        # though no UNWRAP_VARIANT is needed.
        ("main", "IS_VARIANT", ("Result::Ok", result_bytes, "array<u8>"), 33),
        ("main", "IS_VARIANT", ("Result::Error", result_bytes, "text"), 39),
        ("main", "UNWRAP_VARIANT", ("1", result_bytes, "text"), 39),
        ("main", "IS_VARIANT", ("Result::Ok", result_bool, "bool"), 46),
        ("main", "UNWRAP_VARIANT", ("1", result_bool, "bool"), 46),
        ("main", "IS_VARIANT", ("Option::Some", option_text, "text"), 53),
        ("main", "UNWRAP_VARIANT", ("1", option_text, "text"), 53),
        ("main", "IS_VARIANT", ("Option::None", option_text, "void"), 59),
    )
    for function_suffix, opcode, arguments, debug_line in required:
        require_instruction(
            instructions, function_suffix, opcode, arguments, debug_line
        )

    wildcard_unwraps = [
        instruction
        for instruction in instructions
        if instruction.function_name == "main"
        and instruction.opcode == "UNWRAP_VARIANT"
        and instruction.debug_line == 33
    ]
    if wildcard_unwraps:
        raise AssertionError("wildcard match unexpectedly unwraps its payload")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--interpreter", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--expected-stdout", required=True, type=Path)
    parser.add_argument("--stdlib", required=True, type=Path)
    arguments = parser.parse_args()

    compiler = arguments.compiler.resolve()
    interpreter = arguments.interpreter.resolve()
    source = arguments.source.resolve()
    expected_stdout_path = arguments.expected_stdout.resolve()
    stdlib = arguments.stdlib.resolve()
    expected_stdout = expected_stdout_path.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="ny_generic_enum_metadata_") as temp:
        work_dir = Path(temp)
        text_nil = work_dir / "generic_enum_metadata.nil"
        binary_nil = work_dir / "generic_enum_metadata.binary.nil"
        common = (
            str(compiler),
            "--quiet",
            "--no-bundle",
            "--sdk-path",
            str(stdlib),
        )
        run_checked((*common, "--output", str(text_nil), str(source)), work_dir)
        run_checked(
            (
                *common,
                "--emit-binary-nil",
                "--output",
                str(binary_nil),
                str(source),
            ),
            work_dir,
        )

        text_instructions = parse_text_instructions(text_nil)
        binary_instructions = parse_binary_instructions(binary_nil)
        verify_metadata(text_instructions)
        verify_metadata(binary_instructions)

        if variant_instructions(text_instructions) != variant_instructions(
            binary_instructions
        ):
            raise AssertionError(
                "textual and binary NIL variant operand vectors differ"
            )

        for artifact in (text_nil, binary_nil):
            execution = run_checked(
                (str(interpreter), str(artifact)), work_dir
            )
            if execution.stdout != expected_stdout:
                raise AssertionError(
                    "{} stdout mismatch:\nexpected:\n{}\nactual:\n{}".format(
                        artifact.name, expected_stdout, execution.stdout
                    )
                )
            if execution.stderr:
                raise AssertionError(
                    "{} emitted stderr:\n{}".format(
                        artifact.name, execution.stderr
                    )
                )

        # No-typecheck remains a supported compiler route. Exercise the legacy
        # lowercase constructor/type spelling there and prove it selects the
        # same canonical schema as an uppercase Result match.
        alias_source = work_dir / "generic_enum_case_alias.ny"
        alias_source.write_text(
            """import ny.lang.core.*

fn main() -> i32
    let value: result<bool, text> = ok(true)
    match value
        Result.Ok(flag) -> println(flag)
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
""",
            encoding="utf-8",
        )
        alias_text_nil = work_dir / "generic_enum_case_alias.nil"
        alias_binary_nil = work_dir / "generic_enum_case_alias.binary.nil"
        run_checked(
            (
                *common,
                "--no-typecheck",
                "--output",
                str(alias_text_nil),
                str(alias_source),
            ),
            work_dir,
        )
        run_checked(
            (
                *common,
                "--no-typecheck",
                "--emit-binary-nil",
                "--output",
                str(alias_binary_nil),
                str(alias_source),
            ),
            work_dir,
        )
        alias_text_instructions = parse_text_instructions(alias_text_nil)
        alias_binary_instructions = parse_binary_instructions(alias_binary_nil)
        result_bool = "nil::std::core::Result<bool,text>"
        for instructions in (alias_text_instructions, alias_binary_instructions):
            require_instruction(
                instructions,
                "main",
                "NEW_VARIANT",
                ("result::ok", "1", result_bool, "bool"),
                4,
            )
            require_instruction(
                instructions,
                "main",
                "IS_VARIANT",
                ("Result::Ok", result_bool, "bool"),
                5,
            )
        if variant_instructions(alias_text_instructions) != variant_instructions(
            alias_binary_instructions
        ):
            raise AssertionError(
                "case-alias variant operands differ between text and binary NIL"
            )
        for artifact in (alias_text_nil, alias_binary_nil):
            execution = run_checked((str(interpreter), str(artifact)), work_dir)
            if execution.stdout != "1\n" or execution.stderr:
                raise AssertionError(
                    "case-alias execution mismatch for {}:\nstdout:\n{}\nstderr:\n{}".format(
                        artifact.name, execution.stdout, execution.stderr
                    )
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
