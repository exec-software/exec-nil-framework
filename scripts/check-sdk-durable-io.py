#!/usr/bin/env python3
"""Qualify installed durable I/O and console providers on POSIX hosts.

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
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/tools"))
import ordinary_io_parity_contract as support

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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def record(path: Path) -> dict:
    return support.file_record(path)


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
    check_rows(report.get("executions"), EXECUTIONS, ("route", "language"), "durable executions")
    check_rows(report.get("negative_abi"), NEGATIVE_ABI, ("case", "route"), "ABI rejections")
    check_rows(report.get("negative_library"), NEGATIVE_LIBRARY,
               ("operation", "route", "schema"), "library rejections")
    require(all(row.get("filesystem") == "unchanged" for row in report["negative_library"]),
            "library rejection changed its filesystem")
    oracle = {"bytes": "exact", "modes": "0600", "remaining_staging_files": 0,
              "lock_after_process_completion": "released", "private_directories": ["private"],
              "directory_modes": "0700"}
    require(all(row.get("oracle") == oracle for row in report["executions"]),
            "durable host filesystem oracle differs")
    require(report.get("selected_native_runtime") == str(runtime),
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
    expected_inputs = {key: record(path) for key, path in sources.items()}
    tool_records = {item["name"]: item for item in manifest["tools"]}
    for key, item in (("compiler", tool_records["nilcompiler"]),
                      ("interpreter", tool_records["nilinterpreter"]),
                      ("runtime", manifest["native_runtime"])):
        expected_inputs[key] = {field: item[field] for field in ("bytes", "sha256")}
    require(before == expected_inputs, "durable inputs do not match the installed SDK and current helpers")
    compiler, interpreter = str(sdk / "nilcompiler"), str(sdk / "nilinterpreter")
    expected_commands = {"runtime-selection": (False, compiler)}
    for language in ("ny", "matrix"):
        for encoding in ("text", "binary"):
            expected_commands[language + "-compile-" + encoding] = (False, compiler)
            expected_commands[language + "-" + encoding + "-vm"] = (False, interpreter)
        expected_commands[language + "-compile-aot"] = (False, compiler)
        expected_commands[language + "-aot"] = (False, str(work / (language + "-build") / "native"))
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
             ROOT / "tests/tools/ordinary_io_parity_contract.py",
             ROOT / "validation/ny-3d/scripts/debugger_process.py",
             ROOT / "tests/native_output_staging.py",
             ROOT / "tests/matrix/io/durable-file-io.matrix"]
    paths.extend(sorted((ROOT / "tests/language").glob("durable-file-io*.ny")))
    paths.extend(sorted((ROOT / "tests/tools/fixtures").glob("console-input*.*")))
    return paths


def qualify(args: argparse.Namespace, sdk: Path, work: Path) -> dict:
    logs = work / "logs"
    logs.mkdir()
    ownership = support.load_ownership(ROOT)
    # Python assertions are part of the existing console contract. Do not let
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
    deadline = time.monotonic() + 1200
    report = {"schema": "nil.sdk.durable-io.v1", "status": "FAIL",
              "release_declaration": False, "source_revision": args.expected_revision,
              "sdk": str(sdk), "require_rc_checks": args.require_rc_checks,
              "scope": "INSTALLED_POSIX_PROVIDER_REGRESSION_NOT_RELEASE_OR_POWER_CUT_QUALIFICATION",
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
        for library_name in ("nilstd", "nillib"):
            library = sdk / library_name
            durable_output = work / (library_name + "-durable")
            durable_output.mkdir()
            stdout = run(library_name + "-durable", [sys.executable,
                ROOT / "tests/tools/durable_file_io_contract.py", "--compiler", sdk / "nilcompiler",
                "--interpreter", sdk / "nilinterpreter", "--nilstd", library,
                "--runtime", runtime, "--llvm", "ON", "--output", durable_output], 300)
            reports = list(durable_output.glob("run-*/report.json"))
            require(len(reports) == 1 and stdout == ("PASS: " + str(reports[0]) + "\n").encode(),
                    "durable helper did not report exactly one retained PASS result")
            durable = json.loads(reports[0].read_text(encoding="utf-8"))
            check_durable(durable, sdk, library, runtime, manifest, reports[0].parent)
            result = {"library": library_name, "durable_report": str(reports[0]),
                      "durable_report_record": record(reports[0]), "executions": 8,
                      "negative_abi": 12, "negative_library": 36,
                      "routes": sorted(route for route, _ in EXECUTIONS)}
            report["libraries"].append(result)
            stdout = run(library_name + "-console", [sys.executable,
                ROOT / "tests/tools/native_console_contract.py", "--compiler", sdk / "nilcompiler",
                "--interpreter", sdk / "nilinterpreter", "--assembler", sdk / "nilassembler",
                "--linker", sdk / "nillinker", "--runtime", runtime, "--stdlib", library], 240)
            # The unchanged harness owns its precise count (including compile
            # and rejection checks); record that count, not an invented number
            # of program executions. Extra output, SKIP and an empty run fail.
            matched = re.fullmatch(rb"native console contract: ([1-9][0-9]*) executions/checks passed\n", stdout)
            require(matched is not None, "console helper did not report a complete PASS")
            result.update(console_executions_and_checks=int(matched.group(1)), status="PASS")
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
                          negative_library_count=72)
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
    work = None
    try:
        require(os.name == "posix" and sys.platform in ("linux", "darwin"),
                "installed durable qualification supports native Linux/macOS only; Windows is unsupported")
        require(not args.require_rc_checks or sys.platform == "linux", "RC checks require Linux")
        require(re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", args.expected_revision) is not None,
                "expected revision must be a full lowercase Git revision")
        sdk = args.sdk.resolve(strict=True)  # Pin the physical generation once.
        output = args.output.resolve()
        require(sdk != output and sdk not in output.parents, "evidence must be outside the SDK")
        output.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=output))
        report = qualify(args, sdk, work)
        support.save_new(work / "report.json", report)
        print(report["status"] + ": " + str(work / "report.json"))
        return 0 if report["status"] == "PASS" else 1
    except Exception as error:
        if work is not None and not (work / "report.json").exists():
            support.save_new(work / "report.json", {
                "schema": "nil.sdk.durable-io.v1", "status": "FAIL",
                "release_declaration": False, "source_revision": args.expected_revision,
                "error": str(error), "cleanup_verified": False})
        print("installed durable qualification failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
