#!/usr/bin/env python3
"""Build an isolated, pinned PostgreSQL provider; never start a service or cluster."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import stat
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[2]
# SDK support modules use sibling imports; bind them to this exact checkout
# rather than relying on the caller's working directory or PYTHONPATH.
sys.path.insert(0, str(ROOT / "scripts"))
VERSION = "16.15"
VERSION_NUMBER = 160015
ARCHIVE_NAME = f"postgresql-{VERSION}.tar.bz2"
SOURCE_URL = f"https://ftp.postgresql.org/pub/source/v{VERSION}/{ARCHIVE_NAME}"
# Official release, independently checked against Homebrew's postgresql@16
# source declaration and the downloaded release bytes (25,181,309 bytes).
SOURCE_SHA256 = "c1575341fa7bd40f5274ea465b34390f4dc64cdd0770af327005caaeb9f6b7ed"
MiB = 1024**2
MAX_ARCHIVE = 64 * MiB
MAX_EXPANDED = 256 * MiB
MAX_MEMBER = 16 * MiB
MAX_ENTRIES = 20000
WHOLE_SECONDS = 720
CLEANUP_RESERVE = 20
INPUTS = ("scripts/ci/macos-postgresql-provider.py", "scripts/sdk_qualification.py",
          "scripts/check-sdk-routes.py", "tests/tools/ordinary_io_parity_contract.py",
          "validation/ny-3d/scripts/debugger_process.py")

PREFLIGHT = r'''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pg_config.h>
#include <libpq-fe.h>
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  void *library = dlopen("libpq.dylib", RTLD_NOW | RTLD_LOCAL);
  if (!library) { std::fprintf(stderr, "libpq loader: %s\n", dlerror()); return 3; }
  auto version = reinterpret_cast<decltype(&PQlibVersion)>(dlsym(library, "PQlibVersion"));
  Dl_info identity{};
  if (!version || !dladdr(reinterpret_cast<const void *>(version), &identity)) return 4;
  char *actual = realpath(identity.dli_fname, nullptr);
  char *expected = realpath(argv[1], nullptr);
  const int runtime = version();
  const bool matched = actual && expected && std::strcmp(actual, expected) == 0;
  std::printf("header_version=%d\nruntime_version=%d\nruntime_path=%s\n",
              PG_VERSION_NUM, runtime, actual ? actual : "");
  std::free(actual); std::free(expected);
  if (dlclose(library) != 0) return 5;
  return matched && PG_VERSION_NUM == 160015 && runtime == PG_VERSION_NUM ? 0 : 6;
}
'''


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def record(path, maximum=512 * MiB):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(descriptor, "rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= maximum,
                "input is not a bounded regular file: " + str(path))
        digest = hashlib.sha256()
        total = 0
        for block in iter(lambda: stream.read(MiB), b""):
            total += len(block)
            require(total <= maximum, "input grew beyond bound")
            digest.update(block)
        after = os.fstat(stream.fileno())
        require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) and total == after.st_size,
                "input changed while hashing")
    return {"bytes": total, "sha256": digest.hexdigest()}


def save(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def remaining_timeout(deadline, limit, now=None):
    left = deadline - (time.monotonic() if now is None else now)
    require(left > 0, "whole provider work deadline exceeded")
    return min(limit, left)


def inspect_members(members):
    names, total = set(), 0
    require(0 < len(members) <= MAX_ENTRIES, "source archive entry bound exceeded")
    prefix = f"postgresql-{VERSION}"
    for member in members:
        name = PurePosixPath(member.name)
        require(not name.is_absolute() and name.parts and name.parts[0] == prefix and
                ".." not in name.parts and "\\" not in member.name and
                not any(ord(c) < 32 for c in member.name), "unsafe source archive path")
        normalized = name.as_posix()
        require(normalized not in names, "duplicate source archive entry")
        names.add(normalized)
        require(member.isdir() or member.isfile(), "source archive contains links or special files")
        require(not member.mode & 0o7000, "source archive contains special permission bits")
        require(type(member.mtime) in (int, float) and math.isfinite(member.mtime) and
                0 <= member.mtime <= 4102444800, "source archive timestamp is outside the supported range")
        require(0 <= member.size <= MAX_MEMBER and (member.isfile() or member.size == 0),
                "source archive member size exceeds bound")
        total += member.size
        require(total <= MAX_EXPANDED, "source archive expanded size exceeds bound")
    require(prefix in names, "source archive root missing")
    return {"entries": len(members), "expanded_bytes": total}


def extract_source(archive, destination, deadline, expected=SOURCE_SHA256):
    require(record(archive, MAX_ARCHIVE)["sha256"] == expected, "release archive digest differs")
    require(destination.is_dir() and not destination.is_symlink() and not any(destination.iterdir()),
            "extraction requires a fresh empty directory")
    with tarfile.open(archive, "r:bz2") as source:
        # Iteration stops at the entry limit, including before allocating an
        # unbounded getmembers() list. Only the pinned release is admitted.
        members = []
        for member in source:
            remaining_timeout(deadline, 1)
            members.append(member)
            require(len(members) <= MAX_ENTRIES, "source archive entry bound exceeded")
        inventory = inspect_members(members)
        for member in members:
            remaining_timeout(deadline, 1)
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True, mode=0o755)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(member) as incoming, target.open("xb") as outgoing:
                copied = 0
                while block := incoming.read(MiB):
                    remaining_timeout(deadline, 1)
                    copied += len(block)
                    require(copied <= member.size, "archive member exceeds declared size")
                    outgoing.write(block)
                require(copied == member.size, "truncated source archive member")
            target.chmod(0o755 if member.mode & 0o111 else 0o644)
            # Release-generated gram.c/preproc.c must retain their upstream
            # freshness relative to grammar inputs; do not require a runner's
            # incidental Bison/Flex to regenerate the pinned release sources.
            os.utime(target, (member.mtime, member.mtime), follow_symlinks=False)
        for member in reversed(members):
            if member.isdir():
                target = destination.joinpath(*PurePosixPath(member.name).parts)
                os.utime(target, (member.mtime, member.mtime), follow_symlinks=False)
    return inventory


def tree_seal(root, deadline, *, aliases=False):
    result, total = {}, 0
    for path in sorted(root.rglob("*")):
        remaining_timeout(deadline, 1)
        require(len(result) < MAX_ENTRIES, "tree entry bound exceeded")
        mode = path.lstat().st_mode
        name = path.relative_to(root).as_posix()
        if stat.S_ISDIR(mode):
            result[name] = {"directory": True}
        elif stat.S_ISLNK(mode) and aliases:
            target = path.resolve(strict=True)
            require(root in target.parents and target.is_file(), "provider alias leaves private prefix")
            result[name] = {"link": os.readlink(path), "target": target.relative_to(root).as_posix()}
        else:
            require(stat.S_ISREG(mode), "tree contains unexpected link or special file")
            result[name] = {**record(path), "mode": stat.S_IMODE(mode), "mtime_ns": path.stat().st_mtime_ns}
            total += result[name]["bytes"]
            require(total <= 2 * 1024**3, "tree byte bound exceeded")
    require(bool(result), "empty tree seal")
    return result


def provider_environment(temporary, library_dir=None, inherited=None):
    inherited = os.environ if inherited is None else inherited
    # Configure accepts many executable and flag overrides. A small explicit
    # profile also excludes proxy/curl, shell-startup and make option injection.
    env = {key: inherited[key] for key in ("HOME", "USER", "LOGNAME", "__CF_USER_TEXT_ENCODING")
           if key in inherited}
    env.update(PATH="/usr/bin:/bin:/usr/sbin:/sbin", LC_ALL="C", LANG="C", TZ="UTC",
               TMPDIR=str(temporary), PYTHONDONTWRITEBYTECODE="1", ZERO_AR_DATE="1", CONFIG_SITE="/dev/null")
    if library_dir is not None:
        env["DYLD_FALLBACK_LIBRARY_PATH"] = str(library_dir) + ":/usr/lib"
    return env


def parse_layout(raw, prefix):
    lines = raw.decode("utf-8").splitlines()
    require(len(lines) == 5 and lines[0] == "PostgreSQL " + VERSION, "pg_config version or record count differs")
    require(all(Path(value).is_absolute() for value in lines[1:]), "pg_config paths must be absolute")
    result = {key: str(Path(value).resolve(strict=True)) for key, value in
              zip(("include_dir", "library_dir", "bin_dir", "share_dir"), lines[1:])}
    for value in result.values():
        path = Path(value)
        require(path.is_dir() and prefix in path.parents, "pg_config path leaves private prefix")
    for key, relative in (("include_dir", "libpq-fe.h"), ("include_dir", "pg_config.h"),
                          ("library_dir", "libpq.dylib"), ("share_dir", "postgres.bki")):
        path = Path(result[key]) / relative
        target = path.resolve(strict=True)
        require(prefix in target.parents and target.is_file(), "required provider input leaves private prefix")
    for name in ("pg_config", "postgres", "initdb", "pg_ctl", "psql"):
        target = (Path(result["bin_dir"]) / name).resolve(strict=True)
        require(prefix in target.parents and target.is_file() and os.access(target, os.X_OK),
                "required provider executable missing")
    return result


def parse_preflight(raw, expected):
    lines = raw.decode("utf-8").splitlines()
    require(len(lines) == 3 and lines[0] == f"header_version={VERSION_NUMBER}" and
            lines[1] == f"runtime_version={VERSION_NUMBER}" and
            lines[2] == "runtime_path=" + str(expected), "header/runtime version or loader identity differs")
    return {"header_version": VERSION_NUMBER, "version": VERSION_NUMBER, "path": str(expected)}


def fresh_roots(output, work):
    output = output.parent.resolve(strict=True) / output.name
    work = work.parent.resolve(strict=True) / work.name
    for path in (output, work):
        require(path.name not in ("", ".", "..") and not any(ord(c) < 32 for c in str(path)),
                "output/work path contains an invalid component")
        require(not path.exists() and not path.is_symlink(), "output/work directory must be fresh")
    require(output != work and output not in work.parents and work not in output.parents,
            "evidence and provider work trees must be disjoint")
    output.mkdir(mode=0o700, exist_ok=False)
    work.mkdir(mode=0o700, exist_ok=False)
    return output, work


def validate_provider(report):
    """Revalidate the unchanged prerequisite before/after installed consumers."""
    require(report.get("schema") == "nil.macos.postgresql-provider.v1" and
            report.get("status") == "PASS" and report.get("version") == VERSION and
            report.get("qualification") is False and report.get("architecture") in ("arm64", "x86_64"),
            "provider report is not the supported successful prerequisite")
    for key in ("source_unchanged", "release_source_unchanged", "provider_unchanged",
                "archive_unchanged", "cleanup_verified"):
        require(report.get(key) is True, "provider evidence lacks " + key)
    work, prefix = Path(report["work_dir"]), Path(report["prefix"])
    require(work.is_absolute() and work.resolve(strict=True) == work and
            prefix == work / "prefix" and prefix.resolve(strict=True) == prefix and prefix.is_dir(),
            "provider root identity differs")
    info = work.stat()
    require(stat.S_IMODE(info.st_mode) == 0o700 and info.st_uid == os.getuid(), "provider work directory ownership differs")
    raw_layout = ("PostgreSQL " + VERSION + "\n" + "\n".join(report[key] for key in
                  ("include_dir", "library_dir", "bin_dir", "share_dir")) + "\n").encode()
    require(parse_layout(raw_layout, prefix) == {key: report[key] for key in
            ("include_dir", "library_dir", "bin_dir", "share_dir")}, "provider path spelling changed")
    manifest = report["provider_manifest"]
    manifest_path = Path(manifest["path"])
    require(manifest_path.is_absolute() and record(manifest_path, 16 * MiB) ==
            {key: manifest[key] for key in ("bytes", "sha256")}, "provider manifest digest differs")
    expected = json.loads(manifest_path.read_bytes())
    require(isinstance(expected, dict) and 0 < len(expected) <= MAX_ENTRIES, "provider manifest inventory invalid")
    require(tree_seal(prefix, time.monotonic() + 30, aliases=True) == expected,
            "installed provider differs from sealed manifest")
    runtime = report["runtime"]
    declared = Path(report["library_dir"]) / "libpq.dylib"
    target = declared.resolve(strict=True)
    require(runtime["declared_path"] == str(declared) and runtime["path"] == str(target) and
            prefix in target.parents and type(runtime["header_version"]) is int and type(runtime["version"]) is int and
            runtime["header_version"] == runtime["version"] == VERSION_NUMBER and
            record(target) == {key: runtime[key] for key in ("bytes", "sha256")},
            "provider runtime/header identity differs")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path,
                        help="fresh retained private build/prefix directory outside uploaded evidence")
    args = parser.parse_args()
    require(sys.platform == "darwin" and platform.machine() in ("arm64", "x86_64"),
            "native macOS arm64 or x86_64 required")
    output, work = fresh_roots(args.output, args.work_dir)
    started = time.monotonic()
    deadline = started + WHOLE_SECONDS - CLEANUP_RESERVE
    logs = output / "logs"
    temporary, unpacked, build, prefix = [work / name for name in ("tmp", "source", "build", "prefix")]
    for path in (logs, temporary, unpacked, build):
        path.mkdir(mode=0o700)
    support = ownership = qualification = None
    pins = {}
    phases = []
    report = {"schema": "nil.macos.postgresql-provider.v1", "status": "FAIL", "qualification": False,
              "scope": "Private build prerequisite only; no SDK qualification, cluster or service",
              "version": VERSION, "architecture": platform.machine(), "prefix": str(prefix),
              "work_dir": str(work), "work_directory_retained": True,
              "source_before": pins, "phases": phases, "problems": [], "phase": "initialize",
              "release": {"url": SOURCE_URL, "sha256": SOURCE_SHA256},
              "limits": {"whole_seconds": WHOLE_SECONDS, "cleanup_reserve_seconds": CLEANUP_RESERVE,
                         "parallel_jobs": 2, "output_bytes_per_phase": MiB,
                         "archive_bytes": MAX_ARCHIVE, "expanded_bytes": MAX_EXPANDED},
              "profile": "C locale, private Unix socket; ICU/readline/zlib/TLS/GSS/LDAP/XML/LLVM/compression extras disabled"}
    env = provider_environment(temporary)

    def run(name, command, limit=30, *, cwd=work, variables=env, expected_exit=0):
        report["phase"] = name
        row = support.run_process(ownership, [str(x) for x in command], cwd, variables, logs,
                                  name, remaining_timeout(deadline, limit))
        row["expected_exit_code"] = expected_exit
        row["expected_outcome_satisfied"] = bool(row["cleanup_verified"] and not row.get("error") and
                                                 row.get("exit_code") == expected_exit)
        phases.append(row)
        save(logs / (name + ".json"), row)
        require(row["expected_outcome_satisfied"],
                name + " failed; raw phase evidence retained")
        return (logs / row["stdout"]).read_bytes()

    try:
        pins.update({name: record(ROOT / name) for name in INPUTS})
        support = load("postgres_provider_capture", "tests/tools/ordinary_io_parity_contract.py")
        require(support.MAX_OUTPUT == MiB, "owned capture budget differs from declared bound")
        ownership = support.load_ownership(ROOT)
        qualification = load("postgres_provider_source", "scripts/sdk_qualification.py")
        report["repository"] = qualification.source_identity(ROOT)
        require(not report["repository"]["dirty"], "provider requires a clean source revision")
        if os.environ.get("GITHUB_SHA"):
            require(report["repository"]["revision"] == os.environ["GITHUB_SHA"], "dispatched source differs")
        report["toolchain"] = {
            "compiler": run("compiler-version", ["/usr/bin/clang++", "--version"]).decode().strip(),
            "sdk": run("apple-sdk", ["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path"]).decode().strip(),
            "make": run("make-version", ["/usr/bin/make", "--version"]).decode().strip(),
        }
        archive = work / ARCHIVE_NAME
        run("fetch-release", ["/usr/bin/curl", "--disable", "--fail", "--location", "--silent", "--show-error",
                              "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "20",
                              "--max-time", "90", "--max-filesize", str(MAX_ARCHIVE),
                              "--output", archive, SOURCE_URL], 100)
        report["release"]["archive"] = record(archive, MAX_ARCHIVE)
        report["release"]["extraction"] = extract_source(archive, unpacked, deadline)
        source = unpacked / f"postgresql-{VERSION}"
        original_source = tree_seal(source, deadline)
        save(output / "release-source-before.json", original_source)
        # Release tarballs include generated parser sources: no package-manager
        # generators, updates or network access are needed for this build.
        configure = [source / "configure", "--prefix=" + str(prefix),
                     "--without-icu", "--without-readline", "--without-zlib", "--without-ssl",
                     "--without-gssapi", "--without-ldap", "--without-libxml", "--without-libxslt",
                     "--without-llvm", "--without-lz4", "--without-zstd",
                     "CC=/usr/bin/clang", "CXX=/usr/bin/clang++", "CFLAGS=-O2",
                     "PG_SYSROOT=" + report["toolchain"]["sdk"]]
        report["configure_argv"] = [str(x) for x in configure]
        run("configure", configure, 90, cwd=build)
        run("build", ["/usr/bin/make", "--silent", "--jobs=2"], 420, cwd=build)
        run("install", ["/usr/bin/make", "--silent", "--jobs=2", "install"], 90, cwd=build)
        raw = run("pg-config", [prefix / "bin/pg_config", "--version", "--includedir", "--libdir", "--bindir", "--sharedir"])
        report.update(parse_layout(raw, prefix))
        run("pg-config-build", [prefix / "bin/pg_config", "--configure", "--cc", "--cflags", "--ldflags", "--libs"])
        runtime = (Path(report["library_dir"]) / "libpq.dylib").resolve(strict=True)
        runtime_record = record(runtime)
        installed_before = tree_seal(prefix, deadline, aliases=True)
        save(output / "provider-before.json", installed_before)
        probe_source, probe = output / "loader-preflight.cpp", work / "loader-preflight"
        probe_source.write_text(PREFLIGHT, encoding="utf-8")
        run("compile-preflight", ["/usr/bin/clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                                  "-isysroot", report["toolchain"]["sdk"], "-I", report["include_dir"],
                                  probe_source, "-o", probe], 30)
        provider_env = provider_environment(temporary, Path(report["library_dir"]))
        raw = run("loader-preflight", [probe, runtime], variables=provider_env)
        report["runtime"] = {**parse_preflight(raw, runtime), **runtime_record,
                             "declared_path": str(Path(report["library_dir"]) / "libpq.dylib")}
        report["loader_preflight_binary"] = {"path": str(probe), **record(probe)}
        # Exact version cannot excuse a different loaded library identity.
        run("loader-wrong-identity", [probe, prefix / "bin/postgres"], variables=provider_env, expected_exit=6)
        run("runtime-dependencies", ["/usr/bin/otool", "-L", runtime])
        run("server-dependencies", ["/usr/bin/otool", "-L", prefix / "bin/postgres"])
        report["loader_environment"] = {"DYLD_FALLBACK_LIBRARY_PATH": provider_env["DYLD_FALLBACK_LIBRARY_PATH"]}
        final_source = tree_seal(source, deadline)
        save(output / "release-source-after.json", final_source)
        report["release_source_unchanged"] = final_source == original_source
        installed_after = tree_seal(prefix, deadline, aliases=True)
        save(output / "provider-after.json", installed_after)
        report["provider_manifest"] = {"path": str(output / "provider-after.json"), **record(output / "provider-after.json")}
        report["provider_unchanged"] = installed_before == installed_after and runtime_record == record(runtime)
        report["archive_unchanged"] = record(archive, MAX_ARCHIVE) == report["release"]["archive"]
        require(report["release_source_unchanged"] and report["provider_unchanged"] and report["archive_unchanged"],
                "release or provider changed during verification")
        report["phase"] = "complete"
        report["status"] = "PASS"
    except Exception as error:
        report["problems"].append(type(error).__name__ + ": " + str(error))
    finally:
        for name in ("config.log", "config.status"):
            path = build / name
            if path.exists() or path.is_symlink():
                try:
                    before = record(path, 8 * MiB)
                    data = path.read_bytes()
                    require(len(data) == before["bytes"] and hashlib.sha256(data).hexdigest() == before["sha256"],
                            "configuration evidence changed")
                    with (output / name).open("xb") as stream:
                        stream.write(data)
                except Exception as error:
                    report["problems"].append("configuration evidence: " + str(error))
        try:
            report["source_after"] = {name: record(ROOT / name) for name in INPUTS}
            report["source_unchanged"] = pins == report["source_after"]
            require(qualification is not None, "source identity dependency initialization failed")
            report["repository_after"] = qualification.source_identity(ROOT)
            require(report["repository_after"] == report.get("repository"), "repository identity changed")
        except Exception as error:
            report["problems"].append("source seal: " + str(error))
            report["source_unchanged"] = False
        report["cleanup_verified"] = bool(phases) and all(row["cleanup_verified"] for row in phases)
        report["elapsed_seconds"] = time.monotonic() - started
        if (not report["source_unchanged"] or not report["cleanup_verified"] or report["problems"] or
                report["elapsed_seconds"] > WHOLE_SECONDS):
            report["status"] = "FAIL"
        save(output / "report.json", report)
    print(json.dumps({key: report[key] for key in ("status", "version", "prefix", "problems", "cleanup_verified")}))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
