#!/usr/bin/env python3
"""Package mixed Ny/Matrix libraries and consume their source and binary forms."""

from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_sources import is_source, module_declarations, module_artifact_path


def load_script(name: str):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACKAGER = load_script("package-sdk")
VALIDATOR = load_script("validate-sdk")
OPTIONS = None


class MatrixSourceTests(unittest.TestCase):
    def test_extensions(self):
        for source in ("page.ny", "page.matrix", "page.mat", "page.MATRIX", "page.MAT", "page.NY"):
            self.assertTrue(is_source(source), source)
        for source in ("page.matrix.bak", "page.txt", "page.nil", "matrix"):
            self.assertFalse(is_source(source), source)

    def test_matrix_declarations_preserve_identity(self):
        self.assertEqual(module_declarations("BoOk Demo.long-name::detail !!! comment\n", ".matrix"), {"Demo.long_name.detail"})
        self.assertEqual(module_declarations("LiBrArY Demo::`text`\n", ".mat"), {"Demo.text"})
        self.assertEqual(module_declarations("book\nDemo.name.\nvalue another is 1\n", ".matrix"), {"Demo.name"})
        self.assertEqual(module_declarations("book Demo\n::\nlibrary\n", ".matrix"), {"Demo.library"})
        self.assertEqual(module_declarations("book Demo.one; library Demo.two\n", ".matrix"), {"Demo.one", "Demo.two"})

    def test_comments_literals_and_imports_are_not_declarations(self):
        source = '''!!! book fake.comment
book actual.library
read everything from other.library
value quote is "book fake.quote !!! begin text"
value message is BeGiN TeXt
book fake.raw
!!! end text
    EnD    TeXt
value `library` is 1
'''
        self.assertEqual(module_declarations(source, ".matrix"), {"actual.library"})
        self.assertEqual(module_declarations("!!! begin text\nbook actual.book\n", ".mat"), {"actual.book"})
        # A comment after the opener prevents a raw block in the real lexer.
        self.assertEqual(module_declarations("value x is begin text !!! comment\nbook actual.book\n", ".matrix"), {"actual.book"})

    def test_standard_aliases_have_one_identity(self):
        self.assertEqual(module_declarations("# module ignored\nmodule ny.lang.math # comment\npackage nil.std.math\n", ".ny"), {"nil.std.math"})
        self.assertEqual(module_declarations("book ny.lang.math\nlibrary nil.std.math\n", ".matrix"), {"nil.std.math"})
        self.assertEqual(module_artifact_path("nil.std.core").as_posix(), "core.nil")
        self.assertEqual(module_artifact_path("ny.lang.core").as_posix(), "core.nil")
        self.assertEqual(module_artifact_path("ny.lang.nydata").as_posix(), "nildata.nil")
        self.assertEqual(module_artifact_path("nil.std.nydata").as_posix(), "nildata.nil")
        self.assertEqual(module_artifact_path("nil.std.nydata_extra").as_posix(), "nydata_extra.nil")
        self.assertEqual(module_artifact_path("demo.core").as_posix(), "demo/core.nil")
        self.assertEqual(module_artifact_path("ny.language.core").as_posix(), "ny/language/core.nil")

    def test_layout_source_allowlist(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-layout-") as temporary:
            root = Path(temporary)
            files = {"nilstd/page" + suffix: root / ("page" + suffix)
                     for suffix in (".ny", ".matrix", ".mat", ".MATRIX", ".txt")}
            errors = []
            VALIDATOR.validate_exact_layout(root, files, {"nilstd"}, errors)
            source_errors = [error for error in errors if "nilstd contains a non-source" in error]
            self.assertEqual(source_errors, ["nilstd contains a non-source file: nilstd/page.txt"])

    def test_multiple_books_and_unrepresentable_names_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-source-") as temporary:
            root = Path(temporary)
            source = root / "multiple.matrix"
            source.write_text("book demo.one\nbook demo.two\n", encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "multiple logical modules"):
                PACKAGER.logical_module_name(source, root)
            source.write_text("book `bad name`\n", encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "cannot be represented"):
                PACKAGER.logical_module_name(source, root)

    def run_command(self, command, cwd):
        environment = os.environ.copy()
        for name in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_STDLIB_PATH", "NIL_STDLIB_PATH", "NY_LEX_DUMP", "NIL_LEX_DUMP", "NY_PARSE_TRACE", "NIL_PARSE_TRACE"):
            environment.pop(name, None)
        result = subprocess.run([str(item) for item in command], cwd=cwd, env=environment,
                                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, f"command failed: {command}\n{result.stdout}{result.stderr}")
        return result

    def make_stage(self, root, sources):
        stage = root / "stage"
        nilstd = stage / "nilstd"
        for name, contents in sources.items():
            path = nilstd / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        runtime = stage / OPTIONS.runtime.name
        shutil.copyfile(OPTIONS.runtime, runtime)
        shared = stage / OPTIONS.shared_runtime.name
        shutil.copyfile(OPTIONS.shared_runtime, shared)
        modules, count = PACKAGER.compile_standard_library(OPTIONS.compiler, nilstd, stage, runtime, shared)
        files = {path.relative_to(stage).as_posix(): path for path in stage.rglob("*") if path.is_file()}
        artifacts = [path for key, path in files.items() if key.startswith("nillib/") and key.endswith(".nil")]
        manifest = {"format": PACKAGER.nil_library_format({PACKAGER.nil_library_version(path) for path in artifacts}),
                    "strictly_type_checked": True, "source_file_count": count,
                    "logical_module_count": len(modules), "compiled_module_count": len(modules),
                    "artifact_file_count": len(artifacts), "modules": modules}
        errors = []
        VALIDATOR.validate_standard_library(manifest, files, errors)
        self.assertEqual(errors, [])
        self.assertEqual(count, len(sources))
        self.assertFalse(runtime.exists())
        self.assertFalse(shared.exists())
        self.assertTrue((stage / "nillib" / "native" / shared.name).is_file())
        self.assertTrue((stage / "nillib" / "native" / OPTIONS.runtime.name).is_file())
        # Matrix sources must participate in manifest completeness checks.
        files["nilstd/unmapped.matrix"] = next(iter(files.values()))
        errors = []
        VALIDATOR.validate_standard_library(manifest, files, errors)
        self.assertTrue(any("manifest source mapping differs" in error for error in errors))
        return stage, modules

    def consume(self, root, stage, ny_source, matrix_source, expected):
        for route in ("source", "binary"):
            isolated = root / route
            isolated.mkdir()
            library = isolated / "library"
            shutil.copytree(stage / ("nilstd" if route == "source" else "nillib"), library)
            for suffix, contents in ((".ny", ny_source), (".matrix", matrix_source)):
                source = isolated / ("consumer" + suffix)
                source.write_text(contents, encoding="utf-8")
                output = isolated / ("program-" + suffix[1:] + ".nil")
                self.run_command([OPTIONS.compiler, "--no-stdlib", "--no-bundle", "-I", library,
                                  "--output", output, source], isolated)
                result = self.run_command([OPTIONS.interpreter, output], isolated)
                self.assertEqual(result.stdout, expected)
                if OPTIONS.native:
                    executable = isolated / ("native-" + suffix[1:] + (".exe" if os.name == "nt" else ""))
                    self.run_command([OPTIONS.compiler, "--no-stdlib", "--native", "-I", library,
                                      "--output", executable, source], isolated)
                    result = self.run_command([executable], isolated)
                    self.assertEqual(result.stdout, expected)

    def test_mixed_library_packaging_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-mixed-") as temporary:
            root = Path(temporary)
            stage, modules = self.make_stage(root, {
                "demo/mixed/package.ny": "package demo.mixed\nimport demo.mixed.matrix_page\nimport demo.mixed.short\npub fn ny_answer() -> i32\nreturn 1\nend fn\n",
                "demo/mixed/matrix-page.matrix": "BoOk demo.mixed !!! same NIL module as the Ny page\npublic matrix-answer returns override integer begin paragraph\ngive 40\nend paragraph\n",
                "demo/mixed/short.mat": "book demo.mixed\npublic short-answer returns override integer begin paragraph\ngive 1\nend paragraph\n",
            })
            self.assertEqual([module["name"] for module in modules], ["demo.mixed"])
            self.consume(root, stage,
                         "import demo.mixed\nfn main() -> i32\nprintln(ny_answer() + matrix_answer() + short_answer())\nreturn 0\nend fn\n",
                         "book consumer.entry\nread everything from demo.mixed\nmain returns override integer begin paragraph\nsay (ny-answer with nothing) plus (matrix-answer with nothing) plus (short-answer with nothing)\ngive 0\nend paragraph\n",
                         "42\n")

    def test_matrix_only_packaging_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-matrix-") as temporary:
            root = Path(temporary)
            stage, modules = self.make_stage(root, {
                "demo/matrix-only.mat": "book demo.matrix-only\npublic matrix-answer returns override integer begin paragraph\ngive 42\nend paragraph\n",
            })
            self.assertEqual([module["name"] for module in modules], ["demo.matrix_only"])
            self.consume(root, stage,
                         "import demo.matrix_only\nfn main() -> i32\nprintln(matrix_answer())\nreturn 0\nend fn\n",
                         "book consumer.entry\nread everything from demo.matrix-only\nmain returns override integer begin paragraph\nsay matrix-answer with nothing\ngive 0\nend paragraph\n",
                         "42\n")

    def test_standard_flat_namespace_and_legacy_consumers(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-namespace-") as temporary:
            root = Path(temporary)
            stage, modules = self.make_stage(root, {
                "sample/package.ny": "package nil.std.sample\nimport ny.lang.sample.part\npub fn ny_answer() -> i32\nreturn 1\nend fn\n",
                "sample/part.matrix": "book ny.lang.sample\npublic matrix-answer returns override integer begin paragraph\ngive 41\nend paragraph\n",
            })
            self.assertEqual([module["name"] for module in modules], ["nil.std.sample"])
            self.assertEqual(modules[0]["artifact"]["path"], "nillib/sample.nil")
            self.assertFalse((stage / "nillib" / "ny").exists())
            self.assertFalse((stage / "nillib" / "nil").exists())
            self.consume(root, stage,
                         "import nil.std.sample\nimport ny.lang.sample\nfn main() -> i32\nprintln(ny::lang::sample::ny_answer() + nil::std::sample::matrix_answer())\nprintln(\"ny.lang remains text\")\nreturn 0\nend fn\n",
                         "book consumer.entry\nread everything from ny.lang.sample\nread everything from nil.std.sample\nmain returns override integer begin paragraph\nsay (ny.lang.sample::ny-answer with nothing) plus (nil.std.sample::matrix-answer with nothing)\nsay \"ny.lang remains text\"\ngive 0\nend paragraph\n",
                         "42\nny.lang remains text\n")

    def test_colliding_source_aliases_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="nil-sdk-collision-") as temporary:
            root = Path(temporary)
            stage = root / "stage"
            nilstd = stage / "nilstd"
            nilstd.mkdir(parents=True)
            (nilstd / "part.ny").write_text("module demo.shared\npub fn ny_answer() -> i32\nreturn 1\nend fn\n", encoding="utf-8")
            (nilstd / "part.matrix").write_text("book demo.shared\npublic matrix-answer returns override integer begin paragraph\ngive 2\nend paragraph\n", encoding="utf-8")
            runtime = stage / OPTIONS.runtime.name
            shutil.copyfile(OPTIONS.runtime, runtime)
            with self.assertRaisesRegex(SystemExit, "artifacts collide"):
                PACKAGER.compile_standard_library(OPTIONS.compiler, nilstd, stage, runtime)
            self.assertFalse((stage / "nillib").exists())
            self.assertTrue(runtime.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--native", action="store_true")
    OPTIONS = parser.parse_args()
    OPTIONS.compiler = OPTIONS.compiler.resolve(strict=True)
    OPTIONS.interpreter = OPTIONS.interpreter.resolve(strict=True)
    OPTIONS.runtime = OPTIONS.runtime.resolve(strict=True)
    candidates = [directory / name for directory in (OPTIONS.runtime.parent, OPTIONS.runtime.parent.parent / "bin")
                  for name in ("libnil.so", "libnil.dylib", "nil.dll") if (directory / name).is_file()]
    if len(candidates) != 1:
        raise SystemExit("mixed-source packaging test requires one shared runtime next to the static archive")
    OPTIONS.shared_runtime = candidates[0]
    unittest.main(argv=[sys.argv[0]], verbosity=2)
