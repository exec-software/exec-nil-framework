#!/usr/bin/env python3
"""Compile and run every page in matrix/samples.

The samples are the normative corpus: they are what the design documents point
at, so a page that stops compiling is a documentation bug as much as a compiler
one. This checks the whole corpus rather than the hand-picked subset promoted
into tests/matrix/execution.

Three kinds of page:

  entry      has a 'main', so it is compiled and run.
  companion  named in COMPANIONS as another page's dependency; compiled only
             as part of that page, never alone.
  declaration-only  a library page or a shared-type page with no 'main';
             compiled, not run.

Exit 0 when every page behaves as its kind requires.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

# Pages that need companions compiled alongside them, in order. A Matrix book
# and an Ny module can share one program, so a chain may mix extensions.
COMPANIONS = {
    "11-books.matrix": ["11-books-pricing.matrix"],
    "14-cross-language-app.matrix": ["14-cross-language.matrix",
                                     "14-cross-language-helper.ny"],
}


def declares_main(path):
    """True when the page declares a 'main' paragraph or an Ny 'main' fn."""
    for line in path.read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith("!!!") or stripped.startswith("#"):
            continue
        if re.match(r"^main\b.*\bbegin\s+paragraph\b", stripped):
            return True
        if re.match(r"^fn\s+main\s*\(", stripped):
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--interpreter", required=True)
    ap.add_argument("--root", required=True)
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    samples = root / "matrix" / "samples"
    stdlib = root / "nilstd" / "src"
    if not samples.is_dir():
        print(f"FAIL: no sample directory at {samples}")
        return 1

    companions = {name for names in COMPANIONS.values() for name in names}
    pages = sorted(p for p in samples.iterdir()
                   if p.suffix in (".matrix", ".mat") and p.name not in companions)

    failures, ran, compiled_only = [], 0, 0
    with tempfile.TemporaryDirectory() as workdir:
        for page in pages:
            inputs = [samples / name for name in COMPANIONS.get(page.name, [])]
            inputs.append(page)
            nil = pathlib.Path(workdir) / (page.stem + ".nil")
            built = subprocess.run(
                [args.compiler, "--no-bundle", "-I", str(stdlib),
                 "--output", str(nil)] + [str(i) for i in inputs],
                capture_output=True, text=True)
            if built.returncode != 0:
                failures.append(f"{page.name}: compile failed\n    "
                                + (built.stdout + built.stderr).strip()
                                  .replace("\n", "\n    "))
                continue

            # A declaration-only page has no entry point; requiring one would
            # force every shared-type page to grow a pointless main.
            if not declares_main(page):
                compiled_only += 1
                continue

            done = subprocess.run([args.interpreter, str(nil)],
                                  capture_output=True, text=True)
            if done.returncode != 0:
                failures.append(f"{page.name}: exited {done.returncode}\n    "
                                + (done.stdout + done.stderr).strip()
                                  .replace("\n", "\n    "))
                continue
            ran += 1

    if failures:
        print(f"Matrix sample corpus: {len(failures)} failure(s) of "
              f"{len(pages)} pages\n")
        for f in failures:
            print(f"  FAIL {f}")
        return 1
    print(f"Matrix sample corpus: {len(pages)} pages, {ran} ran, "
          f"{compiled_only} declaration-only, "
          f"{len(companions)} companion page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
