#!/usr/bin/env python3
"""Regress private, failure-atomic Ny executable staging and JIT output."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence


LLVM_DISABLED_MARKERS = (
    "native backend (llvm) is not compiled",
    "llvm native codegen disabled",
)
LEGACY_STAGING_CANDIDATES = 1000
PRIVATE_WORKSPACE_PREFIXES = (".ny-native-", ".ny-bundle-", ".ny-jit-")


@dataclass(frozen=True)
class PlantedEntry:
    path: Path
    payload: bytes
    target: Optional[Path]
    link_text: Optional[str]


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument(
        "--work-root",
        type=Path,
        help="existing parent for the isolated temporary test directory",
    )
    return parser.parse_args()


def run_command(
    command: Sequence[str],
    cwd: Path,
    timeout_seconds: float = 90.0,
    environment: Optional[dict[str, str]] = None,
    child_umask: Optional[int] = None,
) -> subprocess.CompletedProcess[str]:
    process_options = {}
    if child_umask is not None:
        if os.name != "posix":
            raise AssertionError("a child umask is only supported on POSIX")

        def set_child_umask() -> None:
            os.umask(child_umask)

        process_options["preexec_fn"] = set_child_umask
    try:
        return subprocess.run(
            [str(argument) for argument in command],
            cwd=str(cwd),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            timeout=timeout_seconds,
            env=environment,
            **process_options,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AssertionError(
            "could not run {!r}: {}".format([str(part) for part in command], error)
        ) from error


def compiler_diagnostic(process: subprocess.CompletedProcess[str]) -> str:
    return process.stdout + process.stderr


def llvm_is_explicitly_disabled(process: subprocess.CompletedProcess[str]) -> bool:
    diagnostic = compiler_diagnostic(process).lower()
    return any(marker in diagnostic for marker in LLVM_DISABLED_MARKERS)


def native_output_path(requested: Path) -> Path:
    if os.name == "nt" and requested.suffix.lower() != ".exe":
        return requested.with_suffix(requested.suffix + ".exe")
    return requested


def bundle_output_path(requested: Path) -> Path:
    if requested.suffix == ".nil":
        return requested.with_suffix(".exe" if os.name == "nt" else "")
    if os.name == "nt" and not requested.suffix:
        return Path(str(requested) + ".exe")
    return requested


def legacy_staging_names(requested: Path) -> List[Path]:
    """Return the predictable sibling paths used by the retired AOT flow."""
    executable = native_output_path(requested)
    object_suffix = ".obj" if os.name == "nt" else ".o"
    return [
        Path(str(executable) + object_suffix),
        Path(str(executable) + ".ny-native.tmp.0"),
        Path(str(requested) + ".ny-nil.tmp.0"),
    ]


def plant_entries(paths: Sequence[Path], label: str) -> List[PlantedEntry]:
    planted: List[PlantedEntry] = []
    for index, path in enumerate(paths):
        payload = ("Ny staging sentinel: {}:{}\n".format(label, index)).encode(
            "utf-8"
        )
        if os.name != "posix" or index != 0:
            path.write_bytes(payload)
            planted.append(PlantedEntry(path, payload, None, None))
            continue

        target = path.parent / (
            "{} legacy target {}.sentinel".format(label, index)
        )
        target.write_bytes(payload)
        link_text = os.path.relpath(str(target), str(path.parent))
        path.symlink_to(link_text)
        planted.append(PlantedEntry(path, payload, target, link_text))
    return planted


def plant_legacy_entries(requested: Path, label: str) -> List[PlantedEntry]:
    return plant_entries(legacy_staging_names(requested), label)


def plant_predictable_namespace(
    output: Path, purpose: str, label: str
) -> List[PlantedEntry]:
    paths = [
        Path(str(output) + ".{}.tmp.{}".format(purpose, index))
        for index in range(LEGACY_STAGING_CANDIDATES)
    ]
    return plant_entries(paths, label)


def verify_planted_entries(planted: Sequence[PlantedEntry], stage: str) -> None:
    for entry in planted:
        if entry.target is None:
            if not entry.path.is_file() or entry.path.is_symlink():
                raise AssertionError(
                    "{} changed legacy sentinel entry type: {}".format(
                        stage, entry.path
                    )
                )
            actual = entry.path.read_bytes()
        else:
            if not entry.path.is_symlink():
                raise AssertionError(
                    "{} replaced legacy staging symlink: {}".format(
                        stage, entry.path
                    )
                )
            if os.readlink(str(entry.path)) != entry.link_text:
                raise AssertionError(
                    "{} redirected legacy staging symlink: {}".format(
                        stage, entry.path
                    )
                )
            if not entry.target.is_file():
                raise AssertionError(
                    "{} removed legacy staging target: {}".format(
                        stage, entry.target
                    )
                )
            actual = entry.target.read_bytes()
        if actual != entry.payload:
            raise AssertionError(
                "{} modified legacy staging sentinel bytes: {}".format(
                    stage, entry.path
                )
            )


def assert_no_private_workspace(root: Path, stage: str) -> None:
    leaked = sorted(
        path.name
        for path in root.iterdir()
        if path.name.startswith(PRIVATE_WORKSPACE_PREFIXES)
    )
    if leaked:
        raise AssertionError(
            "{} leaked private output staging workspace(s): {}".format(
                stage, ", ".join(leaked)
            )
        )


def compile_native(
    compiler: Path,
    source: Path,
    output: Path,
    extra: Sequence[str],
    child_umask: Optional[int] = None,
) -> subprocess.CompletedProcess[str]:
    command = [
        str(compiler),
        "--native",
        "--no-stdlib",
        "--quiet",
        "--output",
        str(output),
    ]
    command.extend(str(argument) for argument in extra)
    command.append(str(source))
    return run_command(command, output.parent, child_umask=child_umask)


def compile_bundle(
    compiler: Path,
    source: Path,
    output: Path,
    environment: Optional[dict[str, str]] = None,
    child_umask: Optional[int] = None,
) -> subprocess.CompletedProcess[str]:
    return run_command(
        [
            str(compiler),
            "--no-stdlib",
            "--quiet",
            "--output",
            str(output),
            str(source),
        ],
        output.parent,
        environment=environment,
        child_umask=child_umask,
    )


def process_failure(process: subprocess.CompletedProcess[str]) -> str:
    diagnostic = compiler_diagnostic(process).strip()
    return "exit={} output={!r}".format(process.returncode, diagnostic)


def verify_successful_program(
    executable: Path, cwd: Path, route: str
) -> subprocess.CompletedProcess[str]:
    if not executable.is_file() or executable.is_symlink():
        raise AssertionError(
            "{} did not publish a regular executable: {}".format(route, executable)
        )
    executed = run_command([str(executable)], cwd, timeout_seconds=30.0)
    if executed.returncode != 0:
        raise AssertionError(
            "{} executable failed: {}".format(route, process_failure(executed))
        )
    return executed


def run_post_staging_bundle_failure(
    compiler: Path, source: Path, root: Path
) -> None:
    if os.name != "posix":
        return

    isolated_tools = root / "isolated compiler without interpreter"
    isolated_tools.mkdir()
    isolated_compiler = isolated_tools / compiler.name
    shutil.copy2(compiler, isolated_compiler)
    empty_path = root / "empty executable search path"
    empty_path.mkdir()
    environment = os.environ.copy()
    environment["PATH"] = str(empty_path)

    requested = root / "post staging bundle failure.nil"
    output = bundle_output_path(requested)
    sentinel = b"post-staging bundle failure must preserve this output\n"
    output.write_bytes(sentinel)
    failed = compile_bundle(
        isolated_compiler, source, requested, environment=environment
    )
    diagnostic = compiler_diagnostic(failed).lower()
    if failed.returncode == 0 or "nilinterpreter was not found" not in diagnostic:
        raise AssertionError(
            "isolated bundle did not reach the post-staging interpreter failure: "
            + process_failure(failed)
        )
    if not output.is_file() or output.is_symlink() or output.read_bytes() != sentinel:
        raise AssertionError("post-staging bundle failure modified its output")
    if requested.exists() or requested.is_symlink():
        raise AssertionError("post-staging bundle failure published a NIL artifact")
    assert_no_private_workspace(root, "post-staging bundle failure")


def run_restrictive_umask_bundle(
    compiler: Path, source: Path, root: Path
) -> None:
    if os.name != "posix":
        return
    requested = root / "restrictive umask bundle.nil"
    executable = bundle_output_path(requested)
    compiled = compile_bundle(compiler, source, requested, child_umask=0o777)
    if compiled.returncode != 0:
        raise AssertionError(
            "bundle compilation failed under umask 0777: "
            + process_failure(compiled)
        )
    verify_successful_program(executable, root, "restrictive-umask bundle")
    assert_no_private_workspace(root, "restrictive-umask bundle")


def run_restrictive_umask_aot(compiler: Path, source: Path, root: Path) -> None:
    if os.name != "posix":
        return
    output = root / "restrictive umask native program"
    compiled = compile_native(
        compiler, source, output, (), child_umask=0o777
    )
    if compiled.returncode != 0:
        raise AssertionError(
            "native AOT compilation failed under umask 0777: "
            + process_failure(compiled)
        )
    verify_successful_program(output, root, "restrictive-umask native AOT")
    assert_no_private_workspace(root, "restrictive-umask AOT")


def run_bundle_regression(
    compiler: Path, source: Path, root: Path
) -> subprocess.CompletedProcess[str]:
    requested = root / "bundled program.nil"
    executable = bundle_output_path(requested)
    replaced_sentinel = b"successful bundle must atomically replace this output\n"
    output_victim: Optional[Path] = None
    if os.name == "posix":
        output_victim = root / "bundle output symlink target.sentinel"
        output_victim.write_bytes(replaced_sentinel)
        executable.symlink_to(os.path.relpath(output_victim, executable.parent))
    else:
        executable.write_bytes(replaced_sentinel)
    nil_plants = plant_predictable_namespace(requested, "ny-nil", "bundle-nil")
    bundle_plants = plant_predictable_namespace(
        executable, "nybundle", "bundle-executable"
    )

    compiled = compile_bundle(compiler, source, requested)
    if compiled.returncode != 0:
        raise AssertionError("bundle compile failed: " + process_failure(compiled))
    verify_planted_entries(nil_plants, "successful bundle")
    verify_planted_entries(bundle_plants, "successful bundle")
    assert_no_private_workspace(root, "successful bundle")
    if requested.exists() or requested.is_symlink():
        raise AssertionError("successful bundle leaked its intermediate NIL output")
    executed = verify_successful_program(executable, root, "bundled")
    if executable.read_bytes() == replaced_sentinel:
        raise AssertionError("successful bundle did not replace the old output")
    if output_victim is not None and output_victim.read_bytes() != replaced_sentinel:
        raise AssertionError("bundle publication modified the output symlink target")

    failed_requested = root / "failed bundle output.nil"
    failed_output = bundle_output_path(failed_requested)
    failure_sentinel = b"caller-owned bundle output must survive compilation failure\n"
    failed_output.write_bytes(failure_sentinel)
    invalid_source = root / "invalid bundle source.ny"
    invalid_source.write_text("fn main( -> i32\n", encoding="utf-8")
    failed = compile_bundle(compiler, invalid_source, failed_requested)
    if failed.returncode == 0:
        raise AssertionError("invalid Ny source unexpectedly produced a bundle")
    if not failed_output.is_file() or failed_output.is_symlink():
        raise AssertionError("failed bundle compilation replaced the output entry")
    if failed_output.read_bytes() != failure_sentinel:
        raise AssertionError("failed bundle compilation modified the output bytes")
    if failed_requested.exists() or failed_requested.is_symlink():
        raise AssertionError("failed bundle compilation published a NIL artifact")
    assert_no_private_workspace(root, "failed bundle")
    run_post_staging_bundle_failure(compiler, source, root)
    run_restrictive_umask_bundle(compiler, source, root)
    return executed


def run_jit_regression(compiler: Path, root: Path) -> None:
    source = root / "jit argument contract.ny"
    source.write_text(
        """@interop(\"std_sys\")
fn arg_count() -> i64
end fn

@interop(\"std_sys\")
fn arg(index: i64) -> text
end fn

fn main() -> i32
    println(arg_count())
    println(arg(0))
    println(arg(1))
    println(arg(2))
    return 0
end fn
""",
        encoding="utf-8",
    )
    requested = root / "jit requested output.nil"
    output_sentinel = b"JIT must not materialize or replace its requested output\n"
    requested.write_bytes(output_sentinel)
    nil_plants = plant_predictable_namespace(requested, "ny-nil", "jit-nil")
    command = [
        str(compiler),
        "--native",
        "--jit",
        "--no-stdlib",
        "--quiet",
        "--output",
        str(requested),
        str(source),
        "--",
        "alpha",
        "",
        "--looks-like-an-option",
    ]
    executed = run_command(command, root)
    if executed.returncode != 0:
        raise AssertionError("native JIT failed: " + process_failure(executed))
    expected_stdout = "3\nalpha\n\n--looks-like-an-option\n"
    if executed.stdout != expected_stdout or executed.stderr:
        raise AssertionError(
            "native JIT argument contract failed: stdout={!r} stderr={!r}".format(
                executed.stdout, executed.stderr
            )
        )
    if not requested.is_file() or requested.is_symlink():
        raise AssertionError("native JIT replaced its requested output entry")
    if requested.read_bytes() != output_sentinel:
        raise AssertionError("native JIT modified its requested output bytes")
    verify_planted_entries(nil_plants, "native JIT")
    assert_no_private_workspace(root, "native JIT")


def run_regression(compiler: Path, source: Path, root: Path) -> int:
    bundled_run = run_bundle_regression(compiler, source, root)

    success_requested = root / "successful native program"
    success_output = native_output_path(success_requested)
    success_plants = plant_legacy_entries(success_requested, "success")

    compiled = compile_native(compiler, source, success_requested, ())
    if compiled.returncode != 0:
        verify_planted_entries(success_plants, "LLVM-disabled compile")
        assert_no_private_workspace(root, "LLVM-disabled compile")
        if llvm_is_explicitly_disabled(compiled):
            print("Ny output staging regression skipped: LLVM is disabled")
            return 77
        raise AssertionError("native AOT compile failed: " + process_failure(compiled))

    verify_planted_entries(success_plants, "successful compile")
    assert_no_private_workspace(root, "successful compile")
    executed = verify_successful_program(success_output, root, "native AOT")
    if (executed.stdout, executed.stderr) != (
        bundled_run.stdout,
        bundled_run.stderr,
    ):
        raise AssertionError(
            "bundle and native AOT execution disagree: bundle={!r}/{!r} "
            "native={!r}/{!r}".format(
                bundled_run.stdout,
                bundled_run.stderr,
                executed.stdout,
                executed.stderr,
            )
        )

    failure_requested = root / "failed native output sentinel"
    failure_output = native_output_path(failure_requested)
    output_sentinel = b"caller-owned native output must survive link failure\n\x00\xff"
    failure_output.write_bytes(output_sentinel)
    failure_plants = plant_legacy_entries(failure_requested, "failure")
    absent_library = root / "guaranteed absent native library.a"
    if absent_library.exists() or absent_library.is_symlink():
        raise AssertionError("absent-library fixture unexpectedly exists")

    failed = compile_native(
        compiler,
        source,
        failure_requested,
        ("--link", str(absent_library)),
    )
    if failed.returncode == 0:
        raise AssertionError("native AOT link unexpectedly accepted an absent library")
    if llvm_is_explicitly_disabled(failed):
        raise AssertionError("LLVM became unavailable after a successful native compile")
    if "all linker strategies failed" not in compiler_diagnostic(failed).lower():
        raise AssertionError(
            "native AOT failure did not reach the forced linker failure: "
            + process_failure(failed)
        )
    if not failure_output.is_file() or failure_output.is_symlink():
        raise AssertionError("failed native link replaced the caller-owned output entry")
    if failure_output.read_bytes() != output_sentinel:
        raise AssertionError("failed native link modified the caller-owned output bytes")
    verify_planted_entries(failure_plants, "failed link")
    assert_no_private_workspace(root, "failed link")

    run_restrictive_umask_aot(compiler, source, root)
    run_jit_regression(compiler, root)

    print("Ny private executable-staging and JIT regression passed")
    return 0


def main() -> int:
    args = arguments()
    compiler = args.compiler.resolve(strict=True)
    source = args.source.resolve(strict=True)
    if not compiler.is_file():
        raise AssertionError("compiler is not a regular file: {}".format(compiler))
    if not source.is_file():
        raise AssertionError("source is not a regular file: {}".format(source))

    temporary_parent: Optional[str] = None
    if args.work_root is not None:
        work_root = args.work_root.resolve(strict=True)
        if not work_root.is_dir():
            raise AssertionError("work root is not a directory: {}".format(work_root))
        temporary_parent = str(work_root)

    with tempfile.TemporaryDirectory(
        prefix="ny private output staging regression ", dir=temporary_parent
    ) as raw_root:
        root = Path(raw_root)
        if " " not in root.name:
            raise AssertionError("isolated staging test directory does not contain spaces")
        return run_regression(compiler, source, root)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print("Ny output staging regression failed: {}".format(error), file=sys.stderr)
        raise SystemExit(1)
