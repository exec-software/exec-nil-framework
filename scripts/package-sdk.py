#!/usr/bin/env python3
"""Compile NIL's standard library and seal a staged SDK with a manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from sdk_artifact_validation import (
    is_nil_library, is_nil_v2_library, is_static_archive,
    nil_library_format, nil_library_version,
)
from sdk_qualification import TOOLS, collect_qualification, qualification_errors, source_identity
from sdk_sources import is_source, module_declarations, module_artifact_path
from sdk_runtime import STATIC_NAMES, SHARED_NAMES, RUNTIME_PAIRS, shared_library_valid


REQUIRED_TOOLS = TOOLS
MANIFEST_NAME = "manifest.json"
NATIVE_RUNTIME_ARCHIVE_NAMES = STATIC_NAMES


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--stage-dir", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--qualification", choices=("development", "rc-checks"),
                        default="development",
                        help="technical SDK checks only; does not declare a language release")
    parser.add_argument("--build-evidence", type=Path,
                        help="pre-test snapshot produced by sdk_qualification.py")
    parser.add_argument("--ctest-report", type=Path,
                        help="JUnit report from the complete corresponding CTest run")
    parser.add_argument(
        "--allow-dirty-source",
        action="store_true",
        help="package an explicitly acknowledged dirty NIL Framework worktree",
    )
    return parser.parse_args()


def executable_name(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_output(repo_root: Path, *args: str) -> str:
    process = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if process.returncode != 0:
        raise SystemExit(
            "git {} failed for {}:\n{}".format(
                " ".join(args), repo_root, process.stderr.rstrip()
            )
        )
    return process.stdout.strip()


def source_state(repo_root: Path, requested_revision: str) -> tuple[str, bool]:
    revision = git_output(repo_root, "rev-parse", "--verify", "HEAD")
    if requested_revision != revision:
        raise SystemExit(
            "SDK revision does not match the NIL Framework checkout: requested {} but HEAD is {}".format(
                requested_revision, revision
            )
        )
    status_output = git_output(
        repo_root,
        "status",
        "--porcelain=v1",
        "--untracked-files=normal",
    )
    return revision, bool(status_output)


def regular_tree_files(root: Path) -> list[Path]:
    """Return a deterministic inventory, rejecting links and special files."""
    files: list[Path] = []
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
        except OSError as exc:
            raise SystemExit("cannot inventory SDK directory {}: {}".format(directory, exc))
        for entry in entries:
            path = Path(entry.path)
            try:
                mode = entry.stat(follow_symlinks=False).st_mode
            except OSError as exc:
                raise SystemExit("cannot inspect SDK path {}: {}".format(path, exc))
            if stat.S_ISLNK(mode):
                raise SystemExit("SDK payload must not contain symlinks: {}".format(path))
            if stat.S_ISDIR(mode):
                pending.append(path)
            elif stat.S_ISREG(mode):
                files.append(path)
            else:
                raise SystemExit("SDK payload contains a special file: {}".format(path))
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def validate_stage_input(stage_dir: Path) -> tuple[Path, Path]:
    expected_directories = {"nilstd", "docs"}
    actual_files: set[str] = set()
    actual_directories: set[str] = set()

    for child in stage_dir.iterdir():
        mode = child.lstat().st_mode
        if stat.S_ISLNK(mode):
            raise SystemExit("staged SDK must not contain symlinks: {}".format(child))
        if stat.S_ISREG(mode):
            actual_files.add(child.name)
        elif stat.S_ISDIR(mode):
            actual_directories.add(child.name)
        else:
            raise SystemExit("staged SDK contains a special root entry: {}".format(child))

    runtime_names = sorted(set(NATIVE_RUNTIME_ARCHIVE_NAMES).intersection(actual_files))
    if len(runtime_names) != 1:
        raise SystemExit(
            "staged SDK must contain exactly one native runtime archive from {}: "
            "found {}".format(list(NATIVE_RUNTIME_ARCHIVE_NAMES), runtime_names)
        )
    runtime_source = stage_dir / runtime_names[0]
    shared_names = sorted(set(SHARED_NAMES).intersection(actual_files))
    if len(shared_names) != 1 or runtime_source.name not in RUNTIME_PAIRS.get(shared_names[0], ()):
        raise SystemExit("staged SDK requires one matching shared native runtime: " + str(shared_names))
    shared_source = stage_dir / shared_names[0]
    if not shared_library_valid(shared_source):
        raise SystemExit("staged shared native runtime has invalid library framing: " + str(shared_source))
    if not is_static_archive(runtime_source):
        raise SystemExit(
            "staged native runtime fails supported archive framing and required "
            "symbol-index offset validation: {}".format(runtime_source)
        )
    expected_files = {executable_name(name) for name in REQUIRED_TOOLS} | {
        "LICENSE",
        runtime_source.name, shared_source.name,
    }
    missing_files = sorted(expected_files - actual_files)
    extra_files = sorted(actual_files - expected_files)
    missing_directories = sorted(expected_directories - actual_directories)
    extra_directories = sorted(actual_directories - expected_directories)
    if missing_files or extra_files or missing_directories or extra_directories:
        raise SystemExit(
            "staged SDK does not match the pre-package layout: "
            "missing files={} extra files={} missing directories={} extra directories={}".format(
                missing_files, extra_files, missing_directories, extra_directories
            )
        )

    regular_tree_files(stage_dir)
    for name in REQUIRED_TOOLS:
        tool = stage_dir / executable_name(name)
        if os.name != "nt" and not os.access(tool, os.X_OK):
            raise SystemExit("staged SDK tool is not executable: {}".format(tool))
    return runtime_source, shared_source


def logical_module_name(source: Path, nilstd: Path) -> str:
    try:
        if source.stat().st_size > 16 * 1024 * 1024:
            raise SystemExit(
                "standard-library source exceeds the 16 MiB limit: {}".format(
                    source
                )
            )
        document = source.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise SystemExit("cannot read standard-library source {}: {}".format(source, exc))
    try:
        declarations = module_declarations(document, source.suffix)
    except ValueError as exc:
        raise SystemExit("cannot identify standard-library module {}: {}".format(source, exc)) from exc
    if len(declarations) > 1:
        raise SystemExit(
            "standard-library source declares multiple logical modules: {} ({})".format(
                source, ", ".join(sorted(declarations))
            )
        )
    if declarations:
        return declarations.pop()

    relative = source.relative_to(nilstd).with_suffix("")
    if not relative.parts:
        raise SystemExit("cannot derive a logical module for {}".format(source))
    return ".".join(relative.parts)


def compile_standard_library(
    compiler: Path, nilstd: Path, stage_dir: Path, runtime_source: Path,
    shared_source: Path | None = None
) -> tuple[list[dict[str, Any]], int]:
    sources = sorted((path for path in nilstd.rglob("*") if path.is_file() and is_source(path)),
                     key=lambda path: path.relative_to(nilstd).as_posix())
    if not sources:
        raise SystemExit("the staged nilstd directory contains no Ny or Matrix sources")

    groups: dict[str, list[Path]] = {}
    for source in sources:
        module_name = logical_module_name(source, nilstd)
        groups.setdefault(module_name, []).append(source)

    failures: list[tuple[str, str]] = []
    modules: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix=".nillib.stage.", dir=stage_dir) as temporary:
        temporary_root = Path(temporary)
        runtime_directory = temporary_root / "native"
        runtime_directory.mkdir()
        runtime_destination = runtime_directory / runtime_source.name
        shutil.copyfile(runtime_source, runtime_destination)
        if shared_source is not None:
            shutil.copyfile(shared_source, runtime_directory / shared_source.name)
        artifact_paths: set[str] = set()
        for module_name in sorted(groups):
            module_sources = groups[module_name]
            artifact_relative = Path(module_artifact_path(module_name))
            artifact_key = artifact_relative.as_posix()
            if artifact_key in artifact_paths:
                raise SystemExit(
                    "logical standard-library modules map to the same artifact: {}".format(
                        artifact_key
                    )
                )
            artifact_paths.add(artifact_key)
            destination = temporary_root / artifact_relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            command = [
                str(compiler),
                "--no-stdlib",
                "--sdk-path",
                str(nilstd),
                "--no-bundle",
                "--emit-binary-nil",
                "--emit-library",
                "--quiet",
                "--output",
                str(destination),
                *(str(source) for source in module_sources),
            ]
            try:
                process = subprocess.run(
                    command,
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=120,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                failures.append((module_name, str(exc)))
                continue
            if process.returncode != 0 or not destination.is_file():
                diagnostic = (process.stdout + process.stderr).strip()
                failures.append((module_name, diagnostic or "compiler produced no artifact"))
                continue
            if not is_nil_library(destination):
                failures.append(
                    (module_name, "compiler output is not a structurally valid NIL v2/v4 library")
                )
                continue

            aliases: list[dict[str, Any]] = []
            for source in module_sources:
                source_relative = source.relative_to(nilstd)
                alias_relative = source_relative.with_suffix(".nil")
                alias_key = alias_relative.as_posix()
                if alias_key == artifact_key:
                    continue
                if alias_key in artifact_paths:
                    raise SystemExit(
                        "logical standard-library artifacts collide at {}".format(
                            alias_key
                        )
                    )
                artifact_paths.add(alias_key)
                alias = temporary_root / alias_relative
                alias.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(destination, alias)
                aliases.append(
                    {
                        "path": "nillib/" + alias_key,
                        "bytes": alias.stat().st_size,
                        "sha256": sha256(alias),
                    }
                )
            modules.append(
                {
                    "name": module_name,
                    "sources": [
                        {
                            "path": "nilstd/" + source.relative_to(nilstd).as_posix(),
                            "bytes": source.stat().st_size,
                            "sha256": sha256(source),
                        }
                        for source in module_sources
                    ],
                    "artifact": {
                        "path": "nillib/" + artifact_key,
                        "bytes": destination.stat().st_size,
                        "sha256": sha256(destination),
                    },
                    "aliases": aliases,
                }
            )

        if failures:
            lines = [
                "standard-library binary packaging failed closed: {} of {} logical modules did not pass strict compilation".format(
                    len(failures), len(groups)
                )
            ]
            for module_name, diagnostic in failures:
                compact = "\n".join(diagnostic.splitlines()[:12])
                lines.append("\n[{}]\n{}".format(module_name, compact))
            raise SystemExit("\n".join(lines))

        nillib = stage_dir / "nillib"
        if nillib.exists() or nillib.is_symlink():
            raise SystemExit("refusing to replace an existing staged nillib directory")
        os.replace(temporary_root, nillib)
        runtime_source.unlink()
        if shared_source is not None:
            shared_source.unlink()

    return modules, len(sources)


def main() -> int:
    args = arguments()
    repo_root = args.repo_root.resolve(strict=True)
    stage_argument = args.stage_dir.absolute()
    if stage_argument.is_symlink():
        raise SystemExit("the SDK stage itself must not be a symlink")
    stage_dir = stage_argument.resolve(strict=True)
    if not repo_root.is_dir() or not stage_dir.is_dir():
        raise SystemExit("NIL Framework repository root and SDK stage must both be directories")
    if stage_dir == Path(stage_dir.anchor) or stage_dir == repo_root:
        raise SystemExit("refusing unsafe SDK stage: {}".format(stage_dir))
    try:
        repo_root.relative_to(stage_dir)
    except ValueError:
        pass
    else:
        raise SystemExit(
            "refusing an SDK stage that contains the NIL Framework repository: {}".format(
                stage_dir
            )
        )

    revision, dirty = source_state(repo_root, args.revision)
    initial_source = source_identity(repo_root)
    if (initial_source["revision"], initial_source["dirty"]) != (revision, dirty):
        raise SystemExit("NIL Framework source identity changed before packaging; refusing stale provenance")
    if args.qualification == "rc-checks" and dirty:
        raise SystemExit("RC SDK checks require clean source, even with --allow-dirty-source")
    if dirty and not args.allow_dirty_source:
        raise SystemExit(
            "refusing to package a dirty NIL Framework worktree; commit/stash the changes or "
            "explicitly pass --allow-dirty-source for a development SDK"
        )

    runtime_source, shared_source = validate_stage_input(stage_dir)
    nilstd = stage_dir / "nilstd"
    docs = stage_dir / "docs"
    compiler = stage_dir / executable_name("nilcompiler")
    try:
        qualification = collect_qualification(
            repo_root, stage_dir, runtime_source, revision, args.qualification,
            args.build_evidence, args.ctest_report, include_capabilities=True, shared_runtime=shared_source)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        raise SystemExit("SDK qualification failed: " + str(error)) from error
    html_pages = sorted(docs.rglob("*.html"), key=lambda path: path.relative_to(docs).as_posix())
    if not html_pages:
        raise SystemExit("compiled SDK documentation is missing from {}".format(docs))

    modules, source_file_count = compile_standard_library(
        compiler, nilstd, stage_dir, runtime_source, shared_source
    )
    packaged_runtime = stage_dir / "nillib" / "native" / runtime_source.name
    packaged_shared = stage_dir / "nillib" / "native" / shared_source.name
    tools = []
    for name in REQUIRED_TOOLS:
        path = stage_dir / executable_name(name)
        tools.append(
            {
                "name": name,
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )

    files = []
    for path in regular_tree_files(stage_dir):
        relative = path.relative_to(stage_dir).as_posix()
        if relative == MANIFEST_NAME:
            raise SystemExit("the SDK stage unexpectedly already contains a manifest")
        files.append(
            {
                "path": relative,
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )

    manifest = {
        "schema": "nil.sdk.v1",
        "source": {
            "revision": revision,
            "worktree_dirty": dirty,
        },
        "host": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "build": {
            "type": args.build_type,
            "llvm": "ON" if qualification["native_backend_available"] else "OFF",
        },
        "qualification": qualification,
        "layout": {
            "tools": ".",
            "compiled_standard_library": "nillib",
            "standard_library_source": "nilstd",
            "documentation": "docs",
        },
        "tools": tools,
        "native_runtime": {
            "path": packaged_runtime.relative_to(stage_dir).as_posix(),
            "bytes": packaged_runtime.stat().st_size,
            "sha256": sha256(packaged_runtime),
        },
        "native_shared_runtime": {
            "path": packaged_shared.relative_to(stage_dir).as_posix(),
            "bytes": packaged_shared.stat().st_size,
            "sha256": sha256(packaged_shared),
        },
        "standard_library": {
            "format": nil_library_format({
                nil_library_version(stage_dir / module["artifact"]["path"])
                for module in modules
            }),
            "strictly_type_checked": True,
            "source_file_count": source_file_count,
            "logical_module_count": len(modules),
            "compiled_module_count": len(modules),
            "artifact_file_count": sum(
                1 + len(module["aliases"]) for module in modules
            ),
            "modules": modules,
        },
        "documentation": {
            "html_page_count": len(html_pages),
        },
        "payload": {
            "file_count": len(files),
            "files": files,
        },
    }
    if source_identity(repo_root) != initial_source:
        raise SystemExit("NIL Framework source identity changed while packaging; refusing stale provenance")
    errors = qualification_errors(manifest, args.qualification == "rc-checks", revision)
    if errors:
        raise SystemExit("SDK qualification failed:\n- " + "\n- ".join(errors))
    manifest_path = stage_dir / MANIFEST_NAME
    temporary_manifest = stage_dir / ".manifest.json.tmp"
    temporary_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary_manifest, manifest_path)
    print(
        "sealed SDK with {} strictly checked standard-library modules and {} files".format(
            len(modules), len(files)
        )
    )
    if dirty:
        print("warning: SDK manifest records an explicitly allowed dirty NIL Framework worktree")
    print("SDK qualification: {} (not a language release declaration)".format(args.qualification))
    if manifest["build"]["llvm"] != args.llvm:
        print("warning: requested LLVM {} but actual backend availability is {}".format(
            args.llvm, manifest["build"]["llvm"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
