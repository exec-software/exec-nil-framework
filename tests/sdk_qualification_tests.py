#!/usr/bin/env python3
"""Bounded SDK evidence contracts; no Ny build or publication is performed."""

from __future__ import annotations

import json
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import sdk_qualification as qualification


def fixture(revision="a" * 40):
    hashes = {name: "b" * 64 for name in qualification.TOOLS}
    return {
        "schema": "ny.sdk.v2",
        "source": {"revision": revision, "worktree_dirty": False},
        "host": {"system": "Linux", "machine": "x86_64"},
        "build": {"type": "Release", "llvm": "ON"},
        "tools": [{"name": name, "sha256": value} for name, value in hashes.items()],
        "native_runtime": {"sha256": "c" * 64},
        "qualification": {
            "mode": "rc-checks", "compiler_version": "nilcompiler 0.1.0",
            "native_backend_available": True,
            "evidence": {
                "source": {"revision": revision, "dirty": False, "fingerprint": "d" * 64},
                "build": {"type": "Release", "llvm_requested": "ON", "tests_requested": "ON", "cmake_version": "3.28.3",
                          "cxx_id": "GNU", "cxx_version": "13.3.0", "cxx_path": "/usr/bin/c++",
                          "llvm_version": "18.1.3", "system": "Linux", "machine": "x86_64",
                          "cross_compiling": False},
                "tests_enabled": True, "tools": hashes, "runtime_sha256": "c" * 64,
                "ctest": {"tests": 2, "passed": 2, "failed": 0, "errors": 0, "skipped": 0,
                          "report_sha256": "e" * 64, "registered_names_sha256": "f" * 64},
            },
        },
    }


class QualificationTests(unittest.TestCase):
    def rejection(self, value, expected):
        errors = qualification.qualification_errors(value, True, "a" * 40)
        self.assertTrue(any(expected in error for error in errors), errors)

    def test_positive_metadata(self):
        self.assertEqual(qualification.qualification_errors(fixture(), True, "a" * 40), [])

    def test_nil_sdk_requires_matching_shared_runtime_evidence(self):
        value = fixture()
        value["schema"] = "nil.sdk.v1"
        value["native_shared_runtime"] = {"sha256": "1" * 64}
        value["qualification"]["capabilities"] = {}
        value["qualification"]["evidence"]["shared_runtime_sha256"] = "1" * 64
        with patch.object(qualification, "capability_errors", return_value=[]):
            self.assertEqual(qualification.qualification_errors(value, True), [])
            value["native_shared_runtime"]["sha256"] = "2" * 64
            self.rejection(value, "shared runtime hash differs")
            value["qualification"]["evidence"].pop("shared_runtime_sha256")
            self.rejection(value, "evidence fields are incomplete")

    def test_dirty_candidate(self):
        value = fixture()
        value["source"]["worktree_dirty"] = True
        value["qualification"]["evidence"]["source"]["dirty"] = True
        self.rejection(value, "clean source")

    def test_tests_skipped_candidate(self):
        value = fixture()
        value["qualification"]["evidence"].update(tests_enabled=False, ctest=None)
        self.rejection(value, "tests ON")

    def test_requested_llvm_is_not_actual_llvm(self):
        value = fixture()
        value["qualification"]["native_backend_available"] = False
        self.rejection(value, "actually available LLVM")
        self.rejection(value, "actual backend availability")

    def test_missing_detected_llvm_version(self):
        value = fixture()
        value["qualification"]["evidence"]["build"]["llvm_version"] = None
        self.rejection(value, "detected LLVM version")

    def test_final_revision_mismatch(self):
        self.rejection(fixture("b" * 40), "expected final revision")

    def test_snapshot_revision_mismatch(self):
        value = fixture()
        value["qualification"]["evidence"]["source"]["revision"] = "b" * 40
        self.rejection(value, "source identity differs")

    def test_artifact_changes_after_tests(self):
        value = fixture()
        value["tools"][0]["sha256"] = "e" * 64
        self.rejection(value, "tool hashes differ")
        value = fixture()
        value["native_runtime"]["sha256"] = "e" * 64
        self.rejection(value, "runtime hash differs")

    def test_configured_test_setting_must_match_evidence(self):
        value = fixture()
        value["qualification"]["evidence"]["build"]["tests_requested"] = "OFF"
        self.rejection(value, "configured CMake BUILD_TESTING")

    def test_failed_empty_and_all_skipped_ctest(self):
        for update in ({"passed": 1, "failed": 1},
                       {"tests": 0, "passed": 0},
                       {"passed": 0, "skipped": 2}):
            with self.subTest(update=update):
                value = fixture()
                value["qualification"]["evidence"]["ctest"].update(update)
                errors = qualification.qualification_errors(value, True, "a" * 40)
                self.assertTrue(any("CTest" in error for error in errors), errors)

    def test_any_skip_is_rejected_for_rc_but_recorded_for_development(self):
        value = fixture()
        value["qualification"]["evidence"]["ctest"].update(passed=1, skipped=1)
        self.rejection(value, "without failures/errors/skips")
        value["qualification"]["mode"] = "development"
        self.assertEqual(qualification.qualification_errors(value), [])

    def test_unsupported_host_and_cross_build(self):
        value = fixture()
        value["host"]["system"] = "Darwin"
        self.rejection(value, "Linux x86_64")
        value = fixture()
        value["qualification"]["evidence"]["build"]["cross_compiling"] = True
        self.rejection(value, "native Linux x86_64")

    def test_legacy_and_unqualified_development_remain_usable(self):
        value = fixture()
        value["schema"] = "ny.sdk.v1"
        value.pop("qualification")
        self.assertEqual(qualification.qualification_errors(value), [])
        self.rejection(value, "development-only")
        value = fixture()
        value["source"]["worktree_dirty"] = True
        value["qualification"].update(mode="development", evidence=None)
        self.assertEqual(qualification.qualification_errors(value), [])
        self.rejection(value, "development-only")

    def test_malformed_metadata_fails_without_traceback(self):
        for key in ("source", "build", "tools", "native_runtime", "qualification"):
            with self.subTest(key=key):
                value = fixture()
                value[key] = None
                self.assertTrue(qualification.qualification_errors(value, True, "a" * 40))

    def test_ctest_report_exact_set_and_freshness(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-evidence-test-") as temporary:
            report = Path(temporary) / "ctest.xml"
            report.write_text('<testsuite><testcase name="a"/><testcase name="b"><skipped/>'
                              '</testcase></testsuite>', encoding="utf-8")
            result = qualification.summarize_ctest(report, ["a", "b"], 0)
            self.assertEqual((result["tests"], result["passed"], result["skipped"]), (2, 1, 1))
            self.assertEqual(result["report_sha256"], qualification.digest(report))
            with self.assertRaisesRegex(ValueError, "registered tests"):
                qualification.summarize_ctest(report, ["a"], 0)
            with self.assertRaisesRegex(ValueError, "predates"):
                qualification.summarize_ctest(report, ["a", "b"], report.stat().st_mtime_ns + 1)
            report.write_text('<!DOCTYPE x [<!ENTITY a "a">]><testsuite/>', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "entity"):
                qualification.summarize_ctest(report, [], 0)

    def test_ctest_duplicate_names_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-report-duplicates-") as temporary:
            report = Path(temporary) / "ctest.xml"
            report.write_text('<testsuite><testcase name="a"/><testcase name="a"/></testsuite>',
                              encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "registered tests"):
                qualification.summarize_ctest(report, ["a", "a"], 0)

    def test_build_configuration_reads_configured_versions_not_current_executables(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-config-evidence-") as temporary:
            root = Path(temporary)
            configured = root / "build/CMakeFiles/3.28.3"
            configured.mkdir(parents=True)
            llvm = root / "llvm"
            llvm.mkdir()
            (llvm / "LLVMConfig.cmake").write_text('set(LLVM_PACKAGE_VERSION "18.1.3")\n',
                                                   encoding="utf-8")
            (root / "build/CMakeCache.txt").write_text(
                "CMAKE_HOME_DIRECTORY:INTERNAL={}\nCMAKE_BUILD_TYPE:STRING=Release\n"
                "CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3\nCMAKE_CACHE_MINOR_VERSION:INTERNAL=28\n"
                "CMAKE_CACHE_PATCH_VERSION:INTERNAL=3\nNIL_ENABLE_LLVM:BOOL=ON\n"
                "BUILD_TESTING:BOOL=ON\nLLVM_DIR:PATH={}\n".format(root, llvm), encoding="utf-8")
            (configured / "CMakeCXXCompiler.cmake").write_text(
                'set(CMAKE_CXX_COMPILER "/configured/c++")\nset(CMAKE_CXX_COMPILER_ID "GNU")\n'
                'set(CMAKE_CXX_COMPILER_VERSION "13.3.0")\n', encoding="utf-8")
            (configured / "CMakeSystem.cmake").write_text(
                'set(CMAKE_SYSTEM_NAME "Linux")\nset(CMAKE_SYSTEM_PROCESSOR "x86_64")\n'
                'set(CMAKE_CROSSCOMPILING "FALSE")\n', encoding="utf-8")
            configuration = qualification.build_configuration(root, root / "build")
            self.assertEqual(configuration["cmake_version"], "3.28.3")
            self.assertEqual(configuration["cxx_path"], "/configured/c++")
            self.assertEqual(configuration["llvm_version"], "18.1.3")
            self.assertEqual(configuration["tests_requested"], "ON")
            self.assertFalse(configuration["cross_compiling"])
            with self.assertRaisesRegex(ValueError, "different source"):
                qualification.build_configuration(root / "other", root / "build")

    def test_build_record_rejects_configured_tests_mismatch(self):
        value = fixture()["qualification"]["evidence"]
        with patch.object(qualification, "source_identity", return_value=value["source"]), \
             patch.object(qualification, "build_configuration", return_value=value["build"]):
            with self.assertRaisesRegex(ValueError, "BUILD_TESTING"):
                qualification.record_build(Path("/unused-repo"), Path("/unused-build"), "a" * 40, False)

    def test_collection_binds_source_tools_runtime_and_complete_report(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-collect-evidence-") as temporary:
            root = Path(temporary)
            stage = root / "stage"
            stage.mkdir()
            for name in qualification.TOOLS:
                (stage / qualification.executable(name)).write_text(name, encoding="utf-8")
            runtime = root / "libnil.a"
            runtime.write_bytes(b"fixture archive")
            shared = root / "libnil.so"
            shared.write_bytes(b"fixture shared runtime")
            report = root / "ctest.xml"
            report.write_text('<testsuite><testcase name="a"/><testcase name="b"/></testsuite>',
                              encoding="utf-8")
            value = fixture()["qualification"]["evidence"]
            snapshot = {"schema": "nil.sdk.build-evidence.v1", "recorded_ns": 0,
                        "source": value["source"], "build": value["build"],
                        "tests_enabled": True, "registered_tests": ["a", "b"],
                        "tools": {name: qualification.digest(stage / qualification.executable(name))
                                  for name in qualification.TOOLS},
                        "runtime_sha256": qualification.digest(runtime),
                        "shared_runtime_sha256": qualification.digest(shared)}
            evidence = root / "evidence.json"
            evidence.write_text(json.dumps(snapshot), encoding="utf-8")
            with patch.object(qualification, "source_identity", return_value=value["source"]) as identity, \
                 patch.object(qualification, "command", return_value="nilcompiler 0.1.0"), \
                 patch.object(qualification, "native_backend_available", return_value=True):
                def collect(evidence_path=evidence, report_path=report):
                    return qualification.collect_qualification(
                        root, stage, runtime, "a" * 40, "rc-checks", evidence_path, report_path, shared_runtime=shared)

                result = collect()
                self.assertEqual(result["evidence"]["ctest"]["passed"], 2)
                self.assertEqual(result["evidence"]["tools"], snapshot["tools"])
                with self.assertRaisesRegex(ValueError, "requires its CTest"):
                    collect(report_path=None)
                with self.assertRaisesRegex(ValueError, "pre-test build evidence"):
                    collect(evidence_path=None)
                identity.return_value = dict(value["source"], fingerprint="e" * 64)
                with self.assertRaisesRegex(ValueError, "source identity changed"):
                    collect()
                identity.return_value = value["source"]
                original_shared = shared.read_bytes()
                shared.write_bytes(b"changed shared runtime")
                with self.assertRaisesRegex(ValueError, "shared runtime differs"):
                    collect()
                shared.write_bytes(original_shared)
                original_runtime = runtime.read_bytes()
                runtime.write_bytes(b"changed archive")
                with self.assertRaisesRegex(ValueError, "tools/runtime differ"):
                    collect()
                runtime.write_bytes(original_runtime)
                (stage / qualification.executable("nilcompiler")).write_bytes(b"changed compiler")
                with self.assertRaisesRegex(ValueError, "tools/runtime differ"):
                    collect()

    def test_backend_probe_requires_successful_nonempty_object(self):
        for status, diagnostic, output in ((0, "", b"object"),
                                           (1, "this Ny build has no LLVM backend", None),
                                           (0, "", None), (0, "", b""),
                                           (1, "unexpected failure", None),
                                           (1, "this Ny build has no LLVM backend", b"partial")):
            with self.subTest(status=status, diagnostic=diagnostic, output=output):
                def run(arguments, **kwargs):
                    if output is not None:
                        Path(arguments[2]).write_bytes(output)
                    return subprocess.CompletedProcess(arguments, status, stdout="", stderr=diagnostic)

                with patch.object(qualification.subprocess, "run", side_effect=run):
                    if status == 0 and output:
                        self.assertTrue(qualification.native_backend_available(Path("/unused/nilassembler")))
                    elif status > 0 and "no LLVM backend" in diagnostic and output is None:
                        self.assertFalse(qualification.native_backend_available(Path("/unused/nilassembler")))
                    else:
                        with self.assertRaisesRegex(ValueError, "capability probe failed"):
                            qualification.native_backend_available(Path("/unused/nilassembler"))

    def test_real_clean_git_identity_and_subsequent_edit(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-clean-source-") as temporary:
            root = Path(temporary)
            source = root / "source.ny"
            source.write_text("fn main() -> i32\nreturn 0\nend fn\n", encoding="utf-8")
            subprocess.run(["git", "init", "--quiet", str(root)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            subprocess.run(["git", "add", "source.ny"], cwd=root, check=True)
            subprocess.run(["git", "-c", "user.name=NIL Framework SDK Tests", "-c", "user.email=sdk@example.invalid",
                            "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgsign=false",
                            "commit", "--quiet", "-m", "qualification fixture"], cwd=root, check=True)
            identity = qualification.source_identity(root)
            self.assertFalse(identity["dirty"])
            value = fixture(identity["revision"])
            value["qualification"]["evidence"]["source"] = identity
            self.assertEqual(qualification.qualification_errors(value, True, identity["revision"]), [])
            source.write_text("changed\n", encoding="utf-8")
            changed = qualification.source_identity(root)
            self.assertTrue(changed["dirty"])
            self.assertNotEqual(changed["fingerprint"], identity["fingerprint"])

    def test_builder_rejects_invalid_rc_settings_before_build(self):
        for setting in ({"NIL_RUN_TESTS": "OFF"}, {"NIL_ENABLE_LLVM": "OFF"},
                        {"NIL_ALLOW_DIRTY_SOURCE": "ON"}, {"NIL_BUILD_TYPE": "Debug"}):
            environment = os.environ.copy()
            environment.update(NIL_SDK_QUALIFICATION="rc-checks", NIL_RUN_TESTS="ON",
                               NIL_ENABLE_LLVM="ON", NIL_ALLOW_DIRTY_SOURCE="OFF",
                               NIL_BUILD_TYPE="Release", NIL_BUILD_EXPERIMENTAL_TOOLS="ON",
                               NIL_BUILD_DIR=str(ROOT / "build"))
            environment.update(setting)
            result = subprocess.run(["bash", str(ROOT / "scripts/build-sdk.sh")], env=environment,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("RC SDK checks require", result.stderr)

    def test_builder_canonical_environment_wins_over_legacy_alias(self):
        environment = {key: value for key, value in os.environ.items() if not key.startswith(("NY_", "NIL_"))}
        environment.update(NIL_SDK_QUALIFICATION="invalid", NY_SDK_QUALIFICATION="development")
        result = subprocess.run(["bash", str(ROOT / "scripts/build-sdk.sh")], env=environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("NIL_SDK_QUALIFICATION must", result.stderr)
        environment.pop("NIL_SDK_QUALIFICATION")
        environment["NY_SDK_QUALIFICATION"] = "invalid"
        result = subprocess.run(["bash", str(ROOT / "scripts/build-sdk.sh")], env=environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("NIL_SDK_QUALIFICATION must", result.stderr)

    def test_explicit_validator_rc_mode_cannot_skip_execution_or_revision(self):
        for arguments, diagnostic in ((["--skip-smoke", "--expected-revision", "a" * 40], "cannot skip"),
                                      ([], "requires --expected-revision")):
            result = subprocess.run([sys.executable, str(ROOT / "scripts/validate-sdk.py"),
                                     "--require-rc-checks", *arguments, "/nonexistent-sdk-fixture"],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 2)
            self.assertIn(diagnostic, result.stderr)

    def test_expected_revision_rejects_non_git_object_lengths(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/validate-sdk.py"),
                                 "--expected-revision", "a" * 41, "/nonexistent-sdk-fixture"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("full lowercase Git revision", result.stderr)


class RuntimeLayoutTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("nil_package_layout_test", ROOT / "scripts/package-sdk.py")
        self.package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.package)
        self.temporary = tempfile.TemporaryDirectory(prefix="nil-sdk-runtime-layout-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ("nilstd", "docs"):
            (self.root / name).mkdir()
        for name in (*self.package.REQUIRED_TOOLS, "LICENSE"):
            path = self.root / (name if name == "LICENSE" else self.package.executable_name(name))
            path.write_text("fixture")
            path.chmod(0o755)
        from sdk_native_archive_validation import sysv_archive
        self.static = self.root / "libnil.a"
        self.static.write_bytes(sysv_archive())
        self.shared = self.root / "libnil.so"
        header = bytearray(120)
        header[:7] = b"\x7fELF\x02\x01\x01"
        header[16:18] = (3).to_bytes(2, "little")
        header[32:40] = (64).to_bytes(8, "little")
        header[54:56] = (56).to_bytes(2, "little")
        header[56:58] = (1).to_bytes(2, "little")
        self.shared.write_bytes(header)

    def test_both_runtime_forms_required_and_returned(self):
        self.assertEqual(self.package.validate_stage_input(self.root), (self.static, self.shared))
        self.shared.unlink()
        with self.assertRaisesRegex(SystemExit, "matching shared native runtime"):
            self.package.validate_stage_input(self.root)

    def test_duplicate_or_malformed_shared_runtime_rejected(self):
        (self.root / "libnil.dylib").write_bytes(self.shared.read_bytes())
        with self.assertRaisesRegex(SystemExit, "matching shared native runtime"):
            self.package.validate_stage_input(self.root)
        (self.root / "libnil.dylib").unlink()
        self.shared.write_bytes(b"not a shared library")
        with self.assertRaisesRegex(SystemExit, "invalid library framing"):
            self.package.validate_stage_input(self.root)

    def test_windows_pair_accepts_mingw_and_msvc_static_names(self):
        self.shared.unlink()
        self.shared = self.root / "nil.dll"
        header = bytearray(512)
        header[:2] = b"MZ"
        header[60:64] = (128).to_bytes(4, "little")
        header[128:132] = b"PE\0\0"
        header[134:136] = (1).to_bytes(2, "little")  # One section.
        header[148:150] = (240).to_bytes(2, "little")
        header[150:152] = (0x2000).to_bytes(2, "little")  # IMAGE_FILE_DLL.
        header[152:154] = (0x20b).to_bytes(2, "little")  # PE32+.
        self.shared.write_bytes(header)
        self.assertEqual(self.package.validate_stage_input(self.root), (self.static, self.shared))
        msvc_static = self.static.rename(self.root / "nil.lib")
        self.assertEqual(self.package.validate_stage_input(self.root), (msvc_static, self.shared))
        self.static.write_bytes(msvc_static.read_bytes())
        with self.assertRaisesRegex(SystemExit, "exactly one native runtime archive"):
            self.package.validate_stage_input(self.root)

    def test_platform_pair_mismatch_rejected(self):
        self.static.rename(self.root / "nil.lib")
        with self.assertRaisesRegex(SystemExit, "matching shared native runtime"):
            self.package.validate_stage_input(self.root)

    def test_validator_existing_stage_fails_cleanly_before_inventory(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/validate-sdk.py"),
                                 str(self.root), "--skip-smoke"], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("SDK validation failed", result.stdout)

    def test_new_manifest_requires_shared_record(self):
        spec = importlib.util.spec_from_file_location("nil_validate_layout_test", ROOT / "scripts/validate-sdk.py")
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)
        path = "nillib/native/libnil.so"
        record = {"path": path, "bytes": self.shared.stat().st_size, "sha256": qualification.digest(self.shared)}
        errors = []
        validator.validate_shared_runtime(record, {path: self.shared}, errors)
        self.assertEqual(errors, [])
        record["sha256"] = "0" * 64
        validator.validate_shared_runtime(record, {path: self.shared}, errors)
        self.assertTrue(any("hash mismatch" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
