#!/usr/bin/env python3
"""Isolated configure-only tests for the narrow LLVM DIA import repair."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def bracket(value):
    value = str(value)
    if "]====]" in value:
        raise ValueError("unsupported fixture delimiter")
    return "[====[" + value + "]====]"


class DiaConfigureTests(unittest.TestCase):
    def setUp(self):
        self.private = tempfile.TemporaryDirectory(prefix="nil-dia-contract-")
        self.addCleanup(self.private.cleanup)
        self.root = Path(self.private.name)
        self.source = self.root / "source"
        self.build = self.root / "build"
        self.source.mkdir()
        self.old = (self.root / "missing VS 2019" / "diaguids.lib").as_posix()
        self.library = self.root / "Visual Studio 2022 café 中 [sdk]" / "diaguids.lib"
        self.library.parent.mkdir()
        self.library.write_bytes(b"private synthetic DIA fixture\n")

    def project(self, links=None, imported=True, target=True, repeat=False):
        commands = ["cmake_minimum_required(VERSION 3.20)", "project(DiaContract NONE)"]
        if target:
            commands.append("add_library(LLVMDebugInfoPDB INTERFACE" + (" IMPORTED)" if imported else ")"))
            if links is not None:
                commands.append("set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES " + bracket(links) + ")")
        commands.append("include(" + bracket(HELPER) + ")")
        commands.append("nil_relocate_llvm_dia()")
        if repeat:
            commands.append("nil_relocate_llvm_dia()")
        if target:
            commands.append("get_target_property(_actual LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)")
            commands.append('file(WRITE "${CMAKE_BINARY_DIR}/observed.txt" "${_actual}")')
        self.source.joinpath("CMakeLists.txt").write_text("\n".join(commands) + "\n", encoding="utf-8")

    def configure(self, replacement=None, success=True, message=None, typed=True):
        command = [CMAKE, "-S", str(self.source), "-B", str(self.build)]
        if GENERATOR:
            command.extend(["-G", GENERATOR])
        if replacement is not None:
            kind = ":FILEPATH" if typed else ""
            command.append("-DNIL_LLVM_DIA_LIBRARY" + kind + "=" + str(replacement))
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)
        detail = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, success, detail)
        if message:
            self.assertIn(message, detail)
        return detail

    def observed(self):
        return (self.build / "observed.txt").read_text(encoding="utf-8")

    def test_exact_order_and_other_entries_preserved(self):
        other = (self.root / "unrelated-missing.lib").as_posix()
        prefix = "LLVMObject;;$<LINK_ONLY:LLVMSupport>;literal\\;semicolon;"
        suffix = ";" + other + ";$<IF:$<BOOL:1>,LLVMA;LLVMB,LLVMC>;"
        self.project(prefix + self.old + suffix)
        self.configure(self.library.as_posix())
        self.assertEqual(self.observed(), prefix + self.library.as_posix() + suffix)

    def test_single_entry(self):
        self.project(self.old)
        self.configure(self.library.as_posix())
        self.assertEqual(self.observed(), self.library.as_posix())

    def test_existing_valid_is_noop(self):
        links = "LLVMObject;" + self.library.as_posix() + ";LLVMSupport"
        self.project(links)
        self.configure(self.old)
        self.assertEqual(self.observed(), links)

    def test_no_dia_is_noop(self):
        self.project("LLVMSupport;$<LINK_ONLY:LLVMObject>")
        self.configure()
        self.assertEqual(self.observed(), "LLVMSupport;$<LINK_ONLY:LLVMObject>")

    def test_no_target_is_noop(self):
        self.project(target=False)
        self.configure()

    def test_no_link_property_is_noop(self):
        self.project()
        self.configure()
        self.assertEqual(self.observed(), "_actual-NOTFOUND")

    def test_repeated_call_is_idempotent(self):
        self.project(self.old, repeat=True)
        self.configure(self.library.as_posix())
        self.assertEqual(self.observed(), self.library.as_posix())

    def test_cache_survives_regeneration(self):
        self.project(self.old)
        self.configure(self.library.as_posix())
        first = self.observed()
        self.configure()
        self.assertEqual(self.observed(), first)
        cache = (self.build / "CMakeCache.txt").read_text(encoding="utf-8")
        self.assertIn("NIL_LLVM_DIA_LIBRARY:FILEPATH=" + self.library.as_posix(), cache)

    def test_missing_override_fails(self):
        self.project(self.old)
        self.configure(success=False, message="Configure -DNIL_LLVM_DIA_LIBRARY:FILEPATH=")

    def test_missing_override_file_fails(self):
        self.project(self.old)
        self.configure(self.old, success=False, message="must be an existing absolute diaguids.lib file")

    def test_relative_override_fails(self):
        self.project(self.old)
        self.configure("relative/diaguids.lib", success=False, message="must be an existing absolute")

    def test_untyped_relative_override_fails(self):
        self.project(self.old)
        self.configure("relative/diaguids.lib", typed=False, success=False, message="must be an existing absolute")

    def test_directory_override_fails(self):
        directory = self.root / "directory" / "diaguids.lib"
        directory.mkdir(parents=True)
        self.project(self.old)
        self.configure(directory.as_posix(), success=False, message="must be an existing absolute")

    def test_wrong_basename_fails(self):
        wrong = self.library.with_name("other.lib")
        shutil.copyfile(self.library, wrong)
        self.project(self.old)
        self.configure(wrong.as_posix(), success=False, message="must be an existing absolute")

    def test_bare_dia_reference_fails(self):
        self.project("LLVMSupport;diaguids.lib")
        self.configure(self.library.as_posix(), success=False, message="cannot interpret LLVM link entry")

    def test_relative_dia_reference_fails(self):
        self.project("DIA SDK/diaguids.lib")
        self.configure(self.library.as_posix(), success=False, message="cannot interpret LLVM link entry")

    def test_generator_dia_reference_fails(self):
        self.project("$<LINK_ONLY:" + self.old + ">")
        self.configure(self.library.as_posix(), success=False, message="cannot interpret LLVM link entry")

    def test_unknown_dia_like_name_fails(self):
        self.project(self.old + ".unknown")
        self.configure(self.library.as_posix(), success=False, message="cannot interpret LLVM link entry")

    def test_multiple_missing_dia_references_fail(self):
        other = (self.root / "other missing" / "diaguids.lib").as_posix()
        self.project(self.old + ";" + other)
        self.configure(self.library.as_posix(), success=False, message="ambiguous multiple")

    def test_duplicate_missing_dia_reference_fails(self):
        self.project(self.old + ";" + self.old)
        self.configure(self.library.as_posix(), success=False, message="ambiguous multiple")

    def test_mixed_existing_missing_dia_is_ambiguous(self):
        self.project(self.library.as_posix() + ";" + self.old)
        self.configure(self.library.as_posix(), success=False, message="ambiguous multiple")

    def test_existing_dia_directory_fails(self):
        self.library.unlink()
        self.library.mkdir()
        self.project(self.library.as_posix())
        self.configure(success=False, message="LLVM DIA link entry is a directory")

    def test_nonimported_target_fails(self):
        self.project(self.old, imported=False)
        self.configure(self.library.as_posix(), success=False, message="requires imported LLVMDebugInfoPDB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator")
    arguments, remaining = parser.parse_known_args()
    HELPER = arguments.helper.resolve().as_posix()
    CMAKE = arguments.cmake
    GENERATOR = arguments.generator
    unittest.main(argv=[__file__] + remaining)
