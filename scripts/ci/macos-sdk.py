#!/usr/bin/env python3
"""Stage isolated native macOS development SDK evidence; never activate or release."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/ci"
OUTPUT = ROOT / "build/macos-sdk-evidence"
STAGE = ROOT / "build/macos-sdk-stage"
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_qualification import TOOLS, source_identity, summarize_ctest

# Official jgm/pandoc release 3.13 assets and SHA256 digests, verified using
# https://api.github.com/repos/jgm/pandoc/releases/tags/3.13 . No package upgrades.
PANDOC_VERSION = "3.13"
# The official ARM64 3.13 executable is 195,370,176 bytes (the x86_64
# executable is smaller). Keep a finite extraction bound above both assets.
PANDOC_BINARY_LIMIT = 256 * 1024**2
PANDOC = {
    "arm64": "f436d18bebbfb3ddd326597686dacab9fac5dc23bd060495020f35a28a43a7c3",
    "x86_64": "1823278715aa405aaf21b59cde5dc789346aa8d4fc307467f9a9ee84162bedc5",
}

SHARED_CONSUMER = r'''#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
extern "C" {
std::uint32_t nyrt_abi_version();
void nyrt_context_begin(int, const char *const *);
void nyrt_context_end();
void *nyrt_array_new(std::uint32_t, std::int64_t);
std::int32_t nyrt_array_set(void *, std::uint32_t, std::int64_t, std::uint64_t);
std::uint64_t nyrt_array_get(void *, std::uint32_t, std::int64_t);
std::int64_t nyrt_array_length(void *, std::uint32_t);
}
int main(int argc, char **argv) {
  Dl_info info{};
  if (argc != 2 || !dladdr(reinterpret_cast<const void *>(&nyrt_abi_version), &info)) return 2;
  char *actual = realpath(info.dli_fname, nullptr);
  char *expected = realpath(argv[1], nullptr);
  const bool same = actual && expected && std::strcmp(actual, expected) == 0;
  std::free(actual); std::free(expected);
  if (!same || nyrt_abi_version() != 1) return 3;
  nyrt_context_begin(0, nullptr);
  void *values = nyrt_array_new(2, 2);
  const bool good = values && nyrt_array_set(values, 2, 1, 123456789) &&
      nyrt_array_length(values, 2) == 2 && nyrt_array_get(values, 2, 1) == 123456789;
  nyrt_context_end();
  if (!good) return 4;
  std::puts("SDK shared ABI and packaged loader path passed");
}
'''


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def record(path):
    digest = hashlib.sha256()
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(descriptor, "rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode), "evidence input is not a regular file")
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        after = os.fstat(stream.fileno())
        require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns),
                "evidence input changed while reading")
    return {"bytes": after.st_size, "sha256": digest.hexdigest()}


def save(name, value):
    with (OUTPUT / name).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def require(value, message):
    if not value:
        raise ValueError(message)


def clean_source():
    value = source_identity(ROOT)
    require(not value["dirty"] and value["revision"] == os.environ["GITHUB_SHA"],
            "SDK jobs require the exact clean dispatched source")
    return value


def environment():
    return load("sdk_consumer_environment", "scripts/check-sdk-routes.py").consumer_environment()


def run(name, command, timeout, *, cwd=ROOT, clean=False):
    """Reuse the tested bounded owner/capture; preserve failure and partial logs."""
    support = load("sdk_owned_capture", "tests/tools/ordinary_io_parity_contract.py")
    ownership = support.load_ownership(ROOT)
    variables = environment() if clean else os.environ.copy()
    row = support.run_process(ownership, [str(x) for x in command], cwd,
                              variables, OUTPUT, name, timeout)
    row["environment_profile"] = "Apple toolchain; no inherited NIL/NY/DYLD/tool overrides" if clean else "recorded build/provider environment"
    save(name + ".json", row)
    require(row["status"] == "PASS" and row["cleanup_verified"],
            name + " failed; retained " + str(OUTPUT / (name + ".json")))
    return row


def payload(root):
    result = {}
    require(root.is_dir() and not root.is_symlink(), "candidate root must be a real directory")
    for path in sorted(root.rglob("*")):
        mode = path.lstat().st_mode
        require(not stat.S_ISLNK(mode), "candidate contains a symlink")
        if stat.S_ISDIR(mode):
            continue
        require(stat.S_ISREG(mode), "candidate contains a special file")
        result[path.relative_to(root).as_posix()] = {**record(path), "mode": stat.S_IMODE(mode)}
    require(bool(result), "candidate is empty")
    return result


def check_pandoc_binary(entry):
    require(stat.S_ISREG(entry.external_attr >> 16), "Pandoc executable is not a regular archive member")
    require(0 < entry.file_size <= PANDOC_BINARY_LIMIT,
            f"Pandoc executable size {entry.file_size} exceeds the 1..{PANDOC_BINARY_LIMIT} byte bound")


def extract_pandoc(archive, output, expected_digest, architecture):
    require(record(archive)["sha256"] == expected_digest, "Pandoc archive hash differs from pinned release")
    require(architecture in PANDOC, "unsupported Pandoc architecture")
    expected = f"pandoc-{PANDOC_VERSION}-{architecture}/bin/pandoc"
    with zipfile.ZipFile(archive) as zipped:
        names = [entry.filename for entry in zipped.infolist()]
        require(len(names) == len(set(names)) and len(names) <= 200, "ambiguous Pandoc archive entries")
        for entry in zipped.infolist():
            name = PurePosixPath(entry.filename)
            require(not name.is_absolute() and ".." not in name.parts, "unsafe Pandoc archive entry")
        entry = zipped.getinfo(expected)
        check_pandoc_binary(entry)
        with zipped.open(entry) as source, output.open("xb") as destination:
            shutil.copyfileobj(source, destination, 1024**2)
    output.chmod(0o755)


def install_pandoc():
    arch = platform.machine()
    name = f"pandoc-{PANDOC_VERSION}-{arch}-macOS.zip"
    url = f"https://github.com/jgm/pandoc/releases/download/{PANDOC_VERSION}/{name}"
    archive = OUTPUT / name
    size = 0
    with urllib.request.urlopen(url, timeout=120) as response, archive.open("xb") as destination:
        while block := response.read(1024**2):
            size += len(block)
            require(size <= 80 * 1024**2, "Pandoc download exceeds bound")
            destination.write(block)
    binary = OUTPUT / "pandoc"
    # The official archive also contains pandoc-lua/server symlinks. Extract
    # only the hash-pinned regular pandoc executable; never follow those links.
    extract_pandoc(archive, binary, PANDOC[arch], arch)
    row = run("pandoc-version", [binary, "--version"], 30)
    first = (OUTPUT / row["stdout"]).read_text().splitlines()[0]
    require(first == "pandoc " + PANDOC_VERSION, "unexpected pinned Pandoc version")
    save("pandoc-provenance.json", {"url": url, "version": PANDOC_VERSION, "architecture": arch,
         "archive": record(archive), "binary": record(binary), "source": "official jgm/pandoc GitHub release"})


def test_inventory():
    collector = load("sdk_ctest_inventory", "scripts/ci/macos-evidence.py")
    listing = json.loads((OUTPUT / "registration.json").read_text())
    junit = BUILD / "ctest-results.xml"
    require(junit.stat().st_size <= 16 * 1024**2, "oversized CTest report")
    raw = junit.read_bytes()
    require(b"<!DOCTYPE" not in raw and b"<!ENTITY" not in raw, "unexpected CTest XML entities")
    inventory = collector.summarize(listing, ET.fromstring(raw))
    errors = collector.inventory_errors(inventory)
    require(not errors and inventory["registration_present"] and inventory["junit_present"],
            "incomplete CTest inventory: " + repr(errors))
    snapshot = json.loads((OUTPUT / "sdk-build-evidence.json").read_text())
    summary = summarize_ctest(junit, snapshot["registered_tests"], snapshot["recorded_ns"])
    require(snapshot["source"] == clean_source(), "source changed after pre-test snapshot")
    save("ctest-inventory.json", {**inventory, "errors": errors, "qualification_summary": summary})


def candidate_location():
    relocation = json.loads((OUTPUT / "relocation.json").read_text())
    sdk = Path(relocation["new_path"])
    require(sdk.is_absolute() and sdk.parent.name.startswith("nil-macos-sdk-") and
            sdk.parent.parent == Path(os.environ["RUNNER_TEMP"]).resolve(strict=True),
            "relocated candidate is outside the private runner temporary root")
    return sdk


def relocated_sdk():
    sdk = candidate_location()
    relocation = json.loads((OUTPUT / "relocation.json").read_text())
    require(not STAGE.exists() and payload(sdk) == relocation["payload"], "relocated SDK bytes or modes changed")
    return sdk


def relocate():
    before = payload(STAGE)
    parent = Path(tempfile.mkdtemp(prefix="nil-macos-sdk-", dir=os.environ["RUNNER_TEMP"])).resolve(strict=True)
    destination = parent / "SDK space λ"
    shutil.move(str(STAGE), str(destination))
    require(not STAGE.exists() and payload(destination) == before, "SDK relocation changed payload")
    save("relocation.json", {"old_path": str(STAGE), "new_path": str(destination),
                            "old_path_absent": True, "payload": before})


def load_commands(text):
    """Extract loader paths from Apple's retained otool -l text, preserving order."""
    commands = []
    command = None
    for line in text.splitlines():
        words = line.strip().split()
        if len(words) == 2 and words[0] == "cmd":
            command = words[1]
        elif command and words and words[0] in ("name", "path") and " (offset " in line:
            value = line.strip().split(" ", 1)[1].rsplit(" (offset ", 1)[0]
            commands.append({"command": command, "path": value})
    return commands


def check_loader_paths(commands, forbidden):
    paths = [item["path"] for item in commands]
    for value in paths:
        resolved = str(Path(value).resolve(strict=False)) if value.startswith("/") else value
        require(not any(resolved == str(root) or resolved.startswith(str(root) + "/") for root in forbidden),
                "Mach-O loader metadata references source/build/original stage: " + value)
        require(value.startswith(("/", "@rpath/", "@loader_path", "@executable_path/")),
                "Mach-O loader metadata contains an undeclared relative lookup: " + value)
    rpaths = [item["path"] for item in commands if item["command"] == "LC_RPATH"]
    if rpaths:
        require(rpaths == ["@loader_path/nillib/native", "@loader_path", "@loader_path/../lib"],
                "Mach-O RPATHs differ from the SDK-first, dylib-local, build-tree profile")


def dependencies():
    sdk = relocated_sdk()
    result = {"scope": "Direct Mach-O loader observations and host prerequisites; not a portable closure or minimum-OS claim",
              "sdk": str(sdk), "tools": {}, "external_prerequisites": {}}
    for index, path in enumerate([sdk / name for name in TOOLS] + [sdk / "nillib/native/libnil.dylib"]):
        row = run("loader-" + str(index), ["/usr/bin/otool", "-l", path], 30, clean=True)
        commands = load_commands((OUTPUT / row["stdout"]).read_text())
        require(commands, "missing Mach-O loader observations")
        check_loader_paths(commands, (ROOT, BUILD, STAGE))
        if path.name == "libnil.dylib":
            require([item["path"] for item in commands if item["command"] == "LC_ID_DYLIB"] == ["@rpath/libnil.dylib"],
                    "shared runtime install name differs from the relocatable SDK profile")
        result["tools"][path.relative_to(sdk).as_posix()] = {"payload": record(path), "commands": commands,
            "raw_load_commands": row["stdout_record"]}
        for item in commands:
            name = item["path"]
            if item["command"] in ("LC_RPATH", "LC_ID_DYLIB") or not name.startswith("/"):
                continue
            result["external_prerequisites"][name] = external_prerequisite(Path(name))
    save("loader-dependencies.json", result)


def external_prerequisite(dependency):
    # Homebrew's declared dylib name can be a legitimate filesystem alias.
    # Keep the declared loader spelling and explicitly bind its real target;
    # sealed SDK/evidence record() itself remains no-follow.
    name = str(dependency)
    system = name.startswith(("/usr/lib/", "/System/Library/"))
    target = dependency.resolve(strict=True) if dependency.exists() or dependency.is_symlink() else None
    require(system or target is not None, "declared non-system dylib is absent: " + name)
    return {"declared_name": name, "canonical_target": str(target) if target is not None else None,
            "kind": "Apple system/shared-cache library" if system else "external development prerequisite",
            "file": record(target) if target is not None else None}


def shadow_validation(sdk, validate):
    """A malformed sibling lib must not replace the packaged shared runtime."""
    before = payload(sdk)
    directory = sdk.parent / "lib"
    directory.mkdir(exist_ok=False)
    shadow = directory / "libnil.dylib"
    shadow.write_bytes(b"Deliberately invalid private sibling library: never load this file.\n")
    original = shadow.lstat()
    expected = record(shadow)
    result = {"scope": "relocated shared/native validation with an invalid private sibling libnil",
              "path": str(shadow), "sentinel": expected, "validation_passed": False,
              "candidate_unchanged": False, "cleanup_verified": False}
    failure = None
    try:
        validate()
        result["validation_passed"] = True
    except BaseException as error:
        failure = error
        result["validation_error"] = type(error).__name__ + ": " + str(error)
    try:
        result["candidate_unchanged"] = payload(sdk) == before
        require(result["candidate_unchanged"], "candidate changed during sibling-shadow validation")
    except (OSError, ValueError) as error:
        result["candidate_error"] = str(error)
        failure = failure or error
    try:
        current = shadow.lstat()
        require(stat.S_ISREG(current.st_mode) and
                (current.st_dev, current.st_ino) == (original.st_dev, original.st_ino) and record(shadow) == expected,
                "private sibling sentinel identity or bytes changed")
        shadow.unlink()
        directory.rmdir()
        result["cleanup_verified"] = True
    except (OSError, ValueError) as error:
        result["cleanup_error"] = str(error)
        failure = failure or error
    save("sibling-shadow.json", result)
    if failure is not None:
        raise failure


def shared_loader_consumer(sdk):
    """Execute an actual @rpath consumer; dladdr proves the packaged library."""
    probe = sdk.parent / "loader-probe"
    (probe / "nillib").mkdir(parents=True, exist_ok=False)
    local = probe / "nillib/native"
    runtime = sdk / "nillib/native/libnil.dylib"
    # This alias is only in the private test application, never in the sealed
    # SDK. The application has exactly the root SDK tool RPATH layout.
    local.symlink_to(runtime.parent, target_is_directory=True)
    source = OUTPUT / "shared-loader-consumer.cpp"
    with source.open("x") as stream:
        stream.write(SHARED_CONSUMER)
    executable = probe / "shared-consumer"
    run("shared-loader-compile", ["/usr/bin/clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         source, runtime, "-Wl,-rpath,@loader_path/nillib/native", "-Wl,-rpath,@loader_path",
         "-Wl,-rpath,@loader_path/../lib", "-o", executable], 90, clean=True)
    shutil.copy2(executable, OUTPUT / "shared-loader-consumer")
    command = [str(executable), str(runtime)]
    positive = run("shared-loader-positive", command, 30, cwd=probe, clean=True)
    require((OUTPUT / positive["stdout"]).read_bytes() == b"SDK shared ABI and packaged loader path passed\n" and
            (OUTPUT / positive["stderr"]).read_bytes() == b"", "shared loader ABI/path proof did not pass")
    # Remove only the private application's alias. Its unchanged executable
    # must now encounter the deliberately malformed sibling library.
    hidden = local.with_name("native.hidden")
    local.rename(hidden)
    try:
        support = load("sdk_shadow_capture", "tests/tools/ordinary_io_parity_contract.py")
        negative = support.run_process(support.load_ownership(ROOT), command, probe,
                                       environment(), OUTPUT, "shared-loader-negative", 30)
        save("shared-loader-negative.json", negative)
        stderr = (OUTPUT / negative["stderr"]).read_text(errors="replace")
        require(negative["cleanup_verified"] and negative.get("exit_code") not in (None, 0) and
                not negative.get("timed_out", False) and not negative.get("error") and
                (OUTPUT / negative["stdout"]).read_bytes() == b"" and "libnil.dylib" in stderr and
                any(value in stderr.lower() for value in ("not a mach-o", "file too short")) and
                any(value in stderr for value in (str(sdk.parent / "lib/libnil.dylib"),
                                                 str(probe / "../lib/libnil.dylib"))),
                "negative loader control did not identify the malformed sibling library")
    finally:
        hidden.rename(local)
    save("shared-loader-proof.json", {"status": "PASS", "scope": "actual shared ABI, dladdr path and sibling precedence",
         "runtime": str(runtime), "runtime_record": record(runtime), "source": record(source),
         "executable": record(executable), "positive": positive, "negative": negative,
         "negative_expected_to_fail": True})


def capture_candidate(sdk, expected):
    """Retain regular payloads even when a candidate has a link/special-file error."""
    observation = {"path": str(sdk), "seal_unchanged": None, "error": None}
    try:
        observed = payload(sdk)
        observation["seal_unchanged"] = expected is not None and observed == expected
    except (OSError, ValueError) as error:
        observation["seal_unchanged"] = False
        observation["error"] = type(error).__name__ + ": " + str(error)
    name = "development-candidate.tar.gz" if observation["seal_unchanged"] else "unqualified-stage.tar.gz"
    def regular_only(member):
        return member if member.isfile() or member.isdir() else None
    # No symlink targets or special files are opened; the complete error is
    # recorded separately. An invalid candidate never receives the sealed name.
    with tarfile.open(OUTPUT / name, "x:gz", dereference=False) as archive:
        archive.add(sdk, arcname="sdk", recursive=True, filter=regular_only)
    observation["archive"] = name
    manifest = sdk / "manifest.json"
    if stat.S_ISDIR(sdk.lstat().st_mode) and manifest.exists() and stat.S_ISREG(manifest.lstat().st_mode):
        shutil.copy2(manifest, OUTPUT / "candidate-manifest.json", follow_symlinks=False)
    return observation


def collect():
    # Always retain failure evidence; this step never translates failed tests
    # into qualification or activates the candidate.
    for source, target in ((BUILD / "ctest-results.xml", "ctest-results.xml"),
                           (BUILD / "CMakeCache.txt", "CMakeCache.txt"),
                           (BUILD / "cmake_install.cmake", "cmake_install.cmake"),
                           (BUILD / "install_manifest.txt", "install_manifest.txt")):
        if source.is_file() and not (OUTPUT / target).exists():
            shutil.copy2(source, OUTPUT / target)
    routes = BUILD / "tests/route-reports"
    if routes.is_dir():
        shutil.copytree(routes, OUTPUT / "build-route-reports")
    candidate = None
    candidate_unchanged = None
    try:
        if (OUTPUT / "relocation.json").is_file():
            sdk = candidate_location()
            expected = json.loads((OUTPUT / "relocation.json").read_text())["payload"]
            candidate = capture_candidate(sdk, expected)
            candidate_unchanged = candidate["seal_unchanged"]
        elif STAGE.is_dir() and not STAGE.is_symlink():
            candidate = capture_candidate(STAGE, None)
    except (OSError, ValueError, KeyError) as error:
        candidate = {"error": type(error).__name__ + ": " + str(error), "seal_unchanged": False}
        candidate_unchanged = False
    save("candidate-capture.json", candidate)
    final = source_identity(ROOT)
    identity = OUTPUT / "identity.json"
    initial = json.loads(identity.read_text())["source"] if identity.is_file() else None
    files, excluded = {}, []
    for path in sorted(OUTPUT.rglob("*")):
        mode = path.lstat().st_mode
        if stat.S_ISREG(mode):
            files[path.relative_to(OUTPUT).as_posix()] = record(path)
        elif not stat.S_ISDIR(mode):
            excluded.append({"path": path.relative_to(OUTPUT).as_posix(), "mode": mode})
    save("evidence.json", {"schema": "nil.macos.development-sdk-evidence.v1", "release_declaration": False,
         "platform_parity": False, "source": final, "source_unchanged": initial == final,
         "relocated_candidate_unchanged": candidate_unchanged, "files": files,
         "nonregular_evidence_entries": excluded})
    require(initial == final, "source changed during development candidate workflow")
    require(candidate_unchanged is not False, "candidate payload changed; diagnostic archive retained")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("identity", "pandoc", "install", "docs", "package", "validate-stage",
                                         "relocate", "dependencies", "validate-relocated", "consumers", "collect"))
    args = parser.parse_args()
    require(sys.platform == "darwin" and platform.machine() in PANDOC, "native macOS x86_64/arm64 required")
    phase = args.phase
    if phase == "identity":
        OUTPUT.mkdir(parents=True, exist_ok=False)
        save("identity.json", {"source": clean_source(), "architecture": platform.machine(),
             "system": platform.platform(), "python": sys.version, "run_id": os.environ["GITHUB_RUN_ID"],
             "attempt": os.environ["GITHUB_RUN_ATTEMPT"], "workflow": record(ROOT / ".github/workflows/macos-sdk.yml"),
             "helper": record(Path(__file__)), "scope": "isolated development candidate; no SDK activation or release"})
    elif phase == "pandoc":
        install_pandoc()
    elif phase == "install":
        test_inventory()
        STAGE.mkdir(exist_ok=False)
        run("install", ["cmake", "--install", BUILD, "--prefix", STAGE], 180)
    elif phase == "docs":
        run("docs", [sys.executable, "-B", ROOT / "scripts/build-docs.py", "--repo-root", ROOT,
             "--output", STAGE / "docs", "--revision", os.environ["GITHUB_SHA"], "--pandoc", OUTPUT / "pandoc"], 300)
    elif phase == "package":
        run("package", [sys.executable, "-B", ROOT / "scripts/package-sdk.py", "--repo-root", ROOT,
             "--stage-dir", STAGE, "--revision", os.environ["GITHUB_SHA"], "--build-type", "Release", "--llvm", "ON",
             "--qualification", "development", "--build-evidence", OUTPUT / "sdk-build-evidence.json",
             "--ctest-report", BUILD / "ctest-results.xml"], 600)
    elif phase.startswith("validate-"):
        sdk = STAGE if phase == "validate-stage" else relocated_sdk()
        if phase == "validate-stage":
            for name, command in (("clean-xcrun", ["/usr/bin/xcrun", "--find", "clang++"]),
                                  ("clean-clang", ["/usr/bin/clang++", "--version"]),
                                  ("clean-sdk", ["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path"])):
                run(name, command, 30, clean=True)
        validate = lambda: run(phase, [sys.executable, "-B", ROOT / "scripts/validate-sdk.py", sdk,
             "--expected-revision", os.environ["GITHUB_SHA"]], 600, clean=True)
        if phase == "validate-relocated":
            def relocated_checks():
                validate()
                shared_loader_consumer(sdk)
            shadow_validation(sdk, relocated_checks)
        else:
            validate()
    elif phase == "relocate":
        relocate()
    elif phase == "dependencies":
        dependencies()
    elif phase == "consumers":
        sdk = relocated_sdk()
        run("consumers", [sys.executable, "-B", ROOT / "scripts/check-sdk-routes.py", "--sdk", sdk,
             "--expected-revision", os.environ["GITHUB_SHA"], "--output", OUTPUT / "consumer-routes.json",
             "--work-dir", OUTPUT / "consumer-work"], 2400, clean=True)
        relocated_sdk()  # Verify the sealed bytes/modes again after all routes.
    elif phase == "collect":
        collect()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("macOS development SDK evidence failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)
