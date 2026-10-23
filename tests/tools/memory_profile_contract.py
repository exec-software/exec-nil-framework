#!/usr/bin/env python3
"""Validate Aperture collector accounting, boundedness, output and isolation."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


COUNTERS = (
    "allocated_bytes", "allocated_objects", "live_bytes", "live_objects",
    "peak_bytes", "peak_objects", "freed_bytes", "freed_objects", "resizes",
)


def invariants(report):
    assert report["schema"] == "aperture.memory.v1"
    assert report["route"] == "native"
    assert report["measurement"] == "runtime_logical_bytes"
    assert isinstance(report["completed"], bool)
    assert report["scope"] and all(isinstance(item, str) for item in report["scope"])
    assert all(isinstance(item, str) for item in report["errors"])
    for counts in [report["totals"], *report["sites"]]:
        for key in COUNTERS:
            assert type(counts[key]) is int and 0 <= counts[key] <= 2**64 - 1, (key, counts)
        for unit in ("bytes", "objects"):
            assert counts[f"allocated_{unit}"] - counts[f"freed_{unit}"] == counts[f"live_{unit}"], counts
            assert counts[f"live_{unit}"] <= counts[f"peak_{unit}"] <= counts[f"allocated_{unit}"], counts
    for key in COUNTERS:
        if not key.startswith("peak_"):
            assert sum(site[key] for site in report["sites"]) == report["totals"][key], key
    for site in report["sites"]:
        assert set(site) == {"file", "function", "line", "kind", *COUNTERS}
        assert all(isinstance(site[key], str) for key in ("file", "function", "kind"))
        assert type(site["line"]) is int and site["line"] >= 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()
    environment = {key: value for key, value in os.environ.items()
                   if key != "NIL_MEMORY_PROFILE"}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="nil-memory-collector-") as directory:
        root = Path(directory)

        def capture(mode, exit_code=0):
            nonlocal checks
            output = root / (mode + ".json")
            result = subprocess.run([str(probe), mode],
                                    env={**environment, "NIL_MEMORY_PROFILE": str(output)},
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == exit_code, (mode, result.returncode, result.stderr)
            assert result.stdout == "", (mode, result.stdout)
            raw = output.read_bytes()
            report = json.loads(raw)
            invariants(report)
            checks += 1
            return report, result, raw

        report, result, _ = capture("basic")
        assert result.stderr == ""
        assert report["completed"] and report["status"] == "completed"
        assert report["totals"] == dict(zip(COUNTERS, (39, 3, 7, 1, 32, 3, 32, 2, 2)))
        assert report["collections"] == {"count": 1, "duration_ns": 25,
                                          "freed_bytes": 32, "freed_objects": 2}
        first, second = report["sites"]
        assert (first["file"], first["function"], first["line"], first["kind"]) == (
            "fixture.ny", "fixture.main", 12, "array")
        assert first["resizes"] == 2 and first["freed_objects"] == 2
        assert second["file"] == "other.matrix" and second["live_bytes"] == 7

        report, _, _ = capture("zero")
        assert report["totals"]["live_bytes"] == 0
        assert report["totals"]["live_objects"] == 1

        report, _, raw = capture("escape")
        site = report["sites"][0]
        assert site["file"] == '/tmp/Δ "path"\\\n\t\0ÿ'
        assert site["function"] == "function\n\r\t\x01"
        assert site["kind"] == 'kind"\\\n'
        assert "Δ".encode() in raw

        for mode, diagnostic in (("duplicate", "duplicate live allocation identity"),
                                 ("overflow", "representable range"),
                                 ("resize-overflow", "representable range")):
            report, result, _ = capture(mode, 2)
            assert not report["completed"] and report["status"] == "profile_error"
            assert diagnostic in result.stderr and any(diagnostic in item for item in report["errors"])
        assert report["totals"]["resizes"] == 0

        report, _, _ = capture("spill")
        assert report["site_limit_reached"]
        assert len(report["sites"]) == 4097
        assert report["totals"]["allocated_objects"] == 4099
        assert report["totals"]["peak_objects"] == 1
        overflow = report["sites"][-1]
        assert overflow["function"] == "<site limit reached>"
        assert overflow["allocated_objects"] == 2
        assert report["sites"][0]["allocated_objects"] == 2

        report, _, _ = capture("worker")
        assert report["excluded_sessions"] == 1
        assert report["totals"]["allocated_bytes"] == 5
        assert all(site["kind"] != "worker" for site in report["sites"])

        report, _, _ = capture("abandoned")
        assert not report["completed"] and report["status"] == "abandoned"
        assert report["totals"]["live_bytes"] == 3
        report, _, _ = capture("failed")
        assert not report["completed"] and report["status"] == "failed"
        assert report["totals"]["live_bytes"] == 3
        report, _, _ = capture("finish-twice")
        assert report["completed"] and report["status"] == "completed"
        assert report["totals"]["allocated_bytes"] == 3

        for extra in ({}, {"NIL_MEMORY_PROFILE": ""}):
            result = subprocess.run([str(probe), "disabled"], env={**environment, **extra},
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 0 and result.stdout == result.stderr == "", result
            checks += 1

        existing = root / "existing.json"
        sentinel = b"existing user data\n"
        existing.write_bytes(sentinel)
        for destination in (existing, root / "missing" / "capture.json"):
            result = subprocess.run([str(probe), "basic"],
                                    env={**environment, "NIL_MEMORY_PROFILE": str(destination)},
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 3 and "could not create capture destination" in result.stderr, result
            assert result.stdout == ""
            assert existing.read_bytes() == sentinel
            checks += 1
        if os.name != "nt":
            link = root / "symlink.json"
            link.symlink_to(existing)
            result = subprocess.run([str(probe), "basic"],
                                    env={**environment, "NIL_MEMORY_PROFILE": str(link)},
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 3 and link.is_symlink()
            assert existing.read_bytes() == sentinel
            checks += 1
    print(f"Aperture memory collector contract: {checks} checks passed")


if __name__ == "__main__":
    main()
