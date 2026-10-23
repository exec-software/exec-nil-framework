#!/usr/bin/env python3
"""Bounded local-release contracts; no SDK build, upload, or activation."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ny_release_assembler", ROOT / "scripts" / "assemble-release.py")
assert spec is not None and spec.loader is not None
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
sys.path.insert(0, str(ROOT / "tests"))
from sdk_qualification_tests import fixture as qualification_fixture


REVISION = "a" * 40
VERSION = "1.0.0-rc.1"
NOTICES = b"LLVM upstream license\nGCC runtime notices\n"
BUILD = {"cxx_id": "GNU", "cxx_version": "13.3.0", "llvm_version": "18.1.3"}
INVENTORY = {"needed": ["libc.so.6", "libstdc++.so.6"],
             "maximum_required_symbol_versions": {"GLIBC": "2.38", "GLIBCXX": "3.4.32", "CXXABI": "1.3.13"}}


def policy():
    return {"schema": "ny.release-dependencies.v1", "reviewed": True,
            "notices_sha256": hashlib.sha256(NOTICES).hexdigest(),
            "expected_needed": INVENTORY["needed"].copy(),
            "maximum_symbol_versions": INVENTORY["maximum_required_symbol_versions"].copy(),
            "static_components": [
                {"name": "LLVM", "version": "18.1.3", "license": "Apache-2.0 WITH LLVM-exception", "notice_marker": "LLVM upstream license"},
                {"name": "GCC", "version": "13.3.0", "license": "GPL-3.0-or-later WITH GCC-exception-3.1", "notice_marker": "GCC runtime notices"}]}


ELF_REPORT = """ELF Header:
  Class: ELF64
  Data: 2's complement, little endian
  Type: DYN (Position-Independent Executable file)
  Machine: Advanced Micro Devices X86-64
  [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  0x00001 (NEEDED) Shared library: [libc.so.6]
Version needs section '.gnu.version_r' contains 1 entry:
  000000: Version: 1 File: libc.so.6 Cnt: 1
  0x0010: Name: GLIBC_2.38 Flags: none Version: 2
"""


class ReleasePolicyTests(unittest.TestCase):
    def check(self, value):
        release.validate_dependency_policy(value, NOTICES, INVENTORY, BUILD)

    def test_explicit_reviewed_policy(self):
        self.check(policy())

    def test_missing_and_unreviewed_policy(self):
        for key in policy():
            value = policy()
            del value[key]
            with self.subTest(key=key), self.assertRaises(release.ReleaseError):
                self.check(value)
        value = policy()
        value["reviewed"] = False
        with self.assertRaisesRegex(release.ReleaseError, "review is not complete"):
            self.check(value)

    def test_notices_are_hash_pinned(self):
        value = policy()
        value["notices_sha256"] = "0" * 64
        with self.assertRaisesRegex(release.ReleaseError, "SHA-256"):
            self.check(value)

    def test_exact_dependencies_and_abi_ceiling(self):
        for values in (["libc.so.6"], ["libc.so.6", "libstdc++.so.6", "libx.so"]):
            value = policy()
            value["expected_needed"] = values
            with self.assertRaisesRegex(release.ReleaseError, "DT_NEEDED"):
                self.check(value)
        value = policy()
        value["maximum_symbol_versions"]["GLIBC"] = "2.34"
        with self.assertRaisesRegex(release.ReleaseError, "exceeds"):
            self.check(value)

    def test_component_notice_and_version_gate(self):
        for field, replacement in (("notice_marker", "missing"), ("version", "19.0.0")):
            value = policy()
            value["static_components"][0][field] = replacement
            with self.subTest(field=field), self.assertRaises(release.ReleaseError):
                self.check(value)
        value = policy()
        value["static_components"] = []
        with self.assertRaisesRegex(release.ReleaseError, "static"):
            self.check(value)
        with self.assertRaisesRegex(release.ReleaseError, "GNU"):
            release.validate_dependency_policy(policy(), NOTICES, INVENTORY, {**BUILD, "cxx_id": "Clang"})

    def test_numeric_version_comparison(self):
        self.assertGreater(release.numeric_version("2.38"), release.numeric_version("2.9"))
        with self.assertRaises(release.ReleaseError):
            release.numeric_version("2.38-private")


class ElfTests(unittest.TestCase):
    def test_direct_requirements(self):
        value = release.parse_elf_report(ELF_REPORT, "nilcompiler")
        self.assertEqual(value["needed"], ["libc.so.6"])
        self.assertEqual(value["required_symbol_versions"], {"libc.so.6": ["GLIBC_2.38"]})

    def test_shared_runtime_inventory_has_no_program_interpreter(self):
        report = "\n".join(line for line in ELF_REPORT.splitlines() if "Requesting program interpreter" not in line)
        value = release.parse_elf_report(report, "nillib/native/libnil.so", shared=True)
        self.assertIsNone(value["interpreter"])
        self.assertEqual(value["needed"], ["libc.so.6"])
        with self.assertRaises(release.ReleaseError):
            release.parse_elf_report(ELF_REPORT, "nillib/native/libnil.so", shared=True)

    def test_wrong_architecture_interpreter_and_search_path(self):
        for report in (ELF_REPORT.replace("X86-64", "ARM"),
                       ELF_REPORT.replace("ELF64", "ELF32"),
                       ELF_REPORT.replace("/lib64/ld-linux-x86-64.so.2", "/tmp/custom-ld"),
                       ELF_REPORT + "0x00001 (RUNPATH) Library runpath: [/home/builder/lib]\n",
                       ELF_REPORT.replace("File: libc.so.6", "File: libother.so.6")):
            with self.subTest(report=report), self.assertRaises(release.ReleaseError):
                release.parse_elf_report(report, "nilcompiler")

    def test_definitions_are_not_requirements(self):
        report = ELF_REPORT + "Version definition section '.gnu.version_d' contains 1 entry:\n Name: GLIBC_99.0 Flags: none\n"
        result = release.parse_elf_report(report, "nilcompiler")
        self.assertNotIn("GLIBC_99.0", str(result))


class SdkGateTests(unittest.TestCase):
    def sdk_rejection(self, modify, expected):
        manifest = qualification_fixture()
        manifest["qualification"]["compiler_version"] = "nilcompiler " + VERSION
        modify(manifest)
        validators = ("validate_exact_layout", "validate_manifest_metadata", "validate_tools",
                      "validate_native_runtime", "validate_shared_runtime", "validate_standard_library", "validate_documentation", "validate_payload")
        patches = [patch.object(release.sdk_validator, name) for name in validators]
        patches += [patch.object(release.sdk_validator, "root_layout_errors", return_value=[]),
                    patch.object(release.sdk_validator, "inventory_tree", return_value=({}, set())),
                    patch.object(release, "load_json", return_value=manifest)]
        with ExitStack() as stack:
            for item in patches:
                stack.enter_context(item)
            with self.assertRaisesRegex(release.ReleaseError, expected):
                release.validate_sdk(Path("/fixture"), REVISION, VERSION)

    def test_dirty_sdk_rejected(self):
        self.sdk_rejection(lambda m: m["source"].update(worktree_dirty=True), "clean source")

    def test_development_sdk_rejected(self):
        self.sdk_rejection(lambda m: m["qualification"].update(mode="development"), "development-only")

    def test_missing_evidence_rejected(self):
        self.sdk_rejection(lambda m: m["qualification"].update(evidence=None), "evidence")

    def test_version_mismatch_rejected(self):
        self.sdk_rejection(lambda m: m["qualification"].update(compiler_version="nilcompiler 0.1.0"), "VERSION")

    def test_revision_mismatch_rejected(self):
        self.sdk_rejection(lambda m: m["source"].update(revision="b" * 40), "expected final revision")

    def test_dirty_checkout_rejected(self):
        with patch.object(release, "capture", return_value=" M source.cpp\n"):
            with self.assertRaisesRegex(release.ReleaseError, "clean NIL Framework checkout"):
                release.source_identity(Path("/fixture"), REVISION)

    def test_missing_readelf_and_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk = root / "sdk"
            sdk.mkdir()
            args = argparse.Namespace(repo_root=ROOT, sdk=sdk, output=root / "output")
            with patch.object(release.shutil, "which", return_value=None):
                with self.assertRaisesRegex(release.ReleaseError, "readelf"):
                    release.assemble(args)
            self.assertFalse(args.output.exists())
            args.output.mkdir()
            with self.assertRaisesRegex(release.ReleaseError, "existing"):
                release.assemble(args)

    def test_unrelated_helper_checkout_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(repo_root=Path(directory))
            with self.assertRaisesRegex(release.ReleaseError, "helper checkout"):
                release.assemble(args)

    def test_only_exact_notice_documentation_paths_are_allowed(self):
        validator = release.sdk_validator
        for path, allowed in (("docs/release/THIRD_PARTY_NOTICES.txt", True),
                              ("docs/release/dependency-policy.json", True),
                              ("docs/release/unreviewed.txt", False),
                              ("docs/THIRD_PARTY_NOTICES.txt", False),
                              ("docs/unexpected.json", False)):
            errors = []
            validator.validate_exact_layout(Path("/fixture"), {path: Path("/fixture") / path}, set(), errors)
            has_error = any("docs contains a non-HTML artifact" in error for error in errors)
            with self.subTest(path=path):
                self.assertEqual(has_error, not allowed)

    def test_older_sdk_documentation_is_still_structurally_allowed(self):
        errors = []
        release.sdk_validator.validate_exact_layout(Path("/fixture"), {"docs/index.html": Path("/fixture/docs/index.html")}, set(), errors)
        self.assertFalse(any("docs contains" in error for error in errors))


class ArchiveTests(unittest.TestCase):
    def test_framework_versions_remain_pinned_with_independent_aperture_preview(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            def identity(arguments):
                name = Path(arguments[0]).name
                return "aperture 0.1.0-dev (NIL Framework)" if name == "aperture" else name + " " + VERSION
            with patch.object(release, "capture", side_effect=identity):
                self.assertEqual(release.validate_tool_versions(sdk, VERSION), {})
                (sdk / "aperture").write_text("fixture")
                self.assertEqual(release.validate_tool_versions(sdk, VERSION), {"aperture": "0.1.0-dev"})
            with patch.object(release, "capture", return_value="nilcompiler wrong"):
                with self.assertRaisesRegex(release.ReleaseError, "nilcompiler.*committed VERSION"):
                    release.validate_tool_versions(sdk, VERSION)

    def test_atomic_no_replace_preserves_existing_empty_directory(self):
        if sys.platform != "linux":
            self.skipTest("release assembly targets Linux only")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, destination = root / "source", root / "destination"
            source.mkdir()
            (source / "payload").write_text("retained")
            destination.mkdir()
            with self.assertRaises(FileExistsError):
                release.rename_no_replace(source, destination)
            self.assertTrue((source / "payload").is_file())
            self.assertEqual(list(destination.iterdir()), [])
            release.rename_no_replace(source, root / "new")
            self.assertEqual((root / "new/payload").read_text(), "retained")
    def test_normalized_archive_is_deterministic_and_keeps_sdk_root_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdk = root / "sdk"
            sdk.mkdir()
            files = {}
            for relative in ("nilcompiler", "aperture", "manifest.json", "nillib/core.nil", "nilstd/core.ny", "docs/index.html"):
                path = sdk / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(relative.encode())
                files[relative] = path
            expected = {name: (path.stat().st_size, release.sha256(path)) for name, path in files.items()}
            first, second = root / "first.tar.gz", root / "second.tar.gz"
            release.write_archive(first, "nil-sdk-1.0.0-linux-x86_64", files, expected, 1234)
            for path in files.values():
                os.utime(path, (9876, 9876))
                path.chmod(0o777)
            release.write_archive(second, "nil-sdk-1.0.0-linux-x86_64", dict(reversed(list(files.items()))), expected, 1234)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with tarfile.open(first) as archive:
                for member in archive:
                    self.assertEqual((member.uid, member.gid, member.uname, member.gname, member.mtime), (0, 0, "", "", 1234))
                    self.assertFalse(member.issym() or member.islnk())
                    if member.name.endswith(("/nilcompiler", "/aperture")):
                        self.assertEqual(member.mode, 0o755)
                    elif member.isfile():
                        self.assertEqual(member.mode, 0o644)

    def test_archive_refuses_changed_payload_and_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "nilcompiler"
            source.write_bytes(b"old")
            expected = {"nilcompiler": (3, release.sha256(source))}
            source.write_bytes(b"new")
            with self.assertRaisesRegex(release.ReleaseError, "changed"):
                release.write_archive(root / "changed.tar.gz", "sdk", {"nilcompiler": source}, expected, 0)
            link = root / "link"
            link.symlink_to(source)
            with self.assertRaisesRegex(release.ReleaseError, "non-regular"):
                release.write_archive(root / "link.tar.gz", "sdk", {"nilcompiler": link}, expected, 0)

    def test_json_rejects_duplicates_and_nonobjects(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.json"
            for value in ('{"reviewed":true,"reviewed":false}', "[]", "null"):
                path.write_text(value)
                with self.assertRaises(release.ReleaseError):
                    release.load_json(path)

    def test_exact_release_assets_and_checksums(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ["SHA256SUMS", "release-manifest.json", "sdk.tar.gz"]
            (root / "sdk.tar.gz").write_bytes(b"test artifact")
            manifest = {"publishedAssets": names, "assets": [{"name": "sdk.tar.gz", "bytes": 13,
                                                              "sha256": release.sha256(root / "sdk.tar.gz")}]}
            (root / "release-manifest.json").write_bytes(release.json_bytes(manifest))
            (root / "SHA256SUMS").write_text("".join(release.sha256(root / name) + "  " + name + "\n" for name in names if name != "SHA256SUMS"))
            release.verify_assets(root)
            (root / "extra.txt").write_bytes(b"unexpected")
            with self.assertRaisesRegex(release.ReleaseError, "exactly"):
                release.verify_assets(root)
            (root / "extra.txt").unlink()
            (root / "sdk.tar.gz").write_bytes(b"changed")
            with self.assertRaisesRegex(release.ReleaseError, "checksum mismatch"):
                release.verify_assets(root)


class VsixTests(unittest.TestCase):
    def fixture(self, path, version=VERSION, extra=None):
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("extension/package.json", json.dumps({"name": "ny-language-support", "publisher": "exec-software", "version": version,
                "main": "./dist/extension.js", "contributes": {
                    "languages": [{"configuration": "./language-configuration.json"}],
                    "grammars": [{"path": "./syntax.json"}], "snippets": [{"path": "./snippets.json"}]}}))
            for name in ("dist/extension.js", "language-configuration.json", "syntax.json", "snippets.json"):
                archive.writestr("extension/" + name, "{}")
            archive.writestr("extension.vsixmanifest", '<PackageManifest xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011"><Metadata><Identity Id="ny-language-support" Publisher="exec-software" Version="' + VERSION + '" /></Metadata></PackageManifest>')
            archive.writestr("extension/THIRD_PARTY_NOTICES.txt", "Reviewed notices")
            if extra:
                archive.writestr(extra, "unsafe")

    def test_identity_and_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extension.vsix"
            self.fixture(path)
            self.assertEqual(release.validate_vsix(path, VERSION), "ny-language-support-" + VERSION + ".vsix")
            self.fixture(path, version="0.1.0")
            with self.assertRaisesRegex(release.ReleaseError, "identity"):
                release.validate_vsix(path, VERSION)

    def test_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extension.vsix"
            self.fixture(path, extra="../outside")
            with self.assertRaisesRegex(release.ReleaseError, "unsafe"):
                release.validate_vsix(path, VERSION)

    def test_drive_path_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extension.vsix"
            self.fixture(path, extra="C:/outside")
            with self.assertRaisesRegex(release.ReleaseError, "unsafe"):
                release.validate_vsix(path, VERSION)

    def test_missing_declared_asset_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extension.vsix"
            self.fixture(path)
            # Repack only in this temporary test directory, omitting its main.
            with zipfile.ZipFile(path) as source:
                contents = {entry.filename: source.read(entry) for entry in source.infolist()
                            if entry.filename != "extension/dist/extension.js"}
            with zipfile.ZipFile(path, "w") as destination:
                for name, value in contents.items():
                    destination.writestr(name, value)
            with self.assertRaisesRegex(release.ReleaseError, "declared asset"):
                release.validate_vsix(path, VERSION)

    def test_expanded_size_limit_precedes_crc(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "extension.vsix"
            self.fixture(path)
            with patch.object(release, "MAX_JSON", 8), patch.object(zipfile.ZipFile, "testzip") as crc:
                with self.assertRaisesRegex(release.ReleaseError, "decompressed size"):
                    release.validate_vsix(path, VERSION)
                crc.assert_not_called()


class LocalStagingTests(unittest.TestCase):
    def setup_stage(self, root, stack, *, mutate_final=False):
        sdk = root / "sdk"
        sdk.mkdir()
        policy_path, notices_path = root / "policy.json", root / "notices.txt"
        policy_path.write_bytes(release.json_bytes(policy()))
        notices_path.write_bytes(NOTICES)
        payload = {name: (name + " fixture").encode() for name in release.TOOLS}
        payload.update({"docs/release/dependency-policy.json": policy_path.read_bytes(),
                        "docs/release/THIRD_PARTY_NOTICES.txt": NOTICES,
                        "nilstd/core.ny": b"module core\n", "nillib/core.nil": b"fixture NIL",
                        "LICENSE": b"fixture license"})
        files = {}
        for name, data in payload.items():
            path = sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            files[name] = path
        manifest = {"qualification": {"evidence": {"build": BUILD}},
                    "payload": {"files": [{"path": name, "bytes": len(payload[name]), "sha256": release.sha256(path)}
                                            for name, path in files.items()]}}
        manifest_path = sdk / "manifest.json"
        manifest_path.write_bytes(release.json_bytes(manifest))
        files["manifest.json"] = manifest_path
        calls = 0
        def validate(*args):
            nonlocal calls
            calls += 1
            if mutate_final and calls == 2:
                manifest_path.write_bytes(manifest_path.read_bytes() + b" ")
            return manifest, files
        stack.enter_context(patch.object(release, "validate_sdk", side_effect=validate))
        stack.enter_context(patch.object(release, "source_identity", return_value=(VERSION, 1234)))
        stack.enter_context(patch.object(release.shutil, "which", return_value="/fixture/readelf"))
        stack.enter_context(patch.object(release, "elf_inventory", return_value={
            **INVENTORY, "platform": {"os": "linux", "architecture": "x86_64"}}))
        stack.enter_context(patch.object(release, "capture", side_effect=lambda command, **kw:
            "aperture 0.1.0-dev (NIL Framework)" if Path(command[0]).name == "aperture" else
            Path(command[0]).name + " " + VERSION if Path(command[0]).name in release.TOOLS else ""))
        return argparse.Namespace(repo_root=ROOT, sdk=sdk, output=root / "release-a", expected_revision=REVISION,
                                  dependency_policy=policy_path, notices=notices_path, vsix=None)

    def test_full_local_assembly_is_deterministic_without_activation(self):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory)
            args = self.setup_stage(root, stack)
            before = release.sha256(args.sdk / "manifest.json")
            first = release.assemble(args)
            args.output = root / "release-b"
            second = release.assemble(args)
            self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()},
                             {p.name: p.read_bytes() for p in second.iterdir()})
            self.assertEqual(release.sha256(args.sdk / "manifest.json"), before)
            self.assertFalse(args.sdk.is_symlink())
            release.verify_assets(first)
            self.assertEqual(len(list(first.iterdir())), 6)

    def test_final_manifest_change_refuses_output(self):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            args = self.setup_stage(Path(directory), stack, mutate_final=True)
            with self.assertRaisesRegex(release.ReleaseError, "manifest changed"):
                release.assemble(args)
            self.assertFalse(args.output.exists())


if __name__ == "__main__":
    unittest.main()
