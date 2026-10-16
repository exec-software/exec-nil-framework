#!/usr/bin/env python3
"""Check source-level fault identities and fail-closed opaque-token contracts."""

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
    environment = {name: value for name, value in os.environ.items() if not name.startswith("NY_")}
    structure = "struct Problem\ncode: i32\nend struct\n"

    def program(body: str, declarations: str = "") -> str:
        return structure + declarations + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    def opaque(body: str) -> str:
        return program("attempt\nraise Problem { code: 1 }\nfault as token\n" + body + "\nend attempt")

    cases = {
        "scalar_raise": (program("raise 1"), "Fault payload must have a concrete struct type"),
        "text_raise": (program('raise "bad"'), "Fault payload must have a concrete struct type"),
        "class_raise": (program("raise Item { code: 1 }", "class Item\ncode: i32\nend class"), "Fault payload must have a concrete struct type"),
        "enum_raise": (program("raise State::Ready", "enum State\nReady\nend enum"), "Fault payload must have a concrete struct type"),
        "opaque_any_raise": (program("let payload: any = Problem { code: 1 }\nraise payload"), "Fault payload must have a concrete struct type"),
        "scalar_filter": (program("attempt\nprintln(1)\nfault i32 as value\nprintln(2)\nend attempt"), "Fault payload must have a concrete struct type"),
        "missing_filter": (program("attempt\nprintln(1)\nfault Missing as value\nprintln(2)\nend attempt"), "Fault payload must have a concrete struct type"),
        "class_filter": (program("attempt\nprintln(1)\nfault Item as value\nprintln(2)\nend attempt", "class Item\nend class"), "Fault payload must have a concrete struct type"),
        "enum_filter": (program("attempt\nprintln(1)\nfault State as value\nprintln(2)\nend attempt", "enum State\nReady\nend enum"), "Fault payload must have a concrete struct type"),
        "open_generic_filter": (program("attempt\nprintln(1)\nfault Box as value\nprintln(2)\nend attempt", "struct Box<T>\nvalue: T\nend struct"), "Fault payload requires exact concrete generic arguments"),
        "any_generic_filter": (program("attempt\nprintln(1)\nfault Box<any> as value\nprintln(2)\nend attempt", "struct Box<T>\nvalue: T\nend struct"), "Fault payload requires resolved concrete generic arguments"),
        "integer_guard": (program("attempt\nraise Problem { code: 1 }\nfault Problem as value when 1\nprintln(2)\nend attempt"), "Fault handler guard condition must be boolean"),
        "any_guard": (program('attempt\nraise Problem { code: 1 }\nfault Problem as value when panic("guard")\nprintln(2)\nend attempt'), "Fault handler guard condition must be boolean"),
        "unreachable_handler": (program("attempt\nraise Problem { code: 1 }\nfault as token\nprintln(1)\nfault Problem as value\nprintln(2)\nend attempt"), "Unreachable fault handler after an unguarded catch-all"),
        "token_field": (opaque("println(token.code)"), "Opaque catch-all fault token may only"),
        "token_print": (opaque("println(token)"), "Opaque catch-all fault token may only"),
        "token_copy": (opaque("let copy = token"), "Opaque catch-all fault token may only"),
        "token_arithmetic": (opaque("println(token + 1)"), "Opaque catch-all fault token may only"),
        "token_cast": (opaque("println(token as i64)"), "Opaque catch-all fault token may only"),
        "token_return": (opaque("return token"), "Opaque catch-all fault token may only"),
        "body_scope": (program("attempt\nlet hidden = 1\nfault as token\nprintln(hidden)\nend attempt"), "Undefined identifier 'hidden'"),
        "handler_scope": (program("attempt\nraise Problem { code: 1 }\nfault Problem as value\nprintln(value.code)\nrecover\nprintln(value.code)\nend attempt"), "Undefined identifier 'value'"),
        "private_filter_alias": (
            "module owner\npub struct Trouble\ncode: i32\nend struct\ntype Hidden = Trouble\n"
            "module client\nfn main() -> i32\nattempt\nraise owner::Trouble { code: 2 }\n"
            "fault owner::Hidden as value\nprintln(value.code)\nend attempt\nreturn 0\nend fn\n",
            "Fault type spelling 'owner::Hidden' is private"),
        "private_filter_namespace_alias": (
            "module owner\npub struct Trouble\ncode: i32\nend struct\ntype Hidden = Trouble\n"
            "module client\nnamespace domain = owner\nfn main() -> i32\nattempt\nraise owner::Trouble { code: 2 }\n"
            "fault domain::Hidden as value\nprintln(value.code)\nend attempt\nreturn 0\nend fn\n",
            "Fault type spelling 'domain::Hidden' is private"),
        "private_filter_generic_argument": (
            "module owner\ntype Hidden = i32\nmodule client\nstruct Box<T>\nvalue: T\nend struct\n"
            "fn main() -> i32\nattempt\nprintln(1)\nfault Box<owner::Hidden> as value\n"
            "println(2)\nend attempt\nreturn 0\nend fn\n",
            "Fault type spelling 'owner::Hidden' is private"),
    }
    positive = """module left
pub struct Problem
code: i32
end struct
pub fn fail() -> i32
raise Problem { code: 11 }
return -1
end fn
module client
struct left__Problem
code: i32
end struct
type Alias = left::Problem
fn main() -> i32
attempt
left::fail()
fault left__Problem as unrelated
println(99)
fault Alias as selected when selected.code == 11
println(selected.code)
end attempt
return 0
end fn
"""
    sentinel = b"prior artifact survives rejection\n"
    with tempfile.TemporaryDirectory(prefix="ny-fault-contract-") as directory:
        root = Path(directory)
        for name, (source, diagnostic) in cases.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                output.write_bytes(sentinel)
                result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *extra,
                                         "--output", str(output), str(path)],
                                        cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                        capture_output=True, text=True, timeout=30, check=False)
                diagnostic_found = diagnostic in result.stderr or (
                    mode.startswith("unchecked-") and (
                        "fault lowering requires checked fault" in result.stderr or
                        "class lowering requires checked class identity" in result.stderr))
                if result.returncode <= 0 or not diagnostic_found:
                    raise AssertionError(f"{name}/{mode}: expected {diagnostic!r}; exit={result.returncode}\n{result.stdout}\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejection replaced a prior artifact")
        source = root / "exact-schema.ny"
        source.write_text(positive, encoding="utf-8")
        output = root / "exact-schema.nil"
        result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                 "--output", str(output), str(source)],
                                cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=30, check=False)
        if result.returncode != 0:
            raise AssertionError(f"exact nominal schema compile failed:\n{result.stdout}\n{result.stderr}")
        artifact = output.read_text(encoding="utf-8")
        for exact in ("struct left::Problem\n", "struct client::left__Problem\n",
                      "FAULT_MATCH", "client::left__Problem", "FAULT_BIND"):
            if exact not in artifact:
                raise AssertionError(f"exact fault schema absent: {exact!r}")
        instructions = {tuple(line.split(";", 1)[0].split()) for line in artifact.splitlines()}
        for exact in (("NEW_STRUCT", "left::Problem"), ("FAULT_RAISE", "left::Problem")):
            if exact not in instructions:
                raise AssertionError(f"exact fault instruction absent: {exact!r}")
        if ("NEW_STRUCT", "left__Problem") in instructions:
            raise AssertionError("fault construction used a lossy sanitized module identity")
    print(f"fault source contract: {len(cases) * 4} rejected compilations; exact nominal NIL schemas checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
