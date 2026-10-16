#!/usr/bin/env python3
"""Consumer qualification rejects vacuous or mismatched route evidence."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sdk_consumer", ROOT / "scripts/check-sdk-routes.py")
consumer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(consumer)


class ConsumerTests(unittest.TestCase):
    sdk = Path("/sdk")
    source = Path("/private/source.ny")
    expected = Path("/private/expected.stdout")
    library = sdk / "nillib"

    def test_consumer_frontend_extension_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for suffix in (".ny", ".matrix", ".mat"):
                source = directory / ("consumer" + suffix)
                source.write_text("fixture")
                self.assertEqual(consumer.fixture_source("consumer", directory), source)
                source.unlink()
            with self.assertRaises(ValueError):
                consumer.fixture_source("consumer", directory)
            (directory / "consumer.ny").write_text("fixture")
            (directory / "consumer.matrix").write_text("fixture")
            with self.assertRaises(ValueError):
                consumer.fixture_source("consumer", directory)

    def fixture(self):
        report = {
            "schema": "ny.route-matrix.v1",
            "inputs": {"source": str(self.source), "compiler": "/sdk/nilcompiler",
                       "interpreter": "/sdk/nilinterpreter", "expected_exit": 0,
                       "routes": list(consumer.ROUTES), "strict": True,
                       "compiler_args": ["--sdk-path", str(self.library)],
                       "expected_stdout": str(self.expected), "expected_stderr": None,
                       "with_stdlib": False, "interpreter_args": [], "program_args": [],
                       "text_nil_contains": [], "xfail_stderr_contains": {}},
            "run_directory": "/private/routes/run",
            "summary": {"PASS": 6, "FAIL": 0, "SKIP": 0, "XFAIL": 0, "XPASS": 0},
            "results": [{"route": name, "status": "PASS", "raw_status": "PASS",
                         "checks": [{"passed": True}],
                         "commands": {"run": {"exit_code": 0, "timed_out": False,
                                               "spawn_error": None}}}
                        for name in consumer.ROUTES],
        }
        for result in report["results"]:
            route = result["route"]
            directory = Path(report["run_directory"]) / route
            result["checks"] = [{"name": name, "passed": True}
                                for name in consumer.check_names(route)]
            result["commands"] = {
                phase: {"argv": argv, "cwd": str(directory), "exit_code": 0,
                        "timed_out": False, "spawn_error": None, "stderr": "",
                        "stdout": "" if phase == "compile" else "ok\n",
                        "stdout_truncated": False, "stderr_truncated": False}
                for phase, argv in consumer.command_argv(report["inputs"], route, directory).items()
            }
        return report

    def check(self, report):
        consumer.check_report(report, self.source, self.sdk, self.library, self.expected, "ok\n")

    def test_positive(self):
        self.check(self.fixture())

    def test_every_nonpass_status_rejects(self):
        for status in ("XFAIL", "XPASS", "FAIL", "SKIP", None):
            for key in ("status", "raw_status"):
                with self.subTest(status=status, key=key):
                    report = self.fixture()
                    report["results"][0][key] = status
                    with self.assertRaises(ValueError):
                        self.check(report)

    def test_route_coverage_must_be_complete_and_unique(self):
        for results in ([], None, self.fixture()["results"][:-1],
                        [self.fixture()["results"][0]] * 6, [None] * 6):
            report = self.fixture()
            report["results"] = results
            with self.assertRaises(ValueError):
                self.check(report)

    def test_input_contract_is_exact(self):
        changes = {"source": "/repo/fallback.ny", "compiler": "/build/nilcompiler",
                   "interpreter": "/build/nilinterpreter", "strict": False,
                   "compiler_args": [], "routes": ["llvm-aot"], "expected_exit": 1,
                   "with_stdlib": True, "expected_stdout": "/wrong/expected.stdout"}
        for key, value in changes.items():
            with self.subTest(key=key):
                report = self.fixture()
                report["inputs"][key] = value
                with self.assertRaises(ValueError):
                    self.check(report)

    def test_checks_and_commands_cannot_be_missing_or_fail(self):
        for key, values in {
            "checks": ([], None, [{"passed": False}], [{}]),
            "commands": ({}, None, {"run": {}},
                         {"run": {"exit_code": 0, "timed_out": True}},
                         {"run": {"exit_code": 1, "timed_out": False}}),
        }.items():
            for value in values:
                report = self.fixture()
                report["results"][0][key] = copy.deepcopy(value)
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.check(report)

    def test_dummy_commands_or_unnamed_checks_do_not_prove_execution(self):
        for change in ("argv", "stdout", "stderr", "stdout_truncated", "unnamed_check"):
            report = self.fixture()
            result = report["results"][0]
            if change == "unnamed_check":
                result["checks"] = [{"passed": True}]
            else:
                result["commands"]["run"][change] = {
                    "argv": ["/bin/true"], "stdout": "wrong\n", "stderr": "warning\n",
                    "stdout_truncated": True}[change]
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.check(report)


if __name__ == "__main__":
    unittest.main()
