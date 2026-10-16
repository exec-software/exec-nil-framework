#!/usr/bin/env python3
"""Keep failed standalone-tool artifacts while successful runs stay temporary."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "native_pipeline", ROOT / "tests/tools/native_toolchain_pipeline.py")
pipeline = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pipeline)


class PipelineArtifactsTests(unittest.TestCase):
    def exercise(self, failure: str | None):
        with tempfile.TemporaryDirectory(prefix="ny-pipeline-artifact-test-") as temporary:
            parent = Path(temporary)
            expected = parent / "expected.stdout"
            expected.write_text("ok\n", encoding="utf-8")
            args = argparse.Namespace(
                compiler=parent / "nilcompiler", assembler=parent / "nilassembler",
                linker=parent / "nillinker", runtime=parent / "libnil.a",
                source=parent / "source.ny", expected_stdout=expected,
                work_dir=parent / "work", stdlib=None)

            def compile_nil(compiler, source, output, **kwargs):
                output.write_bytes(b"NIL diagnostic artifact")
                if failure == "timeout":
                    raise subprocess.TimeoutExpired([str(compiler)], 90)
                return subprocess.CompletedProcess([], 1 if failure == "compiler" else 0,
                                                   "", "compiler failed" if failure == "compiler" else "")

            def assemble_and_link(**kwargs):
                if failure != "assembler":
                    kwargs["object_output"].write_bytes(b"object diagnostic artifact")
                if failure in ("assembler", "linker"):
                    return False, failure + " failed"
                if failure == "skip":
                    return False, "skip: nilassembler was built without LLVM support"
                kwargs["executable_output"].write_bytes(b"executable diagnostic artifact")
                return True, ""

            def run(command, **kwargs):
                return subprocess.CompletedProcess(command, 1 if failure == "library" else 0,
                                                   "", "library failed" if failure == "library" else "")

            stdout, stderr = io.StringIO(), io.StringIO()
            with patch.object(pipeline, "arguments", return_value=args), \
                    patch.object(pipeline, "compile_nil", side_effect=compile_nil), \
                    patch.object(pipeline, "assemble_and_link", side_effect=assemble_and_link), \
                    patch.object(pipeline, "run", side_effect=run), \
                    patch.object(pipeline, "expect_rejection_preserves_output", return_value=None), \
                    redirect_stdout(stdout), redirect_stderr(stderr):
                status = pipeline.main()
            children = list(args.work_dir.iterdir())
            if failure is None:
                self.assertEqual(status, 0)
                self.assertEqual(stdout.getvalue(), "standalone assembler/linker pipeline: passed\n")
                self.assertEqual(stderr.getvalue(), "")
                self.assertEqual(children, [])
            elif failure == "skip":
                self.assertEqual(status, pipeline.SKIP)
                self.assertEqual(stderr.getvalue(), "")
                self.assertEqual(children, [])
            else:
                self.assertEqual(status, 1)
                self.assertEqual(len(children), 1)
                self.assertIn("Toolchain artifacts preserved for diagnosis: " + str(children[0]),
                              stderr.getvalue())
                self.assertEqual((children[0] / "route-text.nil").read_bytes(), b"NIL diagnostic artifact")
                if failure in ("linker", "library"):
                    self.assertEqual((children[0] / "route text;literal.o").read_bytes(),
                                     b"object diagnostic artifact")

    def test_success_cleans_up_and_preserves_exact_stdout(self):
        self.exercise(None)

    def test_compiler_assembler_linker_and_later_failures_keep_artifacts(self):
        for failure in ("compiler", "assembler", "linker", "library", "timeout"):
            with self.subTest(failure=failure):
                self.exercise(failure)

    def test_expected_llvm_skip_keeps_existing_cleanup(self):
        self.exercise("skip")


if __name__ == "__main__":
    unittest.main()
