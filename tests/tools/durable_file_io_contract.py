#!/usr/bin/env python3
"""Public Ny/Matrix durable-file and lock routes with a host filesystem oracle.

This qualifies local POSIX or Windows NTFS source-runtime behavior, not storage
hardware power loss or an installed/release SDK. Each execution has its own
private directory and an independent host filesystem oracle.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
import time

import ordinary_io_parity_contract as support

if os.name == "nt":
    import windows_durable_oracle as windows_oracle

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_output_staging import native_output_path

EXPECTED_STDOUT = b"durable-write-ok\nlocks-ok\nmetadata-ok\n"
EXPECTED_BYTES = bytes((255, 0, 1))


def foreign_library_nil(operation: str, binary: bool, schema: bool) -> bytes:
    """Exercise the VM dispatch boundary with valid operands and a foreign library."""
    payload = "i64" if operation == "file.acquire_file_lock" else (
        "text" if operation == "checksum.sha256_hex" else "bool")
    result = "nil::std::core::Result<" + payload + ",text>"
    target = {"file.write_bytes_durable": "existing.bin",
              "file.acquire_file_lock": "must-not-exist.lock",
              "file.create_private_directory": "must-not-exist.dir",
              "file.is_private_regular_file": "existing.bin"}.get(operation, ".")
    if operation == "file.write_bytes_durable":
        arguments = [("PUSH_CONST_STR", ["target"]), ("NEW_ARRAY_LITERAL", ["0", "u8"]),
                     ("PUSH_CONST", ["1"])]
    elif operation == "file.acquire_file_lock":
        arguments = [("PUSH_CONST_STR", ["target"]), ("PUSH_CONST", ["1"])]
    elif operation in ("file.release_file_lock", "file.file_lock_is_held"):
        arguments = [("PUSH_CONST", ["0"])]
    elif operation == "checksum.sha256_hex":
        arguments = [("NEW_ARRAY_LITERAL", ["0", "u8"])]
    else:
        arguments = [("PUSH_CONST_STR", ["target"])]
    body = arguments + [("INTEROP_CALL", ["nil_contract_foreign", operation, str(len(arguments)), result]),
                        ("POP", []), ("PUSH_CONST", ["0"]), ("RET", [])]
    if not binary:
        lines = ["module durable_library_operand", "const target = " + json.dumps(target)]
        if schema:
            lines += ["enum " + result, "Ok: " + payload, "Error: text", "end enum"]
        lines += ["function main() -> i32:"]
        lines += [" ".join([opcode, *(json.dumps(value) for value in values)]) for opcode, values in body]
        return ("\n".join(lines + ["end function", ""])).encode()

    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        data = value.encode()
        return u32(len(data)) + data

    types = u32(0) + u32(int(schema))
    if schema:
        types += string(result) + u32(2) + string("Ok") + string(payload)
        types += string("Error") + string("text") + u32(0)
    code = u32(1) + string("main") + u32(0) + u32(0) + string("i32")
    code += u32(0) + u32(0) + u32(0) + u32(len(body))
    for opcode, values in body:
        code += string(opcode) + u32(len(values)) + b"".join(map(string, values)) + u32(0)
    sections = [(1, types), (2, code), (3, u32(1) + string("target") + string(target)),
                (4, u32(0)), (5, string("durable_library_operand") + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, data in sections:
        table += struct.pack("<IQQ", kind, offset, len(data))
        offset += len(data)
    return b"NIL\0" + struct.pack("<III", 3, 2, len(sections)) + table + b"".join(data for _, data in sections)


def host_oracle(directory: Path) -> dict:
    if os.name == "nt":
        return windows_oracle.host_oracle(directory, {
            "checkpoint.bin": EXPECTED_BYTES, "empty.bin": b"", "writer.lock": b""})
    import fcntl

    expected = {"checkpoint.bin": EXPECTED_BYTES, "empty.bin": b"", "writer.lock": b""}
    # The fixture also creates one owner-only subdirectory through the public
    # surface, so the namespace is checked as files plus that single directory.
    expected_directories = {"private"}
    observed = {item.name for item in directory.iterdir()}
    if observed != set(expected) | expected_directories:
        raise AssertionError("unexpected durable-file namespace: " + repr(sorted(observed)))
    for name in expected_directories:
        info = (directory / name).lstat()
        if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
                or stat.S_IMODE(info.st_mode) != 0o700):
            raise AssertionError("directory is not owner-only: " + name)
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
            "lock_after_process_completion": "released",
            "private_directories": sorted(expected_directories), "directory_modes": "0700"}


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
              "executions": [], "negative_abi": [], "negative_library": [],
              "llvm_enabled": args.llvm == "ON",
              "scope": ("WINDOWS_NTFS_SOURCE_RUNTIME_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"
                        if os.name == "nt" else
                        "POSIX_SOURCE_RUNTIME_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION")}
    if os.name not in ("posix", "nt"):
        report.update(status="FAIL", error="unsupported host platform: " + os.name)
        support.save_new(output / "report.json", report)
        print("FAIL: " + str(output / "report.json"))
        return 1

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
        "query-width": (ROOT / "tests/language/durable-file-io-query-invalid-width.ny", "file.file_lock_is_held"),
        "query-owner": (ROOT / "tests/language/durable-file-io-query-invalid-owner.ny", "file.file_lock_is_held"),
    }
    inputs = {"compiler": compiler, "interpreter": interpreter, "runner": Path(__file__).resolve(),
              **sources, **{name: row[0] for name, row in invalid.items()}}
    if runtime:
        inputs["runtime"] = runtime
    if os.name == "nt":
        inputs["windows_oracle"] = Path(windows_oracle.__file__).resolve()
    report["inputs_before"] = {name: support.file_record(path) for name, path in inputs.items()}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NIL_", "NY_"))}
    environment["NIL_STRICT_BOUNDS"] = "1"
    # Rejected NIL may produce an optional .nycrash diagnostic in its cwd.
    # Keep that diagnostic side effect out of the exact filesystem oracle;
    # stderr and the harness's retained command logs still record each error.
    environment["NIL_CRASH_REPORT"] = "0"
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
        if os.name == "nt":
            windows_oracle.create_private_directory(data)
        else:
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
        # Library identity must be checked before any durable/checksum handler,
        # even if a forged NIL module also omits its Result schema. Keep a real
        # destination as an independent oracle against writes, creates, or modes.
        operations = ("file.write_bytes_durable", "file.sync_directory", "file.acquire_file_lock",
                      "file.release_file_lock", "file.file_lock_is_held", "file.create_private_directory",
                      "file.is_private_regular_file", "file.is_private_directory", "checksum.sha256_hex")
        for operation in operations:
            for encoding in ("text", "binary"):
                for schema in (True, False):
                    name = "foreign-" + operation + "-" + encoding + ("-schema" if schema else "-no-schema")
                    working = output / name
                    if os.name == "nt":
                        windows_oracle.create_private_directory(working)
                    else:
                        working.mkdir(mode=0o700)
                    existing = working / "existing.bin"
                    existing.write_bytes(EXPECTED_BYTES)
                    existing.chmod(0o600)
                    before = (existing.read_bytes(), existing.lstat().st_ino,
                              stat.S_IMODE(existing.lstat().st_mode), stat.S_IMODE(working.stat().st_mode))
                    before_acl = ({"file": windows_oracle.security_hash(existing),
                                   "directory": windows_oracle.security_hash(working)}
                                  if os.name == "nt" else None)
                    artifact = output / (name + ".nil")
                    artifact.write_bytes(foreign_library_nil(operation, encoding == "binary", schema))
                    run(name, [interpreter, "--no-ai", artifact], working,
                        rejection=(operation + ": requires the intrinsic library").encode())
                    after = (existing.read_bytes(), existing.lstat().st_ino,
                             stat.S_IMODE(existing.lstat().st_mode), stat.S_IMODE(working.stat().st_mode))
                    after_acl = ({"file": windows_oracle.security_hash(existing),
                                  "directory": windows_oracle.security_hash(working)}
                                 if os.name == "nt" else None)
                    if ({p.name for p in working.iterdir()} != {"existing.bin"} or after != before
                            or after_acl != before_acl):
                        raise AssertionError(name + ": rejected library operand changed the filesystem")
                    row = {"operation": operation, "route": encoding + "-vm",
                           "schema": "present" if schema else "absent", "status": "PASS", "filesystem": "unchanged"}
                    if os.name == "nt":
                        row["windows_acl_sha256"] = before_acl
                    report["negative_library"].append(row)
        if len(report["negative_library"]) != 36:
            raise AssertionError("library rejection matrix incomplete")
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
