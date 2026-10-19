#!/usr/bin/env python3
"""Verify the single NIL Framework product version in build, SDK commands, LSP, and VS Code."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


def frame(document: dict) -> bytes:
    payload = json.dumps(document, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload


def messages(output: bytes) -> list[dict]:
    result = []
    while output:
        header, separator, remaining = output.partition(b"\r\n\r\n")
        assert separator, "LSP response has no complete frame header"
        match = re.fullmatch(
            rb"Content-Length: ([0-9]+)(?:\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8)?",
            header)
        assert match, f"Unexpected LSP frame header: {header!r}"
        size = int(match[1])
        assert len(remaining) >= size, "Truncated LSP frame payload"
        result.append(json.loads(remaining[:size]))
        output = remaining[size:]
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--tool", action="append", required=True,
                        help="tool-name=/absolute/path (repeat for each built tool)")
    args = parser.parse_args()
    root = args.root.resolve()
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    number = r"(?:0|[1-9][0-9]*)"
    identifier = r"(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    semver = re.fullmatch(
        rf"({number}\.{number}\.{number})(?:-{identifier}(?:\.{identifier})*)?"
        r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", version)
    assert semver, f"VERSION is not canonical SemVer: {version!r}"
    numeric = semver[1]

    package = json.loads((root / "vscode/package.json").read_text(encoding="utf-8"))
    lock = json.loads((root / "vscode/package-lock.json").read_text(encoding="utf-8"))
    assert package["version"] == version, "VS Code package version drifted from VERSION"
    assert lock["version"] == version, "VS Code lockfile root version drifted"
    assert lock["packages"][""]["version"] == version, "VS Code lockfile package version drifted"

    header = (args.build_dir / "generated/nil-version/nil_version.hpp").read_text(encoding="utf-8")
    assert f'#define NIL_VERSION "{version}"' in header, "Generated full version drifted"
    assert f'#define NIL_VERSION_NUMERIC "{numeric}"' in header, "Generated numeric version drifted"
    cache = (args.build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
    assert f"CMAKE_PROJECT_VERSION:STATIC={numeric}\n" in cache, "CMake project version drifted"

    tools = {}
    for specification in args.tool:
        name, separator, path = specification.partition("=")
        assert separator and name not in tools, f"Invalid or duplicate --tool: {specification}"
        tools[name] = Path(path).resolve(strict=True)
    required = {"nilcompiler", "nilinterpreter", "nilassembler", "nillinker", "nildebugger", "nillsp", "nilbuild"}
    assert required <= tools.keys(), f"Missing core tools: {required - tools.keys()}"
    assert set(tools) in (required, required | {"nilagent", "nilschema"}), "Unexpected tool inventory"

    with tempfile.TemporaryDirectory(prefix="ny-release-identity-") as temporary:
        for name, tool in tools.items():
            result = subprocess.run([str(tool), "--version"], cwd=temporary,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, timeout=10, check=False)
            assert result.returncode == 0 and not result.stderr, (name, result.returncode, result.stderr)
            assert result.stdout == f"{name} {version}\n", (name, result.stdout)

        requests = b"".join(frame(document) for document in (
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
            {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ))
        result = subprocess.run([str(tools["nillsp"])], input=requests, cwd=temporary,
                                capture_output=True, timeout=10, check=False)
        assert result.returncode == 0 and not result.stderr, (result.returncode, result.stderr)
        replies = messages(result.stdout)
        assert len(replies) == 2, replies
        assert replies[0]["id"] == 1 and replies[0]["result"]["serverInfo"] == {
            "name": "nillsp", "version": version}, replies[0]
        assert replies[1] == {"jsonrpc": "2.0", "id": 2, "result": None}, replies[1]
        assert not list(Path(temporary).iterdir()), "Identity queries wrote files"

    print(f"NIL Framework {version}: {len(tools)} tool identities, LSP, CMake, and VS Code agree")


if __name__ == "__main__":
    main()
