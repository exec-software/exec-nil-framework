#!/usr/bin/env python3
"""Affine semantic failures must preserve prior artifacts, including --no-typecheck."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    owner = "let mut owner = owned<i32>(2)\n"
    def program(body: str, extra: str = "") -> str:
        return extra + "\nfn main()->i32\n" + body + "\nreturn 0\nend fn\n"
    cases = {
        "copy": program(owner + "let alias=owner"),
        "use_after_move": program(owner + "let next=owned_move(owner)\nprintln(*owner)"),
        "move_borrowed": program(owner + "let view=owned_borrow(owner)\nlet next=owned_move(owner)"),
        "shared_write": program(owner + "let view=owned_borrow(owner)\n*view=7"),
        "exclusive_alias": program(owner + "let first=owned_borrow_mut(owner)\nlet second=owned_borrow_mut(owner)"),
        "owner_shared_write": program(owner + "let view=owned_borrow(owner)\nowner[0]=7"),
        "owner_exclusive_read": program(owner + "let view=owned_borrow_mut(owner)\nprintln(*owner)"),
        "address_escape": program(owner + "let raw=&owner[0]"),
        "any_escape": program(owner + "let erased:any=owner"),
        "array_escape": program(owner + "let erased=[owner]"),
        "branch_moved": program(owner + "if true then\nprintln(1)\nelse\nlet next=owned_move(owner)\nend if\nprintln(*owner)"),
        "loop_move": program(owner + "for i in range(2)\nlet next=owned_move(owner)\nend for"),
        "bad_other_diagnostic": program(owner + 'let bad:i32="invalid"'),
        "borrow_parameter_without_creation": program("", "fn read(value:borrow<i32>)->i32\nreturn *value\nend fn\n"),
        "owner_parameter_without_creation": program("", "fn read(value:owned<i32>)->i32\nreturn *value\nend fn\n"),
        "borrow_uninitialized": program("let value:borrow<i32>\nprintln(*value)"),
        "owner_uninitialized": program("let value:owned<i32>\nprintln(*value)"),
        "borrow_parameter_alias": program("", "type View=borrow<i32>\nfn read(value:View)->i32\nreturn *value\nend fn\n"),
    }
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    modes = ([], ["--emit-binary-nil"], ["--no-typecheck"], ["--no-typecheck", "--emit-binary-nil"])
    sentinel = b"prior ownership artifact must survive\n"
    with tempfile.TemporaryDirectory(prefix="ny-owned-contract-") as temporary:
        root = Path(temporary)
        for name, source in cases.items():
            path = root / f"{name}.ny"
            path.write_text(source, encoding="utf-8")
            for index, flags in enumerate(modes):
                output = root / f"{name}-{index}.nil"
                output.write_bytes(sentinel)
                result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                    *flags, "--output", str(output), str(path)], cwd=root, env=environment,
                    stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
                if result.returncode <= 0 or "Parse error" in result.stderr or "Syntax error" in result.stderr:
                    raise AssertionError(f"{name}/{index}: expected semantic rejection\n{result}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{index}: failed compilation replaced prior artifact")
    print(f"{len(cases) * len(modes)} ownership rejections preserved existing artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
