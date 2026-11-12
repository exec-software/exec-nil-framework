#!/usr/bin/env python3
"""Check explicit context-registration suppression without losing authored intent."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


NY = '''module context_registration
@ai_context("Unused declaration retains intent")
fn unused() -> i64
    return 7
end fn
@ai_context("Main retains intent π")
fn main() -> i32
    println("42")
    return 0
end fn
'''
MATRIX = '''book context-registration
context "Unused declaration retains intent"
unused returns number begin paragraph
    give 7
end paragraph
context "Main retains intent π"
main returns override integer begin paragraph
    say "42"
    give 0
end paragraph
'''
OPTION = "--no-context-registration"
SENTINEL = b"previous artifact must survive\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_"))}
    checks = 0
    executions = 0
    with tempfile.TemporaryDirectory(prefix="nil-context-registration-") as directory:
        root = Path(directory)

        def run(command: list[str | Path]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(part) for part in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60, check=False)

        def success(command: list[str | Path], label: str,
                    stdout: str | None = None) -> subprocess.CompletedProcess[str]:
            nonlocal checks
            checks += 1
            result = run(command)
            if result.returncode != 0 or result.stderr or (stdout is not None and result.stdout != stdout):
                raise AssertionError(f"{label}: {result}")
            return result

        def inspect(artifact: Path) -> dict[str, object]:
            return json.loads(success([compiler, "--inspect-context", artifact],
                                      "inspect retained annotations").stdout)

        base: list[str | Path] = [compiler, "--quiet", "--no-stdlib", "--no-bundle"]
        for language, text in (("ny", NY), ("matrix", MATRIX)):
            source = root / ("context." + language)
            source.write_text(text, encoding="utf-8", newline="\n")
            reference = None
            for binary in (False, True):
                for suppress in (False, True):
                    artifact = root / f"{language}-{binary}-{suppress}.nil"
                    success([*base, *(["--emit-binary-nil"] if binary else []),
                             *([OPTION] if suppress else []), "-o", artifact, source],
                            "compile source context", "")
                    data = artifact.read_bytes()
                    for marker in (b"__nilaix_register_ai_context_", b"register_ai_context_internal"):
                        if (marker in data) == suppress:
                            raise AssertionError(f"wrong generated registration presence: {artifact.name}")
                    current = inspect(artifact)
                    if reference is None:
                        reference = current
                    if current != reference or current.get("schema") != "ny.context.v2":
                        raise AssertionError("registration option or encoding changed authored context")
                    functions = current.get("functions")
                    if not isinstance(functions, list) or len(functions) != 2:
                        raise AssertionError("both main and unused intent must remain inspectable")
                    retained = [annotation for function in functions
                                for annotation in function["annotations"]]
                    if set(retained) != {'@ai_context("Unused declaration retains intent")',
                                         '@ai_context("Main retains intent π")'}:
                        raise AssertionError("authored intent was altered")
                    success([interpreter, artifact], "execute unchanged program", "42\n")
                    executions += 1

            # Test mode normally adds an initializer calling the registration
            # helpers. Suppression must remove both at generation, leaving the
            # ordinary isolated-test harness intact.
            artifact = root / f"{language}-test-mode.nil"
            success([*base, "--test", OPTION, "-o", artifact, source],
                    "compile test mode without registration", "")
            data = artifact.read_bytes()
            if any(marker in data for marker in (b"__nilaix_register_ai_context_", b"__nilaix_init_",
                                                 b"register_ai_context_internal")):
                raise AssertionError("test mode retained generated registration or a dangling call")
            if b"function main" not in data:
                raise AssertionError("test entry point was lost")
            inspect(artifact)

        source = root / "context.ny"
        output = root / "sentinel.nil"
        invalid = [
            ([*base, OPTION, OPTION, "-o", output, source], "more than once"),
            ([*base, OPTION + "=false", "-o", output, source], "unknown option"),
            ([compiler, OPTION, "--inspect-context", root / "ny-False-True.nil"], "alone"),
            ([compiler, "--inspect-context", root / "ny-False-True.nil", OPTION, "-o", output], "alone"),
            ([compiler, "--test-list", OPTION, source], "discovery only"),
            ([*base, OPTION, "--emit-binary-nil", "--emit-library", "-o", output, source], "source-bearing NIL libraries"),
            ([compiler, "--analyze", OPTION, source], "unsupported analysis option"),
        ]
        for command, diagnostic in invalid:
            output.write_bytes(SENTINEL)
            result = run(command)
            checks += 1
            if result.returncode == 0 or diagnostic not in result.stderr or output.read_bytes() != SENTINEL:
                raise AssertionError(f"invalid option did not refuse and preserve output: {result}")

        help_result = success([compiler, "--help"], "help advertises explicit option")
        if OPTION not in help_result.stdout:
            raise AssertionError("help omits the registration option")

    print(json.dumps({"status": "passed", "checks": checks,
                      "executions": executions, "languages": 2,
                      "default_registration_unchanged": True,
                      "retained_context_equal": True}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
