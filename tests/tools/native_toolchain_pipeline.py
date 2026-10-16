#!/usr/bin/env python3
"""Exercise the standalone NIL assembler and native object linker route."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


SKIP = 77


@contextmanager
def diagnostic_directory(parent: Path):
    root = Path(tempfile.mkdtemp(prefix="ny-tools-", dir=parent)).resolve()
    completed = False

    def complete() -> None:
        nonlocal completed
        completed = True

    try:
        yield root, complete
    finally:
        if completed:
            shutil.rmtree(root)
        else:
            print(f"Toolchain artifacts preserved for diagnosis: {root}", file=sys.stderr)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--assembler", type=Path, required=True)
    parser.add_argument("--linker", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--expected-stdout", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path)
    return parser.parse_args()


def run(command: list[str], *, cwd: Path, timeout: int = 90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=sanitized_environment(),
    )


def sanitized_environment() -> dict[str, str]:
    environment = os.environ.copy()
    for variable in (
        "NY_LIBRARY_PATH", "NIL_LIBRARY_PATH",
        "NY_LEX_DUMP",
        "NY_PARSE_TRACE",
        "NY_STDLIB_PATH", "NIL_STDLIB_PATH",
    ):
        environment.pop(variable, None)
    return environment


def command_failure(label: str, process: subprocess.CompletedProcess[str]) -> str:
    return (
        f"{label} failed with exit {process.returncode}\n"
        f"stdout:\n{process.stdout}\n"
        f"stderr:\n{process.stderr}"
    )


def require_private_file(path: Path, *, executable: bool) -> str | None:
    try:
        status = path.lstat()
    except OSError as error:
        return f"cannot stat {path}: {error}"
    if not stat.S_ISREG(status.st_mode):
        return f"output is not a regular file: {path}"
    if os.name != "nt":
        group_or_other = status.st_mode & (stat.S_IRWXG | stat.S_IRWXO)
        if group_or_other:
            return f"output is accessible outside its owner: {path} mode={status.st_mode:o}"
        owner_exec = bool(status.st_mode & stat.S_IXUSR)
        if owner_exec != executable:
            return f"unexpected executable mode for {path}: {status.st_mode:o}"
    return None


def compile_nil(
    compiler: Path, source: Path, output: Path, *, binary: bool, cwd: Path,
    stdlib: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        str(compiler),
        "--no-stdlib",
        "--no-bundle",
        "--quiet",
        "--output",
        str(output),
    ]
    if binary:
        command.append("--emit-binary-nil")
    if stdlib is not None:
        command.extend(["--sdk-path", str(stdlib.resolve())])
    command.append(str(source))
    return run(command, cwd=cwd)


def assemble_and_link(
    *,
    assembler: Path,
    linker: Path,
    runtime: Path,
    nil_input: Path,
    object_output: Path,
    executable_output: Path,
    expected_stdout: str,
    cwd: Path,
) -> tuple[bool, str]:
    assembled = run(
        [str(assembler), "-o", str(object_output), str(nil_input)], cwd=cwd
    )
    if assembled.returncode != 0:
        combined = (assembled.stdout + assembled.stderr).lower()
        if "llvm" in combined and (
            "not available" in combined
            or "not compiled" in combined
            or "unavailable" in combined
        ):
            return False, "skip: nilassembler was built without LLVM support"
        return False, command_failure("nilassembler", assembled)
    if not object_output.is_file() or object_output.stat().st_size < 32:
        return False, "nilassembler did not publish a non-empty host object"
    if object_output.read_bytes()[:5] in (b"NYOBJ", b"NYEXE"):
        return False, "nilassembler published the retired custom container format"
    permission_error = require_private_file(object_output, executable=False)
    if permission_error:
        return False, permission_error

    linked = run(
        [
            str(linker),
            "--runtime",
            str(runtime),
            "-o",
            str(executable_output),
            str(object_output),
        ],
        cwd=cwd,
    )
    if linked.returncode != 0:
        return False, command_failure("nillinker", linked)
    permission_error = require_private_file(executable_output, executable=True)
    if permission_error:
        return False, permission_error

    executed = run([str(executable_output)], cwd=cwd)
    if (
        executed.returncode != 0
        or executed.stdout != expected_stdout
        or executed.stderr
    ):
        return False, (
            "linked executable contract failed: "
            f"exit={executed.returncode} stdout={executed.stdout!r} "
            f"stderr={executed.stderr!r}"
        )
    return True, ""


def expect_rejection_preserves_output(
    command: list[str], output: Path, *, cwd: Path, label: str
) -> str | None:
    sentinel = b"existing-output-must-survive\n"
    output.write_bytes(sentinel)
    process = run(command, cwd=cwd)
    if process.returncode == 0:
        return f"{label} unexpectedly succeeded"
    try:
        current = output.read_bytes()
    except OSError as error:
        return f"{label} removed its prior output: {error}"
    if current != sentinel:
        return f"{label} modified its prior output before reporting failure"
    return None


def main() -> int:
    args = arguments()
    expected_stdout = args.expected_stdout.read_text(encoding="utf-8")
    args.work_dir.mkdir(parents=True, exist_ok=True)

    try:
        with diagnostic_directory(args.work_dir) as (root, complete):

            for label, binary in (("text", False), ("binary", True)):
                nil_output = root / f"route-{label}.nil"
                compiled = compile_nil(
                    args.compiler, args.source, nil_output, binary=binary, cwd=root,
                    stdlib=args.stdlib,
                )
                if compiled.returncode != 0 or not nil_output.is_file():
                    print(command_failure(f"{label} NIL compilation", compiled), file=sys.stderr)
                    return 1

                # Deliberately exercise paths that would be unsafe if either tool
                # built a shell command from untrusted user strings.
                object_output = root / f"route {label};literal.o"
                executable_output = root / f"route {label};literal"
                ok, message = assemble_and_link(
                    assembler=args.assembler,
                    linker=args.linker,
                    runtime=args.runtime,
                    nil_input=nil_output,
                    object_output=object_output,
                    executable_output=executable_output,
                    expected_stdout=expected_stdout,
                    cwd=root,
                )
                if not ok:
                    if message.startswith("skip:"):
                        print(message)
                        complete()
                        return SKIP
                    print(message, file=sys.stderr)
                    return 1

            malformed_nil = root / "malformed.nil"
            malformed_nil.write_text(
                "module root\nfunction main:\n    NOT_AN_OPCODE\nend function\n",
                encoding="utf-8",
            )
            protected_object = root / "protected.o"
            error = expect_rejection_preserves_output(
                [
                    str(args.assembler),
                    "-o",
                    str(protected_object),
                    str(malformed_nil),
                ],
                protected_object,
                cwd=root,
                label="malformed NIL assembly",
            )
            if error:
                print(error, file=sys.stderr)
                return 1
            library_nil = root / "library.nil"
            library_compile = run(
                [
                    str(args.compiler),
                    "--no-stdlib",
                    "--no-bundle",
                    "--emit-binary-nil",
                    "--emit-library",
                    "--quiet",
                    *(["--sdk-path", str(args.stdlib.resolve())] if args.stdlib else []),
                    "--output",
                    str(library_nil),
                    str(args.source),
                ],
                cwd=root,
            )
            if library_compile.returncode != 0:
                print(command_failure("NIL library compilation", library_compile), file=sys.stderr)
                return 1
            error = expect_rejection_preserves_output(
                [
                    str(args.assembler),
                    "-o",
                    str(protected_object),
                    str(library_nil),
                ],
                protected_object,
                cwd=root,
                label="NIL library assembly",
            )
            if error:
                print(error, file=sys.stderr)
                return 1

            invalid_object = root / "invalid.o"
            invalid_object.write_bytes(b"not a host object\n")
            protected_executable = root / "protected-program"
            error = expect_rejection_preserves_output(
                [
                    str(args.linker),
                    "--runtime",
                    str(args.runtime),
                    "-o",
                    str(protected_executable),
                    str(invalid_object),
                ],
                protected_executable,
                cwd=root,
                label="invalid object link",
            )
            if error:
                print(error, file=sys.stderr)
                return 1
            complete()
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"toolchain integration could not run: {error}", file=sys.stderr)
        return 1

    print("standalone assembler/linker pipeline: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
