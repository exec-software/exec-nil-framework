#!/usr/bin/env python3
"""Record macOS bring-up evidence without asserting SDK or platform parity."""
from __future__ import annotations

import collections
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True, timeout=30).strip()


def record(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def summarize(registration: dict | None, suite: ET.Element | None) -> dict:
    names = [test["name"] for test in registration["tests"]] if registration else []
    cases = []
    for case in suite.iter("testcase") if suite is not None else []:
        if case.find("failure") is not None or case.find("error") is not None:
            result = "failed"
        elif case.find("skipped") is not None:
            result = "skipped"
        elif case.get("status") == "notrun":
            result = "not_run"
        elif case.get("status") == "run":
            result = "passed"
        else:
            result = "unknown"
        cases.append({"name": case.get("name"), "result": result,
                      "status": case.get("status"), "seconds": case.get("time"),
                      "messages": [{"kind": child.tag, "text": child.text or "",
                                    "attributes": child.attrib}
                                   for child in case if child.tag in ("failure", "error", "skipped")]})
    observed = [case["name"] for case in cases]
    duplicate_names = sorted(name for name, count in collections.Counter(observed).items() if count > 1)
    duplicate_registration = sorted(name for name, count in collections.Counter(names).items() if count > 1)
    return {"scope": "macOS build and test bring-up; SDK qualification and platform parity not assessed",
            "registration_present": registration is not None, "junit_present": suite is not None,
            "registered": names, "observed": len(cases),
            "counts": dict(collections.Counter(case["result"] for case in cases)),
            "missing": sorted(set(names) - set(observed)),
            "unexpected": sorted(set(observed) - set(names)), "duplicates": duplicate_names,
            "registration_duplicates": duplicate_registration,
            "unknown_statuses": [{"name": case["name"], "status": case["status"]}
                                 for case in cases if case["status"] not in ("run", "notrun")],
            "cases": cases}


def inventory_errors(inventory: dict) -> list[str]:
    errors = []
    for key in ("unexpected", "duplicates", "registration_duplicates", "unknown_statuses"):
        if inventory[key]:
            errors.append(key + ": " + json.dumps(inventory[key], sort_keys=True))
    if any(case["result"] == "not_run" for case in inventory["cases"]):
        errors.append("CTest has unexecuted cases without an explicit skip or failure")
    if inventory["registration_present"] and not inventory["registered"]:
        errors.append("CTest registration contains zero tests")
    if inventory["junit_present"]:
        if not inventory["registration_present"]:
            errors.append("JUnit is present without valid CTest registration")
        if inventory["observed"] == 0:
            errors.append("CTest JUnit contains zero tests")
        if inventory["missing"]:
            errors.append("CTest JUnit inventory is incomplete")
    return errors


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    os.chdir(root)
    output = root / "build/macos-evidence"
    output.mkdir(parents=True, exist_ok=True)
    phase = sys.argv[1]
    revision = command("git", "rev-parse", "HEAD")
    if phase == "identity":
        expected = os.environ["GITHUB_SHA"]
        if revision != expected or command("git", "status", "--porcelain"):
            raise RuntimeError("Expected clean exact GitHub source revision")
        identity = {"revision": revision, "system": platform.system(), "machine": platform.machine(),
                    "runner": {name: os.environ.get(name) for name in (
                        "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion", "NIL_CI_RUNNER")},
                    "run": {name: os.environ.get(name) for name in (
                        "GITHUB_REPOSITORY", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_REF")},
                    "llvm": os.environ["NIL_CI_LLVM"],
                    "os_version": command("sw_vers"), "xcode": command("xcodebuild", "-version"),
                    "workflow": record(root / ".github/workflows/macos-bringup.yml"),
                    "collector": record(Path(__file__))}
        (output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
        return
    if phase != "collect":
        raise ValueError("Expected identity or collect")

    registration_path = output / "registration.json"
    junit_path = root / "build/ci/ctest-results.xml"
    errors = []
    registration = suite = None
    try:
        if registration_path.exists():
            registration = json.loads(registration_path.read_text())
            if (registration.get("kind") != "ctestInfo" or not isinstance(registration.get("tests"), list)
                    or any(not isinstance(test, dict) or not isinstance(test.get("name"), str)
                           or not test["name"] for test in registration["tests"])):
                raise ValueError("Unexpected CTest registration schema")
    except (ValueError, KeyError, TypeError, AttributeError) as error:
        errors.append(str(error))
        registration = None
    try:
        if junit_path.exists():
            suite = ET.parse(junit_path).getroot()
            if any(not case.get("name") for case in suite.iter("testcase")):
                raise ValueError("CTest JUnit has an unnamed testcase")
    except (ValueError, ET.ParseError, KeyError) as error:
        errors.append(str(error))
        suite = None
    inventory = summarize(registration, suite)
    errors.extend(inventory_errors(inventory))
    inventory["errors"] = errors
    inventory["revision_after"] = revision
    inventory["source_status_after"] = command("git", "status", "--porcelain")
    inventory["source_unchanged"] = revision == os.environ["GITHUB_SHA"] and not inventory["source_status_after"]
    (output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    for relative in ("CMakeCache.txt", "compile_commands.json", "ctest-results.xml",
                     "CMakeFiles/CMakeConfigureLog.yaml", "CMakeFiles/CMakeError.log",
                     "CMakeFiles/CMakeOutput.log", "Testing/Temporary/LastTest.log",
                     "Testing/Temporary/LastTestsFailed.log", "Testing/Temporary/CTestCostData.txt"):
        source = root / "build/ci" / relative
        if source.is_file():
            destination = output / "build" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
    binaries = {}
    # Preserve execute bits and dylib links inside a tar; artifact upload alone
    # does not preserve executable permissions. Partial outputs stay diagnostic.
    with tarfile.open(output / "build-products.tar.gz", "w:gz") as archive:
        for name in ("bin", "lib"):
            directory = root / "build/ci" / name
            if directory.is_dir():
                archive.add(directory, arcname=name)
                for path in sorted(directory.rglob("*")):
                    if path.is_file():
                        binaries[str(path.relative_to(root / "build/ci"))] = record(path)
    (output / "build-products.json").write_text(json.dumps(binaries, indent=2) + "\n")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
        summary.write("### macOS bring-up inventory\n\n")
        summary.write("Build/test diagnosis only. SDK qualification and platform parity are not assessed.\n\n")
        summary.write(f"Registered: {len(inventory['registered'])}; observed: {inventory['observed']}; "
                      f"results: `{json.dumps(inventory['counts'], sort_keys=True)}`.\n\n")
        for title, values in (("Failed", [case["name"] for case in inventory["cases"] if case["result"] == "failed"]),
                              ("Skipped", [case["name"] for case in inventory["cases"] if case["result"] == "skipped"]),
                              ("Missing", inventory["missing"]), ("Unexpected", inventory["unexpected"]),
                              ("Duplicate", inventory["duplicates"]), ("Evidence errors", errors)):
            if values:
                summary.write(title + ":\n\n" + "".join(f"- `{value}`\n" for value in values) + "\n")
    if errors or not inventory["source_unchanged"]:
        raise RuntimeError("Bring-up evidence or source identity is inconsistent")


if __name__ == "__main__":
    main()
