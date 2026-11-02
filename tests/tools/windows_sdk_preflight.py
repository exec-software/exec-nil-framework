#!/usr/bin/env python3
"""Bounded Windows SDK prerequisite checks; no build or candidate is started."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/build-sdk.ps1"


@unittest.skipUnless(sys.platform == "win32", "Windows PowerShell prerequisite contract")
class WindowsSdkPreflightTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nil-sdk-preflight-")
        self.addCleanup(self.directory.cleanup)
        # Brackets exercise literal PowerShell path lookup as well as argument
        # preservation. Metacharacters must never be evaluated by cmd or -Command.
        self.repo = Path(self.directory.name) / "NIL café 中文 & %PATH% [checkout]"
        self.build = self.repo / "configured café 中文 [Release]"
        self.candidate = self.repo / "must not exist [candidates]"
        self.tools = self.repo / "tool lookup"
        (self.repo / "scripts").mkdir(parents=True)
        self.build.mkdir()
        self.tools.mkdir()
        self.script = self.repo / "scripts/build-sdk.ps1"
        shutil.copyfile(SCRIPT, self.script)
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\n"
            "NIL_ENABLE_LLVM:BOOL=ON\n"
            "NIL_BUILD_EXPERIMENTAL_TOOLS:BOOL=ON\n"
            "BUILD_TESTING:BOOL=ON\n"
            "CMAKE_INSTALL_BINDIR:PATH=.\n"
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.repo.as_posix()}\n",
            encoding="utf-8",
        )
        self.powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        self.assertTrue(self.powershell.is_file(), "Windows PowerShell 5.1 is required for this contract")
        self.environment = {
            key: value for key, value in os.environ.items()
            if key.upper() not in {"PATH", "INCLUDE", "LIB", "LIBPATH", "VSCMD_VER", "VSCMD_ARG_TGT_ARCH"}
        }
        self.environment["PATH"] = str(self.tools)
        # Keep assertion output readable without relying on the parent console's
        # codepage. Only ASCII diagnostic fragments are matched below.
        self.environment["NO_COLOR"] = "1"

    def invoke(self, python="python"):
        result = subprocess.run(
            [str(self.powershell), "-NoLogo", "-NoProfile", "-NonInteractive", "-File", str(self.script),
             "-BuildDirectory", str(self.build), "-CandidateParent", str(self.candidate), "-Python", python],
            cwd=self.repo, env=self.environment, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20,
        )
        self.assertNotEqual(result.returncode, 0, "Preflight unexpectedly proceeded to a build")
        self.assertFalse(self.candidate.exists(), "Failed preflight created candidate output")
        return result.stdout.decode("utf-8", errors="replace")

    def developer_environment(self):
        # These are test-only environment fields, not a discovered/activated VS
        # installation. Application lookup must fail before any tool executes.
        self.environment.update(VSCMD_VER="test-only", VSCMD_ARG_TGT_ARCH="x64", INCLUDE="test-only", LIB="test-only")

    def test_desktop_environment_fails_before_creating_candidate(self):
        self.assertIn("An x64 Visual Studio developer environment is required", self.invoke())

    def test_wrong_target_and_missing_include_or_lib_are_rejected(self):
        for field, value in (("VSCMD_ARG_TGT_ARCH", "x86"), ("INCLUDE", ""), ("LIB", "")):
            with self.subTest(field=field):
                self.developer_environment()
                self.environment[field] = value
                self.assertIn("An x64 Visual Studio developer environment is required", self.invoke())

    def test_missing_applications_fail_before_creating_candidate(self):
        self.developer_environment()
        names = ("cl.exe", "link.exe", "cmake.exe", "ctest.exe", "git.exe", "python.exe")
        # Real, inert copies permit Get-Command application lookup. None is
        # executed: every iteration lacks a prerequisite checked before Git.
        for missing in names:
            with self.subTest(application=missing):
                for name in names:
                    destination = self.tools / name
                    if destination.exists():
                        destination.unlink()
                    if name != missing:
                        shutil.copyfile(sys.executable, destination)
                expected = "python" if missing == "python.exe" else missing
                self.assertIn(f"Required application '{expected}' is unavailable", self.invoke())

    def test_explicit_missing_python_path_is_literal(self):
        self.developer_environment()
        for name in ("cl.exe", "link.exe", "cmake.exe", "ctest.exe", "git.exe"):
            shutil.copyfile(sys.executable, self.tools / name)
        missing = self.tools / "Python café 中文 [missing].exe"
        self.assertIn("is unavailable", self.invoke(str(missing)))


if __name__ == "__main__":
    unittest.main()
