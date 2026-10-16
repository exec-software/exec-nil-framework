#!/usr/bin/env python3
"""Guard repository-wide naming and retired-feature cleanup decisions."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
import sys
from typing import List, Optional


SKIP_DIRECTORIES = {
    ".git",
    ".idea",
    "Testing",
    "build",
    "builds",
    "dist",
    "node_modules",
    "out",
    "out-test",
    "sdk",
    ".sdk.store",
}


def iter_repository_files(root: Path):
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if any(
            part in SKIP_DIRECTORIES or part.startswith("cmake-build-")
            for part in relative.parts
        ):
            continue
        yield relative, path


def read_text(path: Path) -> Optional[str]:
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise RuntimeError(f"cannot read {path}: {error}") from error
    if b"\0" in raw:
        return None
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return None


def check_design_records(root: Path) -> List[str]:
    """Keep public decision identities and their local source links usable."""
    violations: List[str] = []
    index = root / "docs/design/README.md"
    index_text = read_text(index)
    if index_text is None:
        return ["docs/design/README.md: missing readable design index"]

    identifiers = {}
    records = []
    filename_id = re.compile(r"^((?:NIL|MAT)-[0-9]{4}|NY-[0-9]{3,4})-")
    for directory in ("docs/design", "notebook/decisions"):
        for path in sorted((root / directory).glob("*.md")):
            match = filename_id.match(path.name)
            if match is None:
                if directory == "notebook/decisions":
                    violations.append(
                        f"{path.relative_to(root)}: decision filename needs a scoped identifier"
                    )
                continue
            identifier = match.group(1)
            relative = path.relative_to(root)
            if identifier in identifiers:
                violations.append(
                    f"{relative}: duplicate design identifier {identifier}"
                )
            identifiers[identifier] = relative
            text = read_text(path)
            if text is None or not text.startswith(f"# {identifier}:"):
                violations.append(
                    f"{relative}: title must begin with '# {identifier}:'"
                )
            elif identifier not in index_text or path.name not in index_text:
                violations.append(f"{relative}: missing from design index")
            if text is not None:
                records.append((path, text))

    # Limit this to the index and architecture records: prose elsewhere also
    # contains deliberately illustrative paths and generated-output links.
    for path, text in [(index, index_text)] + records:
        for target in re.findall(r"\]\(([^)]+)\)", text):
            target = target.split("#", 1)[0]
            if not target or ":" in target:
                continue
            if not (path.parent / target).exists():
                violations.append(
                    f"{path.relative_to(root)}: broken design link {target}"
                )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of tests/)",
    )
    args = parser.parse_args()
    root = args.root.resolve()

    retired_runtime = "ny" + "vm"
    forbidden_text = {
        retired_runtime: "retired public runtime name",
        "ny" + "-vm": "retired public runtime name",
        "ny" + "_vm": "retired public runtime name",
        "ny" + "." + "vm" + ".path": "retired editor setting",
        "trans" + "pil": "retired C++ conversion feature",
        "ML" + "BUNDLE": "retired pre-Ny bundle marker",
    }
    foreign_ny_fragments = {
        "#" + "include": "C/C++ include directive in Ny source",
        "static" + "_cast<": "C++ cast in Ny source",
        "reinterpret" + "_cast<": "C++ cast in Ny source",
        "dynamic" + "_cast<": "C++ cast in Ny source",
        "const" + "_cast<": "C++ cast in Ny source",
    }

    violations: List[str] = check_design_records(root)
    retired_design_id = re.compile(
        r"\bNY" + r"[DGL]-[0-9]+\b|\bA" + r"DR[ -]+[0-9]+\b"
    )
    retired_decision_path = re.compile(r"decisions/0[0-9]{3}-")
    for relative, path in iter_repository_files(root):
        relative_text = relative.as_posix().lower()
        for fragment, description in forbidden_text.items():
            if fragment in relative_text:
                violations.append(f"{relative}: path contains {description}")

        text = read_text(path)
        if text is None:
            continue
        design_text = text
        if relative.as_posix() == "docs/design/README.md":
            # The dated crosswalk is the only intentional historical spelling.
            design_text = text.split("## Identifier migration", 1)[0]
        for line_number, line in enumerate(design_text.splitlines(), start=1):
            if retired_design_id.search(line) or retired_decision_path.search(line):
                violations.append(
                    f"{relative}:{line_number}: use a scoped NIL-, NY-, or MAT- design reference"
                )
        lowered = text.lower()
        for fragment, description in forbidden_text.items():
            if fragment in lowered:
                for line_number, line in enumerate(text.splitlines(), start=1):
                    if fragment in line.lower():
                        violations.append(
                            f"{relative}:{line_number}: contains {description}"
                        )

        if path.suffix == ".ny":
            # nil::std:: is the language's canonical library namespace. Only
            # a standalone std:: root denotes the retired foreign fragment.
            foreign_root = re.compile(r"(?<![A-Za-z0-9_:])" + "std" + r"::")
            for line_number, line in enumerate(text.splitlines(), start=1):
                if foreign_root.search(line):
                    violations.append(
                        f"{relative}:{line_number}: contains C++ standard-library expression in Ny source"
                    )
            for fragment, description in foreign_ny_fragments.items():
                if fragment in lowered:
                    for line_number, line in enumerate(text.splitlines(), start=1):
                        if fragment in line.lower():
                            violations.append(
                                f"{relative}:{line_number}: contains {description}"
                            )

    if violations:
        print("repository policy violations:", file=sys.stderr)
        for violation in violations:
            print(f"  - {violation}", file=sys.stderr)
        return 1

    print("repository naming and retired-feature policy: clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
