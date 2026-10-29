"""Source inventory and module identities shared by SDK packaging checks.

This only extracts declaration names. The compiler still parses and strictly
checks every source before the packager publishes any library artifacts.
"""

from __future__ import annotations

import re
from pathlib import PurePath
from typing import Iterator


SOURCE_SUFFIXES = frozenset({".ny", ".matrix", ".mat"})
NY_MODULE_DECLARATION = re.compile(
    r"^[ \t]*(?:module|package)[ \t]+"
    r"([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)[ \t]*(?:#.*)?$",
    re.MULTILINE,
)
CANONICAL_MODULE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")


def canonical_std_namespace(name: str) -> str:
    """Canonicalize a parsed module identity, never arbitrary source text."""
    if name == "ny.lang" or name.startswith("ny.lang."):
        name = "nil.std" + name[len("ny.lang"):]
    if name == "nil.std.nydata" or name.startswith("nil.std.nydata."):
        name = "nil.std.nildata" + name[len("nil.std.nydata"):]
    return name


def module_artifact_path(name: str) -> PurePath:
    """Standard families are flat on disk; external packages keep their root."""
    name = canonical_std_namespace(name)
    if name.startswith("nil.std."):
        name = name[len("nil.std."):]
    return PurePath(*name.split(".")).with_suffix(".nil")


def is_source(path: str | PurePath) -> bool:
    return PurePath(path).suffix.lower() in SOURCE_SUFFIXES


def _matrix_tokens(document: str) -> Iterator[tuple[str, str]]:
    """Read names/separators while excluding Matrix comments and raw literals.

    Quotes have no escapes and stop at the end of their line. A raw text block
    opens only when `begin text` reaches the physical end of its line; even a
    trailing comment prevents it, matching matrix_lexer.cpp.
    """
    in_text = False
    for line in document.split("\n"):
        if in_text:
            if re.fullmatch(r"[ \t\r]*end[ \t\r]+text[ \t\r]*", line, re.IGNORECASE | re.ASCII):
                in_text = False
            continue
        yield ("boundary", "")
        position = 0
        while position < len(line):
            char = line[position]
            if char in " \t\r":
                position += 1
                continue
            if line.startswith("!!!", position):
                break
            if char in '"`':
                end = line.find(char, position + 1)
                if end < 0:
                    yield ("other", "")
                    break  # The compiler reports the unterminated literal.
                if char == "`":
                    yield ("name", line[position + 1:end])
                else:
                    yield ("other", "")
                position = end + 1
                continue
            if (char.isascii() and (char.isalpha() or char == "_")) or ord(char) >= 128:
                end = position + 1
                while end < len(line):
                    candidate = line[end]
                    if (candidate.isascii() and (candidate.isalnum() or candidate in "_-")) or ord(candidate) >= 128:
                        end += 1
                    else:
                        break
                spelling = line[position:end]
                if spelling.lower() == "begin" and re.fullmatch(r"[ \t]*text[ \t\r]*", line[end:], re.IGNORECASE | re.ASCII):
                    in_text = True
                    yield ("other", "")
                    break
                yield ("word", spelling)
                position = end
                continue
            if line.startswith("::", position):
                yield ("separator", "::")
                position += 2
            else:
                following = line[position + 1:position + 2]
                dotted_name = char == "." and following and (
                    following == "_" or following.isalpha() or ord(following) >= 128
                )
                kind = "separator" if dotted_name else "boundary" if char in ".;" else "other"
                yield (kind, char)
                position += 1


def module_declarations(document: str, suffix: str) -> set[str]:
    if suffix.lower() == ".ny":
        # Keep the established Ny packaging declaration policy unchanged.
        return {canonical_std_namespace(name) for name in NY_MODULE_DECLARATION.findall(document)}

    tokens = list(_matrix_tokens(document))
    declarations = set()

    def next_content(index: int) -> int:
        while index < len(tokens) and tokens[index] == ("boundary", ""):
            index += 1
        return index

    index = 0
    while index < len(tokens):
        kind, spelling = tokens[index]
        if kind != "word" or spelling.lower() not in {"book", "library"} or (index and tokens[index - 1][0] != "boundary"):
            index += 1
            continue
        index = next_content(index + 1)
        if index >= len(tokens) or tokens[index][0] not in {"name", "word"}:
            continue  # Strict compilation will diagnose the missing name.
        segments = [tokens[index][1]]
        index = next_content(index + 1)
        while index < len(tokens) and tokens[index][0] == "separator":
            segment = next_content(index + 1)
            if segment >= len(tokens) or tokens[segment][0] not in {"name", "word"}:
                break
            segments.append(tokens[segment][1])
            index = next_content(segment + 1)
        name = ".".join(segments).replace("-", "_")
        if CANONICAL_MODULE.fullmatch(name) is None:
            raise ValueError("Matrix book name cannot be represented in the SDK manifest: " + name)
        declarations.add(canonical_std_namespace(name))
    return declarations
