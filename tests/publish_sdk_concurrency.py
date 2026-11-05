#!/usr/bin/env python3
"""Regression coverage for concurrent NIL Framework SDK active-link publication."""

from __future__ import annotations

import argparse
import contextlib
import errno
import hashlib
import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch


VALIDATOR_SOURCE = r'''#!/usr/bin/env python3
import json
import os
import sys
import time
from pathlib import Path

sdk = Path(sys.argv[1])
with (sdk / "manifest.json").open("r", encoding="utf-8") as stream:
    identity = json.load(stream)["identity"]

active = Path(os.environ["NY_PUBLISH_TEST_ACTIVE"])
coordination = Path(os.environ["NY_PUBLISH_TEST_COORDINATION"])
is_active = os.path.abspath(str(sdk)) == os.path.abspath(str(active))
if identity == "destination-race" and not is_active:
    (coordination / "stage-validation").touch()
    deadline = time.monotonic() + 5.0
    while not (coordination / "destination-created").exists():
        if time.monotonic() >= deadline:
            print("destination race was not released", file=sys.stderr)
            raise SystemExit(24)
        time.sleep(0.01)
if is_active:
    if identity == "rollback-race":
        (coordination / "rollback-active").touch()
        deadline = time.monotonic() + 5.0
        while not (coordination / "destination-changed").exists():
            if time.monotonic() >= deadline:
                print("rollback race was not released", file=sys.stderr)
                raise SystemExit(25)
            time.sleep(0.01)
        print("intentional rollback-race failure", file=sys.stderr)
        raise SystemExit(26)
    if identity == "older":
        (coordination / "older-active").touch()
        deadline = time.monotonic() + 5.0
        while not (coordination / "newer-active").exists():
            if time.monotonic() >= deadline:
                break
            time.sleep(0.01)
        print("intentional failure of the older publication", file=sys.stderr)
        raise SystemExit(23)
    if identity == "newer":
        (coordination / "newer-active").touch()
'''


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publisher", required=True, type=Path)
    return parser.parse_args()


def write_payload(path: Path, identity: str) -> str:
    path.mkdir()
    manifest = (json.dumps({"identity": identity}, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    (path / "manifest.json").write_bytes(manifest)
    return hashlib.sha256(manifest).hexdigest()


def wait_for_marker(marker: Path, process: subprocess.Popen[str]) -> None:
    deadline = time.monotonic() + 10.0
    while not marker.exists():
        return_code = process.poll()
        if return_code is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(
                "publisher exited before {} (exit {}):\n{}{}".format(
                    marker.name, return_code, stdout, stderr
                )
            )
        if time.monotonic() >= deadline:
            process.kill()
            stdout, stderr = process.communicate()
            raise AssertionError(
                "publisher did not reach {}:\n{}{}".format(
                    marker.name, stdout, stderr
                )
            )
        time.sleep(0.01)


def run_scenario(publisher: Path, with_previous_target: bool) -> None:
    scenario_name = "restore" if with_previous_target else "unlink"
    with tempfile.TemporaryDirectory(prefix="nil-sdk-publish-{}-".format(scenario_name)) as raw:
        root = Path(raw).resolve(strict=True)
        store = root / "store"
        store.mkdir()
        versions = store / "versions"
        versions.mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"

        if with_previous_target:
            previous = versions / "previous"
            write_payload(previous, "previous")
            active.symlink_to(
                os.path.relpath(previous, active.parent), target_is_directory=True
            )

        older_stage = store / ".stage.older"
        newer_stage = store / ".stage.newer"
        older_digest = write_payload(older_stage, "older")
        newer_digest = write_payload(newer_stage, "newer")

        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8", newline='\n')
        environment = os.environ.copy()
        environment["NY_PUBLISH_TEST_ACTIVE"] = str(active)
        environment["NY_PUBLISH_TEST_COORDINATION"] = str(coordination)

        def command(stage: Path) -> list[str]:
            return [
                sys.executable,
                str(publisher),
                "--stage-dir",
                str(stage),
                "--sdk-dir",
                str(active),
                "--store-dir",
                str(store),
                "--validator",
                str(validator),
            ]

        older = subprocess.Popen(
            command(older_stage),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )
        wait_for_marker(coordination / "older-active", older)

        newer = subprocess.Popen(
            command(newer_stage),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )

        try:
            older_stdout, older_stderr = older.communicate(timeout=12)
            newer_stdout, newer_stderr = newer.communicate(timeout=12)
        except subprocess.TimeoutExpired as exc:
            older.kill()
            newer.kill()
            older.communicate()
            newer.communicate()
            raise AssertionError("concurrent publishers deadlocked") from exc

        if older.returncode == 0:
            raise AssertionError(
                "intentionally invalid older publisher succeeded:\n{}{}".format(
                    older_stdout, older_stderr
                )
            )
        if newer.returncode != 0:
            raise AssertionError(
                "newer publisher failed with exit {}:\n{}{}".format(
                    newer.returncode, newer_stdout, newer_stderr
                )
            )

        expected_generation = versions / newer_digest
        if not active.is_symlink() or active.resolve(strict=True) != expected_generation:
            actual = os.readlink(active) if active.is_symlink() else "<missing>"
            raise AssertionError(
                "{} rollback replaced or removed the newer activation; got {}".format(
                    scenario_name, actual
                )
            )

        for digest, identity in (
            (older_digest, "older"),
            (newer_digest, "newer"),
        ):
            generation = versions / digest
            if generation.is_symlink() or not generation.is_dir():
                raise AssertionError("immutable {} generation is missing".format(identity))
            with (generation / "manifest.json").open(
                "r", encoding="utf-8"
            ) as stream:
                if json.load(stream)["identity"] != identity:
                    raise AssertionError(
                        "immutable {} generation was modified".format(identity)
                    )

        lock_path = store / ".publish.lock"
        if lock_path.is_symlink() or not lock_path.is_file():
            raise AssertionError("persistent publication lock is missing or unsafe")
        leaked_lock = active.parent / ".{}.publish.lock".format(active.name)
        if leaked_lock.exists() or leaked_lock.is_symlink():
            raise AssertionError(
                "publication lock leaked into the SDK destination directory"
            )
        if sys.platform == "darwin":
            retained = active.parent / ("." + active.name + ".retained-links")
            links = list(retained.iterdir())
            assert len(links) == (3 if with_previous_target else 1), links
            assert all(path.is_symlink() for path in links)
            assert os.path.relpath(versions / older_digest, active.parent) in {
                os.readlink(path) for path in links
            }, "failed activation was not retained through rollback"


def publisher_command(
    publisher: Path, stage: Path, active: Path, store: Path, validator: Path
) -> list[str]:
    return [
        sys.executable,
        str(publisher),
        "--stage-dir",
        str(stage),
        "--sdk-dir",
        str(active),
        "--store-dir",
        str(store),
        "--validator",
        str(validator),
    ]


def run_restrictive_umask_scenario(publisher: Path) -> None:
    if os.name == "nt":
        return

    with tempfile.TemporaryDirectory(prefix="nil-sdk-publish-umask-") as raw:
        root = Path(raw).resolve(strict=True)
        store = root / "store"
        store.mkdir()
        (store / "versions").mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"
        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8", newline='\n')
        environment = os.environ.copy()
        environment["NY_PUBLISH_TEST_ACTIVE"] = str(active)
        environment["NY_PUBLISH_TEST_COORDINATION"] = str(coordination)

        first_stage = store / ".stage.first"
        write_payload(first_stage, "first")
        previous_umask = os.umask(0o777)
        try:
            first = subprocess.run(
                publisher_command(
                    publisher, first_stage, active, store, validator
                ),
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
                timeout=12,
            )
        finally:
            os.umask(previous_umask)
        if first.returncode != 0:
            raise AssertionError(
                "publication under restrictive umask failed with exit {}:\n{}{}".format(
                    first.returncode, first.stdout, first.stderr
                )
            )

        lock_path = store / ".publish.lock"
        lock_mode = lock_path.stat().st_mode & 0o777
        if lock_mode != 0o600:
            raise AssertionError(
                "persistent publication lock mode is {:04o}, expected 0600".format(
                    lock_mode
                )
            )

        second_stage = store / ".stage.second"
        write_payload(second_stage, "second")
        second = subprocess.run(
            publisher_command(publisher, second_stage, active, store, validator),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
            timeout=12,
        )
        if second.returncode != 0:
            raise AssertionError(
                "second publication could not reuse the persistent lock (exit {}):\n{}{}".format(
                    second.returncode, second.stdout, second.stderr
                )
            )


def run_destination_replacement_race(publisher: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="nil-sdk-publish-destination-") as raw:
        root = Path(raw).resolve(strict=True)
        store = root / "store"
        store.mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"
        stage = store / ".stage.destination"
        write_payload(stage, "destination-race")
        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8", newline='\n')
        environment = os.environ.copy()
        environment["NY_PUBLISH_TEST_ACTIVE"] = str(active)
        environment["NY_PUBLISH_TEST_COORDINATION"] = str(coordination)

        process = subprocess.Popen(
            publisher_command(publisher, stage, active, store, validator),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )
        wait_for_marker(coordination / "stage-validation", process)
        sentinel = b"user-owned destination\n"
        active.write_bytes(sentinel)
        (coordination / "destination-created").touch()
        try:
            stdout, stderr = process.communicate(timeout=12)
        except subprocess.TimeoutExpired as exc:
            process.kill()
            process.communicate()
            raise AssertionError("destination-race publisher deadlocked") from exc

        if process.returncode == 0:
            raise AssertionError(
                "publisher replaced a destination created during validation:\n{}{}".format(
                    stdout, stderr
                )
            )
        if active.is_symlink() or not active.is_file():
            raise AssertionError("publisher changed the raced destination type")
        if active.read_bytes() != sentinel:
            raise AssertionError("publisher changed the raced destination contents")
        if "refusing to replace a non-symlink SDK path" not in stderr:
            raise AssertionError(
                "publisher failed for the wrong reason during destination race:\n{}{}".format(
                    stdout, stderr
                )
            )


def run_rollback_preserves_changed_destination(publisher: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="nil-sdk-publish-rollback-") as raw:
        root = Path(raw).resolve(strict=True)
        store = root / "store"
        store.mkdir()
        versions = store / "versions"
        versions.mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"
        previous = versions / "previous"
        write_payload(previous, "previous")
        active.symlink_to(
            os.path.relpath(previous, active.parent), target_is_directory=True
        )
        stage = store / ".stage.rollback"
        write_payload(stage, "rollback-race")
        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8", newline='\n')
        environment = os.environ.copy()
        environment["NY_PUBLISH_TEST_ACTIVE"] = str(active)
        environment["NY_PUBLISH_TEST_COORDINATION"] = str(coordination)

        process = subprocess.Popen(
            publisher_command(publisher, stage, active, store, validator),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )
        wait_for_marker(coordination / "rollback-active", process)
        active.unlink()
        sentinel = b"replacement created during validation\n"
        active.write_bytes(sentinel)
        (coordination / "destination-changed").touch()
        try:
            stdout, stderr = process.communicate(timeout=12)
        except subprocess.TimeoutExpired as exc:
            process.kill()
            process.communicate()
            raise AssertionError("rollback-race publisher deadlocked") from exc

        if process.returncode == 0:
            raise AssertionError(
                "intentionally failed rollback publication succeeded:\n{}{}".format(
                    stdout, stderr
                )
            )
        if active.is_symlink() or not active.is_file():
            raise AssertionError("rollback changed the replacement entry type")
        if active.read_bytes() != sentinel:
            raise AssertionError("rollback changed the replacement entry contents")
        if not previous.is_dir():
            raise AssertionError("rollback damaged the previous generation")


def run_atomic_reader_scenario(publisher) -> None:
    """Readers see complete old/new generations, including during replacement."""
    with tempfile.TemporaryDirectory(prefix="nil-sdk-publish-reader-") as raw:
        root = Path(raw).resolve(strict=True) / "space \u03bb"
        root.mkdir()
        old, new = root / "old", root / "new"
        write_payload(old, "old")
        write_payload(new, "new")
        old_bytes, new_bytes = (old / "manifest.json").read_bytes(), (new / "manifest.json").read_bytes()
        expected = {old_bytes, new_bytes}
        active = root / "sdk"
        active.symlink_to("old", target_is_directory=True)
        failures = []
        observations = [0]
        ready, observed_new, stop = threading.Event(), threading.Event(), threading.Event()

        def reader():
            while not stop.is_set():
                try:
                    data = (active / "manifest.json").read_bytes()
                    if data not in expected:
                        raise AssertionError("reader observed an incomplete generation: " + repr(data))
                    observations[0] += 1
                    ready.set()
                    if data == new_bytes:
                        observed_new.set()
                except BaseException:
                    # Retain the failing operation and filename, not just an
                    # errno repr, when a host rejects the concurrent read.
                    failures.append(traceback.format_exc())
                    ready.set()
                    observed_new.set()
                    break

        worker = threading.Thread(target=reader)
        # An already opened referent must continue to identify the old SDK.
        with (active / "manifest.json").open("rb") as held:
            worker.start()
            try:
                assert ready.wait(5), "reader did not start"
                publisher.replace_link(active, "new")
                assert observed_new.wait(5), "reader did not observe the new generation"
                for index in range(250):
                    publisher.replace_link(active, "old" if index % 2 == 0 else "new")
                assert held.read() == old_bytes
            finally:
                stop.set()
                worker.join(10)
        assert not worker.is_alive(), "publication reader did not stop"
        assert not failures, failures
        assert observations[0] >= 2, observations
        assert active.resolve(strict=True) == new
        expected_entries = ["new", "old", "sdk"]
        if sys.platform == "darwin":
            retention = publisher.darwin_retention_directory(active)
            expected_entries.append(retention.name)
            anchors = list(retention.iterdir())
            assert len(anchors) == 251, len(anchors)
            assert all(path.is_symlink() and os.readlink(path) in {"old", "new"} for path in anchors)
            assert stat.S_IMODE(retention.stat().st_mode) == 0o700
        assert sorted(path.name for path in root.iterdir()) == sorted(expected_entries)

        # Simulate a filesystem refusing the atomic primitive. replace_link
        # must neither remove the active link nor leave its temporary behind.
        previous_identity = publisher.symlink_identity(active)
        with patch.object(publisher, "replace_directory_symlink",
                          side_effect=OSError("unsupported atomic replacement")):
            try:
                publisher.replace_link(active, "old")
            except OSError as error:
                assert "unsupported atomic replacement" in str(error), error
            else:
                raise AssertionError("failed atomic replacement unexpectedly succeeded")
        assert publisher.symlink_identity(active) == previous_identity
        assert active.resolve(strict=True) == new
        assert sorted(path.name for path in root.iterdir()) == sorted(expected_entries)

        # A rejected source must also retain the old active link.
        try:
            publisher.replace_directory_symlink(root / "missing", active)
        except OSError:
            pass
        else:
            raise AssertionError("missing replacement source unexpectedly succeeded")
        assert publisher.symlink_identity(active) == previous_identity
        assert (active / "manifest.json").read_bytes() in expected
        if os.name == "nt":
            # Hold only our own replacement link without delete sharing. The
            # real Windows source-open must fail before touching the active
            # entry, and the caller must not emulate replacement by unlinking.
            import ctypes
            from ctypes import wintypes

            api = ctypes.WinDLL("kernel32", use_last_error=True)
            api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
            api.CreateFileW.restype = wintypes.HANDLE
            api.CloseHandle.argtypes = (wintypes.HANDLE,)
            api.CloseHandle.restype = wintypes.BOOL
            locked = root / "locked replacement"
            locked.symlink_to("old", target_is_directory=True)
            handle = api.CreateFileW(str(locked), 0x80000000, 0, None, 3,
                                    0x00200000 | 0x02000000, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                try:
                    publisher.replace_directory_symlink(locked, active)
                except OSError as error:
                    assert error.winerror == 32, error  # ERROR_SHARING_VIOLATION
                else:
                    raise AssertionError("replacement ignored missing delete sharing")
                assert publisher.symlink_identity(active) == previous_identity
                assert locked.is_symlink() and (active / "manifest.json").read_bytes() == new_bytes
            finally:
                api.CloseHandle(handle)
                if locked.is_symlink():
                    locked.unlink()


def run_darwin_retention_controls(publisher) -> None:
    """Real no-follow linkat controls on POSIX; platform dispatch is simulated on Linux."""
    if os.name == "nt":
        return
    import ctypes

    with tempfile.TemporaryDirectory(prefix="nil-sdk-retention-controls-") as raw:
        root = Path(raw).resolve(strict=True)
        (root / "old").mkdir()
        (root / "new").mkdir()
        active = root / "sdk"
        active.symlink_to("old", target_is_directory=True)
        previous = publisher.symlink_identity(active)
        retention = publisher.darwin_retention_directory(active)
        with patch.object(publisher.sys, "platform", "darwin"):
            mask = os.umask(0o777)
            try:
                publisher.replace_link(active, "new")
            finally:
                os.umask(mask)
            assert stat.S_IMODE(retention.stat().st_mode) == 0o700
            anchors = list(retention.iterdir())
            assert len(anchors) == 1 and publisher.symlink_identity(anchors[0]) == previous
            activated = publisher.symlink_identity(active)
            publisher.replace_link(active, "old")  # The rollback replacement path.
            assert {publisher.symlink_identity(path) for path in retention.iterdir()} == {previous, activated}
            assert active.resolve(strict=True) == root / "old"
            assert not list(root.glob(".sdk.link.*.tmp")), "successful replacement did not consume source"

            # A failure after anchoring leaves the active link intact and keeps
            # the anchor: deleting it is not required for a valid failed publish.
            current = publisher.symlink_identity(active)
            before = len(list(retention.iterdir()))
            with patch.object(publisher.os, "replace", side_effect=OSError(errno.EIO, "fixture rename failure")):
                try:
                    publisher.replace_link(active, "new")
                except OSError as error:
                    assert error.errno == errno.EIO
                else:
                    raise AssertionError("failed replacement succeeded")
            assert publisher.symlink_identity(active) == current
            assert len(list(retention.iterdir())) == before + 1
            assert not list(root.glob(".sdk.link.*.tmp"))

        for kind in ("file", "symlink", "public-directory", "unsupported-link", "copied-link", "collision"):
            case = root / kind
            case.mkdir()
            destination, source = case / "sdk", case / "next"
            destination.symlink_to("old", target_is_directory=True)
            source.symlink_to("new", target_is_directory=True)
            old_identity = publisher.symlink_identity(destination)
            new_identity = publisher.symlink_identity(source)
            kept = publisher.darwin_retention_directory(destination)
            if kind == "file":
                kept.write_bytes(b"protected non-directory")
            elif kind == "symlink":
                kept.symlink_to(root, target_is_directory=True)
            else:
                kept.mkdir(mode=0o700)
                if kind == "public-directory":
                    kept.chmod(0o755)

            def unsupported(*unused):
                ctypes.set_errno(errno.ENOTSUP)
                return -1

            def copy_link(parent_fd, name, kept_fd, anchor, flags):
                assert flags == 0
                os.symlink(os.readlink(name, dir_fd=parent_fd), anchor, dir_fd=kept_fd)
                return 0  # Same target but a different inode is not retention.

            with contextlib.ExitStack() as stack:
                stack.enter_context(patch.object(publisher.sys, "platform", "darwin"))
                if kind in {"unsupported-link", "copied-link"}:
                    operation = Mock(side_effect=unsupported if kind == "unsupported-link" else copy_link)
                    stack.enter_context(patch.object(ctypes, "CDLL", return_value=SimpleNamespace(linkat=operation)))
                if kind == "collision":
                    (kept / "existing").write_bytes(b"exclusive anchor sentinel")
                    stack.enter_context(patch.object(publisher.uuid, "uuid4", return_value=SimpleNamespace(hex="existing")))
                try:
                    publisher.replace_directory_symlink(source, destination)
                except OSError:
                    pass
                else:
                    raise AssertionError("unsafe retention accepted: " + kind)
            assert publisher.symlink_identity(destination) == old_identity, kind
            assert publisher.symlink_identity(source) == new_identity, kind
            if kind == "collision":
                assert (kept / "existing").read_bytes() == b"exclusive anchor sentinel"
            elif kind == "file":
                assert kept.read_bytes() == b"protected non-directory"
            elif kind == "symlink":
                assert kept.is_symlink() and kept.resolve(strict=True) == root
    print("Darwin retained-link controls passed (native POSIX primitives; " +
          ("native Darwin" if sys.platform == "darwin" else "Darwin dispatch simulated") + ")")


def main() -> int:
    args = arguments()
    publisher = args.publisher.resolve(strict=True)

    specification = importlib.util.spec_from_file_location("sdk_publisher", publisher)
    assert specification is not None and specification.loader is not None
    implementation = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(implementation)

    # Symlink creation can be unavailable under local filesystem/privilege
    # policy. Once links are available, the publisher's actual atomic primitive
    # must work: an implementation failure is not a reason to skip this test.
    with tempfile.TemporaryDirectory(prefix="nil-sdk-symlink-probe-") as raw:
        root = Path(raw).resolve(strict=True)
        target = root / "target"
        target.mkdir()
        link = root / "link"
        replacement = root / "replacement"
        try:
            link.symlink_to(target, target_is_directory=True)
            replacement.symlink_to(target, target_is_directory=True)
        except OSError as exc:
            print("SDK publication concurrency test skipped: {}".format(exc))
            return 77
        implementation.replace_directory_symlink(replacement, link)

    run_darwin_retention_controls(implementation)
    run_atomic_reader_scenario(implementation)
    run_scenario(publisher, with_previous_target=False)
    run_scenario(publisher, with_previous_target=True)
    run_restrictive_umask_scenario(publisher)
    run_destination_replacement_race(publisher)
    run_rollback_preserves_changed_destination(publisher)
    print("SDK publication concurrency regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
