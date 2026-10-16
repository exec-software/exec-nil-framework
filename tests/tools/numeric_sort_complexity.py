#!/usr/bin/env python3
"""Bound executed VM comparisons and call depth for public numeric/text sorts.

Metrics cover complete consumer programs, including linear input setup and
verification. They do not instrument the library or promise native timings.
For text, comparison instruction counts do not count compared bytes; the
common-prefix case checks behavior without claiming constant-time key compares.
An optional historical-source control demonstrates that the previous recursive
quicksort violates the same bounds without making CTest depend on Git history.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


PATTERNS = ("equal", "duplicates", "sorted", "reversed")
TEXT_PATTERNS = (*PATTERNS, "common-prefix")
SIZES = (256, 512)
MAX_CALL_DEPTH = 64
MAX_STEPS = 50_000_000


def run(command: list[str | Path], cwd: Path, *, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    for name in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_STDLIB_PATH", "NIL_STDLIB_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE"):
        environment.pop(name, None)
    result = subprocess.run([str(part) for part in command], cwd=cwd, env=environment,
                            stdin=subprocess.DEVNULL, capture_output=True, text=True,
                            timeout=timeout)
    if result.returncode:
        raise AssertionError(f"command failed ({result.returncode}): {command}\n{result.stdout}{result.stderr}")
    return result


def source_for(kind: str, pattern: str, size: int, descending: bool) -> str:
    if kind == "text":
        text_key = {
            "equal": lambda i: "same",
            "duplicates": lambda i: f"value-{(i * 37 + 11) % 8:02d}",
            "sorted": lambda i: f"value-{i:04d}",
            "reversed": lambda i: f"value-{size - i:04d}",
            "common-prefix": lambda i: "a" * 256 + f"{(i * 37 + 11) % 64:02d}",
        }[pattern]
        values = [text_key(index) for index in range(size)]
        # ASCII keys give an independent Python oracle for bytewise text order.
        # Both input construction and oracle checks still contribute to metrics.
        setup = f"let mut values: array<text> = {json.dumps(values)}"
        expected_values = ", ".join(json.dumps(value) for value in sorted(values, reverse=descending))
        sentinel, second = '"returned-alias"', '"shared-alias"'
    else:
        setup, expected_values, sentinel, second = numeric_source_parts(kind, pattern, size, descending)
    function = f"sort_{kind}" + ("_desc" if descending else "")
    return f"""import ny.lang.core

fn main() -> i32
    {setup}
    let shared = values
    let returned = {function}(values)
    let expected: array<{kind}> = [{expected_values}]
    if values.len() != {size} or returned.len() != {size} then return 1 end if
    for index in range({size})
        if values[index] != expected[index] then return 2 end if
        if returned[index] != expected[index] then return 3 end if
        if shared[index] != expected[index] then return 4 end if
    end for
    returned[0] = {sentinel}
    if values[0] != {sentinel} or shared[0] != {sentinel} then return 5 end if
    shared[{size - 1}] = {second}
    if values[{size - 1}] != {second} or returned[{size - 1}] != {second} then return 6 end if
    returned.push({sentinel})
    if values.len() != {size + 1} or shared.len() != {size + 1} then return 7 end if
    return 0
end fn
"""


def numeric_source_parts(kind: str, pattern: str, size: int, descending: bool) -> tuple[str, str, str, str]:
    expression = {"equal": "7", "duplicates": "(index * 37 + 11) % 8 - 4",
                  "sorted": "index", "reversed": f"{size} - index"}[pattern]
    integers = {"equal": lambda i: 7, "duplicates": lambda i: (i * 37 + 11) % 8 - 4,
                "sorted": lambda i: i, "reversed": lambda i: size - i}[pattern]
    expected = sorted((integers(index) for index in range(size)), reverse=descending)
    if kind == "f64":
        expression = f"(({expression}) as f64) / 4.0"
        expected_values = ", ".join(repr(value / 4.0) for value in expected)
        sentinel, second = "123456.25", "-98765.5"
    else:
        expected_values = ", ".join(str(value) for value in expected)
        sentinel, second = "123456", "-98765"
    setup = f"""let mut values: array<{kind}> = []
    for index in range({size})
        values.push({expression})
    end for"""
    return setup, expected_values, sentinel, second


def profile(args, root: Path, stdlib: Path, kind: str, pattern: str,
            size: int, descending: bool, prefix: str = "current") -> dict:
    label = f"{prefix}-{kind}-{pattern}-{'desc' if descending else 'asc'}-{size}"
    source = root / (label + ".ny")
    artifact = root / (label + ".nil")
    source.write_text(source_for(kind, pattern, size, descending), encoding="utf-8")
    compiled = run([args.compiler, "--quiet", "--no-stdlib", "--no-bundle", "-I", stdlib,
                    "--output", artifact, source], root)
    if compiled.stderr:
        raise AssertionError(f"{label}: unexpected compiler diagnostics: {compiled.stderr}")
    measured = run([args.profiler, artifact, str(MAX_STEPS)], root)
    if measured.stderr:
        raise AssertionError(f"{label}: unexpected program output: {measured.stderr}")
    metrics = json.loads(measured.stdout)
    if set(metrics) != {"steps", "comparisons", "max_call_depth", "exit_code"}:
        raise AssertionError(f"{label}: invalid metrics: {metrics}")
    if any(type(value) is not int for value in metrics.values()):
        raise AssertionError(f"{label}: non-integer metrics: {metrics}")
    if metrics["exit_code"] != 0:
        raise AssertionError(f"{label}: values/alias check failed: {metrics}")
    if metrics["comparisons"] <= 0 or metrics["steps"] < metrics["comparisons"]:
        raise AssertionError(f"{label}: comparison instructions were not counted: {metrics}")
    return {"kind": kind, "pattern": pattern, "descending": descending, "size": size, **metrics}


def bounds_failures(small: dict, large: dict) -> list[str]:
    failures = []
    for result in (small, large):
        if result["max_call_depth"] > MAX_CALL_DEPTH:
            failures.append(f"n={result['size']} call depth {result['max_call_depth']} exceeds {MAX_CALL_DEPTH}")
    # A 2x input may cost more than 2x comparisons for n log n. A 3x bound is
    # deliberately loose, yet distinguishes the roughly 4x quadratic case.
    if large["comparisons"] >= 3 * small["comparisons"]:
        failures.append(f"doubling input grew executed comparisons from {small['comparisons']} to {large['comparisons']} (expected <3x)")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--metrics", "--profiler", dest="profiler", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--baseline-ref", help="optional Git revision whose sorting.ny must violate the bounds")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    for name in ("compiler", "profiler", "stdlib", "repo_root"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ny-sort-complexity-", dir=args.work_dir)).resolve()
    report = {"scope": "whole-program VM comparison instructions, steps, and call depth",
              "sizes": list(SIZES), "max_call_depth": MAX_CALL_DEPTH,
              "comparison_growth_limit": "strictly less than 3x for 2x input",
              "results": [], "baseline": None}
    try:
        for kind in ("i32", "f64", "text"):
            for descending in (False, True):
                for pattern in TEXT_PATTERNS if kind == "text" else PATTERNS:
                    pair = [profile(args, root, args.stdlib, kind, pattern, size, descending) for size in SIZES]
                    report["results"].extend(pair)
                    failures = bounds_failures(*pair)
                    if failures:
                        raise AssertionError(f"{kind} {pattern} descending={descending}: " + "; ".join(failures))
        if args.baseline_ref:
            baseline = root / "baseline-stdlib"
            shutil.copytree(args.stdlib, baseline)
            # Historical controls may precede either the component rename or
            # the standard-namespace flattening. Probe Git identities, while
            # placing the selected source in today's flat library tree.
            for historical_path in ("nilstd/src/core/sorting.ny",
                                    "nilstd/src/ny/lang/core/sorting.ny",
                                    "code/nystdlib/src/ny/lang/core/sorting.ny"):
                probe = subprocess.run(["git", "-C", str(args.repo_root), "show",
                                        args.baseline_ref + ":" + historical_path],
                                       cwd=root, capture_output=True, text=True)
                if probe.returncode == 0:
                    break
            else:
                raise AssertionError("historical sorting source was not found at " + args.baseline_ref)
            (baseline / "core/sorting.ny").write_text(probe.stdout, encoding="utf-8")
            pair = [profile(args, root, baseline, "i32", "equal", size, False, "baseline") for size in SIZES]
            failures = bounds_failures(*pair)
            report["baseline"] = {"revision": args.baseline_ref, "results": pair, "detected_regressions": failures}
            if not failures:
                raise AssertionError("historical sorting control did not violate the algorithmic bounds")
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        largest_depth = max(result["max_call_depth"] for result in report["results"])
        print(f"Numeric/text sort complexity: {len(report['results'])} profiles passed; maximum VM call depth {largest_depth}")
        if report["baseline"]:
            print("Historical control rejected: " + "; ".join(report["baseline"]["detected_regressions"]))
    except Exception:
        (root / "partial-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Numeric/text sort complexity artifacts preserved: {root}", file=sys.stderr)
        raise
    else:
        shutil.rmtree(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
