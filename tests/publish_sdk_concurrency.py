#!/usr/bin/env python3
"""Regression coverage for concurrent NIL Framework SDK active-link publication."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


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
        root = Path(raw)
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
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8")
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
        root = Path(raw)
        store = root / "store"
        store.mkdir()
        (store / "versions").mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"
        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8")
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
        root = Path(raw)
        store = root / "store"
        store.mkdir()
        coordination = root / "coordination"
        coordination.mkdir()
        active = root / "sdk"
        stage = store / ".stage.destination"
        write_payload(stage, "destination-race")
        validator = root / "validator.py"
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8")
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
        root = Path(raw)
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
        validator.write_text(VALIDATOR_SOURCE, encoding="utf-8")
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


def main() -> int:
    args = arguments()
    publisher = args.publisher.resolve(strict=True)

    # SDK publication itself requires directory symlinks. Report a standard
    # CTest skip on platforms or policies that do not permit creating them.
    with tempfile.TemporaryDirectory(prefix="nil-sdk-symlink-probe-") as raw:
        root = Path(raw)
        target = root / "target"
        target.mkdir()
        link = root / "link"
        try:
            link.symlink_to(target, target_is_directory=True)
        except OSError as exc:
            print("SDK publication concurrency test skipped: {}".format(exc))
            return 77

    run_scenario(publisher, with_previous_target=False)
    run_scenario(publisher, with_previous_target=True)
    run_restrictive_umask_scenario(publisher)
    run_destination_replacement_race(publisher)
    run_rollback_preserves_changed_destination(publisher)
    print("SDK publication concurrency regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
