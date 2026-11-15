#!/usr/bin/env python3
"""Public Ny/Matrix durable-file and lock routes with a host filesystem oracle.

This qualifies local POSIX source-runtime behavior, not storage hardware power
loss or an installed/release SDK. Each execution has its own private directory.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import time

import ordinary_io_parity_contract as support

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_output_staging import native_output_path

EXPECTED_STDOUT = b"durable-write-ok\nlocks-ok\n"
EXPECTED_BYTES = bytes((255, 0, 1))


def host_oracle(directory: Path) -> dict:
    import fcntl

    expected = {"checkpoint.bin": EXPECTED_BYTES, "empty.bin": b"", "writer.lock": b""}
    observed = {item.name for item in directory.iterdir()}
    if observed != set(expected):
        raise AssertionError("unexpected durable-file namespace: " + repr(sorted(observed)))
    for name, content in expected.items():
        path = directory / name
        info = path.lstat()
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600):
            raise AssertionError("file is not a private owned regular inode: " + name)
        if path.read_bytes() != content:
            raise AssertionError("durable bytes differ: " + name)
    descriptor = os.open(directory / "writer.lock", os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.flock(descriptor, fcntl.LOCK_UN)
    finally:
        os.close(descriptor)
    return {"bytes": "exact", "modes": "0600", "remaining_staging_files": 0,
            "lock_after_process_completion": "released"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "nilstd"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="retained evidence parent; each run creates a fresh private child")
    args = parser.parse_args()

    output_parent = args.output.resolve()
    if output_parent == ROOT:
        parser.error("evidence parent must not be the repository root")
    output_parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=output_parent))
    logs = output / "logs"
    logs.mkdir()
    report = {"schema": "nil.durable-file-io.v1", "status": "FAIL", "commands": [],
              "executions": [], "negative_abi": [], "llvm_enabled": args.llvm == "ON",
              "scope": "POSIX_SOURCE_RUNTIME_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"}
    if os.name != "posix":
        report.update(status="SKIP", reason="POSIX implementation only; unsupported-host checks are separate")
        support.save_new(output / "report.json", report)
        print("SKIP: " + str(output / "report.json"))
        return 0

    ownership = support.load_ownership(ROOT)
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    nilstd = args.nilstd.resolve(strict=True)
    runtime = args.runtime.resolve(strict=True) if args.runtime else None
    sources = {
        "ny": ROOT / "tests/language/durable-file-io.ny",
        "matrix": ROOT / "tests/matrix/io/durable-file-io.matrix",
    }
    invalid = {
        "width": (ROOT / "tests/language/durable-file-io-invalid-width.ny", "file.acquire_file_lock"),
        "owner": (ROOT / "tests/language/durable-file-io-invalid-owner.ny", "file.sync_directory"),
    }
    inputs = {"compiler": compiler, "interpreter": interpreter, "runner": Path(__file__).resolve(),
              **sources, **{name: row[0] for name, row in invalid.items()}}
    if runtime:
        inputs["runtime"] = runtime
    report["inputs_before"] = {name: support.file_record(path) for name, path in inputs.items()}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NIL_", "NY_"))}
    environment["NIL_STRICT_BOUNDS"] = "1"
    deadline = time.monotonic() + 240
    base = [compiler, "--quiet", "--no-stdlib", "--sdk-path", nilstd]

    def run(name: str, command: list, cwd: Path, expected: bytes | None = b"",
            rejection: bytes | None = None) -> tuple[bytes, bytes]:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError("shared durable-file route deadline exhausted")
        row = support.run_process(ownership, command, cwd, environment, logs, name, min(90, remaining))
        report["commands"].append(row)
        stdout = (logs / row["stdout"]).read_bytes()
        stderr = (logs / row["stderr"]).read_bytes()
        if not row["cleanup_verified"] or row.get("timed_out"):
            raise AssertionError(name + ": process cleanup/deadline failed")
        if rejection is not None:
            if row.get("exit_code", 0) == 0 or stdout or rejection not in stderr:
                raise AssertionError(name + ": expected exact ABI rejection did not occur")
            row["expected_rejection"] = True
        elif row["status"] != "PASS" or stderr or (expected is not None and stdout != expected):
            raise AssertionError(name + ": exit/stdout/stderr contract failed; see retained logs")
        return stdout, stderr

    def execute(name: str, language: str, command: list) -> None:
        working = output / name
        working.mkdir()
        data = working / "data-café 日本語 🦊"
        data.mkdir(mode=0o700)
        run(name, [*command, data], working, EXPECTED_STDOUT)
        report["executions"].append({"route": name, "language": language,
                                     "status": "PASS", "oracle": host_oracle(data)})

    try:
        # Record and check the runtime archive actually selected by the compiler.
        analysis, _ = run("runtime-selection", [compiler, "--analyze", "--no-stdlib",
                          "--sdk-path", nilstd, sources["ny"]], output, None)
        selected = json.loads(analysis)["nativeRuntime"]
        report["selected_native_runtime"] = selected
        if runtime and args.llvm == "ON" and (not selected or Path(selected).resolve() != runtime):
            raise AssertionError("compiler selected a different native runtime archive")
        for language, source in sources.items():
            build = output / (language + "-build")
            build.mkdir()
            for encoding in ("text", "binary"):
                artifact = build / (encoding + ".nil")
                flags = [] if encoding == "text" else ["--emit-binary-nil"]
                run(language + "-compile-" + encoding,
                    [*base, "--no-bundle", *flags, "-o", artifact, source], build)
                if artifact.read_bytes().startswith(b"NIL\0") != (encoding == "binary"):
                    raise AssertionError("unexpected NIL encoding")
                execute(language + "-" + encoding + "-vm", language,
                        [interpreter, "--no-ai", artifact, "--"])
            if args.llvm == "ON":
                executable = build / "native"
                run(language + "-compile-aot", [*base, "--native", "-o", executable, source], build)
                execute(language + "-aot", language, [native_output_path(executable)])
                execute(language + "-jit", language,
                        [*base, "--native", "--jit", "-o", build / "jit", source, "--"])
        for name, (source, operation) in invalid.items():
            for encoding in ("text", "binary"):
                artifact = output / ("invalid-" + name + "-" + encoding + ".nil")
                flags = [] if encoding == "text" else ["--emit-binary-nil"]
                run("compile-invalid-" + name + "-" + encoding,
                    [*base, "--no-bundle", *flags, "-o", artifact, source], output)
                wanted = (operation + ": requires its checked result type and enum schema").encode()
                run("invalid-" + name + "-" + encoding,
                    [interpreter, "--no-ai", artifact], output, rejection=wanted)
                if (output / "must-not-exist.lock").exists():
                    raise AssertionError("rejected VM ABI created a lock")
                report["negative_abi"].append({"case": name, "route": encoding + "-vm", "status": "PASS"})
            if args.llvm == "ON":
                executable = output / ("invalid-" + name)
                wanted = (operation + " requires its exact ordinary-I/O ABI and enum schema").encode()
                run("invalid-" + name, [*base, "--native", "-o", executable, source], output,
                    rejection=wanted)
                if native_output_path(executable).exists() or (output / "must-not-exist.lock").exists():
                    raise AssertionError("rejected ABI published an artifact or lock")
                report["negative_abi"].append({"case": name, "route": "llvm-aot", "status": "PASS"})
        expected_count = 8 if args.llvm == "ON" else 4
        if len(report["executions"]) != expected_count:
            raise AssertionError("route matrix incomplete")
        report["inputs_after"] = {name: support.file_record(path) for name, path in inputs.items()}
        if report["inputs_after"] != report["inputs_before"]:
            raise AssertionError("a test input changed during execution")
        report["status"] = "PASS"
    except Exception as error:
        report["error"] = str(error)
    finally:
        report["cleanup_verified"] = all(row["cleanup_verified"] for row in report["commands"])
        support.save_new(output / "report.json", report)
    print(report["status"] + ": " + str(output / "report.json"))
    if report["status"] != "PASS":
        print(report.get("error", "durable file route contract failed"), file=sys.stderr)
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
