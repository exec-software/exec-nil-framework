#!/usr/bin/env python3
"""Exercise Ny's source-to-execution routes with exact, auditable checks.

The harness deliberately treats compilation, artifact validation, and execution
as separate stages.  A zero exit status alone is not a passing result: the
requested artifact must have the expected format, stdout must match exactly,
and the program's process exit status must match the fixture contract.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


EXECUTION_ROUTES: Tuple[str, ...] = (
    "text-interpreter",
    "binary-interpreter",
    "bundle-text",
    "bundle-binary",
    "llvm-jit",
    "llvm-aot",
)

DIAGNOSTIC_ROUTES: Tuple[str, ...] = (
    "compile-reject",
    "interpreter-reject",
)

ALL_ROUTES = EXECUTION_ROUTES + DIAGNOSTIC_ROUTES
LLVM_UNAVAILABLE_MARKERS = (
    "native backend (llvm) is not compiled",
    "llvm native codegen disabled",
)


@dataclass
class CommandResult:
    argv: List[str]
    cwd: str
    exit_code: Optional[int]
    stdout: str
    stderr: str
    duration_seconds: float
    timed_out: bool = False
    spawn_error: Optional[str] = None

    def as_dict(self, max_output_chars: int) -> Dict[str, Any]:
        return {
            "argv": self.argv,
            "cwd": self.cwd,
            "exit_code": self.exit_code,
            "stdout": truncate(self.stdout, max_output_chars),
            "stderr": truncate(self.stderr, max_output_chars),
            "stdout_truncated": len(self.stdout) > max_output_chars,
            "stderr_truncated": len(self.stderr) > max_output_chars,
            "duration_seconds": round(self.duration_seconds, 6),
            "timed_out": self.timed_out,
            "spawn_error": self.spawn_error,
        }


def truncate(value: str, limit: int) -> str:
    if len(value) <= limit:
        return value
    omitted = len(value) - limit
    return value[:limit] + "\n... <{} characters omitted>\n".format(omitted)


def normalize_newlines(value: str) -> str:
    return value.replace("\r\n", "\n").replace("\r", "\n")


def run_command(
    argv: Sequence[str], cwd: Path, timeout_seconds: float, env: Dict[str, str]
) -> CommandResult:
    started = time.monotonic()
    command = [str(part) for part in argv]
    popen_args: Dict[str, Any] = {
        "cwd": str(cwd),
        "env": env,
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
        "encoding": "utf-8",
        "errors": "replace",
    }
    if os.name == "nt":
        popen_args["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        popen_args["start_new_session"] = True

    try:
        process = subprocess.Popen(command, **popen_args)
    except OSError as exc:
        return CommandResult(
            argv=command,
            cwd=str(cwd),
            exit_code=None,
            stdout="",
            stderr="",
            duration_seconds=time.monotonic() - started,
            spawn_error=str(exc),
        )

    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        if os.name == "nt":
            process.kill()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        stdout, stderr = process.communicate()

    return CommandResult(
        argv=command,
        cwd=str(cwd),
        exit_code=process.returncode,
        stdout=stdout,
        stderr=stderr,
        duration_seconds=time.monotonic() - started,
        timed_out=timed_out,
    )


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def artifact_result(
    path: Path,
    kind: str,
    valid: bool,
    errors: Iterable[str],
    metadata: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    result: Dict[str, Any] = {
        "path": str(path),
        "kind": kind,
        "exists": path.is_file(),
        "valid": valid,
        "errors": list(errors),
    }
    if path.is_file():
        data = path.read_bytes()
        result["size_bytes"] = len(data)
        result["sha256"] = sha256_bytes(data)
    if metadata:
        result["metadata"] = metadata
    return result


def _textual_delimiters_balanced(value: str) -> bool:
    pairs = {")": "(", "]": "[", "}": "{", ">": "<"}
    stack: List[str] = []
    for index, character in enumerate(value):
        if character in "([{<":
            stack.append(character)
        elif character == ">" and index > 0 and value[index - 1] == "-":
            continue
        elif character in pairs:
            if not stack or stack.pop() != pairs[character]:
                return False
    return not stack


def _textual_parameter_fields_valid(value: str) -> bool:
    if not value:
        return True
    if not _textual_delimiters_balanced(value):
        return False

    parens = brackets = braces = angles = 0
    field_start = 0
    for index, character in enumerate(value):
        if character == "(":
            parens += 1
        elif character == ")":
            parens -= 1
        elif character == "[":
            brackets += 1
        elif character == "]":
            brackets -= 1
        elif character == "{":
            braces += 1
        elif character == "}":
            braces -= 1
        elif character == "<":
            angles += 1
        elif character == ">" and (index == 0 or value[index - 1] != "-"):
            angles -= 1
        elif character == "," and not (parens or brackets or braces or angles):
            if not value[field_start:index].strip():
                return False
            field_start = index + 1
    return bool(value[field_start:].strip())


def _parse_textual_function_header(line: str) -> Tuple[Optional[str], Optional[str]]:
    """Recognize the current/legacy header shapes without parsing Ny types."""
    header = line[len("function ") :].strip()
    if not header.endswith(":"):
        return None, "function header does not end with ':'"
    header = header[:-1].strip()
    if not header:
        return None, "function header has an empty name"

    tail = ""
    arrow = header.find("->")
    open_paren = header.find("(")
    has_parameter_list = open_paren != -1 and (arrow == -1 or open_paren < arrow)
    if not has_parameter_list:
        name = header[:arrow].strip() if arrow != -1 else header
        tail = header[arrow:].strip() if arrow != -1 else ""
        if ")" in name:
            return None, "function header has an unmatched ')'"
    else:
        name = header[:open_paren].strip()
        depth = 0
        close_paren: Optional[int] = None
        for index in range(open_paren, len(header)):
            character = header[index]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth < 0:
                    return None, "function parameter list has an unmatched ')'"
                if depth == 0:
                    close_paren = index
                    break
        if close_paren is None:
            return None, "function parameter list is unterminated"
        parameters = header[open_paren + 1 : close_paren]
        if not _textual_parameter_fields_valid(parameters):
            return None, "function parameter list is malformed or unbalanced"
        tail = header[close_paren + 1 :].strip()

    if not name or any(character.isspace() for character in name):
        return None, "function header has an invalid name"
    if any(character in "()[]{}" for character in name):
        return None, "function header has an invalid name"
    if tail:
        if not tail.startswith("->"):
            return None, "function header has an unexpected suffix"
        return_type = tail[2:].strip()
        if not return_type:
            return None, "function header has an empty return type"
        if not _textual_delimiters_balanced(return_type):
            return None, "function return type has unbalanced delimiters"
    return name, None


def _textual_record_operands(line: str) -> List[str]:
    """Match NIL's quoted operands, escapes, optional commas and comments."""
    result: List[str] = []
    position = 0
    escapes = {"n": "\n", "r": "\r", "t": "\t", "0": "\0", '"': '"', "\\": "\\"}
    while position < len(line):
        while position < len(line) and line[position] in " \t":
            position += 1
        if position == len(line) or line[position] == ";":
            break
        if line[position] == '"':
            position += 1
            characters = []
            while position < len(line) and line[position] != '"':
                character = line[position]
                position += 1
                if character == "\\" and position < len(line):
                    escaped = line[position]
                    position += 1
                    characters.append(escapes.get(escaped, "\\" + escaped))
                else:
                    characters.append(character)
            if position == len(line):
                raise ValueError("unterminated quoted operand")
            position += 1
            if position < len(line) and line[position] not in " \t,;":
                raise ValueError("unexpected character after quoted operand")
            if position < len(line) and line[position] == ",":
                position += 1
            result.append("".join(characters))
        else:
            begin = position
            while position < len(line) and line[position] not in " \t;":
                position += 1
            operand = line[begin:position]
            result.append(operand[:-1] if operand.endswith(",") else operand)
    return result


def _validate_class_metadata(classes: List[Dict[str, Any]], structures: List[str],
                             functions: List[str]) -> None:
    """Check the serialized record contract; execution performs semantic proof."""
    def name_ok(value: str) -> bool:
        return bool(value) and all(ord(character) > 32 and ord(character) != 127
                                   for character in value)

    by_name: Dict[str, Dict[str, Any]] = {}
    if len(classes) > MAX_FUNCTION_COUNT or sum(len(item["methods"]) for item in classes) > MAX_INSTRUCTION_COUNT:
        raise ValueError("class metadata exceeds safety limits")
    for declaration in classes:
        name = declaration["name"]
        if not name_ok(name) or name in by_name or structures.count(name) != 1:
            raise ValueError("class requires a unique name and exact struct schema")
        by_name[name] = declaration
        bases = declaration["bases"]
        if (len(bases) > MAX_PARAMETER_COUNT or len(declaration["methods"]) > MAX_PARAMETER_COUNT or
                len(set(bases)) != len(bases) or any(not name_ok(base) for base in bases)):
            raise ValueError("invalid or duplicate class base")
        methods = set()
        for method in declaration["methods"]:
            if not name_ok(method["name"]) or method["name"] in methods:
                raise ValueError("invalid or duplicate class method")
            methods.add(method["name"])
            if method["receiver"] not in (name, "ref<" + name + ">"):
                raise ValueError("class method receiver is not its declaring class")
            types = [method["result"], *method["parameters"]]
            if len(method["parameters"]) > MAX_PARAMETER_COUNT or any(not value or
                   any(ord(character) < 32 or ord(character) == 127 for character in value) or
                   not _textual_delimiters_balanced(value)
                   for value in types) or "void" in method["parameters"]:
                raise ValueError("class method has an invalid type")
            flags = method["flags"]
            implementation = method["implementation"]
            if flags & ~3:
                raise ValueError("unsupported class method flags")
            if flags & 2:
                if not flags & 1 or implementation:
                    raise ValueError("abstract class method must be virtual without implementation")
            elif not name_ok(implementation) or functions.count(implementation) != 1:
                raise ValueError("class method requires one implementation function")
    for declaration in classes:
        if any(base not in by_name for base in declaration["bases"]):
            raise ValueError("class names an unknown base")
    # Bound malformed hierarchy traversal without depending on Python's call stack.
    complete = set()
    for name in by_name:
        stack = [(name, False)]
        active = set()
        while stack:
            current, leaving = stack.pop()
            if leaving:
                active.remove(current)
                complete.add(current)
            elif current not in complete:
                if current in active or len(active) >= 256:
                    raise ValueError("cyclic or excessively deep class ancestry")
                active.add(current)
                stack.append((current, True))
                stack.extend((base, False) for base in by_name[current]["bases"])


def _validate_source_debug_metadata(path: str, locals_: List[Dict[str, Any]]) -> None:
    def valid(value: str, allow_empty: bool = False) -> bool:
        try:
            encoded = value.encode("utf-8")
        except UnicodeEncodeError:
            return False
        return ((allow_empty or bool(value)) and
                not any(ord(character) < 32 or ord(character) == 127 for character in value) and
                len(encoded) <= 16384)

    if not valid(path, True) or len(locals_) > 1024 or (not path and locals_):
        raise ValueError("invalid source debug metadata")
    names, storage = set(), set()
    for local in locals_:
        if (any(not valid(local[field]) for field in ("name", "storage", "type")) or
                not 0 <= local["scope_depth"] <= 1024 or
                local["name"] in names or local["storage"] in storage):
            raise ValueError("invalid lexical debug binding")
        names.add(local["name"])
        storage.add(local["storage"])


def validate_text_nil_bytes(data: bytes) -> Tuple[bool, List[str], Dict[str, Any]]:
    errors: List[str] = []
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        return False, ["not UTF-8 textual NIL: {}".format(exc)], {}

    lines = text.splitlines()
    if not text.startswith("# NIL Textual Module"):
        errors.append("missing '# NIL Textual Module' header")

    functions: List[str] = []
    module_name: Optional[str] = None
    instruction_lines = 0
    block_kind: Optional[str] = None
    block_name = ""
    block_line = 0
    structures: List[str] = []
    classes: List[Dict[str, Any]] = []
    source_debug_pending = False
    source_debug_paths = set()
    function_instruction_count = 0

    for line_number, raw_line in enumerate(lines, start=1):
        if len(raw_line) > 16 * 1024 * 1024:
            errors.append("textual NIL line {} exceeds 16 MiB".format(line_number))
            continue
        line = raw_line.strip(" \t\r")
        if not line:
            continue
        if line.startswith("#"):
            contents = line[1:].strip()
            if contents.split(maxsplit=1)[:1] == ["source_debug"]:
                try:
                    parts = _textual_record_operands(contents)
                    if (block_kind != "function" or source_debug_pending or len(parts) < 4 or
                            not parts[1].isascii() or not parts[1].isdigit() or int(parts[1]) != function_instruction_count or
                            not parts[3].isascii() or not parts[3].isdigit() or not 0 <= int(parts[3]) <= 1024 or
                            len(parts) != 4 + int(parts[3]) * 4 or not parts[2]):
                        raise ValueError("invalid or orphan source debug record")
                    locals_ = []
                    for offset in range(4, len(parts), 4):
                        if not parts[offset + 3].isascii() or not parts[offset + 3].isdigit():
                            raise ValueError("invalid lexical scope depth")
                        locals_.append({"name": parts[offset], "storage": parts[offset + 1],
                                        "type": parts[offset + 2], "scope_depth": int(parts[offset + 3])})
                    _validate_source_debug_metadata(parts[2], locals_)
                    source_debug_paths.add(parts[2])
                    source_debug_pending = True
                except ValueError as exc:
                    errors.append("{} at line {}".format(exc, line_number))
            continue

        if source_debug_pending and (line.startswith(("end ", "function ", "module ", "const ", "struct ", "enum ", "@"))):
            errors.append("orphan source debug record at line {}".format(line_number))
            source_debug_pending = False

        if block_kind is not None:
            expected_terminator = "end " + block_kind
            if line == expected_terminator:
                block_kind = None
                block_name = ""
                continue
            if line.startswith("end "):
                errors.append(
                    "unexpected '{}' inside {} '{}' at line {}".format(
                        line, block_kind, block_name, line_number
                    )
                )
                continue
            if line.startswith(
                ("function ", "struct ", "enum ", "class ", "module ", "const ")
            ):
                errors.append(
                    "nested textual NIL record inside {} '{}' at line {}".format(
                        block_kind, block_name, line_number
                    )
                )
                continue
            if block_kind == "function":
                instruction_lines += 1
                function_instruction_count += 1
                source_debug_pending = False
            elif block_kind == "class":
                try:
                    parts = _textual_record_operands(line)
                except ValueError as exc:
                    errors.append("{} at line {}".format(exc, line_number))
                    continue
                if len(parts) == 2 and parts[0] == "base":
                    classes[-1]["bases"].append(parts[1])
                elif len(parts) >= 8 and parts[0] == "method":
                    try:
                        count = int(parts[7])
                        if (parts[5] not in ("0", "1") or parts[6] not in ("0", "1") or
                                not 0 <= count <= MAX_PARAMETER_COUNT or len(parts) != count + 8):
                            raise ValueError("invalid class method flags or parameter count")
                        classes[-1]["methods"].append({
                            "name": parts[1], "implementation": "" if parts[2] == "-" else parts[2],
                            "receiver": parts[3], "result": parts[4],
                            "flags": int(parts[5]) | (int(parts[6]) << 1), "parameters": parts[8:]})
                    except ValueError as exc:
                        errors.append("{} at line {}".format(exc, line_number))
                else:
                    errors.append("invalid class record at line {}".format(line_number))
            continue

        if line.startswith("module "):
            declared_name = line[len("module ") :].strip()
            if not declared_name:
                errors.append("empty module name at line {}".format(line_number))
            elif module_name is not None:
                errors.append("duplicate module declaration at line {}".format(line_number))
            else:
                module_name = declared_name
            continue
        if line.startswith("const "):
            continue
        if line.startswith("struct "):
            block_name = line[len("struct ") :].strip()
            if not block_name:
                errors.append("empty struct name at line {}".format(line_number))
                continue
            block_kind = "struct"
            block_line = line_number
            structures.append(block_name)
            continue
        if line.startswith("class "):
            try:
                parts = _textual_record_operands(line)
            except ValueError as exc:
                errors.append("{} at line {}".format(exc, line_number))
                continue
            if len(parts) != 3 or parts[2] not in ("0", "1"):
                errors.append("invalid class declaration at line {}".format(line_number))
                continue
            block_name, block_kind, block_line = parts[1], "class", line_number
            classes.append({"name": block_name, "abstract": parts[2] == "1", "bases": [], "methods": []})
            continue
        if line.startswith("enum "):
            block_name = line[len("enum ") :].strip()
            if not block_name:
                errors.append("empty enum name at line {}".format(line_number))
                continue
            block_kind = "enum"
            block_line = line_number
            continue
        if line.startswith("function "):
            name, header_error = _parse_textual_function_header(line)
            if header_error is not None:
                errors.append("{} at line {}".format(header_error, line_number))
                continue
            assert name is not None
            functions.append(name)
            block_kind = "function"
            block_name = name
            block_line = line_number
            function_instruction_count = 0
            continue
        if line.startswith("@test "):
            if not line[len("@test ") :].strip():
                errors.append("empty test registry entry at line {}".format(line_number))
            continue
        if line.startswith("@"):
            # Function annotations are intentionally left to the NIL reader;
            # this validator only needs to distinguish them from declarations.
            continue
        if line in ("end function", "end struct", "end enum", "end class"):
            errors.append("unexpected '{}' at line {}".format(line, line_number))
            continue
        errors.append("unexpected textual NIL record at line {}".format(line_number))

    if block_kind is not None:
        errors.append(
            "unterminated {} '{}' opened at line {}".format(
                block_kind, block_name, block_line
            )
        )
    if source_debug_pending:
        errors.append("orphan source debug record at end of artifact")
    if "main" not in functions:
        errors.append("missing generated main function")
    try:
        _validate_class_metadata(classes, structures, functions)
    except ValueError as exc:
        errors.append("invalid class metadata: " + str(exc))
    metadata = {
        "module": module_name,
        "functions": functions,
        "instruction_lines": instruction_lines,
        "classes": [declaration["name"] for declaration in classes],
        "debug_source_paths": sorted(source_debug_paths),
    }
    return not errors, errors, metadata


def validate_text_nil(path: Path) -> Dict[str, Any]:
    if not path.is_file():
        return artifact_result(path, "text-nil", False, ["artifact is missing"])
    data = path.read_bytes()
    valid, errors, metadata = validate_text_nil_bytes(data)
    return artifact_result(path, "text-nil", valid, errors, metadata)


class BinaryReadError(ValueError):
    pass


MAX_BINARY_NIL_SIZE = 256 * 1024 * 1024
MAX_BINARY_STRING_SIZE = 16 * 1024 * 1024
MAX_FUNCTION_COUNT = 100000
MAX_CONSTANT_COUNT = 1000000
MAX_PARAMETER_COUNT = 65536
MAX_INSTRUCTION_COUNT = 1000000
MAX_OPERAND_COUNT = 65536
MAX_EMBEDDED_SOURCE_COUNT = 10000


class BinaryCursor:
    def __init__(self, data: bytes, start: int, end: int, endian: str):
        if start > end or end > len(data):
            raise BinaryReadError("invalid binary cursor bounds")
        self.data = data
        self.pos = start
        self.end = end
        self.endian = endian

    def take(self, size: int) -> bytes:
        if size < 0 or self.pos + size > self.end:
            raise BinaryReadError("truncated field at byte {}".format(self.pos))
        value = self.data[self.pos : self.pos + size]
        self.pos += size
        return value

    def u32(self) -> int:
        return struct.unpack(self.endian + "I", self.take(4))[0]

    def string(self, field: str) -> str:
        size = self.u32()
        if size > MAX_BINARY_STRING_SIZE:
            raise BinaryReadError(
                "{} length {} exceeds 16 MiB at byte {}".format(
                    field, size, self.pos - 4
                )
            )
        if size > self.end - self.pos:
            raise BinaryReadError(
                "{} length {} exceeds section boundary at byte {}".format(
                    field, size, self.pos
                )
            )
        # The C++ format stores byte strings rather than requiring UTF-8.
        # Surrogate escapes preserve arbitrary bytes for comparisons/metadata.
        return self.take(size).decode("utf-8", errors="surrogateescape")

    def require_consumed(self, section: str) -> None:
        if self.pos != self.end:
            raise BinaryReadError(
                "{} section contains {} trailing byte(s)".format(
                    section, self.end - self.pos
                )
            )


def _bounded_binary_count(cursor: BinaryCursor, maximum: int, field: str) -> int:
    count = cursor.u32()
    if count > maximum:
        raise BinaryReadError(
            "{} {} exceeds safety limit {}".format(field, count, maximum)
        )
    return count


def _unsafe_library_source_path(value: str) -> bool:
    if not value or "\0" in value or "\\" in value:
        return True
    if (
        value.startswith("/")
        or "//" in value
        or value.endswith("/")
        or (len(value) >= 2 and value[1] == ":")
    ):
        return True
    return any(component in ("", ".", "..") for component in value.split("/"))


def validate_binary_nil_bytes(data: bytes) -> Tuple[bool, List[str], Dict[str, Any]]:
    errors: List[str] = []
    metadata: Dict[str, Any] = {}
    if len(data) > MAX_BINARY_NIL_SIZE:
        return False, ["binary NIL exceeds the 256 MiB safety limit"], metadata
    if len(data) < 16:
        return False, ["binary NIL header is truncated"], metadata
    if data[:4] != b"NIL\0":
        return False, ["magic is not NIL\\0"], metadata

    # The current NIL binary format is explicitly little-endian.  Historical
    # MIL magic values used the same payload encoding; the runtime keeps those
    # aliases readable, while newly generated artifacts always use NIL\0.
    endian = "<"
    try:
        version, flags, section_count = struct.unpack_from(endian + "III", data, 4)
    except struct.error as exc:
        return False, ["cannot decode binary NIL header: {}".format(exc)], metadata

    metadata.update(
        {
            "version": version,
            "flags": flags,
            "section_count": section_count,
            "byte_order": "little",
        }
    )
    artifact_contracts = {
        (1, 0): ("executable", set(range(1, 6))),
        (2, 1): ("library", set(range(1, 7))),
        (3, 0): ("executable", set(range(1, 6))),
        (4, 0): ("executable", {1, 2, 3, 4, 5, 7}),
        (4, 1): ("library", set(range(1, 8))),
        (5, 0): ("executable", {1, 2, 3, 4, 5, 7, 8}),
    }
    contract = artifact_contracts.get((version, flags))
    if contract is None:
        return (
            False,
            [
                "unsupported binary NIL version/flags pair {}/{}".format(
                    version, flags
                )
            ],
            metadata,
        )
    artifact_kind, expected_types = contract
    metadata["artifact_kind"] = artifact_kind
    if section_count != len(expected_types):
        return (
            False,
            [
                "binary NIL {} must contain exactly {} sections, found {}".format(
                    artifact_kind, len(expected_types), section_count
                )
            ],
            metadata,
        )

    table_end = 16 + section_count * 20
    if table_end > len(data):
        return False, ["section table is truncated"], metadata

    sections: List[Tuple[int, int, int]] = []
    section_types = set()
    for index in range(section_count):
        offset = 16 + index * 20
        section_type, section_offset, section_size = struct.unpack_from(
            endian + "IQQ", data, offset
        )
        sections.append((section_type, section_offset, section_size))
        if section_type not in expected_types:
            errors.append("unknown section type {}".format(section_type))
        elif section_type in section_types:
            errors.append("duplicate section type {}".format(section_type))
        section_types.add(section_type)
        if section_size == 0:
            errors.append("section {} has an empty payload".format(section_type))
        if section_offset < table_end:
            errors.append(
                "section {} overlaps the header or section table".format(section_type)
            )
        if section_offset > len(data) or section_size > len(data) - section_offset:
            errors.append(
                "section {} lies outside the artifact".format(section_type)
            )

    missing_types = expected_types - section_types
    if missing_types:
        errors.append(
            "missing section type(s) {}".format(
                ", ".join(str(kind) for kind in sorted(missing_types))
            )
        )

    metadata["sections"] = [
        {"type": kind, "offset": offset, "size": size}
        for kind, offset, size in sections
    ]
    if errors:
        return False, errors, metadata

    ranges = sorted(
        (section_offset, section_offset + section_size, section_type)
        for section_type, section_offset, section_size in sections
    )
    if ranges[0][0] != table_end:
        errors.append("unclaimed bytes exist between the section table and sections")
    for previous, current in zip(ranges, ranges[1:]):
        if previous[1] > current[0]:
            errors.append(
                "sections {} and {} overlap".format(previous[2], current[2])
            )
        elif previous[1] < current[0]:
            errors.append(
                "unclaimed byte gap exists between sections {} and {}".format(
                    previous[2], current[2]
                )
            )
    if ranges[-1][1] != len(data):
        errors.append("unclaimed bytes exist after the final section")
    if errors:
        return False, errors, metadata

    section_by_type = {
        section_type: (section_offset, section_size)
        for section_type, section_offset, section_size in sections
    }
    function_names: List[str] = []
    function_instruction_counts: List[int] = []
    debug_source_paths = set()
    total_instruction_count = 0
    module_name: Optional[str] = None
    test_names: List[str] = []
    source_paths: List[str] = []
    dependencies: List[str] = []
    structures: List[str] = []
    classes: List[Dict[str, Any]] = []

    for section_type in sorted(expected_types):
        section_offset, section_size = section_by_type[section_type]
        cursor = BinaryCursor(
            data, section_offset, section_offset + section_size, endian
        )
        try:
            if section_type == 1:
                if version in (3, 4, 5):
                    total_members = 0
                    for kind in ("struct", "enum"):
                        type_count = _bounded_binary_count(
                            cursor, MAX_FUNCTION_COUNT, kind + " count")
                        for _ in range(type_count):
                            name = cursor.string(kind + " name")
                            if not name or "\0" in name:
                                raise BinaryReadError("invalid type name")
                            if kind == "struct":
                                structures.append(name)
                            member_count = _bounded_binary_count(
                                cursor, MAX_PARAMETER_COUNT, "member count")
                            total_members += member_count
                            if total_members > MAX_INSTRUCTION_COUNT:
                                raise BinaryReadError("total type member count exceeds safety limit")
                            for _ in range(member_count):
                                name = cursor.string("member name")
                                value_type = cursor.string("member type")
                                if (not name or "\0" in name or "\0" in value_type
                                        or (kind == "struct" and not value_type)):
                                    raise BinaryReadError("invalid type member")
                elif cursor.u32() != 0:
                    raise BinaryReadError(
                        "type count is nonzero in a format with no type records"
                    )
                cursor.require_consumed("type")
            elif section_type == 2:
                function_count = _bounded_binary_count(
                    cursor, MAX_FUNCTION_COUNT, "function count"
                )
                for _ in range(function_count):
                    function_name = cursor.string("function name")
                    if not function_name:
                        raise BinaryReadError("function name is empty")
                    function_names.append(function_name)
                    parameter_count = _bounded_binary_count(
                        cursor, MAX_PARAMETER_COUNT, "parameter count"
                    )
                    for _ in range(parameter_count):
                        cursor.string("parameter")
                    generic_count = _bounded_binary_count(
                        cursor, MAX_PARAMETER_COUNT, "generic parameter count"
                    )
                    for _ in range(generic_count):
                        cursor.string("generic parameter")
                    cursor.string("return type")
                    if version in (3, 4, 5):
                        if cursor.u32() & ~3:
                            raise BinaryReadError("unsupported function flags")
                        if cursor.u32() > 0x7FFFFFFF:
                            raise BinaryReadError("function source line exceeds INT_MAX")
                        annotations = _bounded_binary_count(
                            cursor, MAX_PARAMETER_COUNT, "annotation count")
                        for _ in range(annotations):
                            annotation = cursor.string("function annotation")
                            if not annotation.startswith("@") or "\0" in annotation:
                                raise BinaryReadError("invalid function annotation")
                    instruction_count = _bounded_binary_count(
                        cursor, MAX_INSTRUCTION_COUNT, "instruction count"
                    )
                    total_instruction_count += instruction_count
                    function_instruction_counts.append(instruction_count)
                    if total_instruction_count > MAX_INSTRUCTION_COUNT:
                        raise BinaryReadError(
                            "total instruction count exceeds safety limit {}".format(
                                MAX_INSTRUCTION_COUNT
                            )
                        )
                    for _ in range(instruction_count):
                        cursor.string("opcode")
                        argument_count = _bounded_binary_count(
                            cursor, MAX_OPERAND_COUNT, "operand count"
                        )
                        for _ in range(argument_count):
                            cursor.string("operand")
                        debug_line = cursor.u32()
                        if debug_line > 0x7FFFFFFF:
                            raise BinaryReadError("debug line exceeds INT_MAX")
                cursor.require_consumed("function")
            elif section_type == 3:
                constant_count = _bounded_binary_count(
                    cursor, MAX_CONSTANT_COUNT, "constant count"
                )
                for _ in range(constant_count):
                    if not cursor.string("constant name"):
                        raise BinaryReadError("constant name is empty")
                    cursor.string("constant value")
                cursor.require_consumed("constant")
                metadata["constant_count"] = constant_count
            elif section_type == 4:
                if cursor.u32() != 0:
                    raise BinaryReadError(
                        "symbol count is nonzero in a format with no symbol records"
                    )
                cursor.require_consumed("symbol")
            elif section_type == 5:
                module_name = cursor.string("module name")
                test_count = _bounded_binary_count(
                    cursor, MAX_FUNCTION_COUNT, "test count"
                )
                for _ in range(test_count):
                    test_names.append(cursor.string("test name"))
                cursor.require_consumed("module")
                if artifact_kind == "library" and not module_name:
                    raise BinaryReadError("library module name is empty")
            elif section_type == 6:
                source_count = _bounded_binary_count(
                    cursor, MAX_EMBEDDED_SOURCE_COUNT, "source count"
                )
                seen_source_paths = set()
                for _ in range(source_count):
                    source_path = cursor.string("source path")
                    source_contents = cursor.string("source contents")
                    if (
                        _unsafe_library_source_path(source_path)
                        or "\0" in source_contents
                        or source_path in seen_source_paths
                    ):
                        raise BinaryReadError("library contains an unsafe source entry")
                    seen_source_paths.add(source_path)
                    source_paths.append(source_path)
                dependency_count = _bounded_binary_count(
                    cursor, MAX_FUNCTION_COUNT, "dependency count"
                )
                seen_dependencies = set()
                for _ in range(dependency_count):
                    dependency = cursor.string("dependency name")
                    if not dependency or dependency in seen_dependencies:
                        raise BinaryReadError(
                            "library contains an invalid dependency entry"
                        )
                    seen_dependencies.add(dependency)
                    dependencies.append(dependency)
                cursor.require_consumed("source interface")
                if not source_paths:
                    raise BinaryReadError("library source metadata is empty")
            elif section_type == 7:
                count = _bounded_binary_count(cursor, MAX_FUNCTION_COUNT, "class count")
                if count == 0 and version == 4:
                    raise BinaryReadError("version 4 requires class metadata")
                total_methods = 0
                for _ in range(count):
                    name = cursor.string("class name")
                    flags = cursor.u32()
                    if flags > 1:
                        raise BinaryReadError("unsupported class flags")
                    base_count = _bounded_binary_count(cursor, MAX_PARAMETER_COUNT, "class base count")
                    bases = [cursor.string("class base") for _ in range(base_count)]
                    method_count = _bounded_binary_count(cursor, MAX_PARAMETER_COUNT, "class method count")
                    total_methods += method_count
                    if total_methods > MAX_INSTRUCTION_COUNT:
                        raise BinaryReadError("total class method count exceeds safety limit")
                    methods = []
                    for _ in range(method_count):
                        method = {"name": cursor.string("class method name"),
                                  "implementation": cursor.string("class method implementation"),
                                  "receiver": cursor.string("class method receiver"),
                                  "result": cursor.string("class method return type"),
                                  "flags": cursor.u32()}
                        parameter_count = _bounded_binary_count(
                            cursor, MAX_PARAMETER_COUNT, "class method parameter count")
                        method["parameters"] = [cursor.string("class method parameter")
                                                for _ in range(parameter_count)]
                        methods.append(method)
                    classes.append({"name": name, "abstract": flags == 1, "bases": bases, "methods": methods})
                cursor.require_consumed("class")
            elif section_type == 8:
                count = _bounded_binary_count(cursor, MAX_FUNCTION_COUNT, "debug function count")
                if count != len(function_instruction_counts):
                    raise BinaryReadError("source debug function count mismatch")
                for expected_count in function_instruction_counts:
                    if cursor.u32() != expected_count:
                        raise BinaryReadError("source debug instruction count mismatch")
                    for _ in range(expected_count):
                        path = cursor.string("debug source file")
                        local_count = _bounded_binary_count(cursor, 1024, "debug local count")
                        locals_ = []
                        for _ in range(local_count):
                            locals_.append({"name": cursor.string("debug local name"),
                                            "storage": cursor.string("debug local storage"),
                                            "type": cursor.string("debug local type"),
                                            "scope_depth": cursor.u32()})
                        _validate_source_debug_metadata(path, locals_)
                        if path:
                            debug_source_paths.add(path)
                cursor.require_consumed("source debug")
        except ValueError as exc:
            errors.append("invalid section {}: {}".format(section_type, exc))

    metadata["functions"] = function_names
    metadata["module"] = module_name
    metadata["tests"] = test_names
    metadata["source_paths"] = source_paths
    metadata["dependencies"] = dependencies
    metadata["instruction_count"] = total_instruction_count
    metadata["classes"] = [declaration["name"] for declaration in classes]
    metadata["debug_source_paths"] = sorted(debug_source_paths)
    try:
        _validate_class_metadata(classes, structures, function_names)
    except ValueError as exc:
        errors.append("invalid class metadata: " + str(exc))
    if artifact_kind == "executable" and "main" not in function_names:
        errors.append("missing generated main function")
    return not errors, errors, metadata


def validate_binary_nil(path: Path) -> Dict[str, Any]:
    if not path.is_file():
        return artifact_result(path, "binary-nil", False, ["artifact is missing"])
    data = path.read_bytes()
    valid, errors, metadata = validate_binary_nil_bytes(data)
    return artifact_result(path, "binary-nil", valid, errors, metadata)


def executable_format(data: bytes) -> Optional[str]:
    if data.startswith(b"\x7fELF"):
        return "elf"
    if data.startswith(b"MZ"):
        return "pe"
    if data[:4] in (
        b"\xfe\xed\xfa\xce",
        b"\xce\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xcf\xfa\xed\xfe",
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
    ):
        return "mach-o"
    return None


def validate_bundle(path: Path, expected_payload: str) -> Dict[str, Any]:
    errors: List[str] = []
    metadata: Dict[str, Any] = {"expected_payload": expected_payload}
    if not path.is_file():
        return artifact_result(path, "bundle", False, ["artifact is missing"])

    data = path.read_bytes()
    image_format = executable_format(data)
    metadata["executable_format"] = image_format
    if image_format is None:
        errors.append("bundle prefix is not a recognized native executable")
    if os.name != "nt" and not os.access(path, os.X_OK):
        errors.append("bundle is not executable")
    if len(data) < 16:
        errors.append("bundle is too short for a trailer")
        return artifact_result(path, "bundle", False, errors, metadata)

    trailer = data[-8:]
    metadata["trailer"] = trailer.decode("ascii", errors="replace")
    if trailer != b"NYBUNDLE":
        errors.append("missing NYBUNDLE trailer")
        return artifact_result(path, "bundle", False, errors, metadata)

    payload_size = int.from_bytes(data[-16:-8], byteorder="little")
    payload_start = len(data) - 16 - payload_size
    metadata["payload_size"] = payload_size
    metadata["payload_offset"] = payload_start
    if payload_size <= 0 or payload_start < 4:
        errors.append("invalid embedded NIL size")
        return artifact_result(path, "bundle", False, errors, metadata)

    payload = data[payload_start : payload_start + payload_size]
    if expected_payload == "binary":
        payload_valid, payload_errors, payload_metadata = validate_binary_nil_bytes(
            payload
        )
    else:
        payload_valid, payload_errors, payload_metadata = validate_text_nil_bytes(
            payload
        )
    metadata["payload"] = payload_metadata
    metadata["payload_sha256"] = sha256_bytes(payload)
    if not payload_valid:
        errors.extend("embedded NIL: " + error for error in payload_errors)
    return artifact_result(path, "bundle", not errors, errors, metadata)


def validate_native_executable(path: Path) -> Dict[str, Any]:
    errors: List[str] = []
    metadata: Dict[str, Any] = {}
    if not path.is_file():
        return artifact_result(
            path, "native-executable", False, ["artifact is missing"]
        )
    data = path.read_bytes()
    image_format = executable_format(data)
    metadata["executable_format"] = image_format
    if image_format is None:
        errors.append("artifact is not ELF, PE, or Mach-O")
    if os.name != "nt" and not os.access(path, os.X_OK):
        errors.append("artifact is not executable")
    return artifact_result(path, "native-executable", not errors, errors, metadata)


def add_check(
    checks: List[Dict[str, Any]], name: str, passed: bool, detail: str
) -> None:
    checks.append({"name": name, "passed": bool(passed), "detail": detail})


def check_process_contract(
    checks: List[Dict[str, Any]],
    process: CommandResult,
    expected_stdout: str,
    expected_stderr: Optional[str],
    expected_exit: int,
    phase: str,
) -> None:
    add_check(checks, phase + ".spawn", process.spawn_error is None, process.spawn_error or "ok")
    add_check(
        checks,
        phase + ".timeout",
        not process.timed_out,
        "timed out" if process.timed_out else "completed",
    )
    add_check(
        checks,
        phase + ".exit",
        process.exit_code == expected_exit,
        "expected {}, got {}".format(expected_exit, process.exit_code),
    )
    actual_stdout = normalize_newlines(process.stdout)
    add_check(
        checks,
        phase + ".stdout",
        actual_stdout == expected_stdout,
        "exact match" if actual_stdout == expected_stdout else "stdout differs",
    )
    actual_stderr = normalize_newlines(process.stderr)
    required_stderr = expected_stderr if expected_stderr is not None else ""
    add_check(
        checks,
        phase + ".stderr",
        actual_stderr == required_stderr,
        "exact match" if actual_stderr == required_stderr else "stderr differs",
    )


def check_successful_compile(
    checks: List[Dict[str, Any]], process: CommandResult
) -> None:
    add_check(
        checks,
        "compile.spawn",
        process.spawn_error is None,
        process.spawn_error or "ok",
    )
    add_check(
        checks,
        "compile.timeout",
        not process.timed_out,
        "timed out" if process.timed_out else "completed",
    )
    add_check(
        checks,
        "compile.exit",
        process.exit_code == 0,
        "expected 0, got {}".format(process.exit_code),
    )
    compile_stdout = normalize_newlines(process.stdout)
    compile_stderr = normalize_newlines(process.stderr)
    add_check(
        checks,
        "compile.stdout",
        compile_stdout == "",
        "empty" if compile_stdout == "" else "unexpected compiler stdout",
    )
    add_check(
        checks,
        "compile.stderr",
        compile_stderr == "",
        "empty" if compile_stderr == "" else "unexpected compiler stderr",
    )


def compiler_command(
    compiler: Path,
    source: Path,
    output: Path,
    extra: Sequence[str],
    with_stdlib: bool,
    *,
    no_bundle: bool = False,
    binary: bool = False,
    native: bool = False,
    jit: bool = False,
) -> List[str]:
    command = [str(compiler)]
    if not with_stdlib:
        command.append("--no-stdlib")
    if no_bundle:
        command.append("--no-bundle")
    if binary:
        command.append("--emit-binary-nil")
    if native:
        command.append("--native")
    if jit:
        command.append("--jit")
    command.extend(extra)
    command.extend(("--quiet", "--output", str(output), str(source)))
    return command


def llvm_is_unavailable(process: CommandResult) -> bool:
    combined = (process.stdout + "\n" + process.stderr).lower()
    return any(marker in combined for marker in LLVM_UNAVAILABLE_MARKERS)


def new_route_result(route: str, directory: Path) -> Dict[str, Any]:
    return {
        "route": route,
        "work_directory": str(directory),
        "commands": {},
        "artifacts": [],
        "checks": [],
        "raw_status": "FAIL",
        "status": "FAIL",
    }


def finalize_raw_status(result: Dict[str, Any]) -> None:
    result["raw_status"] = (
        "PASS" if all(check["passed"] for check in result["checks"]) else "FAIL"
    )
    result["status"] = result["raw_status"]


def check_text_nil_fragments(
    result: Dict[str, Any], required_fragments: Sequence[str]
) -> None:
    """Check textual NIL contracts before successful artifacts are discarded."""
    if not required_fragments or result["raw_status"] == "SKIP":
        return

    artifact = next(
        (
            item
            for item in result["artifacts"]
            if item["kind"] == "text-nil" and item["exists"]
        ),
        None,
    )
    text = ""
    read_error: Optional[str] = None
    if artifact is None:
        read_error = "textual NIL artifact is missing"
    else:
        try:
            text = Path(artifact["path"]).read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            read_error = "could not read textual NIL artifact: {}".format(exc)

    for fragment in required_fragments:
        passed = read_error is None and fragment in text
        result["checks"].append(
            {
                "name": "text-nil.contains",
                "expected": fragment,
                "passed": passed,
                "detail": (
                    "found required fragment"
                    if passed
                    else read_error or "missing required fragment {!r}".format(fragment)
                ),
            }
        )
        if not passed:
            # Artifact contracts are independent of route-level XFAILs. A
            # missing required fragment must remain a hard failure even when
            # execution itself has an expected-failure allowance.
            result["status"] = "FAIL"


def run_route(
    route: str,
    route_dir: Path,
    compiler: Path,
    interpreter: Path,
    source: Path,
    expected_stdout: str,
    expected_stderr: Optional[str],
    expected_exit: int,
    timeout_seconds: float,
    compiler_args: Sequence[str],
    interpreter_args: Sequence[str],
    program_args: Sequence[str],
    with_stdlib: bool,
    env: Dict[str, str],
    max_output_chars: int,
) -> Dict[str, Any]:
    route_dir.mkdir(parents=True, exist_ok=False)
    result = new_route_result(route, route_dir)
    checks: List[Dict[str, Any]] = result["checks"]

    def record_command(name: str, process: CommandResult) -> None:
        result["commands"][name] = process.as_dict(max_output_chars)

    if route in ("text-interpreter", "binary-interpreter"):
        binary = route == "binary-interpreter"
        output = route_dir / ("program.binary.nil" if binary else "program.nil")
        compile_process = run_command(
            compiler_command(
                compiler,
                source,
                output,
                compiler_args,
                with_stdlib,
                no_bundle=True,
                binary=binary,
            ),
            route_dir,
            timeout_seconds,
            env,
        )
        record_command("compile", compile_process)
        check_successful_compile(checks, compile_process)
        artifact = validate_binary_nil(output) if binary else validate_text_nil(output)
        result["artifacts"].append(artifact)
        add_check(
            checks,
            "artifact.valid",
            artifact["valid"],
            "; ".join(artifact["errors"]) or "valid",
        )
        if compile_process.exit_code == 0 and artifact["valid"]:
            run_process = run_command(
                [
                    str(interpreter),
                    *interpreter_args,
                    str(output),
                    "--",
                    *program_args,
                ],
                route_dir,
                timeout_seconds,
                env,
            )
            record_command("run", run_process)
            check_process_contract(
                checks,
                run_process,
                expected_stdout,
                expected_stderr,
                expected_exit,
                "run",
            )

    elif route in ("bundle-text", "bundle-binary"):
        binary = route == "bundle-binary"
        output = route_dir / "program"
        compile_process = run_command(
            compiler_command(
                compiler,
                source,
                output,
                compiler_args,
                with_stdlib,
                binary=binary,
            ),
            route_dir,
            timeout_seconds,
            env,
        )
        record_command("compile", compile_process)
        check_successful_compile(checks, compile_process)
        if os.name == "nt":
            output = output.with_suffix(".exe")
        artifact = validate_bundle(output, "binary" if binary else "text")
        result["artifacts"].append(artifact)
        add_check(
            checks,
            "artifact.valid",
            artifact["valid"],
            "; ".join(artifact["errors"]) or "valid",
        )
        if compile_process.exit_code == 0 and artifact["valid"]:
            run_process = run_command(
                [str(output)], route_dir, timeout_seconds, env
            )
            record_command("run", run_process)
            check_process_contract(
                checks,
                run_process,
                expected_stdout,
                expected_stderr,
                expected_exit,
                "run",
            )

    elif route == "llvm-jit":
        output = route_dir / "program.jit.nil"
        jit_command = compiler_command(
            compiler,
            source,
            output,
            compiler_args,
            with_stdlib,
            native=True,
            jit=True,
        )
        jit_command.extend(("--", *program_args))
        jit_process = run_command(
            jit_command,
            route_dir,
            timeout_seconds,
            env,
        )
        record_command("compile_and_run", jit_process)
        if llvm_is_unavailable(jit_process):
            result["raw_status"] = "SKIP"
            result["status"] = "SKIP"
            result["skip_reason"] = "nilcompiler was built without a usable LLVM backend"
            return result
        # JIT is an in-process execution route.  Some compiler versions retain
        # the lowered textual NIL when --output is supplied, while others do
        # not create a persistent artifact at all.  Validate and record the
        # intermediate when present, but do not manufacture an artifact
        # requirement for a route whose deliverable is the executed program.
        if output.exists():
            artifact = validate_text_nil(output)
            result["artifacts"].append(artifact)
            add_check(
                checks,
                "artifact.valid",
                artifact["valid"],
                "; ".join(artifact["errors"]) or "valid",
            )
        else:
            result["artifacts"].append(
                artifact_result(
                    output,
                    "optional-jit-intermediate",
                    True,
                    [],
                    {"retained": False},
                )
            )
        check_process_contract(
            checks,
            jit_process,
            expected_stdout,
            expected_stderr,
            expected_exit,
            "run",
        )

    elif route == "llvm-aot":
        output_base = route_dir / "program"
        compile_process = run_command(
            compiler_command(
                compiler,
                source,
                output_base,
                compiler_args,
                with_stdlib,
                native=True,
            ),
            route_dir,
            timeout_seconds,
            env,
        )
        record_command("compile", compile_process)
        if llvm_is_unavailable(compile_process):
            result["raw_status"] = "SKIP"
            result["status"] = "SKIP"
            result["skip_reason"] = "nilcompiler was built without a usable LLVM backend"
            return result
        check_successful_compile(checks, compile_process)
        expected_executable = (
            output_base.with_suffix(".exe") if os.name == "nt" else output_base
        )
        artifact = validate_native_executable(expected_executable)
        candidates = []
        for candidate in (
            output_base,
            Path(str(output_base) + ".exe"),
            Path(str(output_base) + ".obj"),
            Path(str(output_base) + ".exe.obj"),
        ):
            if candidate.exists():
                candidates.append(str(candidate))
        artifact.setdefault("metadata", {})["observed_candidates"] = candidates
        result["artifacts"].append(artifact)
        add_check(
            checks,
            "artifact.valid",
            artifact["valid"],
            "; ".join(artifact["errors"]) or "valid",
        )
        if compile_process.exit_code == 0 and artifact["valid"]:
            run_process = run_command(
                [str(expected_executable), *program_args],
                route_dir,
                timeout_seconds,
                env,
            )
            record_command("run", run_process)
            check_process_contract(
                checks,
                run_process,
                expected_stdout,
                expected_stderr,
                expected_exit,
                "run",
            )

    elif route == "compile-reject":
        output = route_dir / "unexpected.nil"
        compile_command = compiler_command(
            compiler,
            source,
            output,
            compiler_args,
            with_stdlib,
            no_bundle=True,
        )
        if program_args:
            compile_command.extend(("--", *program_args))
        compile_process = run_command(
            compile_command,
            route_dir,
            timeout_seconds,
            env,
        )
        record_command("compile", compile_process)
        check_process_contract(
            checks,
            compile_process,
            expected_stdout,
            expected_stderr,
            expected_exit,
            "compile",
        )
        add_check(
            checks,
            "artifact.absent",
            not output.exists(),
            "no artifact" if not output.exists() else "unexpected artifact exists",
        )

    elif route == "interpreter-reject":
        run_process = run_command(
            [str(interpreter), str(source)], route_dir, timeout_seconds, env
        )
        record_command("run", run_process)
        check_process_contract(
            checks,
            run_process,
            expected_stdout,
            expected_stderr,
            expected_exit,
            "run",
        )

    else:
        raise AssertionError("unhandled route {}".format(route))

    finalize_raw_status(result)
    return result


def parse_route_reason(values: Sequence[str], option_name: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(
                "{} expects ROUTE=REASON, got {!r}".format(option_name, value)
            )
        route, reason = value.split("=", 1)
        route = route.strip()
        reason = reason.strip()
        if route not in ALL_ROUTES:
            raise ValueError("unknown route {!r} in {}".format(route, option_name))
        if not reason:
            raise ValueError("empty reason for route {!r}".format(route))
        result[route] = reason
    return result


def parse_routes(value: str) -> List[str]:
    requested: List[str] = []
    for route in (part.strip() for part in value.split(",")):
        if not route:
            continue
        expanded = EXECUTION_ROUTES if route == "all" else (route,)
        for item in expanded:
            if item not in ALL_ROUTES:
                raise ValueError("unknown route {!r}".format(item))
            if item not in requested:
                requested.append(item)
    if not requested:
        raise ValueError("at least one route is required")
    return requested


def resolve_existing_file(value: str, label: str, executable: bool = False) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise ValueError("{} does not exist: {}".format(label, path))
    if executable and os.name != "nt" and not os.access(path, os.X_OK):
        raise ValueError("{} is not executable: {}".format(label, path))
    return path


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compile, validate, and execute Ny route-matrix fixtures."
    )
    parser.add_argument("--compiler", required=True, help="Path to nilcompiler")
    parser.add_argument(
        "--interpreter", required=True, help="Path to the nilinterpreter executable"
    )
    parser.add_argument("--source", required=True, help="Ny source or diagnostic input")
    parser.add_argument(
        "--expected-stdout", required=True, help="File containing exact expected stdout"
    )
    parser.add_argument(
        "--expected-stderr", help="Optional file containing exact expected stderr"
    )
    parser.add_argument("--expected-exit", required=True, type=int)
    parser.add_argument(
        "--routes",
        default=",".join(EXECUTION_ROUTES),
        help="Comma-separated routes; 'all' expands to the six execution routes",
    )
    parser.add_argument(
        "--work-dir",
        default=str(Path(tempfile.gettempdir()) / "ny-route-matrix"),
        help=(
            "Parent directory for a unique run directory; successful runs are "
            "removed and failed runs are preserved"
        ),
    )
    parser.add_argument("--report-json", help="JSON report path (default: run directory)")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument(
        "--compiler-arg",
        action="append",
        default=[],
        help="Additional nilcompiler argument; repeat (use --compiler-arg=--flag)",
    )
    parser.add_argument(
        "--interpreter-arg",
        action="append",
        default=[],
        help=(
            "Additional nilinterpreter argument for text/binary interpreter "
            "routes; repeat (use --interpreter-arg=--flag)"
        ),
    )
    parser.add_argument(
        "--program-arg",
        action="append",
        default=[],
        help=(
            "Argument passed to the Ny program for text/binary interpreter, "
            "LLVM JIT, and LLVM AOT routes; repeat (use --program-arg=value)"
        ),
    )
    parser.add_argument(
        "--text-nil-contains",
        action="append",
        default=[],
        metavar="TEXT",
        help=(
            "Require TEXT in the text-interpreter artifact before cleanup; "
            "repeat as needed"
        ),
    )
    parser.add_argument(
        "--with-stdlib",
        action="store_true",
        help="Do not add --no-stdlib to compiler commands",
    )
    parser.add_argument(
        "--xfail",
        action="append",
        default=[],
        metavar="ROUTE=REASON",
        help="Mark a selected route as an expected failure; repeat as needed",
    )
    parser.add_argument(
        "--xfail-stderr-contains",
        action="append",
        default=[],
        metavar="ROUTE=TEXT",
        help=(
            "Require TEXT in command stderr before accepting ROUTE as XFAIL; "
            "repeat as needed"
        ),
    )
    parser.add_argument(
        "--skip",
        action="append",
        default=[],
        metavar="ROUTE=REASON",
        help="Skip a selected route with an explicit reason; repeat as needed",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Treat requested SKIP results as failures; XFAIL remains allowed",
    )
    parser.add_argument(
        "--max-report-output",
        type=int,
        default=1_000_000,
        help="Maximum stdout/stderr characters stored per command in JSON",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    try:
        routes = parse_routes(args.routes)
        xfails = parse_route_reason(args.xfail, "--xfail")
        xfail_stderr = parse_route_reason(
            args.xfail_stderr_contains, "--xfail-stderr-contains"
        )
        skips = parse_route_reason(args.skip, "--skip")
        overlap = set(xfails).intersection(skips)
        if overlap:
            raise ValueError(
                "routes cannot be both skipped and xfailed: {}".format(
                    ", ".join(sorted(overlap))
                )
            )
        without_xfail = set(xfail_stderr).difference(xfails)
        if without_xfail:
            raise ValueError(
                "--xfail-stderr-contains requires matching --xfail for: {}".format(
                    ", ".join(sorted(without_xfail))
                )
            )
        compiler = resolve_existing_file(args.compiler, "compiler", executable=True)
        interpreter = resolve_existing_file(
            args.interpreter, "interpreter", executable=True
        )
        source = resolve_existing_file(args.source, "source")
        expected_stdout_path = resolve_existing_file(
            args.expected_stdout, "expected stdout"
        )
        expected_stderr_path = (
            resolve_existing_file(args.expected_stderr, "expected stderr")
            if args.expected_stderr
            else None
        )
        if args.timeout <= 0:
            raise ValueError("--timeout must be greater than zero")
        if args.max_report_output <= 0:
            raise ValueError("--max-report-output must be greater than zero")
    except ValueError as exc:
        parser.error(str(exc))

    expected_stdout = normalize_newlines(
        expected_stdout_path.read_text(encoding="utf-8")
    )
    expected_stderr = (
        normalize_newlines(expected_stderr_path.read_text(encoding="utf-8")).replace(
            "{{SOURCE}}", str(source)
        )
        if expected_stderr_path
        else None
    )

    work_parent = Path(args.work_dir).expanduser().resolve()
    work_parent.mkdir(parents=True, exist_ok=True)
    run_root = Path(
        tempfile.mkdtemp(prefix=source.stem + "-", dir=str(work_parent))
    ).resolve()

    env = os.environ.copy()
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    env["NO_COLOR"] = "1"

    results: List[Dict[str, Any]] = []
    for route in routes:
        route_dir = run_root / route
        if route in skips:
            route_dir.mkdir(parents=True, exist_ok=False)
            result = new_route_result(route, route_dir)
            result["raw_status"] = "SKIP"
            result["status"] = "SKIP"
            result["skip_reason"] = skips[route]
        else:
            result = run_route(
                route=route,
                route_dir=route_dir,
                compiler=compiler,
                interpreter=interpreter,
                source=source,
                expected_stdout=expected_stdout,
                expected_stderr=expected_stderr,
                expected_exit=args.expected_exit,
                timeout_seconds=args.timeout,
                compiler_args=args.compiler_arg,
                interpreter_args=args.interpreter_arg,
                program_args=args.program_arg,
                with_stdlib=args.with_stdlib,
                env=env,
                max_output_chars=args.max_report_output,
            )

        if result["raw_status"] != "SKIP" and route in xfails:
            result["expected_failure_reason"] = xfails[route]
            if result["raw_status"] == "FAIL":
                required_diagnostic = xfail_stderr.get(route)
                observed_stderr = "\n".join(
                    command.get("stderr", "")
                    for command in result["commands"].values()
                )
                if required_diagnostic and required_diagnostic not in observed_stderr:
                    add_check(
                        result["checks"],
                        "xfail.stderr",
                        False,
                        "missing required diagnostic {!r}".format(
                            required_diagnostic
                        ),
                    )
                    result["status"] = "FAIL"
                else:
                    result["status"] = "XFAIL"
            else:
                result["status"] = "XPASS"
        if route == "text-interpreter":
            check_text_nil_fragments(result, args.text_nil_contains)
        results.append(result)

    counts = {status: 0 for status in ("PASS", "FAIL", "SKIP", "XFAIL", "XPASS")}
    for result in results:
        counts[result["status"]] += 1

    hard_failures = counts["FAIL"] + counts["XPASS"]
    if args.strict:
        hard_failures += counts["SKIP"]
    all_skipped = bool(results) and counts["SKIP"] == len(results)

    report: Dict[str, Any] = {
        "schema": "ny.route-matrix.v1",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "host": {
            "platform": platform.platform(),
            "python": platform.python_version(),
            "byte_order": sys.byteorder,
        },
        "inputs": {
            "compiler": str(compiler),
            "interpreter": str(interpreter),
            "source": str(source),
            "expected_stdout": str(expected_stdout_path),
            "expected_stderr": str(expected_stderr_path) if expected_stderr_path else None,
            "expected_exit": args.expected_exit,
            "routes": routes,
            "timeout_seconds": args.timeout,
            "compiler_args": args.compiler_arg,
            "interpreter_args": args.interpreter_arg,
            "program_args": args.program_arg,
            "text_nil_contains": args.text_nil_contains,
            "with_stdlib": args.with_stdlib,
            "strict": args.strict,
            "xfail_stderr_contains": xfail_stderr,
        },
        "run_directory": str(run_root),
        "artifact_retention": {
            "state": (
                "preserved_after_failure"
                if hard_failures
                else "cleanup_pending"
            ),
            "retained": True,
            "policy": "preserve failures; remove passes, XFAILs, and expected skips",
        },
        "summary": counts,
        "results": results,
    }

    report_path = (
        Path(args.report_json).expanduser().resolve()
        if args.report_json
        else run_root / "report.json"
    )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report["report_path"] = str(report_path)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    for result in results:
        reason = result.get("skip_reason") or result.get("expected_failure_reason")
        suffix = " - " + reason if reason else ""
        print("{:<5} {:<13}{}".format(result["status"], result["route"], suffix))
        if result["status"] in ("FAIL", "XPASS"):
            for check in result["checks"]:
                if not check["passed"]:
                    print("      {}: {}".format(check["name"], check["detail"]))
    if hard_failures:
        print("Report: {}".format(report_path))
        print("Artifacts preserved for diagnosis: {}".format(run_root))
        return 1

    try:
        shutil.rmtree(run_root)
    except OSError as exc:
        report["artifact_retention"] = {
            "state": "cleanup_failed",
            "retained": run_root.exists(),
            "policy": "preserve failures; remove passes, XFAILs, and expected skips",
            "error": str(exc),
        }
        if report_path.is_file():
            report_path.write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print("Report: {}".format(report_path))
        print(
            "FAIL  could not remove successful run artifacts at {}: {}".format(
                run_root, exc
            ),
            file=sys.stderr,
        )
        if run_root.exists():
            print("Remaining artifacts preserved for diagnosis: {}".format(run_root))
        return 1

    report["artifact_retention"] = {
        "state": "removed_after_success",
        "retained": False,
        "policy": "preserve failures; remove passes, XFAILs, and expected skips",
    }
    if report_path.is_file():
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("Report: {}".format(report_path))
    else:
        print(
            "Report: removed with successful artifacts "
            "(use --report-json outside --work-dir to retain it)"
        )
    print("Artifacts removed after successful run: {}".format(run_root))

    if all_skipped:
        return 77
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
