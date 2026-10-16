#!/usr/bin/env python3
"""Run every curated Ny application through the execution-route matrix."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence


APPLICATION_ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = APPLICATION_ROOT.parents[1]
MATRIX = REPOSITORY_ROOT / "tests" / "routes" / "ny_route_matrix.py"
MANIFEST = APPLICATION_ROOT / "suite.json"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the curated Ny applications through all execution routes."
    )
    parser.add_argument("--compiler", required=True, help="Path to nilcompiler")
    parser.add_argument(
        "--interpreter", required=True, help="Path to the nilinterpreter executable"
    )
    parser.add_argument(
        "--routes",
        default="all",
        help="Routes forwarded to ny_route_matrix.py (default: all six)",
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument(
        "--output-dir",
        default=str(REPOSITORY_ROOT / "build" / "application-conformance"),
        help="Directory for route artifacts and JSON reports",
    )
    return parser


def read_expected_exit(app_dir: Path) -> int:
    value = (app_dir / "expected.exit").read_text(encoding="utf-8").strip()
    try:
        return int(value)
    except ValueError as exc:
        raise ValueError(f"{app_dir / 'expected.exit'} must contain an integer") from exc


def load_manifest() -> Dict[str, Any]:
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if document.get("schema") != "ny.application-suite.v1":
        raise ValueError(f"unsupported application manifest schema in {MANIFEST}")
    applications = document.get("applications")
    if not isinstance(applications, list) or not applications:
        raise ValueError(f"{MANIFEST} must define at least one application")
    return document


def run_application(
    spec: Dict[str, Any],
    compiler: Path,
    interpreter: Path,
    routes: str,
    timeout: float,
    output_dir: Path,
) -> Dict[str, Any]:
    name = str(spec["name"])
    app_dir = APPLICATION_ROOT / name
    report_path = output_dir / "reports" / f"{name}.json"
    work_dir = output_dir / "work"

    command: List[str] = [
        sys.executable,
        str(MATRIX),
        "--compiler",
        str(compiler),
        "--interpreter",
        str(interpreter),
        "--source",
        str(app_dir / "main.ny"),
        "--expected-stdout",
        str(app_dir / "expected.stdout"),
        "--expected-exit",
        str(read_expected_exit(app_dir)),
        "--routes",
        routes,
        "--timeout",
        str(timeout),
        "--work-dir",
        str(work_dir),
        "--report-json",
        str(report_path),
        "--strict",
    ]

    for extra_source in spec.get("extra_sources", []):
        command.append(f"--compiler-arg={app_dir / extra_source}")
    for fragment in spec.get("text_nil_contains", []):
        command.append(f"--text-nil-contains={fragment}")
    for route, reason in spec.get("xfail", {}).items():
        command.extend(("--xfail", f"{route}={reason}"))
    for route, fragment in spec.get("xfail_stderr_contains", {}).items():
        command.extend(("--xfail-stderr-contains", f"{route}={fragment}"))

    print(f"\n== {name} ==", flush=True)
    report_path.unlink(missing_ok=True)
    try:
        completed = subprocess.run(command, cwd=REPOSITORY_ROOT, check=False)
    except OSError as exc:
        return {
            "name": name,
            "features": spec.get("features", []),
            "exit_code": 1,
            "summary": {"error": f"could not launch route harness: {exc}"},
            "report": str(report_path),
            "contract_checks": [],
            "routes": [],
        }

    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
        route_results = report["results"]
        if not isinstance(route_results, list):
            raise ValueError("results is not a list")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"FAIL  route harness did not produce a valid report: {exc}", flush=True)
        return {
            "name": name,
            "features": spec.get("features", []),
            "exit_code": 1,
            "summary": {
                "error": "route harness did not produce a valid report",
                "detail": str(exc),
                "harness_exit_code": completed.returncode,
            },
            "report": str(report_path),
            "contract_checks": [],
            "routes": [],
        }

    contract_checks: List[Dict[str, Any]] = []
    text_result = next(
        (
            result
            for result in route_results
            if result["route"] == "text-interpreter"
        ),
        None,
    )
    if text_result is not None:
        for check in text_result["checks"]:
            if check.get("name") != "text-nil.contains":
                continue
            fragment = str(check.get("expected", ""))
            passed = bool(check.get("passed"))
            contract_checks.append(
                {
                    "name": "text-nil.contains",
                    "expected": fragment,
                    "passed": passed,
                }
            )
            print(
                f"{'PASS' if passed else 'FAIL':<5} text-nil contains {fragment!r}",
                flush=True,
            )

    contract_failed = any(not check["passed"] for check in contract_checks)
    exit_code = 1 if contract_failed else completed.returncode
    run_directory = report.get("run_directory")
    artifacts_retained = bool(
        report.get("artifact_retention", {}).get("retained", exit_code != 0)
    )
    if exit_code != 0 and run_directory and artifacts_retained:
        print(
            f"Artifacts preserved for failed application {name}: {run_directory}",
            file=sys.stderr,
            flush=True,
        )
    return {
        "name": name,
        "features": spec.get("features", []),
        "exit_code": exit_code,
        "summary": report["summary"],
        "report": str(report_path),
        "run_directory": run_directory,
        "artifacts_retained": artifacts_retained,
        "contract_checks": contract_checks,
        "routes": [
            {"route": item["route"], "status": item["status"]}
            for item in route_results
        ],
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    compiler = Path(args.compiler).expanduser().resolve()
    interpreter = Path(args.interpreter).expanduser().resolve()
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        raise SystemExit(f"nilcompiler is not an executable file: {compiler}")
    if not interpreter.is_file() or not os.access(interpreter, os.X_OK):
        raise SystemExit(f"nilinterpreter is not an executable file: {interpreter}")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be greater than zero")

    output_dir = Path(args.output_dir).expanduser().resolve()
    (output_dir / "reports").mkdir(parents=True, exist_ok=True)
    (output_dir / "work").mkdir(parents=True, exist_ok=True)

    manifest = load_manifest()
    results = [
        run_application(
            spec,
            compiler,
            interpreter,
            args.routes,
            args.timeout,
            output_dir,
        )
        for spec in manifest["applications"]
    ]

    suite_report = {
        "schema": "ny.application-suite-report.v1",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "compiler": str(compiler),
        "interpreter": str(interpreter),
        "routes": args.routes,
        "manifest": str(MANIFEST),
        "applications": results,
    }
    suite_report_path = output_dir / "suite-report.json"
    suite_report_path.write_text(
        json.dumps(suite_report, indent=2) + "\n", encoding="utf-8"
    )

    failed = [result["name"] for result in results if result["exit_code"] != 0]
    print(f"\nSuite report: {suite_report_path}")
    if failed:
        print("Failed applications: " + ", ".join(failed), file=sys.stderr)
        return 1
    print(f"All {len(results)} application contracts passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
