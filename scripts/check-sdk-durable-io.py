#!/usr/bin/env python3
"""Qualify installed durable I/O, console and nominal globals on supported hosts.

Uses the existing source contracts and SDK validator. Evidence is retained in a
fresh directory outside the SDK; success is not a language release declaration.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import stat
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/tools"))
import ordinary_io_parity_contract as support

WINDOWS = os.name == "nt"
WINDOWS_WORKSPACE_UNITS = 96

OPERATIONS = (
    "file.write_bytes_durable", "file.sync_directory", "file.acquire_file_lock",
    "file.release_file_lock", "file.file_lock_is_held", "file.create_private_directory",
    "file.is_private_regular_file", "file.is_private_directory", "checksum.sha256_hex",
)
EXECUTIONS = {(language + "-" + route, language)
              for language in ("ny", "matrix")
              for route in ("text-vm", "binary-vm", "aot", "jit")}
NEGATIVE_ABI = {(case, route) for case in ("width", "owner", "query-width", "query-owner")
                for route in ("text-vm", "binary-vm", "llvm-aot")}
NEGATIVE_LIBRARY = {(operation, route, schema) for operation in OPERATIONS
                    for route in ("text-vm", "binary-vm")
                    for schema in ("present", "absent")}
GLOBAL_SUMMARY = (
    b"global contracts passed: 9 negative and 3 control cases in text/binary NIL\n"
    b"nominal global contracts passed: 8 VM executions, 2 initialization rejections, 2 native rejections\n"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def record(path: Path) -> dict:
    return support.file_record(path)


def windows_workspace_fits(path: Path) -> bool:
    # Reserve 160 UTF-16 units for the current helper/tool descendants, including
    # niltest's directory, compiler private staging and atomic temporary names.
    # This is a qualification layout bound, not general long-path support.
    return len(str(path).encode("utf-16-le")) // 2 <= WINDOWS_WORKSPACE_UNITS


def windows_execution_workspace() -> Path:
    import windows_durable_oracle as oracle
    parent = Path(tempfile.gettempdir()).resolve(strict=True)
    for _ in range(128):
        candidate = parent / ("nilq-" + secrets.token_hex(8))
        require(windows_workspace_fits(candidate),
                "Windows qualification requires a system-temp workspace of at most "
                "96 UTF-16 units; select a shorter process TMP/TEMP directory")
        try:
            # Apply the effective user's protected ACL at exclusive creation;
            # verify local NTFS and retain this directory on success or failure.
            oracle.create_private_directory(candidate)
            return candidate
        except OSError as error:
            if error.errno not in (80, 183):  # Existing names are never adopted.
                raise ValueError("cannot reserve Windows qualification workspace at " +
                                 str(candidate) + "; any created entry is retained") from error
        except Exception as error:
            raise ValueError("cannot verify Windows qualification workspace at " +
                             str(candidate) + "; any created entry is retained") from error
    raise ValueError("cannot reserve a fresh Windows qualification workspace")


def retention_inventory(root: Path, deadline: float) -> dict:
    result, pending, total = {}, [root], 0
    while pending:
        require(time.monotonic() < deadline, "evidence retention exceeded its time budget")
        directory = pending.pop()
        mode = directory.lstat()
        require(stat.S_ISDIR(mode.st_mode) and not
                getattr(mode, "st_file_attributes", 0) & 0x400,
                "evidence retention requires ordinary non-reparse directories")
        for path in sorted(directory.iterdir()):
            require(len(result) < 8192, "evidence retention exceeds its entry budget")
            mode = path.lstat()
            require(not stat.S_ISLNK(mode.st_mode) and not
                    getattr(mode, "st_file_attributes", 0) & 0x400,
                    "evidence retention refuses links and reparse points")
            name = path.relative_to(root).as_posix()
            if stat.S_ISDIR(mode.st_mode):
                result[name] = {"kind": "directory"}
                pending.append(path)
            else:
                require(stat.S_ISREG(mode.st_mode), "evidence retention refuses special files")
                item = record(path)
                total += item["bytes"]
                require(total <= 1024**3, "evidence retention exceeds its byte budget")
                result[name] = {"kind": "file", **item}
    return dict(sorted(result.items()))


def copy_retained_file(source: Path, destination: Path, expected: dict,
                       deadline: float) -> None:
    copied = 0
    with source.open("rb") as incoming, destination.open("xb") as outgoing:
        while True:
            require(time.monotonic() < deadline, "evidence retention exceeded its time budget")
            data = incoming.read(128 * 1024)
            if not data:
                break
            copied += len(data)
            require(copied <= expected["bytes"], "source evidence grew during retention")
            outgoing.write(data)
    require(copied == expected["bytes"], "source evidence shrank during retention")


def retain_windows_evidence(source: Path, destination: Path) -> dict:
    mode = source.lstat()
    require(stat.S_ISDIR(mode.st_mode) and not
            getattr(mode, "st_file_attributes", 0) & 0x400,
            "execution workspace must be an ordinary non-reparse directory")
    source = source.resolve(strict=True)
    destination = destination.parent.resolve(strict=True) / destination.name
    require(source != destination and source not in destination.parents and
            destination not in source.parents, "execution and retained evidence must be separate")
    deadline = time.monotonic() + 120
    identity = source.stat()
    before = retention_inventory(source, deadline)
    destination.mkdir()  # A prior evidence tree is never adopted or replaced.
    for name, item in before.items():
        target = destination / name
        if item["kind"] == "directory":
            target.mkdir()
        else:
            copy_retained_file(source / name, target, item, deadline)
    require(retention_inventory(destination, deadline) == before,
            "retained evidence bytes or namespace differ from execution evidence")
    require(retention_inventory(source, deadline) == before,
            "execution evidence changed during retention")
    after = source.stat()
    require((identity.st_dev, identity.st_ino) == (after.st_dev, after.st_ino),
            "execution workspace changed during retention")
    return {"status": "PASS", "execution_workspace": str(source),
            "retained_copy": str(destination), "original_workspace_retained": True,
            "files_and_directories": before,
            "file_count": sum(item["kind"] == "file" for item in before.values()),
            "byte_count": sum(item.get("bytes", 0) for item in before.values())}


def summary_bytes(output: bytes) -> bytes:
    # Only human-readable helper summaries pass through this function. Binary
    # fixture bytes, child output contracts, hashes and retained logs do not.
    return output.replace(b"\r\n", b"\n") if WINDOWS else output


def sdk_tools(sdk: Path, manifest: dict) -> dict[str, Path]:
    result = {}
    for item in manifest["tools"]:
        name = item["name"]
        expected = name + (".exe" if WINDOWS else "")
        require(item["path"] == expected and name not in result,
                "manifest tools must be unique canonical root executables")
        result[name] = sdk / item["path"]
    return result


def check_windows_oracle(oracle: dict) -> None:
    fixed = {"bytes": "exact", "filesystem": "local-NTFS", "remaining_staging_files": 0,
             "lock_after_process_completion": "released", "lock_primitive": "LockFileEx-byte-0-length-1",
             "private_directories": ["private"], "windows_owner": "effective-user",
             "windows_dacl": "one-FILE_ALL_ACCESS-owner-allow-ACE"}
    require(isinstance(oracle, dict) and
            set(oracle) == set(fixed) | {"windows_acl_profiles", "windows_acl_sha256"} and
            all(oracle[key] == value for key, value in fixed.items()),
            "Windows durable host filesystem oracle differs")
    entries = {".", "private", "checkpoint.bin", "empty.bin", "writer.lock"}
    profiles, seals = oracle["windows_acl_profiles"], oracle["windows_acl_sha256"]
    require(isinstance(profiles, dict) and set(profiles) == entries and
            all(profile in ("protected-owner", "inherited-owner") for profile in profiles.values()),
            "Windows oracle omitted an owner-only ACL profile")
    require(isinstance(seals, dict) and set(seals) == entries and
            all(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) for value in seals.values()),
            "Windows oracle omitted an entry security seal")


def check_rows(rows: list, expected: set, keys: tuple[str, ...], label: str) -> None:
    require(isinstance(rows, list) and len(rows) == len(expected), label + " count differs")
    require(all(isinstance(row, dict) and row.get("status") == "PASS" for row in rows),
            label + " has a non-PASS result")
    require({tuple(row.get(key) for key in keys) for row in rows} == expected,
            label + " has missing or duplicate cases")


def check_durable(report: dict, sdk: Path, library: Path, runtime: Path,
                  manifest: dict, work: Path) -> None:
    require(report.get("schema") == "nil.durable-file-io.v1" and
            report.get("status") == "PASS" and report.get("llvm_enabled") is True and
            report.get("cleanup_verified") is True, "durable contract did not qualify")
    require(report.get("scope") == ("WINDOWS_NTFS_SOURCE_RUNTIME_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"
            if WINDOWS else "POSIX_SOURCE_RUNTIME_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"),
            "durable contract has the wrong platform qualification scope")
    check_rows(report.get("executions"), EXECUTIONS, ("route", "language"), "durable executions")
    check_rows(report.get("negative_abi"), NEGATIVE_ABI, ("case", "route"), "ABI rejections")
    check_rows(report.get("negative_library"), NEGATIVE_LIBRARY,
               ("operation", "route", "schema"), "library rejections")
    require(all(row.get("filesystem") == "unchanged" for row in report["negative_library"]),
            "library rejection changed its filesystem")
    oracle = {"bytes": "exact", "modes": "0600", "remaining_staging_files": 0,
              "lock_after_process_completion": "released", "private_directories": ["private"],
              "directory_modes": "0700"}
    if WINDOWS:
        for row in report["executions"]:
            check_windows_oracle(row.get("oracle"))
        for row in report["negative_library"]:
            seals = row.get("windows_acl_sha256")
            require(isinstance(seals, dict) and set(seals) == {"file", "directory"} and
                    all(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) for value in seals.values()),
                    "Windows library rejection omitted unchanged ACL evidence")
    else:
        require(all(row.get("oracle") == oracle for row in report["executions"]),
                "durable host filesystem oracle differs")
    selected = report.get("selected_native_runtime")
    require(isinstance(selected, str) and
            (Path(selected).resolve() == runtime.resolve() if WINDOWS else selected == str(runtime)),
            "durable compiler selected a runtime outside this SDK")
    before = report.get("inputs_before")
    require(isinstance(before, dict) and before == report.get("inputs_after"),
            "durable inputs changed")
    sources = {"runner": ROOT / "tests/tools/durable_file_io_contract.py",
               "ny": ROOT / "tests/language/durable-file-io.ny",
               "matrix": ROOT / "tests/matrix/io/durable-file-io.matrix"}
    for case in ("width", "owner", "query-width", "query-owner"):
        suffix = "query-invalid-" + case[6:] if case.startswith("query-") else "invalid-" + case
        sources[case] = ROOT / "tests/language" / ("durable-file-io-" + suffix + ".ny")
    if WINDOWS:
        sources["windows_oracle"] = ROOT / "tests/tools/windows_durable_oracle.py"
    expected_inputs = {key: record(path) for key, path in sources.items()}
    tool_records = {item["name"]: item for item in manifest["tools"]}
    for key, item in (("compiler", tool_records["nilcompiler"]),
                      ("interpreter", tool_records["nilinterpreter"]),
                      ("runtime", manifest["native_runtime"])):
        expected_inputs[key] = {field: item[field] for field in ("bytes", "sha256")}
    require(before == expected_inputs, "durable inputs do not match the installed SDK and current helpers")
    tools = sdk_tools(sdk, manifest)
    compiler, interpreter = str(tools["nilcompiler"]), str(tools["nilinterpreter"])
    expected_commands = {"runtime-selection": (False, compiler)}
    for language in ("ny", "matrix"):
        for encoding in ("text", "binary"):
            expected_commands[language + "-compile-" + encoding] = (False, compiler)
            expected_commands[language + "-" + encoding + "-vm"] = (False, interpreter)
        expected_commands[language + "-compile-aot"] = (False, compiler)
        expected_commands[language + "-aot"] = (False, str(work / (language + "-build") /
                                                       ("native.exe" if WINDOWS else "native")))
        expected_commands[language + "-jit"] = (False, compiler)
    for case in ("width", "owner", "query-width", "query-owner"):
        for encoding in ("text", "binary"):
            expected_commands["compile-invalid-" + case + "-" + encoding] = (False, compiler)
            expected_commands["invalid-" + case + "-" + encoding] = (True, interpreter)
        expected_commands["invalid-" + case] = (True, compiler)
    for operation in OPERATIONS:
        for encoding in ("text", "binary"):
            for schema in ("schema", "no-schema"):
                expected_commands["foreign-" + operation + "-" + encoding + "-" + schema] = (True, interpreter)
    commands = report.get("commands")
    require(isinstance(commands, list) and len(commands) == len(expected_commands) == 71,
            "durable command inventory differs")
    names = [row.get("name") for row in commands]
    require(set(names) == set(expected_commands), "missing or duplicate durable command names")
    for command in commands:
        rejected, executable = expected_commands[command["name"]]
        require(command.get("expected_rejection", False) is rejected and
                command.get("cleanup_verified") is True and not command.get("timed_out") and
                not command.get("error") and
                type(command.get("exit_code")) is int and
                (command["exit_code"] != 0 if rejected else command["exit_code"] == 0) and
                command.get("status") == ("FAIL" if rejected else "PASS"),
                "durable child execution/cleanup failed")
        argv = command.get("argv", [])
        require(bool(argv) and argv[0] == executable, "durable command used the wrong executable")
        if executable == compiler:
            require("--sdk-path" in argv and argv[argv.index("--sdk-path") + 1] == str(library),
                    "durable compiler used the wrong installed library")


def helper_inputs() -> list[Path]:
    paths = [Path(__file__), ROOT / "scripts/validate-sdk.py",
             ROOT / "scripts/check-sdk-routes.py", ROOT / "scripts/sdk_capabilities.py",
             ROOT / "tests/tools/durable_file_io_contract.py",
             ROOT / "tests/tools/native_console_contract.py",
             ROOT / "tests/tools/global_contract.py",
             ROOT / "tests/tools/ordinary_io_parity_contract.py",
             ROOT / "validation/ny-3d/scripts/debugger_process.py",
             ROOT / "tests/native_output_staging.py",
             ROOT / "tests/matrix/io/durable-file-io.matrix",
             ROOT / "tests/language/module-global-contract.ny",
             ROOT / "tests/language/module-global-contract.stdout"]
    paths.extend(sorted((ROOT / "tests/language").glob("durable-file-io*.ny")))
    paths.extend(sorted((ROOT / "tests/tools/fixtures").glob("console-input*.*")))
    if WINDOWS:
        paths.append(ROOT / "tests/tools/windows_durable_oracle.py")
    return paths


def qualify(args: argparse.Namespace, sdk: Path, work: Path) -> dict:
    logs = work / "logs"
    logs.mkdir()
    ownership = support.load_ownership(ROOT)
    # Python assertions are part of the existing console/global contracts. Do not let
    # caller optimization or module-path overrides disable/replace that proof.
    spec = importlib.util.spec_from_file_location("nil_installed_route_policy", ROOT / "scripts/check-sdk-routes.py")
    route_policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(route_policy)
    environment = route_policy.consumer_environment()
    for key in ("PYTHONOPTIMIZE", "PYTHONPATH", "PYTHONHOME"):
        environment.pop(key, None)
    environment.update(PYTHONDONTWRITEBYTECODE="1", PYTHONNOUSERSITE="1")
    temporary = work / "temporary"
    temporary.mkdir()
    environment["TMPDIR"] = str(temporary)
    if WINDOWS:
        environment.update(TMP=str(temporary), TEMP=str(temporary))
    deadline = time.monotonic() + 1800
    report = {"schema": "nil.sdk.durable-io.v1", "status": "FAIL",
              "release_declaration": False, "source_revision": args.expected_revision,
              "sdk": str(sdk), "require_rc_checks": args.require_rc_checks,
              "scope": ("INSTALLED_WINDOWS_NTFS_PROVIDER_REGRESSION_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"
                        if WINDOWS else
                        "INSTALLED_POSIX_PROVIDER_REGRESSION_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION"),
              "commands": [], "libraries": [], "errors": []}
    manifest_record = None
    identity = sdk.stat()
    inputs = helper_inputs()
    report["helpers_before"] = {str(path.relative_to(ROOT)): record(path) for path in inputs}
    validator = [sys.executable, str(ROOT / "scripts/validate-sdk.py"), str(sdk),
                 "--expected-revision", args.expected_revision]
    if args.require_rc_checks:
        validator.append("--require-rc-checks")

    def run(name: str, command: list, limit: int, *, final: bool = False) -> bytes:
        # The durable helper has its own 240-second deadline and owned child
        # sessions. Give it its entire outer budget, including cleanup, rather
        # than interrupting that owner early at a shortened shared deadline.
        require(final or deadline - time.monotonic() >= limit,
                "installed durable qualification has insufficient remaining budget")
        row = support.run_process(ownership, command, work, environment, logs, name, limit)
        report["commands"].append(row)
        require(row.get("status") == "PASS" and row.get("exit_code") == 0 and
                row.get("cleanup_verified") is True and not row.get("timed_out"),
                name + " failed; see retained logs")
        require((logs / row["stderr"]).read_bytes() == b"", name + " emitted stderr")
        return (logs / row["stdout"]).read_bytes()

    try:
        run("validate-before", validator, 180)
        manifest_path = sdk / "manifest.json"
        manifest_record = record(manifest_path)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        require(manifest["build"]["llvm"] == "ON", "installed durable checks require LLVM ON")
        report.update(sdk_manifest=manifest_record, payload_file_count=manifest["payload"]["file_count"],
                      installed_tools=manifest["tools"], native_runtime=manifest["native_runtime"])
        runtime = sdk / manifest["native_runtime"]["path"]
        tools = sdk_tools(sdk, manifest)
        for library_name in ("nilstd", "nillib"):
            library = sdk / library_name
            durable_output = work / (library_name + "-durable")
            durable_output.mkdir()
            stdout = run(library_name + "-durable", [sys.executable,
                ROOT / "tests/tools/durable_file_io_contract.py", "--compiler", tools["nilcompiler"],
                "--interpreter", tools["nilinterpreter"], "--nilstd", library,
                "--runtime", runtime, "--llvm", "ON", "--output", durable_output], 300)
            reports = list(durable_output.glob("run-*/report.json"))
            require(len(reports) == 1 and summary_bytes(stdout) == ("PASS: " + str(reports[0]) + "\n").encode(),
                    "durable helper did not report exactly one retained PASS result")
            durable = json.loads(reports[0].read_text(encoding="utf-8"))
            check_durable(durable, sdk, library, runtime, manifest, reports[0].parent)
            result = {"library": library_name, "durable_report": str(reports[0]),
                      "durable_report_record": record(reports[0]), "executions": 8,
                      "negative_abi": 12, "negative_library": 36,
                      "routes": sorted(route for route, _ in EXECUTIONS)}
            report["libraries"].append(result)
            stdout = run(library_name + "-console", [sys.executable,
                ROOT / "tests/tools/native_console_contract.py", "--compiler", tools["nilcompiler"],
                "--interpreter", tools["nilinterpreter"], "--assembler", tools["nilassembler"],
                "--linker", tools["nillinker"], "--runtime", runtime, "--stdlib", library], 240)
            # The unchanged harness owns its precise count (including compile
            # and rejection checks); record that count, not an invented number
            # of program executions. Extra output, SKIP and an empty run fail.
            matched = re.fullmatch(rb"native console contract: ([1-9][0-9]*) executions/checks passed\n", summary_bytes(stdout))
            require(matched is not None, "console helper did not report a complete PASS")
            result["console_executions_and_checks"] = int(matched.group(1))
            stdout = run(library_name + "-globals", [sys.executable,
                ROOT / "tests/tools/global_contract.py", "--compiler", tools["nilcompiler"],
                "--interpreter", tools["nilinterpreter"], "--assembler", tools["nilassembler"],
                "--linker", tools["nillinker"], "--runtime", runtime,
                "--nilstd", library, "--llvm", "ON"], 180)
            require(summary_bytes(stdout) == GLOBAL_SUMMARY, "global helper did not report every required PASS case")
            result.update(nominal_global_vm_executions=8,
                          nominal_global_initialization_rejections=2,
                          nominal_global_native_rejections=2, status="PASS")
    except Exception as error:
        report["errors"].append(str(error))
    finally:
        if manifest_record is not None:
            try:
                # The validator rechecks every payload hash against the same
                # manifest. Keep a separate bounded budget even after failure.
                run("validate-after", validator, 180, final=True)
                after = sdk.stat()
                require((identity.st_dev, identity.st_ino) == (after.st_dev, after.st_ino),
                        "physical SDK directory changed during qualification")
                require(record(sdk / "manifest.json") == manifest_record,
                        "SDK manifest changed during qualification")
                report["sdk_unchanged"] = True
            except Exception as error:
                report["errors"].append(str(error))
        try:
            report["helpers_after"] = {str(path.relative_to(ROOT)): record(path) for path in inputs}
            require(report["helpers_after"] == report["helpers_before"], "qualification helpers changed")
        except Exception as error:
            report["errors"].append(str(error))
        if not report["errors"] and len(report["libraries"]) == 2 and report.get("sdk_unchanged"):
            report.update(status="PASS", execution_count=16, negative_abi_count=24,
                          negative_library_count=72, nominal_global_vm_executions=16,
                          nominal_global_initialization_rejections=4,
                          nominal_global_native_rejections=4)
        report["cleanup_verified"] = bool(report["commands"]) and all(
            row.get("cleanup_verified") is True for row in report["commands"])
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--require-rc-checks", action="store_true")
    parser.add_argument("--output", type=Path, required=True,
                        help="evidence parent outside the SDK; always creates a fresh retained child")
    args = parser.parse_args()
    work, execution, report = None, None, None
    try:
        require((os.name == "posix" and sys.platform in ("linux", "darwin")) or
                (os.name == "nt" and sys.platform == "win32"),
                "installed durable qualification requires native Linux, macOS or Windows")
        require(not args.require_rc_checks or sys.platform == "linux", "RC checks require Linux")
        require(re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", args.expected_revision) is not None,
                "expected revision must be a full lowercase Git revision")
        sdk = args.sdk.resolve(strict=True)  # Pin the physical generation once.
        output = args.output.resolve()
        require(sdk != output and sdk not in output.parents, "evidence must be outside the SDK")
        output.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=output))
        execution = windows_execution_workspace() if WINDOWS else work
        report = qualify(args, sdk, execution)
        if WINDOWS:
            report["workspace_provenance"] = {
                "execution_workspace": str(execution), "requested_evidence": str(work),
                "execution_path_utf16_units": len(str(execution).encode("utf-16-le")) // 2,
                "execution_path_limit_utf16_units": WINDOWS_WORKSPACE_UNITS,
                "general_long_path_support_qualified": False,
                "original_workspace_retained": True}
            support.save_new(execution / "report.json", report)
            report["evidence_retention"] = retain_windows_evidence(execution, work / "execution")
        support.save_new(work / "report.json", report)
        print(report["status"] + ": " + str(work / "report.json"))
        return 0 if report["status"] == "PASS" else 1
    except Exception as error:
        if work is not None and not (work / "report.json").exists():
            failed = report if report is not None else {
                "schema": "nil.sdk.durable-io.v1", "release_declaration": False,
                "source_revision": args.expected_revision, "cleanup_verified": False}
            failed.update(status="FAIL", error=str(error),
                          execution_workspace=str(execution) if execution is not None else None)
            support.save_new(work / "report.json", failed)
        print("installed durable qualification failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
