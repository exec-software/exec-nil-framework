#!/usr/bin/env python3
"""Capability evidence contracts using synthetic reports, without a Ny build."""

from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import sdk_capabilities as capabilities
import sdk_qualification as qualification
from sdk_qualification_tests import fixture


class CapabilityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ny-capability-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name) / "build"
        self.catalog = capabilities.load_catalog(ROOT)
        listing = {"tests": []}
        for proof in self.catalog["proofs"]:
            inputs = capabilities.expected_inputs(ROOT, self.build, proof)
            argv = [sys.executable, str(ROOT / "tests/routes/ny_route_matrix.py")]
            for option, value in (
                    ("--compiler", inputs["compiler"]), ("--interpreter", inputs["interpreter"]),
                    ("--work-dir", str(self.build / "tests/routes")),
                    ("--report-json", str(self.build / "tests/route-reports" / (proof["test"] + ".json"))),
                    ("--source", inputs["source"]), ("--expected-stdout", inputs["expected_stdout"]),
                    ("--expected-exit", "0"), ("--routes", "all")):
                argv += [option, value]
            argv += ["--compiler-arg=" + value for value in inputs["compiler_args"]]
            listing["tests"].append({"name": proof["test"], "command": argv})
        self.listing = listing
        captured = capabilities.record_capabilities(ROOT, self.build, listing)
        self.report_paths = []
        for item in captured["contracts"]:
            report = self.route_report(item)
            path = Path(item["report_path"])
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(report), encoding="utf-8")
            self.report_paths.append(path)
        # read_proof requires every route report's st_mtime_ns to be >=
        # recorded_ns. Taking recorded_ns from time.time_ns() before writing
        # the reports looks correct but is not: st_mtime_ns comes from the
        # filesystem's coarse clock while time.time_ns() is fine-grained, so a
        # file written microseconds later can carry an mtime that rounds below
        # it. That made this fixture fail roughly once in 25 runs, serially as
        # well as in parallel.
        #
        # Anchor the snapshot to the reports it describes instead of to a
        # second clock. The evidence is then consistent by construction, which
        # is what this fixture is for -- exercising the validator on
        # well-formed input, not measuring clock skew.
        recorded_ns = min(path.stat().st_mtime_ns for path in self.report_paths)
        self.snapshot = {"schema": "ny.sdk.build-evidence.v2", "recorded_ns": recorded_ns,
                         "tests_enabled": True, "capabilities": captured}
        self.junit = self.build / "ctest.xml"
        self.write_junit()
        self.evidence = fixture()["qualification"]["evidence"]
        self.evidence["shared_runtime_sha256"] = "1" * 64
        self.evidence["ctest"] = qualification.summarize_ctest(
            self.junit, [proof["test"] for proof in self.catalog["proofs"]], self.snapshot["recorded_ns"])

    def write_junit(self, child=""):
        self.junit.write_text("<testsuite>" + "".join(
            '<testcase name="{}">{}</testcase>'.format(proof["test"], child if index == 0 else "")
            for index, proof in enumerate(self.catalog["proofs"])) + "</testsuite>", encoding="utf-8")

    def route_report(self, item):
        root = self.build / "tests/routes" / item["test"]
        inputs = item["inputs"]
        stdout = Path(inputs["expected_stdout"]).read_text(encoding="utf-8")
        results = []
        for route in capabilities.ROUTES:
            directory = root / route
            commands = {}
            for phase, argv in capabilities.command_argv(inputs, route, directory).items():
                commands[phase] = {"argv": argv, "cwd": str(directory), "exit_code": 0,
                    "stdout": "" if phase == "compile" else stdout, "stderr": "",
                    "stdout_truncated": False, "stderr_truncated": False,
                    "duration_seconds": 0.001, "timed_out": False, "spawn_error": None}
            results.append({"route": route, "work_directory": str(directory), "commands": commands,
                "checks": [{"name": name, "passed": True} for name in capabilities.check_names(route)],
                "raw_status": "PASS", "status": "PASS"})
        return {"schema": "ny.route-matrix.v1", "created_at": datetime.now(timezone.utc).isoformat(),
            "inputs": inputs, "run_directory": str(root), "report_path": item["report_path"],
            "summary": {"PASS": 6, "FAIL": 0, "SKIP": 0, "XFAIL": 0, "XPASS": 0}, "results": results}

    def collect(self, strict=True):
        return capabilities.collect_capabilities(ROOT, self.snapshot, self.junit, self.evidence, strict)

    def edit_report(self, change):
        path = self.report_paths[0]
        value = json.loads(path.read_text(encoding="utf-8"))
        change(value)
        path.write_text(json.dumps(value), encoding="utf-8")

    def rejection(self, change):
        path = self.report_paths[0]
        original = path.read_bytes()
        try:
            self.edit_report(change)
            with self.assertRaises(ValueError):
                self.collect()
        finally:
            path.write_bytes(original)

    def test_verified_record_and_nil_manifest(self):
        record = self.collect()
        self.assertEqual(record["status"], "verified")
        self.assertEqual(capabilities.capability_errors(record, self.evidence, True), [])
        value = fixture()
        value["schema"] = "nil.sdk.v1"
        value["native_shared_runtime"] = {"sha256": self.evidence["shared_runtime_sha256"]}
        value["qualification"]["evidence"] = self.evidence
        value["qualification"]["capabilities"] = record
        self.assertEqual(qualification.qualification_errors(value, True, "a" * 40), [])
        self.assertEqual(len(record["catalog"]["groups"]), 16)
        self.assertEqual(len(record["evidence"]["proofs"]), 31)
        self.assertEqual(sum(len(proof["routes"]) for proof in record["evidence"]["proofs"]), 186)

    def test_legacy_manifests_remain_unchanged(self):
        self.assertEqual(qualification.qualification_errors(fixture(), True, "a" * 40), [])
        value = fixture()
        value["schema"] = "ny.sdk.v1"
        value.pop("qualification")
        self.assertEqual(qualification.qualification_errors(value), [])
        self.assertTrue(qualification.qualification_errors(value, True))

    def test_development_without_proof_is_unverified(self):
        record = capabilities.collect_capabilities(ROOT, None, None, None, False)
        self.assertEqual(record["status"], "unverified")
        self.assertEqual(capabilities.capability_errors(record, None, False), [])
        self.assertTrue(capabilities.capability_errors(record, None, True))
        with self.assertRaises(ValueError):
            capabilities.collect_capabilities(ROOT, None, None, None, True)

    def test_old_snapshot_cannot_qualify_new_profile(self):
        self.snapshot["schema"] = "ny.sdk.build-evidence.v1"
        self.assertEqual(self.collect(False)["status"], "unverified")
        with self.assertRaises(ValueError):
            self.collect()

    def test_missing_registration_and_unregistered_skip_options(self):
        listing = deepcopy(self.listing)
        listing["tests"].pop()
        with self.assertRaises(ValueError):
            capabilities.record_capabilities(ROOT, self.build, listing)
        for arguments in (["--xfail", "llvm-jit=known failure"], ["--skip", "llvm-aot=unsupported"],
                          ["--expected-exit", "1"], ["--routes", "text-interpreter"]):
            with self.subTest(arguments=arguments):
                listing = deepcopy(self.listing)
                listing["tests"][0]["command"] += arguments
                with self.assertRaises(ValueError):
                    capabilities.record_capabilities(ROOT, self.build, listing)

    def test_catalog_and_fixture_changes_rejected(self):
        for field in ("source_sha256", "expected_stdout_sha256", "harness_sha256"):
            with self.subTest(field=field):
                original = self.snapshot["capabilities"]["contracts"][0][field]
                self.snapshot["capabilities"]["contracts"][0][field] = "0" * 64
                with self.assertRaises(ValueError):
                    self.collect()
                self.snapshot["capabilities"]["contracts"][0][field] = original
        self.snapshot["capabilities"]["catalog_sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            self.collect()

    def test_missing_and_stale_reports_rejected(self):
        path = self.report_paths[0]
        original = path.read_bytes()
        path.unlink()
        with self.assertRaises(ValueError):
            self.collect()
        path.write_bytes(original)
        os.utime(path, ns=(0, 0))
        with self.assertRaises(ValueError):
            self.collect()

    def test_stale_timestamp_not_rescued_by_fresh_mtime(self):
        self.rejection(lambda value: value.update(created_at="2000-01-01T00:00:00+00:00"))
        self.rejection(lambda value: value.update(created_at="2099-01-01T00:00:00+00:00"))

    def test_changed_inputs_rejected(self):
        for name, value in (("compiler", "/other/nilcompiler"), ("interpreter", "/other/nilinterpreter"),
                            ("source", "/other/source.ny"), ("routes", capabilities.ROUTES[:4]),
                            ("compiler_args", ["--no-type-check"]), ("expected_exit", False)):
            with self.subTest(name=name):
                self.rejection(lambda report: report["inputs"].update({name: value}))

    def test_xfail_skip_and_fail_never_count_as_support(self):
        for status in ("XFAIL", "SKIP", "FAIL", "XPASS"):
            with self.subTest(status=status):
                self.rejection(lambda value: value["results"][0].update(status=status))
                self.rejection(lambda value: value["results"][0].update(raw_status=status))

    def test_exact_result_counts_and_unique_routes(self):
        self.rejection(lambda value: value["results"].pop())
        self.rejection(lambda value: value["results"].append(deepcopy(value["results"][0])))
        self.rejection(lambda value: value["results"].__setitem__(1, deepcopy(value["results"][0])))
        self.rejection(lambda value: value["summary"].update(FAIL=False))

    def test_missing_failed_and_duplicate_checks(self):
        self.rejection(lambda value: value["results"][0].update(checks=[]))
        self.rejection(lambda value: value["results"][0]["checks"].pop())
        self.rejection(lambda value: value["results"][0]["checks"][0].update(passed=False))
        self.rejection(lambda value: value["results"][0]["checks"].append(value["results"][0]["checks"][0]))

    def test_command_outcomes_and_output_not_just_status(self):
        for field, value in (("exit_code", 1), ("exit_code", False), ("timed_out", True),
                             ("spawn_error", "failed"), ("stdout", "forged success"),
                             ("stderr", "warning"), ("stdout_truncated", True), ("argv", ["/other/tool"])):
            with self.subTest(field=field, value=value):
                self.rejection(lambda report: report["results"][0]["commands"]["run"].update({field: value}))
        self.rejection(lambda value: value["results"][0]["commands"].pop("compile"))

    def test_junit_selected_proof_must_pass(self):
        for child in ("<failure/>", "<error/>", "<skipped/>"):
            with self.subTest(child=child):
                self.write_junit(child)
                with self.assertRaises(ValueError):
                    self.collect()

    def test_manifest_proof_bindings(self):
        record = self.collect()
        for field in ("source", "tools", "runtime_sha256", "shared_runtime_sha256", "ctest_report_sha256"):
            with self.subTest(field=field):
                changed = deepcopy(record)
                changed["evidence"][field] = None
                self.assertTrue(capabilities.capability_errors(changed, self.evidence, True))
        changed = deepcopy(record)
        changed["catalog"]["groups"][0]["observed"] = "changed"
        self.assertTrue(capabilities.capability_errors(changed, self.evidence, True))
        changed = deepcopy(record)
        changed["evidence"]["proofs"][0]["routes"][0]["commands"]["run"]["argv_sha256"] = "0" * 64
        self.assertTrue(capabilities.capability_errors(changed, self.evidence, True))

    def test_malformed_manifest_shapes_report_errors_not_tracebacks(self):
        record = self.collect()
        mutations = [
            lambda value: value.update(catalog=[]),
            lambda value: value["catalog"].update(proofs=[None]),
            lambda value: value["catalog"].update(groups=[{"id": []}]),
            lambda value: value.update(evidence=[]),
            lambda value: value["evidence"].update(proofs=[None] * len(self.catalog["proofs"])),
            lambda value: value["evidence"]["proofs"][0].update(contract=None),
            lambda value: value["evidence"]["proofs"][0]["contract"].update(inputs=[]),
            lambda value: value["evidence"]["proofs"][0].update(routes=[None] * 6),
            lambda value: value["evidence"]["proofs"][0]["routes"][0].update(route=[]),
            lambda value: value["evidence"]["proofs"][0]["routes"][0].update(checks=[]),
            lambda value: value["evidence"]["proofs"][0]["routes"][0].update(commands=[]),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = deepcopy(record)
                mutate(changed)
                self.assertTrue(capabilities.capability_errors(changed, self.evidence, True))
        for invalid in (None, [], True, 12, "claimed"):
            self.assertTrue(capabilities.capability_errors(invalid, self.evidence, True))


if __name__ == "__main__":
    unittest.main()
