"""Small artifact-bound observations, deliberately not a language support roadmap."""

from __future__ import annotations

from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import time
from typing import Any
import xml.etree.ElementTree as ET


ROUTES = ["text-interpreter", "binary-interpreter", "bundle-text", "bundle-binary", "llvm-jit", "llvm-aot"]
DIGEST = re.compile(r"[0-9a-f]{64}")


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError("capability evidence: " + message)


def sha(value: Any) -> bool:
    return isinstance(value, str) and DIGEST.fullmatch(value) is not None


def strings(value: Any) -> bool:
    return isinstance(value, list) and all(isinstance(item, str) and item for item in value)


def validate_catalog(value: Any) -> None:
    require(isinstance(value, dict) and set(value) == {
        "schema", "release_declaration", "coverage", "routes", "groups", "proofs", "limitations"},
        "catalog fields are incomplete")
    require(value["schema"] == "ny.sdk.capability-catalog.v1" and
            value["release_declaration"] is False, "catalog is not an observed capability profile")
    require(isinstance(value["coverage"], str) and bool(value["coverage"]), "coverage statement is missing")
    require(value["routes"] == ROUTES, "catalog must identify exactly the six execution routes")
    require(strings(value["limitations"]) and bool(value["limitations"]), "catalog limitations are missing")
    require(isinstance(value["proofs"], list) and bool(value["proofs"]), "catalog proofs are missing")
    names = []
    for proof in value["proofs"]:
        require(isinstance(proof, dict) and set(proof) == {"test", "fixture", "stdlib"}, "invalid catalog proof")
        require(isinstance(proof["fixture"], str) and
                re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", proof["fixture"]) is not None and
                proof["test"] == "ny.routes." + proof["fixture"] and
                isinstance(proof["stdlib"], bool), "invalid proof fixture identity")
        names.append(proof["test"])
    require(len(set(names)) == len(names), "duplicate catalog proof")
    require(isinstance(value["groups"], list) and bool(value["groups"]), "catalog groups are missing")
    groups, referenced = [], set()
    for group in value["groups"]:
        require(isinstance(group, dict) and set(group) == {"id", "observed", "tests", "limitations"}, "invalid group")
        require(isinstance(group["id"], str) and re.fullmatch(r"[a-z0-9-]+", group["id"]) is not None and
                isinstance(group["observed"], str) and bool(group["observed"]) and
                strings(group["tests"]) and bool(group["tests"]) and
                len(set(group["tests"])) == len(group["tests"]) and
                set(group["tests"]).issubset(names) and strings(group["limitations"]) and
                bool(group["limitations"]), "invalid group observation, proof references, or limitations")
        groups.append(group["id"])
        referenced.update(group["tests"])
    require(len(set(groups)) == len(groups) and referenced == set(names), "catalog group coverage is inconsistent")


def load_catalog(repo: Path) -> dict[str, Any]:
    value = json.loads((repo / "scripts/sdk_capabilities.json").read_text(encoding="utf-8"))
    validate_catalog(value)
    return value


def expected_inputs(repo: Path, build: Path, proof: dict[str, Any]) -> dict[str, Any]:
    suffix = ".exe" if os.name == "nt" else ""
    sources = [repo / "tests/language" / (proof["fixture"] + extension)
               for extension in (".ny", ".matrix", ".mat")
               if (repo / "tests/language" / (proof["fixture"] + extension)).is_file()]
    require(len(sources) == 1, "proof fixture must have exactly one Ny or Matrix source")
    return {
        "compiler": str(build / "bin" / ("nilcompiler" + suffix)),
        "interpreter": str(build / "bin" / ("nilinterpreter" + suffix)),
        "source": str(sources[0]),
        "expected_stdout": str(repo / "tests/language" / (proof["fixture"] + ".stdout")),
        "expected_stderr": None, "expected_exit": 0, "routes": ROUTES,
        "timeout_seconds": 10.0,
        "compiler_args": ["--sdk-path", str(repo / "nilstd/src")] if proof["stdlib"] else [],
        "interpreter_args": [], "program_args": [], "text_nil_contains": [],
        "with_stdlib": False, "strict": False, "xfail_stderr_contains": {},
    }


def contract(repo: Path, build: Path, proof: dict[str, Any], argv: Any,
             *, with_hashes: bool = True) -> dict[str, Any]:
    """Require the exact small positive CTest contract, not arbitrary test-name reuse."""
    require(strings(argv) and len(argv) >= 2 and argv[1] == str(repo / "tests/routes/ny_route_matrix.py"),
            "proof command must run the route matrix harness")
    inputs = expected_inputs(repo, build, proof)
    options, extras = {}, []
    arguments = iter(argv[2:])
    for argument in arguments:
        if argument.startswith("--compiler-arg="):
            extras.append(argument.split("=", 1)[1])
            continue
        require(argument in {"--compiler", "--interpreter", "--source", "--expected-stdout",
                             "--expected-exit", "--routes", "--work-dir", "--report-json"} and
                argument not in options, "unexpected or duplicate positive proof option")
        value = next(arguments, None)
        require(isinstance(value, str), "proof option has no value")
        options[argument] = value
    report = build / "tests/route-reports" / (proof["test"] + ".json")
    expected = {"--compiler": inputs["compiler"], "--interpreter": inputs["interpreter"],
                "--source": inputs["source"], "--expected-stdout": inputs["expected_stdout"],
                "--expected-exit": "0", "--routes": "all", "--work-dir": str(build / "tests/routes"),
                "--report-json": str(report)}
    require(options == expected and extras == inputs["compiler_args"], "registered proof command differs from catalog")
    result = {"test": proof["test"], "command": argv, "inputs": inputs, "report_path": str(report)}
    if with_hashes:
        result.update(source_sha256=file_hash(Path(inputs["source"])),
                      expected_stdout_sha256=file_hash(Path(inputs["expected_stdout"])),
                      harness_sha256=file_hash(repo / "tests/routes/ny_route_matrix.py"))
    return result


def record_capabilities(repo: Path, build: Path, listing: dict[str, Any] | None) -> dict[str, Any]:
    catalog = load_catalog(repo)
    contracts = []
    if listing is not None:
        tests = {test["name"]: test for test in listing["tests"]}
        for proof in catalog["proofs"]:
            require(proof["test"] in tests, "required CTest proof is not registered: " + proof["test"])
            contracts.append(contract(repo, build, proof, tests[proof["test"]].get("command")))
    return {"catalog_sha256": canonical_hash(catalog), "repo_root": str(repo),
            "build_root": str(build), "contracts": contracts}


def check_names(route: str) -> list[str]:
    names = ["run." + part for part in ("spawn", "timeout", "exit", "stdout", "stderr")]
    if route != "llvm-jit":
        names += ["compile." + part for part in ("spawn", "timeout", "exit", "stdout", "stderr")]
        names += ["artifact.valid"]
    return sorted(names)


def command_argv(inputs: dict[str, Any], route: str, directory: Path) -> dict[str, list[str]]:
    compiler = [inputs["compiler"], "--no-stdlib"]
    filename = "program"
    if route in ("text-interpreter", "binary-interpreter"):
        compiler += ["--no-bundle"]
        filename = "program.binary.nil" if route == "binary-interpreter" else "program.nil"
    if route in ("binary-interpreter", "bundle-binary"):
        compiler += ["--emit-binary-nil"]
    if route in ("llvm-jit", "llvm-aot"):
        compiler += ["--native"]
    if route == "llvm-jit":
        compiler += ["--jit"]
        filename = "program.jit.nil"
    output = str(directory / filename)
    compiler += inputs["compiler_args"] + ["--quiet", "--output", output, inputs["source"]]
    if route == "llvm-jit":
        return {"compile_and_run": compiler + ["--"]}
    runner = [inputs["interpreter"], output, "--"] if route.endswith("interpreter") else [output]
    if os.name == "nt" and route in ("bundle-text", "bundle-binary", "llvm-aot"):
        runner[0] += ".exe"
    return {"compile": compiler, "run": runner}


def read_proof(item: dict[str, Any], recorded_ns: int) -> dict[str, Any]:
    path = Path(item["report_path"])
    require(not path.is_symlink() and path.is_file(), "route report is missing or a symlink")
    stat = path.stat()
    require(stat.st_mtime_ns >= recorded_ns and stat.st_size <= 16 * 1024 * 1024,
            "route report is stale or too large")
    raw = path.read_bytes()
    report = json.loads(raw)
    require(isinstance(report, dict) and report.get("schema") == "ny.route-matrix.v1", "invalid route report schema")
    require(canonical_hash(report.get("inputs")) == canonical_hash(item["inputs"]) and report.get("report_path") == str(path),
            "route report inputs or path differ from the registered proof")
    try:
        created = datetime.fromisoformat(report["created_at"])
        require(created.tzinfo is not None, "route report timestamp lacks timezone")
        timestamp = created.timestamp()
    except (KeyError, TypeError, ValueError, OverflowError) as error:
        raise ValueError("capability evidence: invalid route report timestamp") from error
    require(recorded_ns / 1e9 - 0.001 <= timestamp <= time.time() + 1,
            "route report timestamp is stale or in the future")
    require(canonical_hash(report.get("summary")) == canonical_hash({"PASS": 6, "FAIL": 0, "SKIP": 0, "XFAIL": 0, "XPASS": 0}),
            "positive route proof must contain six PASS results only")
    results = report.get("results")
    require(isinstance(results, list) and len(results) == len(ROUTES), "incomplete route proof results")
    run_root = report.get("run_directory")
    require(isinstance(run_root, str) and Path(run_root).is_absolute() and
            Path(run_root).parent == path.parent.parent / "routes", "route work directory differs from build")
    expected_stdout = Path(item["inputs"]["expected_stdout"]).read_text(encoding="utf-8")
    expected_stdout = expected_stdout.replace("\r\n", "\n").replace("\r", "\n")
    routes = []
    for result in results:
        require(isinstance(result, dict) and result.get("route") in ROUTES and
                result.get("status") == "PASS" and result.get("raw_status") == "PASS",
                "route support requires PASS/raw PASS, never XFAIL, SKIP, or FAIL")
        route = result["route"]
        require(route not in [value["route"] for value in routes], "duplicate route proof")
        directory = Path(run_root) / route
        require(result.get("work_directory") == str(directory), "route work directory mismatch")
        checks = result.get("checks")
        require(isinstance(checks, list) and bool(checks) and all(
            isinstance(check, dict) and isinstance(check.get("name"), str) and
            check.get("passed") is True for check in checks), "route contains missing or unsuccessful checks")
        names = sorted(check["name"] for check in checks)
        expected_names = check_names(route)
        require(names == expected_names or (route == "llvm-jit" and names == sorted(expected_names + ["artifact.valid"])),
                "route checks do not cover the complete compile/run contract")
        commands = result.get("commands")
        expected_commands = command_argv(item["inputs"], route, directory)
        require(isinstance(commands, dict) and set(commands) == set(expected_commands), "route command phases differ")
        for phase, argv in expected_commands.items():
            process = commands[phase]
            require(isinstance(process, dict) and process.get("argv") == argv and
                    process.get("cwd") == str(directory) and type(process.get("exit_code")) is int and
                    process["exit_code"] == 0 and process.get("timed_out") is False and
                    "spawn_error" in process and process["spawn_error"] is None and
                    process.get("stdout_truncated") is False and process.get("stderr_truncated") is False,
                    "route command did not complete successfully with the expected arguments")
            stdout = process.get("stdout")
            require(isinstance(stdout, str) and stdout.replace("\r\n", "\n").replace("\r", "\n") ==
                    ("" if phase == "compile" else expected_stdout) and process.get("stderr") == "",
                    "route command output does not match the fixture")
        routes.append({"route": route, "status": "PASS", "raw_status": "PASS",
                       "checks": names, "commands": {phase: {"exit_code": 0, "timed_out": False,
                       "spawn_error": None, "argv_sha256": canonical_hash(argv)}
                       for phase, argv in expected_commands.items()}})
    return {"test": item["test"], "report_sha256": hashlib.sha256(raw).hexdigest(),
            "run_directory": run_root, "contract": item, "routes": routes}


def collect_capabilities(repo: Path, snapshot: dict[str, Any] | None, report: Path | None,
                         evidence: dict[str, Any] | None, strict: bool) -> dict[str, Any]:
    catalog = load_catalog(repo)
    result = {"schema": "ny.sdk.capabilities.v1", "status": "unverified", "catalog": catalog,
              "catalog_sha256": canonical_hash(catalog), "evidence": None,
              "reason": "No complete positive route proof was supplied."}
    try:
        require(isinstance(snapshot, dict) and snapshot.get("schema") in ("ny.sdk.build-evidence.v2", "nil.sdk.build-evidence.v1") and
                snapshot.get("tests_enabled") is True and evidence is not None and report is not None,
                "verified capability observations require v2 tested build evidence")
        require(type(snapshot.get("recorded_ns")) is int and snapshot["recorded_ns"] >= 0,
                "invalid pre-test snapshot timestamp")
        captured = snapshot.get("capabilities")
        require(isinstance(captured, dict) and set(captured) == {
            "catalog_sha256", "repo_root", "build_root", "contracts"} and
            captured["catalog_sha256"] == result["catalog_sha256"] and captured["repo_root"] == str(repo),
            "catalog or repository identity changed after the build snapshot")
        require(isinstance(captured["build_root"], str) and Path(captured["build_root"]).is_absolute(), "invalid build root")
        contracts = captured["contracts"]
        require(isinstance(contracts, list) and len(contracts) == len(catalog["proofs"]), "missing proof contracts")
        raw_junit = report.read_bytes()  # summarize_ctest already bounded and screened this XML.
        require(isinstance(evidence.get("ctest"), dict) and
                hashlib.sha256(raw_junit).hexdigest() == evidence["ctest"].get("report_sha256"),
                "CTest report changed while collecting capability evidence")
        junit = ET.fromstring(raw_junit)
        passed = {case.get("name") for case in junit.iter("testcase") if
                  not any(case.find(tag) is not None for tag in ("failure", "error", "skipped")) and
                  case.get("status") not in ("notrun", "disabled")}
        proofs = []
        for definition, item in zip(catalog["proofs"], contracts):
            require(isinstance(item, dict) and item.get("test") in passed, "capability CTest proof did not PASS")
            expected = contract(repo, Path(captured["build_root"]), definition, item.get("command"))
            require(canonical_hash(item) == canonical_hash(expected), "proof fixture, harness, or command changed after the snapshot")
            proofs.append(read_proof(item, snapshot["recorded_ns"]))
        result.update(status="verified", reason=None, evidence={
            "source": evidence["source"], "tools": evidence["tools"],
            "runtime_sha256": evidence["runtime_sha256"], "ctest_report_sha256": evidence["ctest"]["report_sha256"],
            "proofs": proofs})
        if "shared_runtime_sha256" in evidence:
            result["evidence"]["shared_runtime_sha256"] = evidence["shared_runtime_sha256"]
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        if strict:
            raise ValueError("positive SDK capability proof is required: " + str(error)) from error
        result["reason"] = str(error)
    return result


def capability_errors(record: Any, evidence: Any, strict: bool) -> list[str]:
    """Validate the portable manifest record without trusting arbitrary nested shapes."""
    try:
        require(isinstance(record, dict) and set(record) == {
            "schema", "status", "catalog", "catalog_sha256", "evidence", "reason"} and
            record["schema"] == "ny.sdk.capabilities.v1", "capability record fields are incomplete")
        validate_catalog(record["catalog"])
        require(record["catalog_sha256"] == canonical_hash(record["catalog"]), "catalog hash mismatch")
        require(record["status"] in ("verified", "unverified"), "invalid capability verification status")
        if record["status"] == "unverified":
            require(record["evidence"] is None and isinstance(record["reason"], str) and bool(record["reason"]),
                    "unverified capabilities must have a reason and no proof")
            require(not strict, "RC checks require verified positive capability proofs")
            return []
        require(record["reason"] is None, "verified capability record must not have an unverified reason")
        proof = record["evidence"]
        proof_keys = {"source", "tools", "runtime_sha256", "ctest_report_sha256", "proofs"}
        if isinstance(evidence, dict) and "shared_runtime_sha256" in evidence:
            proof_keys.add("shared_runtime_sha256")
        require(isinstance(proof, dict) and set(proof) == proof_keys and isinstance(evidence, dict),
            "verified capabilities lack build evidence")
        require(all(proof[name] == evidence.get(name) for name in ("source", "tools", "runtime_sha256")) and
                isinstance(evidence.get("ctest"), dict) and
                proof["ctest_report_sha256"] == evidence["ctest"].get("report_sha256") and
                sha(proof["ctest_report_sha256"]) and evidence.get("tests_enabled") is True and
                type(evidence["ctest"].get("passed")) is int and
                evidence["ctest"]["passed"] >= len(record["catalog"]["proofs"]),
                "capability proof differs from source, tools, runtime, or CTest evidence")
        if "shared_runtime_sha256" in proof_keys:
            require(sha(proof["shared_runtime_sha256"]) and proof["shared_runtime_sha256"] == evidence["shared_runtime_sha256"],
                    "capability shared runtime differs from build evidence")
        observations = proof["proofs"]
        require(isinstance(observations, list) and len(observations) == len(record["catalog"]["proofs"]), "missing capability proof")
        for definition, observed in zip(record["catalog"]["proofs"], observations):
            require(isinstance(observed, dict) and set(observed) == {"test", "report_sha256", "run_directory", "contract", "routes"} and
                    observed["test"] == definition["test"] and sha(observed["report_sha256"]), "invalid capability proof identity")
            item = observed["contract"]
            require(isinstance(item, dict) and set(item) == {
                "test", "command", "inputs", "report_path", "source_sha256", "expected_stdout_sha256", "harness_sha256"} and
                item["test"] == definition["test"] and strings(item["command"]) and
                all(sha(item[name]) for name in ("source_sha256", "expected_stdout_sha256", "harness_sha256")), "invalid proof contract")
            inputs = item["inputs"]
            require(isinstance(inputs, dict) and isinstance(inputs.get("source"), str) and
                    isinstance(inputs.get("compiler"), str), "invalid proof input paths")
            repo = Path(inputs["source"]).parent.parent.parent
            build = Path(inputs["compiler"]).parent.parent
            require(repo.is_absolute() and build.is_absolute() and canonical_hash(inputs) == canonical_hash(expected_inputs(repo, build, definition)) and
                    item["report_path"] == str(build / "tests/route-reports" / (definition["test"] + ".json")), "invalid proof input contract")
            expected = contract(repo, build, definition, item["command"], with_hashes=False)
            require(all(item[key] == value for key, value in expected.items()), "invalid registered proof command")
            run_root = observed["run_directory"]
            require(isinstance(run_root, str) and Path(run_root).is_absolute() and
                    Path(run_root).parent == build / "tests/routes", "invalid capability run directory")
            routes = observed["routes"]
            require(isinstance(routes, list) and len(routes) == len(ROUTES), "incomplete capability route observations")
            seen = set()
            for route in routes:
                require(isinstance(route, dict) and set(route) == {"route", "status", "raw_status", "checks", "commands"} and
                        isinstance(route["route"], str) and route["route"] in ROUTES and route["route"] not in seen and
                        route["status"] == "PASS" and route["raw_status"] == "PASS", "capability route is not a unique positive PASS")
                name = route["route"]
                seen.add(name)
                checks = route["checks"]
                require(checks == check_names(name) or (name == "llvm-jit" and checks == sorted(check_names(name) + ["artifact.valid"])),
                        "capability route lacks required checks")
                commands = route["commands"]
                require(isinstance(commands, dict) and set(commands) == ({"compile_and_run"} if name == "llvm-jit" else {"compile", "run"}),
                        "capability route lacks required commands")
                expected_commands = command_argv(inputs, name, Path(run_root) / name)
                for phase, process in commands.items():
                    require(isinstance(process, dict) and set(process) == {"exit_code", "timed_out", "spawn_error", "argv_sha256"} and
                            type(process["exit_code"]) is int and process["exit_code"] == 0 and process["timed_out"] is False and
                            process["spawn_error"] is None and process["argv_sha256"] == canonical_hash(expected_commands[phase]),
                            "unsuccessful or mismatched capability command")
        return []
    except (ValueError, KeyError, TypeError, OverflowError) as error:
        return [str(error)]
