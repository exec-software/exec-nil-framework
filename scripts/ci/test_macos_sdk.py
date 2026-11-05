#!/usr/bin/env python3
"""Controls for development evidence, without substituting for native SDK execution."""
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

SPEC = importlib.util.spec_from_file_location("macos_sdk", Path(__file__).with_name("macos-sdk.py"))
sdk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sdk)


class CandidateTests(unittest.TestCase):
    def test_payload_seal_covers_bytes_modes_and_namespace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            file = root / "tool"
            file.write_bytes(b"original")
            file.chmod(0o755)
            before = sdk.payload(root)
            file.write_bytes(b"modified")
            self.assertNotEqual(sdk.payload(root), before)
            file.write_bytes(b"original")
            file.chmod(0o644)
            self.assertNotEqual(sdk.payload(root), before)
            file.chmod(0o755)
            self.assertEqual(sdk.payload(root), before)
            (root / "extra").write_bytes(b"extra")
            self.assertNotEqual(sdk.payload(root), before)

    def test_payload_refuses_links_and_empty_stage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "empty"):
                sdk.payload(root)
            (root / "target").write_text("payload")
            (root / "alias").symlink_to("target")
            with self.assertRaisesRegex(ValueError, "symlink"):
                sdk.payload(root)

    def test_sibling_shadow_exists_during_validation_and_is_cleaned_on_both_outcomes(self):
        for failure in (False, True):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                candidate = root / "sdk"; candidate.mkdir()
                (candidate / "payload").write_text("sealed")
                output = root / "evidence"; output.mkdir()
                observed = []
                def validate():
                    observed.append((root / "lib/libnil.dylib").read_bytes())
                    if failure:
                        raise ValueError("fixture validation failure")
                with mock.patch.object(sdk, "OUTPUT", output):
                    if failure:
                        with self.assertRaisesRegex(ValueError, "fixture validation failure"):
                            sdk.shadow_validation(candidate, validate)
                    else:
                        sdk.shadow_validation(candidate, validate)
                self.assertEqual(len(observed), 1)
                self.assertIn(b"invalid private sibling", observed[0])
                self.assertFalse((root / "lib").exists())
                report = json.loads((output / "sibling-shadow.json").read_text())
                self.assertTrue(report["candidate_unchanged"])
                self.assertTrue(report["cleanup_verified"])
                self.assertEqual(report["validation_passed"], not failure)

    def test_sibling_shadow_detects_candidate_mutation_and_preserves_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "sdk"; candidate.mkdir()
            (candidate / "payload").write_text("before")
            output = root / "evidence"; output.mkdir()
            with mock.patch.object(sdk, "OUTPUT", output), self.assertRaisesRegex(ValueError, "candidate changed"):
                sdk.shadow_validation(candidate, lambda: (candidate / "payload").write_text("after"))
            self.assertEqual((candidate / "payload").read_text(), "after")
            self.assertTrue(json.loads((output / "sibling-shadow.json").read_text())["cleanup_verified"])

    def test_bad_candidate_keeps_regular_evidence_without_following_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "sdk"; candidate.mkdir()
            (candidate / "payload").write_text("retained")
            before = sdk.payload(candidate)
            external = root / "outside"; external.mkdir()
            (external / "external-file").write_text("must not archive")
            (candidate / "alias").symlink_to(external, target_is_directory=True)
            output = root / "evidence"; output.mkdir()
            with mock.patch.object(sdk, "OUTPUT", output):
                report = sdk.capture_candidate(candidate, before)
            self.assertFalse(report["seal_unchanged"])
            self.assertIn("symlink", report["error"])
            self.assertEqual(report["archive"], "unqualified-stage.tar.gz")
            with tarfile.open(output / report["archive"]) as archive:
                self.assertEqual(archive.getnames(), ["sdk", "sdk/payload"])
                self.assertEqual(archive.extractfile("sdk/payload").read(), b"retained")

    def archive(self, root, *, bad_name=None, binary_mode=stat.S_IFREG | 0o755):
        archive = root / "pandoc.zip"
        name = f"pandoc-{sdk.PANDOC_VERSION}-arm64/bin/pandoc"
        with zipfile.ZipFile(archive, "x") as zipped:
            info = zipfile.ZipInfo(name)
            info.external_attr = binary_mode << 16
            zipped.writestr(info, b"pinned binary fixture")
            # The official archive has these aliases. They must not be extracted.
            alias = zipfile.ZipInfo(name + "-lua")
            alias.external_attr = (stat.S_IFLNK | 0o777) << 16
            zipped.writestr(alias, "pandoc")
            if bad_name:
                zipped.writestr(bad_name, "untrusted")
        return archive, hashlib.sha256(archive.read_bytes()).hexdigest()

    def test_pandoc_extracts_only_hash_pinned_regular_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, digest = self.archive(root)
            sdk.extract_pandoc(archive, root / "pandoc", digest, "arm64")
            self.assertEqual((root / "pandoc").read_bytes(), b"pinned binary fixture")
            self.assertEqual(stat.S_IMODE((root / "pandoc").stat().st_mode), 0o755)
            self.assertEqual({p.name for p in root.iterdir()}, {"pandoc.zip", "pandoc"})

    def test_pandoc_rejects_bad_hash_traversal_and_linked_binary(self):
        for failure in ("digest", "path", "link"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive, digest = self.archive(root, bad_name="../escape" if failure == "path" else None,
                                              binary_mode=(stat.S_IFLNK if failure == "link" else stat.S_IFREG) | 0o755)
                with self.assertRaises(ValueError):
                    sdk.extract_pandoc(archive, root / "pandoc", "0" * 64 if failure == "digest" else digest, "arm64")
                self.assertFalse((root / "pandoc").exists())

    def test_loader_observation_preserves_spelling_order_and_sdk_priority(self):
        output = """Load command 0
          cmd LC_RPATH
         path @loader_path/nillib/native (offset 12)
Load command 1
          cmd LC_RPATH
         path @loader_path (offset 12)
Load command 2
          cmd LC_RPATH
         path @loader_path/../lib (offset 12)
Load command 3
          cmd LC_LOAD_DYLIB
         name /external SDK/libexample.dylib (offset 24)
"""
        commands = sdk.load_commands(output)
        self.assertEqual(commands[-1]["path"], "/external SDK/libexample.dylib")
        sdk.check_loader_paths(commands, (Path("/checkout"),))
        commands[:3] = list(reversed(commands[:3]))
        with self.assertRaisesRegex(ValueError, "RPATH"):
            sdk.check_loader_paths(commands, (Path("/checkout"),))

    def test_loader_rejects_original_paths_and_implicit_relative_search(self):
        for value in ("/checkout/build/libnil.dylib", "/elsewhere/../checkout/libnil.dylib", "libnil.dylib"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sdk.check_loader_paths([{"command": "LC_LOAD_DYLIB", "path": value}], (Path("/checkout"),))

    def test_external_dylib_alias_records_target_and_rejects_unavailable_alias(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(strict=True)
            target = root / "libprovider.1.dylib"; target.write_bytes(b"external prerequisite")
            alias = root / "libprovider.dylib"; alias.symlink_to(target.name)
            observed = sdk.external_prerequisite(alias)
            self.assertEqual(observed["declared_name"], str(alias))
            self.assertEqual(observed["canonical_target"], str(target))
            self.assertEqual(observed["file"], sdk.record(target))
            target.unlink()
            with self.assertRaises(FileNotFoundError):
                sdk.external_prerequisite(alias)
            with self.assertRaisesRegex(ValueError, "absent"):
                sdk.external_prerequisite(target)

    def fixture_inventory(self, root, *, recorded_ns=0, name="failed"):
        build, output = root / "build", root / "evidence"
        build.mkdir(); output.mkdir()
        (output / "registration.json").write_text(json.dumps({"tests": [{"name": "passed"}, {"name": "failed"}]}))
        (build / "ctest-results.xml").write_text(
            '<testsuite tests="2"><testcase name="passed" status="run"/>'
            f'<testcase name="{name}" status="fail"><failure message="real failure"/></testcase></testsuite>')
        source = {"revision": "fixture", "dirty": False, "fingerprint": "0" * 64}
        (output / "sdk-build-evidence.json").write_text(json.dumps({
            "source": source, "recorded_ns": recorded_ns, "registered_tests": ["passed", "failed"]}))
        return build, output, source

    def test_complete_failed_ctest_is_recorded_as_failed_not_skipped_or_qualified(self):
        with tempfile.TemporaryDirectory() as temporary:
            build, output, source = self.fixture_inventory(Path(temporary))
            with mock.patch.object(sdk, "BUILD", build), mock.patch.object(sdk, "OUTPUT", output), \
                    mock.patch.object(sdk, "clean_source", return_value=source):
                sdk.test_inventory()
            report = json.loads((output / "ctest-inventory.json").read_text())
            self.assertEqual(report["counts"], {"passed": 1, "failed": 1})
            self.assertEqual(report["qualification_summary"]["failed"], 1)
            self.assertEqual(report["qualification_summary"]["skipped"], 0)

    def test_missing_or_stale_ctest_cannot_authorize_staging(self):
        for failure in ("missing", "stale"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                build, output, source = self.fixture_inventory(Path(temporary),
                    name="unexpected" if failure == "missing" else "failed",
                    recorded_ns=2**63 if failure == "stale" else 0)
                with mock.patch.object(sdk, "BUILD", build), mock.patch.object(sdk, "OUTPUT", output), \
                        mock.patch.object(sdk, "clean_source", return_value=source), self.assertRaises(ValueError):
                    sdk.test_inventory()
                self.assertFalse((output / "ctest-inventory.json").exists())


if __name__ == "__main__":
    unittest.main()
