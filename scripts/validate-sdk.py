#!/usr/bin/env python3
"""Validate the canonical NIL Framework SDK layout, manifest, and runtime smoke path."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from sdk_artifact_validation import (
    is_nil_library, is_nil_v2_library, is_static_archive,
    nil_library_format, nil_library_version,
)
from sdk_qualification import LEGACY_TOOLS, TOOLS, qualification_errors
from sdk_sources import is_source, module_artifact_path, canonical_std_namespace
from sdk_runtime import STATIC_NAMES, SHARED_NAMES, RUNTIME_PAIRS, shared_library_valid, shared_runtime_smoke


REQUIRED_TOOLS = TOOLS
SDK_DIRECTORIES = ("nillib", "nilstd", "docs")
RELEASE_DOCUMENTATION_FILES = {
    "docs/release/THIRD_PARTY_NOTICES.txt",
    "docs/release/dependency-policy.json",
}
MANIFEST_NAME = "manifest.json"
RETIRED_RUNTIME_NAME = "ny" + "vm"
NATIVE_RUNTIME_ARCHIVE_NAMES = STATIC_NAMES
NATIVE_RUNTIME_PATHS = tuple(
    "nillib/native/" + name for name in NATIVE_RUNTIME_ARCHIVE_NAMES
)
SHARED_RUNTIME_PATHS = tuple("nillib/native/" + name for name in SHARED_NAMES)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
REVISION_PATTERN = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})")
MODULE_PATTERN = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
)
MAXIMUM_PAYLOAD_ENTRIES = 100_000


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--require-rc-checks", action="store_true",
                        help="require technical SDK evidence; does not declare a language release")
    parser.add_argument("--expected-revision",
                        help="require this exact final source Git revision")
    parser.add_argument(
        "--skip-smoke",
        action="store_true",
        help="perform structural validation only; do not execute SDK tools",
    )
    args = parser.parse_args()
    if args.require_rc_checks and args.skip_smoke:
        parser.error("--require-rc-checks cannot skip installed-tool execution smoke")
    if args.require_rc_checks and not args.expected_revision:
        parser.error("--require-rc-checks requires --expected-revision")
    if args.expected_revision and REVISION_PATTERN.fullmatch(args.expected_revision) is None:
        parser.error("--expected-revision must be a full lowercase Git revision")
    return args


def executable_name(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def inventory_tools(paths) -> tuple[str, ...]:
    """Recognize the two sealed v1 inventories, without permitting omissions."""
    return REQUIRED_TOOLS if executable_name("aperture") in paths else LEGACY_TOOLS


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError("SDK manifest exceeds the 16 MiB safety limit")
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError("cannot read SDK manifest: {}".format(exc)) from exc
    if not isinstance(value, dict):
        raise ValueError("SDK manifest root must be an object")
    return value


def inventory_tree(root: Path, errors: list[str]) -> tuple[dict[str, Path], set[str]]:
    files: dict[str, Path] = {}
    directories: set[str] = set()
    pending: list[tuple[Path, PurePosixPath | None]] = [(root, None)]
    while pending:
        directory, relative_parent = pending.pop()
        try:
            entries = sorted(os.scandir(directory), key=lambda entry: entry.name)
        except OSError as exc:
            errors.append("cannot inventory SDK directory {}: {}".format(directory, exc))
            continue
        for entry in entries:
            if len(files) + len(directories) >= MAXIMUM_PAYLOAD_ENTRIES:
                errors.append(
                    "SDK payload exceeds the {}-entry safety limit".format(
                        MAXIMUM_PAYLOAD_ENTRIES
                    )
                )
                return files, directories
            relative = (
                PurePosixPath(entry.name)
                if relative_parent is None
                else relative_parent / entry.name
            )
            key = relative.as_posix()
            path = Path(entry.path)
            try:
                mode = entry.stat(follow_symlinks=False).st_mode
            except OSError as exc:
                errors.append("cannot inspect SDK path {}: {}".format(key, exc))
                continue
            if stat.S_ISLNK(mode):
                errors.append("SDK payload must not contain symlinks: {}".format(key))
            elif stat.S_ISDIR(mode):
                directories.add(key)
                pending.append((path, relative))
            elif stat.S_ISREG(mode):
                files[key] = path
            else:
                errors.append("SDK payload contains a special file: {}".format(key))
    return files, directories


def root_layout_errors(sdk: Path) -> list[str]:
    errors: list[str] = []
    expected_files = {
        *(executable_name(name) for name in LEGACY_TOOLS),
        "LICENSE",
        MANIFEST_NAME,
    }
    expected_directories = set(SDK_DIRECTORIES)
    actual_files: set[str] = set()
    actual_directories: set[str] = set()
    try:
        entries = list(os.scandir(sdk))
    except OSError as exc:
        return ["cannot inspect SDK root {}: {}".format(sdk, exc)]
    for entry in entries:
        try:
            mode = entry.stat(follow_symlinks=False).st_mode
        except OSError as exc:
            errors.append("cannot inspect SDK root entry {}: {}".format(entry.name, exc))
            continue
        if stat.S_ISLNK(mode):
            errors.append("SDK root entry must not be a symlink: {}".format(entry.name))
        elif stat.S_ISREG(mode):
            actual_files.add(entry.name)
        elif stat.S_ISDIR(mode):
            actual_directories.add(entry.name)
        else:
            errors.append("SDK root contains a special entry: {}".format(entry.name))
    if executable_name("aperture") in actual_files:
        expected_files.add(executable_name("aperture"))
    if actual_files != expected_files:
        errors.append(
            "SDK root files differ: missing={} extra={}".format(
                sorted(expected_files - actual_files),
                sorted(actual_files - expected_files),
            )
        )
    if actual_directories != expected_directories:
        errors.append(
            "SDK root directories differ: missing={} extra={}".format(
                sorted(expected_directories - actual_directories),
                sorted(actual_directories - expected_directories),
            )
        )
    return errors


def safe_relative_path(value: Any, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value:
        errors.append("{} must be a non-empty relative POSIX path".format(label))
        return None
    path = PurePosixPath(value)
    if (
        "\\" in value
        or path.is_absolute()
        or path.as_posix() != value
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        errors.append("{} is not a canonical relative POSIX path: {!r}".format(label, value))
        return None
    return value


def require_keys(
    value: Any, expected: set[str], label: str, errors: list[str]
) -> dict[str, Any] | None:
    if not isinstance(value, dict):
        errors.append("{} must be an object".format(label))
        return None
    actual = set(value)
    if actual != expected:
        errors.append(
            "{} fields differ: missing={} extra={}".format(
                label, sorted(expected - actual), sorted(actual - expected)
            )
        )
    return value


def valid_size(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def validate_file_record(
    record: Any,
    label: str,
    files: dict[str, Path],
    errors: list[str],
) -> str | None:
    item = require_keys(record, {"path", "bytes", "sha256"}, label, errors)
    if item is None:
        return None
    key = safe_relative_path(item.get("path"), label + ".path", errors)
    if key is None:
        return None
    path = files.get(key)
    if path is None:
        errors.append("{} refers to a missing file: {}".format(label, key))
        return key
    expected_size = item.get("bytes")
    if not valid_size(expected_size):
        errors.append("{}.bytes must be a non-negative integer".format(label))
    else:
        try:
            actual_size = path.stat().st_size
        except OSError as exc:
            errors.append("cannot stat {}: {}".format(key, exc))
        else:
            if expected_size != actual_size:
                errors.append("{} size mismatch".format(key))
    expected_hash = item.get("sha256")
    if not isinstance(expected_hash, str) or SHA256_PATTERN.fullmatch(expected_hash) is None:
        errors.append("{}.sha256 must be a lowercase SHA-256 digest".format(label))
    else:
        try:
            actual_hash = sha256(path)
        except OSError as exc:
            errors.append("cannot hash {}: {}".format(key, exc))
        else:
            if expected_hash != actual_hash:
                errors.append("{} hash mismatch".format(key))
    return key


def validate_exact_layout(
    sdk: Path,
    files: dict[str, Path],
    directories: set[str],
    errors: list[str],
) -> None:
    expected_root_files = {
        *(executable_name(name) for name in inventory_tools(files)),
        "LICENSE",
        MANIFEST_NAME,
    }
    expected_root_directories = set(SDK_DIRECTORIES)
    actual_root_files = {key for key in files if "/" not in key}
    actual_root_directories = {key for key in directories if "/" not in key}
    if actual_root_files != expected_root_files:
        errors.append(
            "SDK root files differ: missing={} extra={}".format(
                sorted(expected_root_files - actual_root_files),
                sorted(actual_root_files - expected_root_files),
            )
        )
    if actual_root_directories != expected_root_directories:
        errors.append(
            "SDK root directories differ: missing={} extra={}".format(
                sorted(expected_root_directories - actual_root_directories),
                sorted(actual_root_directories - expected_root_directories),
            )
        )

    populated_directories = set(SDK_DIRECTORIES)
    for key in files:
        parent = PurePosixPath(key).parent
        while parent != PurePosixPath("."):
            populated_directories.add(parent.as_posix())
            parent = parent.parent
    empty_directories = directories - populated_directories
    if empty_directories:
        errors.append(
            "SDK contains unexpected empty directories: {}".format(
                sorted(empty_directories)
            )
        )

    for key in files:
        if key.startswith("nilstd/") and not is_source(key):
            errors.append("nilstd contains a non-source file: {}".format(key))
        if (
            key.startswith("nillib/")
            and not key.endswith(".nil")
            and key not in NATIVE_RUNTIME_PATHS + SHARED_RUNTIME_PATHS
        ):
            errors.append("nillib contains a non-NIL artifact: {}".format(key))
        if (key.startswith("docs/") and not key.endswith(".html")
                and key not in RELEASE_DOCUMENTATION_FILES):
            errors.append("docs contains a non-HTML artifact: {}".format(key))
        if PurePosixPath(key).name.lower().startswith(RETIRED_RUNTIME_NAME):
            errors.append("retired public runtime name is present in the SDK: {}".format(key))

    for name in REQUIRED_TOOLS:
        key = executable_name(name)
        path = files.get(key)
        if path is not None and os.name != "nt" and not os.access(path, os.X_OK):
            errors.append("root tool is not executable: {}".format(key))
    runtime_paths = sorted(set(NATIVE_RUNTIME_PATHS).intersection(files))
    if len(runtime_paths) != 1:
        errors.append(
            "SDK must contain exactly one native runtime archive from {}: found {}".format(
                list(NATIVE_RUNTIME_PATHS), runtime_paths
            )
        )
    shared_paths = sorted(set(SHARED_RUNTIME_PATHS).intersection(files))
    if len(shared_paths) != 1:
        errors.append("SDK must contain exactly one shared native runtime: " + str(shared_paths))
    elif len(runtime_paths) == 1 and PurePosixPath(runtime_paths[0]).name not in RUNTIME_PAIRS[PurePosixPath(shared_paths[0]).name]:
        errors.append("SDK static and shared runtime platforms differ")
    if sdk == Path(sdk.anchor):
        errors.append("refusing to validate the filesystem root as an SDK")


def validate_manifest_metadata(manifest: dict[str, Any], errors: list[str]) -> None:
    expected_keys = {
        "schema",
        "source",
        "host",
        "build",
        "layout",
        "tools",
        "native_runtime",
        "native_shared_runtime",
        "standard_library",
        "documentation",
        "payload",
    }
    if manifest.get("schema") == "nil.sdk.v1":
        expected_keys.add("qualification")
    require_keys(manifest, expected_keys, "manifest", errors)
    if manifest.get("schema") != "nil.sdk.v1":
        errors.append("manifest schema must be nil.sdk.v1")

    source = require_keys(
        manifest.get("source"), {"revision", "worktree_dirty"}, "source", errors
    )
    if source is not None:
        revision = source.get("revision")
        if not isinstance(revision, str) or REVISION_PATTERN.fullmatch(revision) is None:
            errors.append("source.revision must be a full lowercase Git object id")
        if not isinstance(source.get("worktree_dirty"), bool):
            errors.append("source.worktree_dirty must be a boolean")

    host = require_keys(manifest.get("host"), {"system", "machine"}, "host", errors)
    if host is not None:
        for field in ("system", "machine"):
            if not isinstance(host.get(field), str) or not host[field]:
                errors.append("host.{} must be a non-empty string".format(field))

    build = require_keys(manifest.get("build"), {"type", "llvm"}, "build", errors)
    if build is not None:
        if not isinstance(build.get("type"), str) or not build["type"]:
            errors.append("build.type must be a non-empty string")
        if build.get("llvm") not in ("ON", "OFF"):
            errors.append("build.llvm must be ON or OFF")

    expected_layout = {
        "tools": ".",
        "compiled_standard_library": "nillib",
        "standard_library_source": "nilstd",
        "documentation": "docs",
    }
    if manifest.get("layout") != expected_layout:
        errors.append("manifest layout does not match the canonical SDK layout")


def validate_tools(
    value: Any, files: dict[str, Path], errors: list[str]
) -> None:
    if not isinstance(value, list):
        errors.append("tools must be an array")
        return
    names: list[str] = []
    paths: list[str] = []
    for index, record in enumerate(value):
        label = "tools[{}]".format(index)
        item = require_keys(record, {"name", "path", "bytes", "sha256"}, label, errors)
        if item is None:
            continue
        name = item.get("name")
        path = item.get("path")
        if isinstance(name, str):
            names.append(name)
        else:
            errors.append("{}.name must be a string".format(label))
        if isinstance(path, str):
            paths.append(path)
        expected_path = executable_name(name) if name in REQUIRED_TOOLS else None
        if expected_path is not None and path != expected_path:
            errors.append("{} must point to the root tool {}".format(label, expected_path))
        validate_file_record(
            {"path": path, "bytes": item.get("bytes"), "sha256": item.get("sha256")},
            label,
            files,
            errors,
        )
    expected_tools = inventory_tools(files)
    if names != list(expected_tools):
        errors.append("manifest tool names must be the canonical ordered tool set")
    expected_paths = [executable_name(name) for name in expected_tools]
    if paths != expected_paths:
        errors.append("manifest tool paths must name each canonical root executable exactly once")


def validate_native_runtime(
    value: Any, files: dict[str, Path], errors: list[str]
) -> None:
    key = validate_file_record(value, "native_runtime", files, errors)
    if key is None:
        return
    if key not in NATIVE_RUNTIME_PATHS:
        errors.append(
            "native_runtime.path must be one of {}".format(
                list(NATIVE_RUNTIME_PATHS)
            )
        )
        return
    runtime = files.get(key)
    if runtime is not None and not is_static_archive(runtime):
        errors.append(
            "native_runtime fails supported archive framing and required "
            "symbol-index offset validation: {}".format(key)
        )
    runtime_paths = sorted(set(NATIVE_RUNTIME_PATHS).intersection(files))
    if len(runtime_paths) == 1 and key != runtime_paths[0]:
        errors.append(
            "native_runtime.path must identify the packaged archive {}".format(
                runtime_paths[0]
            )
        )


def validate_shared_runtime(value: Any, files: dict[str, Path], errors: list[str]) -> None:
    key = validate_file_record(value, "native_shared_runtime", files, errors)
    if key is None:
        return
    if key not in SHARED_RUNTIME_PATHS:
        errors.append("native_shared_runtime.path must identify a supported shared runtime")
        return
    runtime = files.get(key)
    if runtime is not None and not shared_library_valid(runtime):
        errors.append("native_shared_runtime has invalid shared-library framing: " + key)
    actual = sorted(set(SHARED_RUNTIME_PATHS).intersection(files))
    if len(actual) == 1 and key != actual[0]:
        errors.append("native_shared_runtime.path must identify the packaged shared library")


def validate_standard_library(
    value: Any, files: dict[str, Path], errors: list[str]
) -> int:
    expected_keys = {
        "format",
        "strictly_type_checked",
        "source_file_count",
        "logical_module_count",
        "compiled_module_count",
        "artifact_file_count",
        "modules",
    }
    library = require_keys(value, expected_keys, "standard_library", errors)
    if library is None:
        return 0
    if library.get("format") not in {
        nil_library_format({2}), nil_library_format({4}), nil_library_format({2, 4})
    }:
        errors.append("standard_library.format is unsupported")
    if library.get("strictly_type_checked") is not True:
        errors.append("standard_library must be strictly type checked")

    actual_sources = {key for key in files if key.startswith("nilstd/") and is_source(key)}
    actual_artifacts = {key for key in files if key.startswith("nillib/") and key.endswith(".nil")}
    modules = library.get("modules")
    if not isinstance(modules, list):
        errors.append("standard_library.modules must be an array")
        return len(actual_artifacts)

    names: list[str] = []
    mapped_sources: set[str] = set()
    mapped_artifacts: set[str] = set()
    for index, record in enumerate(modules):
        label = "standard_library.modules[{}]".format(index)
        module = require_keys(
            record, {"name", "sources", "artifact", "aliases"}, label, errors
        )
        if module is None:
            continue
        name = module.get("name")
        if (not isinstance(name, str) or MODULE_PATTERN.fullmatch(name) is None
                or canonical_std_namespace(name) != name):
            errors.append("{}.name is not a canonical module name".format(label))
            name = None
        else:
            names.append(name)

        sources = module.get("sources")
        source_paths: list[str] = []
        if not isinstance(sources, list) or not sources:
            errors.append("{}.sources must be a non-empty array".format(label))
        else:
            for source_index, source in enumerate(sources):
                source_label = "{}.sources[{}]".format(label, source_index)
                key = validate_file_record(source, source_label, files, errors)
                if key is not None:
                    source_paths.append(key)
                    if not key.startswith("nilstd/") or not is_source(key):
                        errors.append("{} is outside nilstd or not Ny/Matrix source".format(key))
                    if key in mapped_sources:
                        errors.append("standard-library source is mapped more than once: {}".format(key))
                    mapped_sources.add(key)
            if source_paths != sorted(source_paths):
                errors.append("{}.sources must be sorted by path".format(label))

        artifact = module.get("artifact")
        artifact_key = validate_file_record(artifact, label + ".artifact", files, errors)
        canonical_hash: str | None = None
        if artifact_key is not None:
            if artifact_key in mapped_artifacts:
                errors.append("standard-library artifact is mapped more than once: {}".format(artifact_key))
            mapped_artifacts.add(artifact_key)
            if name is not None:
                expected_artifact = "nillib/" + module_artifact_path(name).as_posix()
                if artifact_key != expected_artifact:
                    errors.append(
                        "{} artifact path must be {}".format(name, expected_artifact)
                    )

            if isinstance(artifact, dict) and isinstance(artifact.get("sha256"), str):
                canonical_hash = artifact["sha256"]

        aliases = module.get("aliases")
        alias_paths: list[str] = []
        if not isinstance(aliases, list):
            errors.append("{}.aliases must be an array".format(label))
        else:
            for alias_index, alias in enumerate(aliases):
                alias_label = "{}.aliases[{}]".format(label, alias_index)
                alias_key = validate_file_record(alias, alias_label, files, errors)
                if alias_key is None:
                    continue
                alias_paths.append(alias_key)
                if alias_key in mapped_artifacts:
                    errors.append(
                        "standard-library artifact is mapped more than once: {}".format(
                            alias_key
                        )
                    )
                mapped_artifacts.add(alias_key)
                if (
                    canonical_hash is not None
                    and isinstance(alias, dict)
                    and alias.get("sha256") != canonical_hash
                ):
                    errors.append(
                        "{} is not byte-identical to its canonical module artifact".format(
                            alias_key
                        )
                    )
            if alias_paths != sorted(alias_paths):
                errors.append("{}.aliases must be sorted by path".format(label))

        if artifact_key is not None and source_paths:
            expected_aliases = sorted(
                {
                    "nillib/"
                    + PurePosixPath(
                        source_path[len("nilstd/") :]
                        if source_path.startswith("nilstd/")
                        else source_path
                    ).with_suffix(".nil").as_posix()
                    for source_path in source_paths
                }
                - {artifact_key}
            )
            if alias_paths != expected_aliases:
                errors.append(
                    "{} artifact aliases differ: expected={} actual={}".format(
                        name or label, expected_aliases, alias_paths
                    )
                )

    if names != sorted(names) or len(names) != len(set(names)):
        errors.append("standard-library logical modules must be unique and sorted by name")
    if mapped_sources != actual_sources:
        errors.append(
            "manifest source mapping differs: missing={} extra={}".format(
                sorted(actual_sources - mapped_sources), sorted(mapped_sources - actual_sources)
            )
        )
    if mapped_artifacts != actual_artifacts:
        errors.append(
            "manifest artifact mapping differs: missing={} extra={}".format(
                sorted(actual_artifacts - mapped_artifacts),
                sorted(mapped_artifacts - actual_artifacts),
            )
        )

    expected_counts = {
        "source_file_count": len(actual_sources),
        "logical_module_count": len(modules),
        "compiled_module_count": len(modules),
        "artifact_file_count": len(actual_artifacts),
    }
    for field, expected in expected_counts.items():
        value = library.get(field)
        if not valid_size(value) or value != expected:
            errors.append("standard_library.{} must equal {}".format(field, expected))

    artifact_versions: set[int] = set()
    for key in actual_artifacts:
        try:
            version = nil_library_version(files[key])
            if version is None:
                errors.append("compiled library is not a structurally valid NIL v2/v4 library: {}".format(key))
            else:
                artifact_versions.add(version)
        except OSError as exc:
            errors.append("cannot read compiled library {}: {}".format(key, exc))
    if artifact_versions and library.get("format") != nil_library_format(artifact_versions):
        errors.append("standard_library.format does not match its artifact versions")
    return len(modules)


def validate_documentation(value: Any, files: dict[str, Path], errors: list[str]) -> int:
    documentation = require_keys(value, {"html_page_count"}, "documentation", errors)
    pages = [key for key in files if key.startswith("docs/") and key.endswith(".html")]
    if not pages:
        errors.append("SDK documentation contains no HTML pages")
    if documentation is not None:
        page_count = documentation.get("html_page_count")
        if not valid_size(page_count) or page_count != len(pages):
            errors.append("documentation.html_page_count must equal {}".format(len(pages)))
    return len(pages)


def validate_payload(value: Any, files: dict[str, Path], errors: list[str]) -> None:
    payload = require_keys(value, {"file_count", "files"}, "payload", errors)
    if payload is None:
        return
    records = payload.get("files")
    if not isinstance(records, list):
        errors.append("payload.files must be an array")
        return
    paths: list[str] = []
    for index, record in enumerate(records):
        key = validate_file_record(record, "payload.files[{}]".format(index), files, errors)
        if key is not None:
            paths.append(key)
    if paths != sorted(paths):
        errors.append("payload.files must be sorted by path")
    if len(paths) != len(set(paths)):
        errors.append("payload.files contains duplicate paths")
    actual_payload = set(files) - {MANIFEST_NAME}
    if set(paths) != actual_payload:
        errors.append(
            "manifest payload differs from disk: missing={} extra={}".format(
                sorted(actual_payload - set(paths)), sorted(set(paths) - actual_payload)
            )
        )
    file_count = payload.get("file_count")
    if not valid_size(file_count) or file_count != len(actual_payload):
        errors.append("payload.file_count must equal {}".format(len(actual_payload)))


def tool_identity_error(
    tool: Path,
    name: str,
    usage: str,
    cwd: Path,
    environment: dict[str, str],
    version_suffix: str = "",
) -> str | None:
    try:
        help_process = subprocess.run(
            [str(tool), "--help"],
            cwd=cwd,
            check=False,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "SDK {} --help could not run: {}".format(name, exc)
    if (
        help_process.returncode != 0
        or usage not in help_process.stdout
        or help_process.stderr
    ):
        return (
            "SDK {} --help identity failed: exit={} stdout={!r} stderr={!r}".format(
                name,
                help_process.returncode,
                help_process.stdout,
                help_process.stderr,
            )
        )

    try:
        version_process = subprocess.run(
            [str(tool), "--version"],
            cwd=cwd,
            check=False,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "SDK {} --version could not run: {}".format(name, exc)
    if (
        version_process.returncode != 0
        or re.fullmatch(re.escape(name) + r" [^\s]+" +
                        re.escape(version_suffix) + r"\n?", version_process.stdout)
        is None
        or version_process.stderr
    ):
        return (
            "SDK {} --version identity failed: exit={} stdout={!r} stderr={!r}".format(
                name,
                version_process.returncode,
                version_process.stdout,
                version_process.stderr,
            )
        )
    return None


def debugger_smoke_error(
    debugger: Path,
    artifact: Path,
    cwd: Path,
    environment: dict[str, str],
) -> str | None:
    requests = "\n".join(
        json.dumps({"cmd": command}) for command in ("run", "run", "quit")
    ) + "\n"
    try:
        process = subprocess.run(
            [str(debugger), "--json", "run", str(artifact)],
            cwd=cwd,
            check=False,
            env=environment,
            input=requests,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "SDK nildebugger JSONL smoke could not run: {}".format(exc)
    if process.returncode != 0 or process.stderr:
        return (
            "SDK nildebugger JSONL smoke failed: exit={} stdout={!r} stderr={!r}".format(
                process.returncode, process.stdout, process.stderr
            )
        )

    events: list[dict[str, Any]] = []
    try:
        for line in process.stdout.splitlines():
            event = json.loads(line)
            if not isinstance(event, dict):
                raise ValueError("JSONL event is not an object")
            events.append(event)
    except (json.JSONDecodeError, ValueError) as exc:
        return "SDK nildebugger emitted invalid JSONL: {} stdout={!r}".format(
            exc, process.stdout
        )
    kinds = [event.get("event") for event in events]
    outputs = [
        event
        for event in events
        if event.get("event") == "output" and event.get("category") == "stdout"
    ]
    if (
        not kinds
        or kinds[0] != "started"
        or kinds[-2:] != ["quit", "detached"]
        or "error" in kinds
        or kinds.count("running") != 2
        or kinds.count("terminated") != 2
        or kinds.count("output") != 2
        or [event.get("output") for event in outputs]
        != ["NIL Framework SDK ready\n", "NIL Framework SDK ready\n"]
    ):
        return "SDK nildebugger run/restart/output contract failed: events={!r}".format(
            events
        )
    return None


def run_smoke(sdk: Path, native_enabled: bool) -> list[str]:
    errors: list[str] = []
    compiler = sdk / executable_name("nilcompiler")
    interpreter = sdk / executable_name("nilinterpreter")
    assembler = sdk / executable_name("nilassembler")
    linker = sdk / executable_name("nillinker")
    debugger = sdk / executable_name("nildebugger")
    smoke_environment = os.environ.copy()
    for variable in (
        "NY_LIBRARY_PATH", "NIL_LIBRARY_PATH",
        "NY_LEX_DUMP", "NIL_LEX_DUMP",
        "NY_PARSE_TRACE", "NIL_PARSE_TRACE",
        "NY_STDLIB_PATH", "NIL_STDLIB_PATH",
    ):
        smoke_environment.pop(variable, None)
    with tempfile.TemporaryDirectory(prefix="nil-sdk-smoke-") as temporary:
        root = Path(temporary)
        for tool, name, usage in (
            (assembler, "nilassembler", "Usage: nilassembler"),
            (linker, "nillinker", "Usage: nillinker"),
        ):
            identity_error = tool_identity_error(
                tool, name, usage, root, smoke_environment
            )
            if identity_error is not None:
                errors.append(identity_error)
        aperture = sdk / executable_name("aperture")
        if aperture.is_file():
            identity_error = tool_identity_error(
                aperture, "aperture", "Usage: aperture", root, smoke_environment,
                version_suffix=" (NIL Framework)")
            if identity_error is not None:
                errors.append(identity_error)
        if errors:
            return errors

        source = root / "sdk_smoke.ny"
        artifact = root / "sdk_smoke.nil"
        source.write_text(
            "import ny.lang.core\n\n"
            "fn main() -> i32\n"
            "    println(\"NIL Framework SDK ready\")\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        try:
            compile_process = subprocess.run(
                [
                    str(compiler),
                    "--no-stdlib",
                    "--sdk-path",
                    str(sdk / "nillib"),
                    "--no-bundle",
                    "--emit-binary-nil",
                    "--quiet",
                    "--output",
                    str(artifact),
                    str(source),
                ],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK compiler smoke could not run: {}".format(exc)]
        if compile_process.returncode != 0 or not artifact.is_file():
            errors.append(
                "SDK compiler/binary-library discovery smoke failed:\n{}".format(
                    (compile_process.stdout + compile_process.stderr).strip()
                    or "compiler produced no artifact"
                )
            )
            return errors
        try:
            run_process = subprocess.run(
                [str(interpreter), str(artifact)],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK interpreter smoke could not run: {}".format(exc)]
        if (
            run_process.returncode != 0
            or run_process.stdout != "NIL Framework SDK ready\n"
            or run_process.stderr
        ):
            errors.append(
                "SDK interpreter smoke contract failed: exit={} stdout={!r} stderr={!r}".format(
                    run_process.returncode, run_process.stdout, run_process.stderr
                )
            )
            return errors

        debugger_error = debugger_smoke_error(
            debugger, artifact, root, smoke_environment
        )
        if debugger_error is not None:
            errors.append(debugger_error)
            return errors

        object_output = root / ("sdk_smoke.obj" if os.name == "nt" else "sdk_smoke.o")
        if not native_enabled:
            try:
                assemble_process = subprocess.run(
                    [str(assembler), "-o", str(object_output), str(artifact)],
                    cwd=root,
                    check=False,
                    env=smoke_environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=60,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                return ["SDK LLVM-disabled assembler smoke could not run: {}".format(exc)]
            staging_leaks = sorted(path.name for path in root.glob(".nilassembler-*"))
            if (
                assemble_process.returncode == 0
                or assemble_process.stdout
                or "nilassembler:" not in assemble_process.stderr
                or "no LLVM backend" not in assemble_process.stderr
                or object_output.exists()
                or staging_leaks
            ):
                errors.append(
                    "SDK LLVM-disabled assembler capability contract failed: "
                    "exit={} stdout={!r} stderr={!r} object={} staging={!r}".format(
                        assemble_process.returncode,
                        assemble_process.stdout,
                        assemble_process.stderr,
                        object_output.exists(),
                        staging_leaks,
                    )
                )
            return errors

        linked_output = root / "sdk_toolchain_smoke"
        linked_executable = (
            linked_output.with_suffix(".exe") if os.name == "nt" else linked_output
        )
        try:
            assemble_process = subprocess.run(
                [str(assembler), "-o", str(object_output), str(artifact)],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK assembler smoke could not run: {}".format(exc)]
        if assemble_process.returncode != 0 or not object_output.is_file():
            errors.append(
                "SDK NIL-to-object smoke failed: exit={} stdout={!r} "
                "stderr={!r} object={}".format(
                    assemble_process.returncode,
                    assemble_process.stdout,
                    assemble_process.stderr,
                    object_output.is_file(),
                )
            )
            return errors

        try:
            link_process = subprocess.run(
                [
                    str(linker),
                    "-o",
                    str(linked_output),
                    str(object_output),
                ],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK object linker smoke could not run: {}".format(exc)]
        if link_process.returncode != 0 or not linked_executable.is_file():
            errors.append(
                "SDK object link smoke failed: exit={} stdout={!r} stderr={!r} "
                "executable={}".format(
                    link_process.returncode,
                    link_process.stdout,
                    link_process.stderr,
                    linked_executable.is_file(),
                )
            )
            return errors

        try:
            linked_run = subprocess.run(
                [str(linked_executable)],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK linked executable smoke could not run: {}".format(exc)]
        if (
            linked_run.returncode != 0
            or linked_run.stdout != "NIL Framework SDK ready\n"
            or linked_run.stderr
        ):
            errors.append(
                "SDK linked executable contract failed: exit={} stdout={!r} "
                "stderr={!r}".format(
                    linked_run.returncode,
                    linked_run.stdout,
                    linked_run.stderr,
                )
            )
            return errors

        native_output = root / "sdk_native_smoke"
        native_executable = (
            native_output.with_suffix(".exe") if os.name == "nt" else native_output
        )
        try:
            native_compile = subprocess.run(
                [
                    str(compiler),
                    "--native",
                    "--sdk-path",
                    str(sdk),
                    "--quiet",
                    "--output",
                    str(native_output),
                    str(source),
                ],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK native AOT compiler smoke could not run: {}".format(exc)]
        if (
            native_compile.returncode != 0
            or native_compile.stdout
            or native_compile.stderr
            or not native_executable.is_file()
        ):
            errors.append(
                "SDK native AOT compile smoke failed: exit={} stdout={!r} "
                "stderr={!r} executable={}".format(
                    native_compile.returncode,
                    native_compile.stdout,
                    native_compile.stderr,
                    native_executable.is_file(),
                )
            )
            return errors
        try:
            native_run = subprocess.run(
                [str(native_executable)],
                cwd=root,
                check=False,
                env=smoke_environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return ["SDK native AOT executable smoke could not run: {}".format(exc)]
        if (
            native_run.returncode != 0
            or native_run.stdout != "NIL Framework SDK ready\n"
            or native_run.stderr
        ):
            errors.append(
                "SDK native AOT execution smoke failed: exit={} stdout={!r} "
                "stderr={!r}".format(
                    native_run.returncode, native_run.stdout, native_run.stderr
                )
            )
    return errors


def validation_success_message(
    structural_only: bool, module_count: int, page_count: int,
    tool_count: int = len(REQUIRED_TOOLS),
) -> str:
    summary = (
        "{} tools, {} logical library modules, {} documentation pages".format(
            tool_count, module_count, page_count
        )
    )
    if structural_only:
        return (
            "NIL Framework SDK structural validation only passed (execution smoke skipped): "
            + summary
        )
    return "NIL Framework SDK contract valid: " + summary


def main() -> int:
    args = arguments()
    requested_sdk = args.sdk.absolute()
    errors: list[str] = []
    try:
        sdk = requested_sdk.resolve(strict=True)
    except OSError as exc:
        print("SDK validation failed:\n- cannot resolve SDK path {}: {}".format(requested_sdk, exc))
        return 1
    if not sdk.is_dir():
        print("SDK validation failed:\n- SDK path is not a directory: {}".format(requested_sdk))
        return 1

    if sdk == Path(sdk.anchor):
        print("SDK validation failed:\n- refusing to inspect the filesystem root as an SDK")
        return 1
    preflight_errors = root_layout_errors(sdk)
    if preflight_errors:
        print("SDK validation failed:")
        for error in preflight_errors:
            print("- {}".format(error))
        return 1

    files, directories = inventory_tree(sdk, errors)
    validate_exact_layout(sdk, files, directories, errors)
    manifest_path = files.get(MANIFEST_NAME)
    manifest_loaded = False
    if manifest_path is None:
        errors.append("SDK manifest is missing")
        manifest: dict[str, Any] = {}
    else:
        try:
            manifest = load_manifest(manifest_path)
        except ValueError as exc:
            errors.append(str(exc))
            manifest = {}
        else:
            manifest_loaded = True

    if manifest_loaded:
        validate_manifest_metadata(manifest, errors)
        errors.extend(qualification_errors(manifest, args.require_rc_checks,
                                           args.expected_revision))
        validate_tools(manifest.get("tools"), files, errors)
        validate_native_runtime(manifest.get("native_runtime"), files, errors)
        validate_shared_runtime(manifest.get("native_shared_runtime"), files, errors)
        module_count = validate_standard_library(
            manifest.get("standard_library"), files, errors
        )
        page_count = validate_documentation(
            manifest.get("documentation"), files, errors
        )
        validate_payload(manifest.get("payload"), files, errors)
    else:
        module_count = 0
        page_count = 0

    if not args.skip_smoke and not errors:
        build = manifest.get("build") if manifest_loaded else None
        native_enabled = isinstance(build, dict) and build.get("llvm") == "ON"
        shared_error = shared_runtime_smoke(sdk / manifest["native_shared_runtime"]["path"])
        if shared_error:
            errors.append(shared_error)
        errors.extend(run_smoke(sdk, native_enabled))

    if errors:
        print("SDK validation failed:")
        for error in errors:
            print("- {}".format(error))
        return 1
    print(validation_success_message(args.skip_smoke, module_count, page_count,
                                     len(inventory_tools(files))))
    qualification = manifest.get("qualification", {})
    mode = qualification.get("mode", "development (legacy manifest)")
    print("SDK qualification: {} (not a language release declaration)".format(mode))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
