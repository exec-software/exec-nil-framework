#!/usr/bin/env python3
"""Evidence for technical SDK checks; never a language release declaration."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

from sdk_capabilities import capability_errors, collect_capabilities, record_capabilities
from sdk_runtime import STATIC_NAMES, SHARED_NAMES, RUNTIME_PAIRS

TOOLS = ("nilcompiler", "nilinterpreter", "nilassembler", "nillinker", "nillsp",
         "nildebugger", "nilschema", "nilagent")
DIGEST = re.compile(r"[0-9a-f]{64}")


def executable(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def command(arguments: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(arguments, cwd=cwd, check=False, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=60)
    if result.returncode:
        raise ValueError("qualification command failed: {}: {}".format(
            arguments[0], result.stderr.strip()))
    return result.stdout.strip()


def source_identity(repo: Path) -> dict[str, Any]:
    revision = command(["git", "rev-parse", "--verify", "HEAD"], repo)
    status = command(["git", "status", "--porcelain=v1", "--untracked-files=all"], repo)
    # Include uncommitted contents, not just a dirty boolean, so development
    # evidence also detects edits between testing and packaging.
    changes = subprocess.run(["git", "diff", "--binary", "HEAD"], cwd=repo,
                             check=True, capture_output=True, timeout=60).stdout
    fingerprint = hashlib.sha256(revision.encode() + b"\0" + changes)
    untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard", "-z"],
                              cwd=repo, check=True, capture_output=True, timeout=60).stdout
    for raw_name in sorted(filter(None, untracked.split(b"\0"))):
        path = repo / os.fsdecode(raw_name)
        fingerprint.update(b"\0" + raw_name + b"\0")
        if path.is_symlink():
            fingerprint.update(os.fsencode(os.readlink(path)))
        else:
            fingerprint.update(digest(path).encode())
    return {"revision": revision, "dirty": bool(status),
            "fingerprint": fingerprint.hexdigest()}


def cmake_value(document: str, name: str) -> str:
    match = re.search(r"^set\(" + re.escape(name) + r'\s+"?([^"\)\r\n]*)"?\)',
                      document, re.MULTILINE)
    return match.group(1).strip() if match else ""


def build_configuration(repo: Path, build: Path) -> dict[str, Any]:
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([^/#][^:]*):[^=]+=(.*)", line)
        if match:
            cache[match.group(1)] = match.group(2)
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != repo.resolve():
        raise ValueError("build cache belongs to a different source directory")
    version = ".".join(cache.get("CMAKE_CACHE_{}_VERSION".format(part), "")
                       for part in ("MAJOR", "MINOR", "PATCH"))
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is None:
        raise ValueError("build cache does not identify its configured CMake version")
    configuration = build / "CMakeFiles" / version
    compiler = (configuration / "CMakeCXXCompiler.cmake").read_text(encoding="utf-8")
    system = (configuration / "CMakeSystem.cmake").read_text(encoding="utf-8")
    llvm_config = Path(cache.get("LLVM_DIR", "")) / "LLVMConfig.cmake"
    llvm_version = (cmake_value(llvm_config.read_text(encoding="utf-8"), "LLVM_PACKAGE_VERSION")
                    if llvm_config.is_file() else None)
    return {
        "type": cache.get("CMAKE_BUILD_TYPE", ""),
        "llvm_requested": cache.get("NIL_ENABLE_LLVM", cache.get("NY_ENABLE_LLVM", "OFF")),
        "tests_requested": cache.get("BUILD_TESTING", "OFF"),
        "cmake_version": version,
        "cxx_id": cmake_value(compiler, "CMAKE_CXX_COMPILER_ID"),
        "cxx_version": cmake_value(compiler, "CMAKE_CXX_COMPILER_VERSION"),
        "cxx_path": cmake_value(compiler, "CMAKE_CXX_COMPILER"),
        "llvm_version": llvm_version,
        "system": cmake_value(system, "CMAKE_SYSTEM_NAME"),
        "machine": cmake_value(system, "CMAKE_SYSTEM_PROCESSOR"),
        "cross_compiling": cmake_value(system, "CMAKE_CROSSCOMPILING") != "FALSE",
    }


def record_build(repo: Path, build: Path, revision: str, tests: bool) -> dict[str, Any]:
    source = source_identity(repo)
    if source["revision"] != revision:
        raise ValueError("build evidence revision does not match HEAD")
    configuration = build_configuration(repo, build)
    if configuration["tests_requested"] != ("ON" if tests else "OFF"):
        raise ValueError("test setting does not match the configured CMake BUILD_TESTING value")
    runtime = [build / "lib" / name for name in STATIC_NAMES
               if (build / "lib" / name).is_file()]
    if len(runtime) != 1:
        raise ValueError("build evidence requires exactly one native runtime archive")
    shared = [build / directory / name for directory in ("lib", "bin") for name in SHARED_NAMES
              if (build / directory / name).is_file()]
    if len(shared) != 1 or runtime[0].name not in RUNTIME_PAIRS.get(shared[0].name, ()):
        raise ValueError("build evidence requires one matching shared native runtime")
    registered = []
    listing = None
    if tests:
        listing = json.loads(command(["ctest", "--test-dir", str(build), "--show-only=json-v1"]))
        registered = sorted(test["name"] for test in listing["tests"])
        if not registered or len(set(registered)) != len(registered):
            raise ValueError("qualification requires a nonempty unique registered test set")
    capabilities = record_capabilities(repo, build, listing)
    return {"schema": "nil.sdk.build-evidence.v1", "recorded_ns": time.time_ns(),
            "source": source, "build": configuration,
            "tests_enabled": tests, "registered_tests": registered,
            "tools": {name: digest(build / "bin" / executable(name)) for name in TOOLS},
            "runtime_sha256": digest(runtime[0]), "shared_runtime_sha256": digest(shared[0]),
            "capabilities": capabilities}


def summarize_ctest(report: Path, names: list[str], recorded_ns: int) -> dict[str, Any]:
    if report.stat().st_mtime_ns < recorded_ns:
        raise ValueError("CTest report predates this build evidence")
    if report.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("CTest report exceeds 16 MiB")
    raw = report.read_bytes()
    if b"<!DOCTYPE" in raw or b"<!ENTITY" in raw:
        raise ValueError("CTest report must not contain XML entity declarations")
    root = ET.fromstring(raw)
    cases = list(root.iter("testcase"))
    actual_names = sorted(case.get("name", "") for case in cases)
    if actual_names != sorted(names) or len(set(actual_names)) != len(actual_names):
        raise ValueError("CTest report does not cover exactly the registered tests")
    summary = {"tests": len(cases), "passed": 0, "failed": 0, "errors": 0, "skipped": 0}
    for case in cases:
        if case.find("failure") is not None:
            summary["failed"] += 1
        elif case.find("error") is not None:
            summary["errors"] += 1
        elif case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
            summary["skipped"] += 1
        else:
            summary["passed"] += 1
    summary["report_sha256"] = hashlib.sha256(raw).hexdigest()
    summary["registered_names_sha256"] = hashlib.sha256(
        json.dumps(actual_names, separators=(",", ":")).encode()).hexdigest()
    return summary


def native_backend_available(assembler: Path) -> bool:
    with tempfile.TemporaryDirectory(prefix="nil-sdk-backend-probe-") as temporary:
        root = Path(temporary)
        source, output = root / "probe.nil", root / "probe.o"
        source.write_text("module root\nfunction main -> i32:\nPUSH_CONST 0\nRET\nend function\n",
                          encoding="utf-8")
        result = subprocess.run([str(assembler), "-o", str(output), str(source)],
                                cwd=root, check=False, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=60)
        if result.returncode == 0 and output.is_file() and output.stat().st_size > 0:
            return True
        if result.returncode > 0 and "no LLVM backend" in result.stderr and not output.exists():
            return False
        raise ValueError("native backend capability probe failed: " + result.stderr.strip())


def collect_qualification(repo: Path, stage: Path, runtime: Path, revision: str,
                          mode: str, evidence_path: Path | None,
                          report: Path | None, *, include_capabilities: bool = False,
                          shared_runtime: Path | None = None) -> dict[str, Any]:
    evidence = None
    snapshot = None
    if evidence_path is not None:
        snapshot = json.loads(evidence_path.read_text(encoding="utf-8"))
        if not isinstance(snapshot, dict) or snapshot.get("schema") not in (
                "ny.sdk.build-evidence.v1", "ny.sdk.build-evidence.v2", "nil.sdk.build-evidence.v1"):
            raise ValueError("unsupported build evidence schema")
        current = source_identity(repo)
        if snapshot["source"] != current or current["revision"] != revision:
            raise ValueError("source identity changed after the tested build snapshot")
        expected_tools = {name: digest(stage / executable(name)) for name in TOOLS}
        if snapshot["tools"] != expected_tools or snapshot["runtime_sha256"] != digest(runtime):
            raise ValueError("installed tools/runtime differ from the tested build snapshot")
        if shared_runtime is not None and snapshot.get("shared_runtime_sha256") != digest(shared_runtime):
            raise ValueError("installed shared runtime differs from the tested build snapshot")
        if snapshot.get("schema") == "nil.sdk.build-evidence.v1" and shared_runtime is None:
            raise ValueError("NIL build evidence requires its shared runtime")
        tests = snapshot["tests_enabled"]
        if tests and report is None:
            raise ValueError("tested build evidence requires its CTest JUnit report")
        if not tests and report is not None:
            raise ValueError("test-skipped build must not claim a CTest report")
        evidence = {"source": current, "build": snapshot["build"],
                    "tests_enabled": tests, "tools": expected_tools,
                    "runtime_sha256": snapshot["runtime_sha256"],
                    "ctest": summarize_ctest(report, snapshot["registered_tests"],
                                              snapshot["recorded_ns"]) if tests else None}
        if shared_runtime is not None:
            evidence["shared_runtime_sha256"] = snapshot["shared_runtime_sha256"]
    elif report is not None:
        raise ValueError("CTest report requires a pre-test build evidence snapshot")
    result = {"mode": mode,
            "compiler_version": command([str(stage / executable("nilcompiler")), "--version"]),
            "native_backend_available": native_backend_available(stage / executable("nilassembler")),
            "evidence": evidence}
    if include_capabilities:
        result["capabilities"] = collect_capabilities(repo, snapshot, report, evidence, mode == "rc-checks")
    return result


def qualification_errors(manifest: dict[str, Any], require_rc: bool = False,
                         expected_revision: str | None = None) -> list[str]:
    errors = []
    source = manifest.get("source") if isinstance(manifest.get("source"), dict) else {}
    build = manifest.get("build") if isinstance(manifest.get("build"), dict) else {}
    runtime = manifest.get("native_runtime") if isinstance(manifest.get("native_runtime"), dict) else {}
    if expected_revision is not None and source.get("revision") != expected_revision:
        errors.append("SDK source revision differs from the expected final revision")
    if manifest.get("schema") == "ny.sdk.v1":
        if require_rc:
            errors.append("legacy ny.sdk.v1 manifests are development-only; RC evidence is missing")
        return errors
    record = manifest.get("qualification")
    expected_record = {"mode", "compiler_version", "native_backend_available", "evidence"}
    if manifest.get("schema") in ("ny.sdk.v3", "nil.sdk.v1"):
        expected_record.add("capabilities")
    if not isinstance(record, dict) or set(record) != expected_record:
        return errors + ["qualification must contain the complete SDK qualification record"]
    mode = record.get("mode")
    if mode not in ("development", "rc-checks"):
        errors.append("qualification.mode must be development or rc-checks")
    if require_rc and mode != "rc-checks":
        errors.append("SDK is development-only, not marked for technical RC checks")
    strict = require_rc or mode == "rc-checks"
    if manifest.get("schema") in ("ny.sdk.v3", "nil.sdk.v1"):
        errors.extend(capability_errors(record.get("capabilities"), record.get("evidence"), strict))
    if not isinstance(record.get("compiler_version"), str) or re.fullmatch(
            r"nilcompiler \S+", record["compiler_version"]) is None:
        errors.append("qualification.compiler_version must identify the actual NIL compiler")
    if not isinstance(record.get("native_backend_available"), bool):
        errors.append("qualification.native_backend_available must be boolean")
    elif build.get("llvm") != (
            "ON" if record["native_backend_available"] else "OFF"):
        errors.append("build.llvm must describe actual backend availability")
    if strict:
        if source.get("worktree_dirty") is not False:
            errors.append("RC SDK checks require clean source")
        if manifest.get("host") != {"system": "Linux", "machine": "x86_64"}:
            errors.append("RC SDK checks currently support only Linux x86_64")
        if build.get("type") != "Release":
            errors.append("RC SDK checks require Release build type")
        if record.get("native_backend_available") is not True:
            errors.append("RC SDK checks require an actually available LLVM native backend")
    evidence = record.get("evidence")
    if evidence is None:
        if strict:
            errors.append("RC SDK checks require final-revision build and test evidence")
        return errors
    evidence_keys = {"source", "build", "tests_enabled", "tools", "runtime_sha256", "ctest"}
    if manifest.get("schema") == "nil.sdk.v1":
        evidence_keys.add("shared_runtime_sha256")
    if not isinstance(evidence, dict) or set(evidence) != evidence_keys:
        return errors + ["qualification evidence fields are incomplete"]
    identity = evidence.get("source")
    if not isinstance(identity, dict) or set(identity) != {"revision", "dirty", "fingerprint"}:
        errors.append("qualification source identity is incomplete")
    elif (identity["revision"] != source.get("revision") or
          identity["dirty"] != source.get("worktree_dirty") or
          not isinstance(identity["dirty"], bool) or
          not isinstance(identity["fingerprint"], str) or
          DIGEST.fullmatch(identity["fingerprint"]) is None):
        errors.append("qualification source identity differs from the manifest")
    tools = manifest.get("tools") if isinstance(manifest.get("tools"), list) else []
    expected_tools = {tool.get("name"): tool.get("sha256") for tool in tools
                      if isinstance(tool, dict) and isinstance(tool.get("name"), str)}
    if evidence.get("tools") != expected_tools or set(expected_tools) != set(TOOLS):
        errors.append("qualification tool hashes differ from the packaged tools")
    if evidence.get("runtime_sha256") != runtime.get("sha256"):
        errors.append("qualification runtime hash differs from the packaged runtime")
    if manifest.get("schema") == "nil.sdk.v1":
        shared = manifest.get("native_shared_runtime")
        if not isinstance(shared, dict) or evidence.get("shared_runtime_sha256") != shared.get("sha256"):
            errors.append("qualification shared runtime hash differs from the packaged shared runtime")
    configuration = evidence.get("build")
    if not isinstance(configuration, dict) or set(configuration) != {
            "type", "llvm_requested", "tests_requested", "cmake_version", "cxx_id", "cxx_version", "cxx_path",
            "llvm_version", "system", "machine", "cross_compiling"}:
        errors.append("qualification build configuration is incomplete")
    else:
        for name in ("type", "cmake_version", "cxx_id", "cxx_version", "cxx_path", "system", "machine"):
            if not isinstance(configuration[name], str) or not configuration[name]:
                errors.append("qualification build {} must be nonempty".format(name))
        if configuration["type"] != build.get("type"):
            errors.append("qualification build type differs from manifest")
        if configuration["llvm_requested"] not in ("ON", "OFF"):
            errors.append("qualification requested LLVM setting is invalid")
        if configuration["tests_requested"] != ("ON" if evidence.get("tests_enabled") is True else "OFF"):
            errors.append("qualification test setting differs from configured CMake BUILD_TESTING")
        if not isinstance(configuration["cross_compiling"], bool):
            errors.append("qualification cross-compilation flag must be boolean")
        if configuration["llvm_version"] is not None and not isinstance(configuration["llvm_version"], str):
            errors.append("qualification LLVM version must be a string or null")
        if strict and (configuration["system"] != "Linux" or configuration["machine"] != "x86_64"
                       or configuration["cross_compiling"] is not False):
            errors.append("RC SDK build evidence must describe a native Linux x86_64 build")
        if strict and (configuration["llvm_requested"] != "ON" or not configuration["llvm_version"]):
            errors.append("RC SDK evidence requires detected LLVM version and enabled backend")
    enabled = evidence.get("tests_enabled")
    if not isinstance(enabled, bool):
        errors.append("qualification tests_enabled must be boolean")
    if strict and enabled is not True:
        errors.append("RC SDK checks require tests ON")
    report = evidence.get("ctest")
    if not enabled and report is not None:
        errors.append("test-skipped evidence must not claim test results")
    if enabled:
        if not isinstance(report, dict) or set(report) != {
                "tests", "passed", "failed", "errors", "skipped", "report_sha256", "registered_names_sha256"}:
            errors.append("qualification CTest result summary is incomplete")
        else:
            counts = [report[name] for name in ("tests", "passed", "failed", "errors", "skipped")]
            if any(not isinstance(count, int) or isinstance(count, bool) or count < 0 for count in counts):
                errors.append("qualification CTest counts must be nonnegative integers")
            elif counts[0] != sum(counts[1:]) or counts[0] == 0:
                errors.append("qualification CTest counts do not cover a nonempty test suite")
            elif strict and (report["passed"] == 0 or report["failed"] or report["errors"]
                             or report["skipped"]):
                errors.append("RC SDK checks require a passing CTest suite without failures/errors/skips")
            for name in ("report_sha256", "registered_names_sha256"):
                if not isinstance(report[name], str) or DIGEST.fullmatch(report[name]) is None:
                    errors.append("qualification CTest {} must be SHA-256".format(name))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--tests", choices=("ON", "OFF"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    evidence = record_build(args.repo_root.resolve(), args.build_dir.resolve(),
                            args.revision, args.tests == "ON")
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        raise SystemExit("SDK qualification failed: " + str(error))
