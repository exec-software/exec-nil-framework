#!/usr/bin/env python3
"""Consumer qualification rejects vacuous or mismatched route evidence."""

from __future__ import annotations

import copy
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sdk_consumer", ROOT / "scripts/check-sdk-routes.py")
consumer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(consumer)
DURABLE_SPEC = importlib.util.spec_from_file_location(
    "sdk_durable_consumer", ROOT / "scripts/check-sdk-durable-io.py")
durable = importlib.util.module_from_spec(DURABLE_SPEC)
DURABLE_SPEC.loader.exec_module(durable)


# check_report requires an absolute run directory, and on Windows a path with
# no drive letter is not absolute -- "/private/..." is rooted but relative to
# the current drive. Anchor these notional paths to a real root so the fixture
# describes an absolute location on every host.
ANCHOR = Path(Path.cwd().anchor or "/")


class ConsumerTests(unittest.TestCase):
    sdk = ANCHOR / "sdk"
    source = ANCHOR / "private/source.ny"
    expected = ANCHOR / "private/expected.stdout"
    library = sdk / "nillib"

    def test_consumer_frontend_extension_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for suffix in (".ny", ".matrix", ".mat"):
                source = directory / ("consumer" + suffix)
                source.write_text("fixture", newline='\n')
                self.assertEqual(consumer.fixture_source("consumer", directory), source)
                source.unlink()
            with self.assertRaises(ValueError):
                consumer.fixture_source("consumer", directory)
            (directory / "consumer.ny").write_text("fixture", newline='\n')
            (directory / "consumer.matrix").write_text("fixture", newline='\n')
            with self.assertRaises(ValueError):
                consumer.fixture_source("consumer", directory)

    def fixture(self):
        report = {
            "schema": "ny.route-matrix.v1",
            "inputs": {"source": str(self.source),
                       "compiler": str(consumer.sdk_tool(self.sdk, "nilcompiler")),
                       "interpreter": str(consumer.sdk_tool(self.sdk, "nilinterpreter")),
                       "expected_exit": 0,
                       "routes": list(consumer.ROUTES), "strict": True,
                       "compiler_args": ["--sdk-path", str(self.library)],
                       "expected_stdout": str(self.expected), "expected_stderr": None,
                       "with_stdlib": False, "interpreter_args": [], "program_args": [],
                       "text_nil_contains": [], "xfail_stderr_contains": {}},
            "run_directory": str(self.source.parent / "routes/run"),
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

    def test_platform_tools_and_exact_evidence(self):
        for platform, suffix in (("linux", ""), ("win32", ".exe"), ("darwin", "")):
            with self.subTest(platform=platform), mock.patch.object(consumer.sys, "platform", platform):
                for name in ("nilcompiler", "nilinterpreter", "nilassembler", "nillinker"):
                    self.assertEqual(consumer.sdk_tool(self.sdk, name), self.sdk / (name + suffix))
                report = self.fixture()
                self.check(report)
                report["inputs"]["compiler"] = str(self.sdk / (
                    "nilcompiler" if suffix else "nilcompiler.exe"))
                with self.assertRaises(ValueError):
                    self.check(report)

    def test_development_hosts_do_not_broaden_rc_qualification(self):
        for platform in ("linux", "win32", "darwin", "freebsd13"):
            with self.subTest(platform=platform), \
                    mock.patch.object(consumer.sys, "platform", platform), \
                    mock.patch.object(consumer.platform, "machine", return_value="arm64"):
                if platform in ("linux", "win32", "darwin"):
                    consumer.require_supported_host(False)
                else:
                    with self.assertRaises(ValueError):
                        consumer.require_supported_host(False)
                if platform == "linux":
                    consumer.require_supported_host(True)
                else:
                    with self.assertRaisesRegex(ValueError, "RC.*requires Linux"):
                        consumer.require_supported_host(True)

    def test_darwin_admits_only_native_supported_architectures(self):
        with mock.patch.object(consumer.sys, "platform", "darwin"):
            for machine in ("x86_64", "arm64", "i386", "arm64e", "aarch64", ""):
                with self.subTest(machine=machine), \
                        mock.patch.object(consumer.platform, "machine", return_value=machine):
                    if machine in ("x86_64", "arm64"):
                        consumer.require_supported_host(False)
                    else:
                        with self.assertRaisesRegex(ValueError, "native x86_64 or arm64"):
                            consumer.require_supported_host(False)

    def test_darwin_consumer_removes_source_and_loader_overrides(self):
        values = {"PATH": "/toolchain", "HOME": "/consumer", "NIL_LIBRARY_PATH": "/source",
                  "NY_STDLIB_PATH": "/source", "DYLD_LIBRARY_PATH": "/build",
                  "DYLD_FALLBACK_LIBRARY_PATH": "/provider", "DYLD_INSERT_LIBRARIES": "/inject",
                  "CXX": "/foreign/compiler", "SDKROOT": "/foreign/sdk", "LIBRARY_PATH": "/build",
                  "DEVELOPER_DIR": "/foreign/xcode", "TOOLCHAINS": "foreign",
                  "COMPILER_PATH": "/foreign", "GCC_EXEC_PREFIX": "/foreign",
                  "CCC_OVERRIDE_OPTIONS": "foreign"}
        with mock.patch.dict(consumer.os.environ, values, clear=True):
            for platform in ("linux", "win32", "darwin"):
                with self.subTest(platform=platform), mock.patch.object(consumer.sys, "platform", platform):
                    expected = {key: value for key, value in values.items()
                                if not key.startswith(("NIL_", "NY_"))}
                    if platform == "darwin":
                        expected = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "HOME": "/consumer"}
                    self.assertEqual(consumer.consumer_environment(), expected)

    def test_explicit_consumer_work_retains_success_and_failure_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary).resolve(strict=True)
            for failure in (False, True):
                output = parent / str(failure)
                try:
                    with consumer.diagnostic_directory(output) as work:
                        self.assertEqual(work, output)
                        (work / "raw.json").write_text("partial or successful evidence")
                        if failure:
                            raise ValueError("fixture failed")
                except ValueError as error:
                    self.assertTrue(failure)
                    self.assertIn(str(output), str(error))
                self.assertEqual((output / "raw.json").read_text(), "partial or successful evidence")
                with self.assertRaises(FileExistsError):
                    with consumer.diagnostic_directory(output):
                        self.fail("reused existing work directory")

    def test_report_publish_closes_temporary_before_replace(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "consumer.json"
            original_factory = tempfile.NamedTemporaryFile
            original_replace = consumer.os.replace
            opened = []

            def capture(*args, **kwargs):
                stream = original_factory(*args, **kwargs)
                opened.append(stream)
                return stream

            def replace(source, destination):
                self.assertTrue(opened[-1].closed)
                original_replace(source, destination)

            with mock.patch.object(consumer.tempfile, "NamedTemporaryFile", side_effect=capture), \
                    mock.patch.object(consumer.os, "replace", side_effect=replace):
                consumer.write_report(output, {"status": "PASS", "execution_count": 16})
            self.assertEqual(json.loads(output.read_bytes()), {"status": "PASS", "execution_count": 16})
            self.assertNotIn(b"\r", output.read_bytes())
            self.assertEqual(list(output.parent.iterdir()), [output])

    def test_report_publish_failure_preserves_previous_and_cleans_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "consumer.json"
            output.write_bytes(b"previous report\n")
            with mock.patch.object(consumer.os, "replace", side_effect=PermissionError("held open")):
                with self.assertRaises(PermissionError):
                    consumer.write_report(output, {"status": "PASS"})
            self.assertEqual(output.read_bytes(), b"previous report\n")
            self.assertEqual(list(output.parent.iterdir()), [output])

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


class DurableEvidenceTests(unittest.TestCase):
    def fixtures(self, parent):
        source = parent / "execution"
        source.mkdir()
        (source / "logs").mkdir()
        (source / "empty").mkdir()
        (source / "logs" / "raw.bin").write_bytes(b"before\x00\xff\r\nafter")
        (source / "report.json").write_text(json.dumps({"status": "PASS", "work": str(source)}))
        return source, parent / "retained"

    def test_retention_preserves_all_bytes_empty_directories_and_original_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = self.fixtures(Path(directory))
            proof = durable.retain_windows_evidence(source, target)
            self.assertEqual(proof["status"], "PASS")
            self.assertEqual(proof["execution_workspace"], str(source.resolve()))
            self.assertEqual(proof["retained_copy"], str(target.resolve()))
            self.assertTrue(proof["original_workspace_retained"])
            self.assertEqual(proof["file_count"], 2)
            self.assertEqual(durable.retention_inventory(source, time.monotonic()+30),
                             durable.retention_inventory(target, time.monotonic()+30))
            self.assertEqual(json.loads((target / "report.json").read_text())["work"], str(source))
            self.assertTrue((source / "empty").is_dir())

    def test_copy_corruption_and_source_mutation_fail_closed(self):
        for change in ("copy", "source", "namespace"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                source, target = self.fixtures(Path(directory))
                original = durable.copy_retained_file

                def corrupt(incoming, outgoing, expected, deadline):
                    original(incoming, outgoing, expected, deadline)
                    if incoming.name == "raw.bin":
                        if change == "namespace":
                            (source / "unexpected").write_bytes(b"new")
                        else:
                            (outgoing if change == "copy" else incoming).write_bytes(b"changed")

                with mock.patch.object(durable, "copy_retained_file", side_effect=corrupt):
                    with self.assertRaises(ValueError):
                        durable.retain_windows_evidence(source, target)
                self.assertTrue(source.is_dir())

    def test_existing_destination_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = self.fixtures(Path(directory))
            target.mkdir()
            marker = target / "marker"
            marker.write_bytes(b"retained")
            with self.assertRaises(FileExistsError):
                durable.retain_windows_evidence(source, target)
            self.assertEqual(marker.read_bytes(), b"retained")

    def test_symlink_and_windows_reparse_entries_are_not_followed(self):
        for kind in ("symlink", "reparse"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as directory:
                source, target = self.fixtures(Path(directory))
                blocked = source / "logs"
                original = Path.lstat

                def attributes(path, *args, **kwargs):
                    actual = original(path, *args, **kwargs)
                    if path == blocked:
                        return SimpleNamespace(
                            st_mode=stat.S_IFLNK if kind == "symlink" else actual.st_mode,
                            st_file_attributes=0x400 if kind == "reparse" else 0)
                    return actual

                # Exercise both actual lstat verdicts on every host without
                # requiring Windows symlink privileges or creating a junction.
                with mock.patch.object(Path, "lstat", attributes):
                    with self.assertRaisesRegex(ValueError, "links and reparse"):
                        durable.retain_windows_evidence(source, target)
                self.assertFalse(target.exists())
                self.assertEqual((blocked / "raw.bin").read_bytes(), b"before\x00\xff\r\nafter")

    def test_utf16_path_budget_accounts_for_surrogate_pairs(self):
        self.assertTrue(durable.windows_workspace_fits(Path("a" * 96)))
        self.assertFalse(durable.windows_workspace_fits(Path("a" * 97)))
        self.assertTrue(durable.windows_workspace_fits(Path("a" * 94 + "\U0001f512")))
        self.assertFalse(durable.windows_workspace_fits(Path("a" * 95 + "\U0001f512")))

    def test_main_retention_failure_cannot_report_qualification_success(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            sdk, source, output = parent / "sdk", parent / "execution", parent / "evidence"
            sdk.mkdir(); source.mkdir()
            argv = ["qualify", "--sdk", str(sdk), "--expected-revision", "a" * 40,
                    "--output", str(output)]
            report = {"status": "PASS", "cleanup_verified": True, "errors": []}
            stdout, stderr = io.StringIO(), io.StringIO()
            with mock.patch.object(durable, "WINDOWS", True), \
                 mock.patch.object(durable.sys, "argv", argv), \
                 mock.patch.object(durable, "windows_execution_workspace", return_value=source), \
                 mock.patch.object(durable, "qualify", return_value=report), \
                 mock.patch.object(durable, "retain_windows_evidence", side_effect=ValueError("copy failed")), \
                 contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                self.assertEqual(durable.main(), 1)
            retained = list(output.glob("run-*/report.json"))
            self.assertEqual(len(retained), 1)
            failed = json.loads(retained[0].read_text())
            self.assertEqual(failed["status"], "FAIL")
            self.assertEqual(failed["execution_workspace"], str(source))
            self.assertEqual(json.loads((source / "report.json").read_text())["status"], "PASS")
            self.assertNotIn("PASS:", stdout.getvalue())
            self.assertIn("copy failed", stderr.getvalue())

    def test_posix_keeps_requested_execution_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            sdk, output = parent / "sdk", parent / "evidence"
            sdk.mkdir()
            argv = ["qualify", "--sdk", str(sdk), "--expected-revision", "a" * 40,
                    "--output", str(output)]
            with mock.patch.object(durable, "WINDOWS", False), \
                 mock.patch.object(durable.sys, "argv", argv), \
                 mock.patch.object(durable, "windows_execution_workspace") as reserve, \
                 mock.patch.object(durable, "qualify", return_value={"status": "PASS"}) as qualify, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(durable.main(), 0)
            reserve.assert_not_called()
            self.assertEqual(qualify.call_args.args[2].parent, output)
            saved = json.loads(next(output.glob("run-*/report.json")).read_text())
            self.assertNotIn("workspace_provenance", saved)


if __name__ == "__main__":
    unittest.main()
