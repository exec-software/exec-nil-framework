#!/usr/bin/env python3
"""Publish a validated SDK generation behind an atomically replaced symlink."""

from __future__ import annotations

import argparse
import contextlib
import errno
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-dir", required=True, type=Path)
    parser.add_argument("--sdk-dir", required=True, type=Path)
    parser.add_argument("--store-dir", required=True, type=Path)
    parser.add_argument("--validator", required=True, type=Path)
    return parser.parse_args()


def exists_or_link(path: Path) -> bool:
    return path.exists() or path.is_symlink()


def is_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def validate_sdk(validator: Path, sdk: Path, label: str) -> None:
    try:
        process = subprocess.run(
            [sys.executable, str(validator), str(sdk)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=180,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SystemExit("{} validator could not run: {}".format(label, exc)) from exc
    if process.returncode != 0:
        output = (process.stdout + process.stderr).strip()
        raise SystemExit("{} failed validation:\n{}".format(label, output))


def manifest_digest(sdk: Path) -> str:
    digest = hashlib.sha256()
    with (sdk / "manifest.json").open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def symlink_identity(path: Path):
    """Return enough identity to avoid rolling back over a changed entry."""
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        return None
    if not stat.S_ISLNK(metadata.st_mode):
        return None
    return (metadata.st_dev, metadata.st_ino, os.readlink(path))


def validate_generation(
    validator: Path, generation: Path, expected_digest: str, label: str
) -> None:
    validate_sdk(validator, generation, label)
    actual_digest = manifest_digest(generation)
    if actual_digest != expected_digest:
        raise SystemExit(
            "{} manifest digest is {}, expected {}".format(
                label, actual_digest, expected_digest
            )
        )


def replace_link(destination: Path, target: str) -> None:
    temporary_link = destination.parent / ".{}.link.{}.tmp".format(
        destination.name, os.getpid()
    )
    if exists_or_link(temporary_link):
        if not temporary_link.is_symlink():
            raise SystemExit(
                "refusing to remove a non-symlink temporary publication path: {}".format(
                    temporary_link
                )
            )
        temporary_link.unlink()
    try:
        temporary_link.symlink_to(target, target_is_directory=True)
        os.replace(temporary_link, destination)
    finally:
        if temporary_link.is_symlink():
            temporary_link.unlink()


@contextlib.contextmanager
def publication_lock(lock_path: Path):
    """Serialize activation, validation, and rollback for one SDK link.

    The lock file is intentionally persistent. Removing it after unlocking
    would introduce an inode-replacement race in which two publishers could
    each hold a lock on a different file with the same pathname.
    """

    if exists_or_link(lock_path) and (
        lock_path.is_symlink() or not lock_path.is_file()
    ):
        raise SystemExit(
            "SDK publication lock is not a regular file: {}".format(lock_path)
        )

    flags = os.O_RDWR | os.O_CREAT
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW

    # This is a single-threaded command-line publisher. On POSIX, temporarily
    # clear the process umask so a newly created persistent lock is born 0600;
    # repairing it only after open would leave a crash and concurrency window
    # in which the lock could remain unreadable.
    previous_umask = None
    try:
        if os.name != "nt":
            previous_umask = os.umask(0)
        descriptor = os.open(lock_path, flags, 0o600)
    except OSError as exc:
        raise SystemExit(
            "could not open SDK publication lock {}: {}".format(lock_path, exc)
        ) from exc
    finally:
        if previous_umask is not None:
            os.umask(previous_umask)

    locked = False
    try:
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise SystemExit(
                    "SDK publication lock is not a regular file: {}".format(
                        lock_path
                    )
                )

            # The lock is persistent, so its creation mode must not inherit a
            # caller umask that makes a later publisher unable to reopen it.
            # Operate on the already verified descriptor to avoid a pathname
            # substitution race.
            if os.name != "nt":
                if not hasattr(os, "fchmod"):
                    raise SystemExit(
                        "cannot establish publication lock permissions on this platform"
                    )
                try:
                    os.fchmod(descriptor, 0o600)
                except OSError as exc:
                    raise SystemExit(
                        "could not secure SDK publication lock {}: {}".format(
                            lock_path, exc
                        )
                    ) from exc
                lock_mode = stat.S_IMODE(os.fstat(descriptor).st_mode)
                if lock_mode != 0o600:
                    raise SystemExit(
                        "SDK publication lock mode is {:04o}, expected 0600: {}".format(
                            lock_mode, lock_path
                        )
                    )

            if os.name == "nt":
                import msvcrt

                # msvcrt locks a byte range rather than a whole file. Keep
                # byte zero allocated and retry the non-blocking operation so
                # the wait is not limited to LK_LOCK's retry count.
                if os.fstat(descriptor).st_size == 0:
                    os.write(descriptor, b"\0")
                while True:
                    os.lseek(descriptor, 0, os.SEEK_SET)
                    try:
                        msvcrt.locking(descriptor, msvcrt.LK_NBLCK, 1)
                        locked = True
                        break
                    except OSError as exc:
                        if exc.errno not in (
                            errno.EACCES,
                            errno.EAGAIN,
                            errno.EDEADLK,
                        ):
                            raise
                        time.sleep(0.05)
            else:
                import fcntl

                fcntl.flock(descriptor, fcntl.LOCK_EX)
                locked = True
        except OSError as exc:
            raise SystemExit(
                "could not lock SDK publication path {}: {}".format(
                    lock_path, exc
                )
            ) from exc

        try:
            yield
        finally:
            if locked:
                if os.name == "nt":
                    import msvcrt

                    os.lseek(descriptor, 0, os.SEEK_SET)
                    msvcrt.locking(descriptor, msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl

                    fcntl.flock(descriptor, fcntl.LOCK_UN)
    finally:
        os.close(descriptor)


def main() -> int:
    args = arguments()
    stage_argument = args.stage_dir.absolute()
    if stage_argument.is_symlink():
        raise SystemExit("the SDK stage itself must not be a symlink")
    stage = stage_argument.resolve(strict=True)
    store = args.store_dir.resolve(strict=True)
    validator = args.validator.resolve(strict=True)
    destination_parent = args.sdk_dir.absolute().parent.resolve(strict=True)
    destination = destination_parent / args.sdk_dir.name

    if not stage.is_dir() or not store.is_dir():
        raise SystemExit("SDK stage and generation store must both be directories")
    if not validator.is_file():
        raise SystemExit("SDK validator is not a regular file: {}".format(validator))
    if stage.parent != store or not stage.name.startswith(".stage."):
        raise SystemExit("SDK stage must be a direct .stage.* child of the generation store")
    if store == Path(store.anchor) or destination == Path(destination.anchor):
        raise SystemExit("refusing an unsafe SDK store or destination")
    if is_within(destination, store) or is_within(store, destination):
        raise SystemExit("SDK destination and generation store must not contain one another")
    if is_within(validator, stage) or is_within(validator, store):
        raise SystemExit("SDK validator must live outside the staged payload and generation store")
    if exists_or_link(destination) and not destination.is_symlink():
        raise SystemExit(
            "refusing to replace a non-symlink SDK path: {}\n"
            "Move the legacy directory aside once, then rerun the SDK builder.".format(
                destination
            )
        )

    manifest = stage / "manifest.json"
    if not manifest.is_file() or manifest.is_symlink():
        raise SystemExit("validated SDK stage or regular manifest is missing")
    validate_sdk(validator, stage, "staged SDK")

    digest = manifest_digest(stage)
    generations = store / "versions"
    if exists_or_link(generations) and (
        generations.is_symlink() or not generations.is_dir()
    ):
        raise SystemExit("SDK versions path is not a directory: {}".format(generations))
    generations.mkdir(parents=True, exist_ok=True)
    generation = generations / digest
    reused_generation = False

    if exists_or_link(generation):
        if generation.is_symlink() or not generation.is_dir():
            raise SystemExit("SDK generation path is not a regular directory: {}".format(generation))
        validate_generation(
            validator, generation, digest, "existing SDK generation"
        )
        reused_generation = True
    else:
        try:
            os.replace(stage, generation)
        except OSError:
            # Another publisher may have installed this immutable generation
            # after our existence check. Only reuse it after full validation.
            if generation.is_symlink() or not generation.is_dir():
                raise
            validate_generation(
                validator,
                generation,
                digest,
                "concurrently published SDK generation",
            )
            reused_generation = True
        else:
            validate_generation(validator, generation, digest, "new SDK generation")

    relative_target = os.path.relpath(generation, destination.parent)
    with publication_lock(store / ".publish.lock"):
        # Catch a path that appeared after the initial preflight check while
        # the staged SDK was being validated. The store lock serializes NIL Framework SDK
        # publishers; the destination parent remains a trusted, user-owned
        # directory rather than an adversarial same-user mutation boundary.
        if exists_or_link(destination) and not destination.is_symlink():
            raise SystemExit(
                "refusing to replace a non-symlink SDK path: {}\n"
                "Move the legacy directory aside once, then rerun the SDK builder.".format(
                    destination
                )
            )
        # Read the rollback target only after taking the same lock used by all
        # publishers. Keep the lock through validation and rollback: an older
        # failed publisher must never unlink or restore over a newer activation.
        previous_target = (
            os.readlink(destination) if destination.is_symlink() else None
        )
        activated_identity = None
        replace_link(destination, relative_target)
        try:
            activated_identity = symlink_identity(destination)
            if (
                activated_identity is None
                or destination.resolve(strict=True) != generation
            ):
                raise SystemExit(
                    "published SDK link does not resolve to the selected generation"
                )
            validate_sdk(validator, destination, "published SDK")
        except BaseException:
            # Do not compensate over an entry changed by anything outside this
            # publication transaction during validation.
            if (
                activated_identity is not None
                and symlink_identity(destination) == activated_identity
            ):
                if previous_target is None:
                    destination.unlink()
                else:
                    replace_link(destination, previous_target)
            raise

    if reused_generation:
        shutil.rmtree(stage)
    print("published NIL Framework SDK generation {}".format(digest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
