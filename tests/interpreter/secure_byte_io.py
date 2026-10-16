#!/usr/bin/env python3
"""Exercise hardened Ny file I/O through textual and binary NIL."""

from __future__ import annotations

import argparse
import os
import stat
import subprocess
import tempfile
import time
from pathlib import Path


EXPECTED_STDOUT = """\
read-exact
write-new
file.write_bytes_atomic: destination already exists
write-replace
file.read_regular_bytes: input exceeds maximum_bytes
empty-read
file.read_regular_bytes: maximum_bytes must be between 0 and 536870912
file.read_regular_bytes: maximum_bytes must be between 0 and 536870912
symlink-read-rejected
special-read-rejected
dynamic-size-read-rejected
file.read_regular_bytes: path must be non-empty and contain no NUL byte
file.write_bytes_atomic: path must be non-empty and contain no NUL byte
file.write_bytes_atomic: path must be non-empty and contain no NUL byte
symlink-output-no-clobber
symlink-output-replace-rejected
write-replace-absent
directory-output-rejected
write-metadata-replace
dynamic-array-write
"""


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--interpreter", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--stdlib", required=True, type=Path)
    return parser.parse_args()


def compile_fixture(
    args: argparse.Namespace, artifact: Path, binary: bool
) -> subprocess.CompletedProcess[str]:
    command = [
        str(args.compiler),
        "--no-stdlib",
        "--sdk-path",
        str(args.stdlib),
        "--no-bundle",
    ]
    if binary:
        command.append("--emit-binary-nil")
    command.extend(["--quiet", "--output", str(artifact), str(args.source)])
    return subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
    )


def interpreter_command(
    args: argparse.Namespace, artifact: Path, *paths: Path
) -> list[str]:
    return [
        str(args.interpreter),
        "--no-ai",
        str(artifact),
        "--",
        *(str(path) for path in paths),
    ]


def require_regular_publication(path: Path, route: str, purpose: str) -> None:
    mode = path.lstat().st_mode
    if not stat.S_ISREG(mode):
        raise AssertionError(
            "{} {} is not a regular file".format(route, purpose)
        )
    actual = stat.S_IMODE(mode)
    if os.name == "posix" and actual != 0o600:
        raise AssertionError(
            "{} {} mode is {:04o}, expected 0600".format(route, purpose, actual)
        )


def set_restrictive_umask() -> None:
    os.umask(0o777)


def run_no_clobber_race(
    args: argparse.Namespace,
    artifact: Path,
    route_root: Path,
    route: str,
    restrictive_umask: bool,
) -> Path:
    race_root = route_root / "no-clobber-race"
    race_root.mkdir()
    destination = race_root / "winner.bin"
    gate = race_root / "start.gate"
    expected_loser = "file.write_bytes_atomic: destination already exists\n"
    worker_count = 8
    workers = []

    try:
        for index in range(worker_count):
            payload = bytes((index, 255 - index)) * (128 * 1024)
            payload_path = race_root / "payload-{}.bin".format(index)
            ready_path = race_root / "ready-{}".format(index)
            payload_path.write_bytes(payload)
            process = subprocess.Popen(
                interpreter_command(
                    args, artifact, payload_path, destination, ready_path, gate
                ),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                preexec_fn=set_restrictive_umask if restrictive_umask else None,
            )
            workers.append((process, payload, ready_path))

        ready_deadline = time.monotonic() + 20
        while not all(ready_path.exists() for _, _, ready_path in workers):
            for process, _, ready_path in workers:
                if process.poll() is not None and not ready_path.exists():
                    stdout, stderr = process.communicate()
                    raise AssertionError(
                        "{} no-clobber worker exited before the gate: exit={} "
                        "stdout={!r} stderr={!r}".format(
                            route, process.returncode, stdout, stderr
                        )
                    )
            if time.monotonic() >= ready_deadline:
                missing = [
                    ready_path.name
                    for _, _, ready_path in workers
                    if not ready_path.exists()
                ]
                raise AssertionError(
                    "{} no-clobber workers did not reach the gate: {}".format(
                        route, ", ".join(missing)
                    )
                )
            time.sleep(0.01)

        gate.write_bytes(b"go")
        winner_payloads = []
        for process, payload, _ in workers:
            try:
                stdout, stderr = process.communicate(timeout=30)
            except subprocess.TimeoutExpired as error:
                raise AssertionError(
                    "{} no-clobber worker timed out".format(route)
                ) from error
            if process.returncode != 0 or stderr:
                raise AssertionError(
                    "{} no-clobber worker failed: exit={} stdout={!r} "
                    "stderr={!r}".format(
                        route, process.returncode, stdout, stderr
                    )
                )
            if stdout == "race-winner\n":
                winner_payloads.append(payload)
            elif stdout != expected_loser:
                raise AssertionError(
                    "{} no-clobber worker returned unexpected stdout: {!r}".format(
                        route, stdout
                    )
                )

        if len(winner_payloads) != 1:
            raise AssertionError(
                "{} no-clobber race produced {} winners, expected one".format(
                    route, len(winner_payloads)
                )
            )
        if destination.read_bytes() != winner_payloads[0]:
            raise AssertionError(
                "{} no-clobber destination is not the complete winning payload".format(
                    route
                )
            )
        return destination
    finally:
        for process, _, _ in workers:
            if process.poll() is None:
                process.terminate()
        for process, _, _ in workers:
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()


def run_route(
    args: argparse.Namespace,
    root: Path,
    route: str,
    binary: bool,
    restrictive_umask: bool = False,
) -> None:
    route_root = root / route
    route_root.mkdir()
    artifact = route_root / (
        "secure-byte-io.binary.nil" if binary else "secure-byte-io.nil"
    )
    compiled = compile_fixture(args, artifact, binary)
    if compiled.returncode != 0:
        raise AssertionError(
            "{} compilation failed:\n{}".format(
                route, compiled.stdout + compiled.stderr
            )
        )
    artifact_bytes = artifact.read_bytes()
    if not binary and b"\0" in artifact_bytes:
        raise AssertionError("textual NIL contains a raw NUL byte")
    if binary and not artifact_bytes.startswith(b"NIL\0"):
        raise AssertionError("binary NIL is missing canonical NIL magic")

    original = bytes(range(256))
    input_path = route_root / "input-private-name.bin"
    output_path = route_root / "output.bin"
    existing_path = route_root / "existing.bin"
    input_path.write_bytes(original)
    existing_path.write_bytes(b"preserve-me")

    symlink_input_path = route_root / "input-link.bin"
    special_input_path = route_root / "input-special"
    dangling_target_path = route_root / "must-not-be-created.bin"
    symlink_output_path = route_root / "dangling-output.bin"
    if os.name == "posix":
        symlink_input_path.symlink_to(input_path)
        os.mkfifo(special_input_path)
        symlink_output_path.symlink_to(dangling_target_path)
        dynamic_size_input_path = Path("/proc/self/cmdline")
        if not dynamic_size_input_path.exists():
            dynamic_size_input_path = special_input_path
    else:
        # Symlink creation is not available to an ordinary Windows test user.
        # Directories still prove that non-regular/reparse-like entries are
        # rejected; the POSIX route above carries the explicit symlink proof.
        symlink_input_path.mkdir()
        special_input_path.mkdir()
        symlink_output_path.mkdir()
        dynamic_size_input_path = special_input_path

    dynamic_output_path = route_root / "dynamic.bin"
    empty_input_path = route_root / "empty.bin"
    empty_input_path.write_bytes(b"")
    replace_absent_path = route_root / "replace-absent.bin"
    directory_output_path = route_root / "directory-output"
    directory_output_path.mkdir()
    metadata_replace_path = route_root / "metadata-replace.bin"
    metadata_replace_path.write_bytes(b"old-metadata-file")
    if os.name == "posix":
        metadata_replace_path.chmod(0o666)

    command = interpreter_command(
        args,
        artifact,
        input_path,
        output_path,
        existing_path,
        symlink_input_path,
        special_input_path,
        symlink_output_path,
        dynamic_output_path,
        empty_input_path,
        dynamic_size_input_path,
        replace_absent_path,
        directory_output_path,
        metadata_replace_path,
    )
    executed = subprocess.run(
        command,
        check=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        preexec_fn=set_restrictive_umask if restrictive_umask else None,
    )
    if (
        executed.returncode != 0
        or executed.stdout != EXPECTED_STDOUT
        or executed.stderr
    ):
        raise AssertionError(
            "{} runtime mismatch: exit={} stdout={!r} stderr={!r}".format(
                route,
                executed.returncode,
                executed.stdout,
                executed.stderr,
            )
        )

    replaced = bytearray(original)
    replaced[0] = 42
    require_regular_publication(output_path, route, "replacement publication")
    require_regular_publication(dynamic_output_path, route, "fresh publication")
    require_regular_publication(
        replace_absent_path, route, "replace-absent publication"
    )
    require_regular_publication(
        metadata_replace_path, route, "external-file replacement publication"
    )
    if output_path.read_bytes() != bytes(replaced):
        raise AssertionError("{} atomic replacement bytes differ".format(route))
    if existing_path.read_bytes() != b"preserve-me":
        raise AssertionError("{} no-clobber failure modified destination".format(route))
    if dynamic_output_path.read_bytes() != bytes((0, 128, 255)):
        raise AssertionError("{} dynamic array bytes differ".format(route))
    if replace_absent_path.read_bytes() != b"":
        raise AssertionError("{} replace=true creation bytes differ".format(route))
    if metadata_replace_path.read_bytes() != b"":
        raise AssertionError("{} metadata replacement bytes differ".format(route))
    if not directory_output_path.is_dir():
        raise AssertionError("{} directory destination was replaced".format(route))
    if os.name == "posix":
        if not symlink_output_path.is_symlink() or dangling_target_path.exists():
            raise AssertionError("{} dangling output symlink was modified".format(route))

    race_destination = run_no_clobber_race(
        args, artifact, route_root, route, restrictive_umask
    )
    require_regular_publication(race_destination, route, "race winner")
    if list(route_root.rglob(".ny-write-*")):
        raise AssertionError("{} left a staging file behind".format(route))


def main() -> int:
    args = arguments()
    with tempfile.TemporaryDirectory(prefix="ny-secure-byte-io-") as temporary:
        root = Path(temporary)
        run_route(args, root, "text", binary=False)
        run_route(args, root, "binary", binary=True)
        if os.name == "posix":
            run_route(
                args,
                root,
                "text-umask-0777",
                binary=False,
                restrictive_umask=True,
            )
            run_route(
                args,
                root,
                "binary-umask-0777",
                binary=True,
                restrictive_umask=True,
            )
    print("Ny secure byte-I/O routes passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
