#!/usr/bin/env python3
"""Repository policy scans source while leaving retained validation work alone."""

from contextlib import redirect_stderr, redirect_stdout
import errno
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import repository_policy as policy


class RepositoryPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="repository-policy-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.write("docs/design/README.md", "# Design records\n")

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def run_policy(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("sys.argv", ["repository_policy.py", "--root", str(self.root)]), \
                redirect_stdout(stdout), redirect_stderr(stderr):
            result = policy.main()
        return result, stdout.getvalue(), stderr.getvalue()

    def test_generated_validation_trees_are_not_entered_or_read(self):
        generated = [self.root / name for name in (
            "validation/ny-3d/work", "validation/ny-3d/reports",
            "validation/matrix-3d/work", "validation/matrix-3d/reports",
        )]
        for directory in generated:
            directory.mkdir(parents=True)
            # The content would violate policy if this generated tree leaked
            # into the scan; the permissions simulation also rejects traversal.
            (directory / "retained.txt").write_text("ny" + "vm", encoding="utf-8")
        attempted = []
        original = os.scandir

        def guarded(path):
            if Path(path) in generated:
                attempted.append(Path(path))
                raise PermissionError(errno.EACCES, "retained evidence", str(path))
            return original(path)

        with patch.object(policy.os, "scandir", side_effect=guarded):
            result, _, stderr = self.run_policy()
        self.assertEqual(attempted, [])
        self.assertEqual((result, stderr), (0, ""))

    def test_existing_build_exclusion_is_pruned_before_descent(self):
        directory = self.root / "component/build"
        directory.mkdir(parents=True)
        original = os.scandir

        def guarded(path):
            if Path(path) == directory:
                self.fail("policy entered an excluded build directory")
            return original(path)

        with patch.object(policy.os, "scandir", side_effect=guarded):
            self.assertEqual(self.run_policy()[0], 0)

    def test_similarly_named_source_directories_remain_in_scope(self):
        names = (
            "src/work/source.ny", "docs/reports/source.md",
            "validation/ny-3d/src/work/source.ny",
            "validation/ny-3d/workbench/source.ny",
            "validation/ny-3d/probes/source.ny",
            "validation/other-project/work/source.ny",
        )
        for name in names:
            self.write(name, "source\n")
        actual = {relative.as_posix() for relative, _ in policy.iter_repository_files(self.root)}
        self.assertTrue(set(names) <= actual)

    def test_readable_untracked_source_still_reports_policy_violation(self):
        self.write("src/fresh.ny", "ny" + "vm\n")
        result, _, stderr = self.run_policy()
        self.assertEqual(result, 1)
        self.assertIn("fresh.ny", stderr)
        self.assertIn("retired public runtime name", stderr)

    def test_unreadable_source_file_is_an_error(self):
        source = self.write("validation/ny-3d/probes/source.ny", "source\n")
        original = Path.read_bytes

        def guarded(path):
            if path == source:
                raise PermissionError(errno.EACCES, "source is unreadable", str(path))
            return original(path)

        with patch.object(Path, "read_bytes", guarded), self.assertRaisesRegex(RuntimeError, "cannot read"):
            self.run_policy()

    def test_unreadable_source_directory_is_an_error(self):
        directory = self.write("src/nested/source.ny", "source\n").parent
        original = os.scandir

        def guarded(path):
            if Path(path) == directory:
                raise PermissionError(errno.EACCES, "source directory is unreadable", str(path))
            return original(path)

        with patch.object(policy.os, "scandir", side_effect=guarded), \
                self.assertRaisesRegex(RuntimeError, "cannot scan"):
            list(policy.iter_repository_files(self.root))

    def test_source_metadata_failure_is_an_error(self):
        source = self.write("src/source.ny", "source\n")
        original = Path.stat

        def guarded(path, *args, **kwargs):
            if path == source:
                raise PermissionError(errno.EACCES, "source metadata is unreadable", str(path))
            return original(path, *args, **kwargs)

        with patch.object(Path, "stat", guarded), self.assertRaisesRegex(RuntimeError, "cannot inspect"):
            list(policy.iter_repository_files(self.root))


if __name__ == "__main__":
    unittest.main(verbosity=2)
