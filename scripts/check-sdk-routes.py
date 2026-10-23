#!/usr/bin/env python3
"""Exercise installed Ny sources and binary libraries outside the repository.

This is consumer regression evidence, not a declaration of language completeness.
The sealed SDK is read-only; all compiler outputs live in a private temporary tree.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_capabilities import check_names, command_argv

ROUTES = ("text-interpreter", "binary-interpreter", "bundle-text", "bundle-binary",
          "llvm-jit", "llvm-aot")
FIXTURES = ("scalar-array-parity", "stl-array-primitives", "native-nested-reference",
            "native-global-contract", "native-time-contract", "native-scalar-fields",
            "module-global-contract", "native-managed-lifetime", "class-execution",
            "fault-control-flow", "fault-nominal-identity", "fault-managed-lifetime",
            "class-polymorphism", "reference-handles", "owned-buffers", "parallel-map",
            "checked-hex-runtime", "checked-binary-runtime",
            "checked-varint-runtime", "checked-crc-runtime", "scalar-array-handle-collision",
            "checked-array-runtime", "checked-array-matrix-runtime",
            "stable-index-ordering", "stable-index-ordering-matrix",
            "numeric-value-ordering", "numeric-value-ordering-matrix",
            "text-comparison-runtime", "text-value-ordering", "text-value-ordering-matrix",
            "text-whitespace-escapes", "text-whitespace-escapes-matrix",
            "reflection-values", "reflection-values-matrix",
            "standard-namespace-aliases", "database-sqlite-runtime",
            "vector-norms", "vector-norms-matrix",
            "qualified-enum-patterns", "qualified-enum-patterns-matrix",
            "scientific-literals", "scientific-literals-matrix",
            "signed-numbers-matrix", "formatted-text", "formatted-text-matrix",
            "tuples", "tuples-matrix",
            "tuple-arrays", "tuple-arrays-matrix",
            "integer-literal-widths", "integer-literal-widths-matrix",
            "checked-text-spans", "checked-text-spans-matrix",
            "checked-i64-conversion", "checked-i64-conversion-matrix",
            "average-f64", "average-f64-matrix",
            "random-generators", "random-generators-matrix", "math-runtime")


def fixture_source(name: str, directory: Path | None = None) -> Path:
    directory = directory if directory is not None else ROOT / "tests/language"
    matches = [directory / (name + suffix) for suffix in (".ny", ".matrix", ".mat")
               if (directory / (name + suffix)).is_file()]
    if len(matches) != 1:
        raise ValueError("consumer fixture must have exactly one Ny or Matrix source: " + name)
    return matches[0]


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def check_report(report: dict, source: Path, sdk: Path, library: Path,
                 expected: Path, expected_text: str) -> None:
    """Do not turn XFAIL, skipped routes, or an empty report into a proof."""
    expected_inputs = {
        "source": str(source), "compiler": str(sdk / "nilcompiler"),
        "interpreter": str(sdk / "nilinterpreter"), "expected_exit": 0,
        "routes": list(ROUTES), "strict": True,
        "compiler_args": ["--sdk-path", str(library)],
        "expected_stdout": str(expected), "expected_stderr": None,
        "with_stdlib": False, "interpreter_args": [], "program_args": [],
        "text_nil_contains": [], "xfail_stderr_contains": {},
    }
    inputs = report.get("inputs")
    if report.get("schema") != "ny.route-matrix.v1" or not isinstance(inputs, dict):
        raise ValueError("invalid installed route report schema/inputs")
    if any(inputs.get(key) != value for key, value in expected_inputs.items()):
        raise ValueError("installed route report does not match SDK/input contract")
    if report.get("summary") != {"PASS": 6, "FAIL": 0, "SKIP": 0, "XFAIL": 0, "XPASS": 0}:
        raise ValueError("installed route summary must contain six positive passes")
    run_root = report.get("run_directory")
    if (not isinstance(run_root, str) or not Path(run_root).is_absolute() or
            Path(run_root).parent != source.parent / "routes"):
        raise ValueError("installed route work directory differs from private consumer tree")
    results = report.get("results")
    if not isinstance(results, list) or len(results) != len(ROUTES):
        raise ValueError("installed route report does not contain six routes")
    names = [result.get("route") for result in results if isinstance(result, dict)]
    if len(names) != len(results) or sorted(names) != sorted(ROUTES):
        raise ValueError("installed route report has missing or duplicate routes")
    for result in results:
        route = result["route"]
        if result.get("status") != "PASS" or result.get("raw_status") != "PASS":
            raise ValueError("installed route must actually PASS: " + str(result.get("route")))
        checks = result.get("checks")
        commands = result.get("commands")
        if (not isinstance(checks, list) or not checks or
                any(not isinstance(check, dict) or check.get("passed") is not True
                    for check in checks)):
            raise ValueError("installed route has missing or unsuccessful checks")
        names = [check.get("name") for check in checks]
        if (any(not isinstance(name, str) for name in names) or
                (sorted(names) != check_names(route) and
                 not (route == "llvm-jit" and
                      sorted(names) == sorted(check_names(route) + ["artifact.valid"])))):
            raise ValueError("installed route lacks required execution checks")
        directory = Path(run_root) / route
        expected_commands = command_argv(inputs, route, directory)
        if not isinstance(commands, dict) or set(commands) != set(expected_commands):
            raise ValueError("installed route has missing command phases")
        for phase, command in commands.items():
            text = "" if phase == "compile" else expected_text
            if (not isinstance(command, dict) or type(command.get("exit_code")) is not int or
                    command["exit_code"] != 0 or command.get("timed_out") is not False or
                    command.get("spawn_error") is not None or
                    command.get("argv") != expected_commands[phase] or
                    command.get("cwd") != str(directory) or command.get("stderr") != "" or
                    command.get("stdout") != text or command.get("stdout_truncated") is not False or
                    command.get("stderr_truncated") is not False):
                raise ValueError("installed route has unsuccessful or mismatched command evidence")


@contextmanager
def diagnostic_directory():
    root = Path(tempfile.mkdtemp(prefix="ny-installed-routes-")).resolve()
    try:
        yield root
    except Exception as error:
        raise ValueError("{}\nConsumer artifacts preserved for diagnosis: {}".format(error, root)) from error
    else:
        shutil.rmtree(root)


def run(arguments: list[str], cwd: Path, environment: dict[str, str]) -> str:
    process = subprocess.run(arguments, cwd=cwd, env=environment, check=False,
                             capture_output=True, text=True, encoding="utf-8",
                             errors="replace", stdin=subprocess.DEVNULL, timeout=240)
    if process.returncode:
        raise ValueError("consumer command failed ({}): {}\n{}\n{}".format(
            process.returncode, arguments, process.stdout, process.stderr))
    if process.stderr:
        raise ValueError("consumer command emitted unexpected stderr: " + process.stderr)
    return process.stdout


def run_contract(sdk: Path, revision: str, require_rc: bool) -> dict:
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NY_", "NIL_"))}
    validator = ROOT / "scripts/validate-sdk.py"
    harness = ROOT / "tests/routes/ny_route_matrix.py"
    pipeline = ROOT / "tests/tools/native_toolchain_pipeline.py"
    if sys.platform != "linux":
        raise ValueError("installed native consumer qualification currently requires Linux")
    with diagnostic_directory() as work:
        validation = [sys.executable, str(validator), str(sdk),
                      "--expected-revision", revision]
        run(validation + (["--require-rc-checks"] if require_rc else []), work, environment)
        manifest_path = sdk / "manifest.json"
        manifest_hash = digest(manifest_path)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("build", {}).get("llvm") != "ON":
            raise ValueError("installed consumer qualification requires the native backend")
        runtime = sdk / manifest["native_runtime"]["path"]
        result = {
            "schema": "ny.sdk.consumer-routes.v1", "release_declaration": False,
            "source_revision": revision, "sdk_manifest_sha256": manifest_hash,
            "runner_sha256": digest(Path(__file__)),
            "evidence_helpers_sha256": digest(ROOT / "scripts/sdk_capabilities.py"),
            "route_harness_sha256": digest(harness), "native_pipeline_sha256": digest(pipeline),
            "fixtures": [], "execution_count": 0,
        }
        for name in FIXTURES:
            input_source = fixture_source(name)
            fixture_output = input_source.with_suffix(".stdout")
            # The extension selects the frontend, including for source stored
            # inside a binary NIL library. Preserve it in the isolated tree.
            source = work / input_source.name
            expected = work / (name + ".stdout")
            source.write_bytes(input_source.read_bytes())
            expected.write_bytes(fixture_output.read_bytes())
            fixture = {"name": name, "source_sha256": digest(source),
                       "stdout_sha256": digest(expected), "libraries": []}
            for library_name in ("nilstd", "nillib"):
                library = sdk / library_name
                report_path = work / (name + "-" + library_name + ".json")
                run([sys.executable, str(harness), "--compiler", str(sdk / "nilcompiler"),
                     "--interpreter", str(sdk / "nilinterpreter"), "--source", str(source),
                     "--expected-stdout", str(expected), "--expected-exit", "0",
                     "--routes", "all", "--strict", "--compiler-arg=--sdk-path",
                     "--compiler-arg=" + str(library), "--work-dir", str(work / "routes"),
                     "--report-json", str(report_path)], work, environment)
                report = json.loads(report_path.read_text(encoding="utf-8"))
                check_report(report, source, sdk, library, expected,
                             expected.read_text(encoding="utf-8"))
                output = run([sys.executable, str(pipeline), "--compiler", str(sdk / "nilcompiler"),
                              "--assembler", str(sdk / "nilassembler"),
                              "--linker", str(sdk / "nillinker"), "--runtime", str(runtime),
                              "--source", str(source), "--expected-stdout", str(expected),
                              "--stdlib", str(library), "--work-dir", str(work / "pipeline")],
                             work, environment)
                if output != "standalone assembler/linker pipeline: passed\n":
                    raise ValueError("installed object pipeline did not confirm success")
                fixture["libraries"].append({"path": library_name, "status": "PASS",
                    "routes": list(ROUTES) + ["text-object-link-run", "binary-object-link-run"],
                    "route_report_sha256": digest(report_path), "route_report": report})
                result["execution_count"] += 8
            result["fixtures"].append(fixture)
        # Check every sealed payload hash again, not just the manifest identity.
        run(validation + ["--skip-smoke"], work, environment)
        if digest(manifest_path) != manifest_hash:
            raise ValueError("SDK manifest changed during consumer qualification")
        result["status"] = "PASS"
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--require-rc-checks", action="store_true")
    parser.add_argument("--output", type=Path, required=True,
                        help="consumer evidence JSON outside the sealed SDK")
    args = parser.parse_args()
    try:
        sdk = args.sdk.resolve(strict=True)
        output = args.output.absolute()
        if output.is_symlink() or sdk == output.resolve() or sdk in output.resolve().parents:
            raise ValueError("consumer report must be outside the sealed SDK and not a symlink")
        report = run_contract(sdk, args.expected_revision, args.require_rc_checks)
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                         prefix=".ny-consumer-", delete=False) as stream:
            pending = Path(stream.name)
            try:
                json.dump(report, stream, indent=2, sort_keys=True)
                stream.write("\n")
                stream.flush()
                os.replace(pending, output)
            finally:
                pending.unlink(missing_ok=True)
        print("installed SDK consumer routes: {} passed (source and binary libraries)".format(
            report["execution_count"]))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("installed SDK consumer qualification failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
