#!/usr/bin/env python3
"""Assemble a local, qualified Linux SDK candidate; never publish or activate it.

Requires an existing clean rc-checks SDK, the matching clean Git checkout,
committed VERSION, and explicitly reviewed dependency policy/notices. See
--help. This deliberately never publishes or activates anything.
"""

from __future__ import annotations

import argparse
import ctypes
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from typing import Any
import zipfile
import xml.etree.ElementTree as ET


SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
from sdk_qualification import qualification_errors

_spec = importlib.util.spec_from_file_location("ny_release_sdk_validator", SCRIPTS / "validate-sdk.py")
assert _spec is not None and _spec.loader is not None
sdk_validator = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sdk_validator)

VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-rc\.([1-9][0-9]*))?")
REVISION = re.compile(r"[0-9a-f]{40}|[0-9a-f]{64}")
DIGEST = re.compile(r"[0-9a-f]{64}")
SONAME = re.compile(r"[A-Za-z0-9_+.-]+")
MAX_JSON = 16 * 1024 * 1024
TOOLS = sdk_validator.REQUIRED_TOOLS
ASSET_NAMES = {"release-manifest.json", "runtime-requirements.json", "dependency-policy.json",
               "THIRD_PARTY_NOTICES.txt", "SHA256SUMS"}


class ReleaseError(ValueError):
    pass


def sha256(path: Path) -> str:
    return sdk_validator.sha256(path)


def json_bytes(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n").encode("utf-8")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ReleaseError("duplicate JSON field: " + key)
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    if not stat.S_ISREG(path.lstat().st_mode) or path.stat().st_size > MAX_JSON:
        raise ReleaseError("JSON input must be a bounded regular file: " + str(path))
    value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
    if not isinstance(value, dict):
        raise ReleaseError("JSON root must be an object: " + str(path))
    return value


def capture(command: list[str], *, cwd: Path | None = None, timeout: int = 30) -> str:
    result = subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                            text=True, encoding="utf-8", errors="strict", timeout=timeout,
                            env={**os.environ, "LC_ALL": "C"}, check=False)
    if result.returncode != 0:
        raise ReleaseError("command failed: {}: {}".format(command[0], result.stderr.strip()[:2000]))
    if len(result.stdout) > MAX_JSON:
        raise ReleaseError("command output exceeds safety bound: " + command[0])
    return result.stdout


def source_identity(repo: Path, expected_revision: str) -> tuple[str, int]:
    if REVISION.fullmatch(expected_revision) is None:
        raise ReleaseError("expected revision must be a full lowercase Git object id")
    if capture(["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=repo).strip():
        raise ReleaseError("release assembly requires a clean NIL Framework checkout")
    revision = capture(["git", "rev-parse", "HEAD"], cwd=repo).strip()
    if revision != expected_revision:
        raise ReleaseError("checkout HEAD differs from expected SDK revision")
    version = capture(["git", "show", revision + ":VERSION"], cwd=repo).strip()
    if VERSION.fullmatch(version) is None:
        raise ReleaseError("committed VERSION must be SemVer X.Y.Z or X.Y.Z-rc.N")
    if (repo / "VERSION").read_text(encoding="utf-8").strip() != version:
        raise ReleaseError("working VERSION differs from committed VERSION")
    epoch = int(capture(["git", "show", "-s", "--format=%ct", revision], cwd=repo).strip())
    if not 0 <= epoch <= 0xFFFFFFFF:
        raise ReleaseError("commit timestamp is outside the supported archive range")
    return version, epoch


def validate_sdk(sdk: Path, revision: str, version: str) -> tuple[dict[str, Any], dict[str, Path]]:
    errors = sdk_validator.root_layout_errors(sdk)
    files, directories = sdk_validator.inventory_tree(sdk, errors)
    sdk_validator.validate_exact_layout(sdk, files, directories, errors)
    manifest = load_json(sdk / "manifest.json")
    sdk_validator.validate_manifest_metadata(manifest, errors)
    errors.extend(qualification_errors(manifest, True, revision))
    sdk_validator.validate_tools(manifest.get("tools"), files, errors)
    sdk_validator.validate_native_runtime(manifest.get("native_runtime"), files, errors)
    sdk_validator.validate_shared_runtime(manifest.get("native_shared_runtime"), files, errors)
    sdk_validator.validate_standard_library(manifest.get("standard_library"), files, errors)
    sdk_validator.validate_documentation(manifest.get("documentation"), files, errors)
    sdk_validator.validate_payload(manifest.get("payload"), files, errors)
    qualification = manifest.get("qualification", {})
    if not isinstance(qualification, dict) or qualification.get("compiler_version") != "nilcompiler " + version:
        errors.append("SDK compiler version does not match committed VERSION")
    if errors:
        raise ReleaseError("SDK rejected:\n- " + "\n- ".join(errors))
    return manifest, files


def parse_elf_report(report: str, name: str, *, shared: bool = False) -> dict[str, Any]:
    if not re.search(r"Class:\s+ELF64\b", report) or not re.search(
            r"Machine:\s+Advanced Micro Devices X86-64\s*$", report, re.MULTILINE):
        raise ReleaseError(name + " is not a supported Linux x86_64 ELF64 executable")
    if not re.search(r"Data:\s+2's complement, little endian", report):
        raise ReleaseError(name + " has an unsupported ELF byte order")
    if not re.search(r"Type:\s+(?:DYN|EXEC)\b", report):
        raise ReleaseError(name + " is not an executable ELF object")
    if re.search(r"\((?:RPATH|RUNPATH)\)", report):
        raise ReleaseError(name + " contains an unreviewed embedded library search path")
    interpreters = re.findall(r"\[Requesting program interpreter: ([^\]]+)\]", report)
    if interpreters != ([] if shared else ["/lib64/ld-linux-x86-64.so.2"]):
        raise ReleaseError(name + " must use the reviewed Linux x86_64 interpreter")
    needed = sorted(set(re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", report)))
    if not needed or any(SONAME.fullmatch(item) is None for item in needed):
        raise ReleaseError(name + " has missing or invalid DT_NEEDED entries")
    required: dict[str, list[str]] = {}
    section = ""
    library: str | None = None
    for line in report.splitlines():
        if line.startswith("Version "):
            section = "needs" if line.startswith("Version needs section") else "other"
        if section != "needs":
            continue
        file_match = re.search(r"\bFile: (\S+)\s+Cnt:", line)
        if file_match:
            library = file_match.group(1)
            if library not in needed:
                raise ReleaseError(name + " has a version dependency absent from DT_NEEDED")
            required.setdefault(library, [])
        symbol_match = re.search(r"\bName: (\S+)\s+Flags:", line)
        if symbol_match:
            if library is None:
                raise ReleaseError(name + " has malformed symbol-version requirements")
            required[library].append(symbol_match.group(1))
    return {"path": name, "interpreter": None if shared else interpreters[0], "needed": needed,
            "required_symbol_versions": {key: sorted(set(values)) for key, values in sorted(required.items())}}


def numeric_version(value: str) -> tuple[int, ...]:
    if re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", value) is None:
        raise ReleaseError("invalid numeric ABI version: " + value)
    return tuple(map(int, value.split(".")))


def elf_inventory(sdk: Path, readelf: str) -> dict[str, Any]:
    records = []
    for name in (*TOOLS, "nillib/native/libnil.so"):
        tool = sdk / name
        with tool.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ReleaseError(name + " is not an ELF executable")
        report = capture([readelf, "--wide", "--file-header", "--program-headers",
                          "--dynamic", "--version-info", str(tool)])
        record = parse_elf_report(report, name, shared=name.endswith(".so"))
        record["sha256"] = sha256(tool)
        records.append(record)
    maximum: dict[str, str] = {}
    for record in records:
        for versions in record["required_symbol_versions"].values():
            for value in versions:
                match = re.fullmatch(r"(GLIBC|GLIBCXX|CXXABI)_([0-9]+(?:\.[0-9]+)*)", value)
                if match:
                    family, number = match.groups()
                    if family not in maximum or numeric_version(number) > numeric_version(maximum[family]):
                        maximum[family] = number
                elif value.startswith(("GLIBC_", "GLIBCXX_", "CXXABI_")):
                    raise ReleaseError("unreviewed nonnumeric/private ABI requirement: " + value)
    return {"schema": "ny.release-runtime.v1", "platform": {"os": "linux", "architecture": "x86_64"},
            "scope": "Direct ELF requirements only; transitive host libraries, dlopen, CPU ISA and OS behavior require separate qualification.",
            "maximum_required_symbol_versions": maximum,
            "needed": sorted({item for record in records for item in record["needed"]}),
            "elf_files": records}


def validate_dependency_policy(policy: dict[str, Any], notices: bytes,
                               inventory: dict[str, Any], build: dict[str, Any]) -> None:
    expected = {"schema", "reviewed", "notices_sha256", "expected_needed", "maximum_symbol_versions", "static_components"}
    if set(policy) != expected or policy.get("schema") != "ny.release-dependencies.v1":
        raise ReleaseError("dependency policy schema/fields are incomplete")
    if policy.get("reviewed") is not True:
        raise ReleaseError("dependency and notice review is not complete")
    if not notices.strip() or len(notices) > MAX_JSON:
        raise ReleaseError("third-party notices must be nonempty and bounded")
    text = notices.decode("utf-8")
    if policy.get("notices_sha256") != hashlib.sha256(notices).hexdigest():
        raise ReleaseError("reviewed notices SHA-256 does not match")
    expected_needed = policy.get("expected_needed")
    if not isinstance(expected_needed, list) or expected_needed != inventory["needed"]:
        raise ReleaseError("actual DT_NEEDED set differs from the reviewed exact dependency list")
    ceilings = policy.get("maximum_symbol_versions")
    if not isinstance(ceilings, dict) or set(ceilings) != {"GLIBC", "GLIBCXX", "CXXABI"}:
        raise ReleaseError("ABI ceilings must explicitly cover GLIBC, GLIBCXX and CXXABI")
    for family, ceiling in ceilings.items():
        if not isinstance(ceiling, str):
            raise ReleaseError("ABI ceilings must be numeric version strings")
        limit = numeric_version(ceiling)
        actual = inventory["maximum_required_symbol_versions"].get(family)
        if actual is not None and numeric_version(actual) > limit:
            raise ReleaseError(f"{family}_{actual} exceeds reviewed ceiling {ceiling}")
    components = policy.get("static_components")
    if not isinstance(components, list) or not components:
        raise ReleaseError("statically incorporated third-party components need explicit notice review")
    names: set[str] = set()
    versions: dict[str, str] = {}
    for component in components:
        if not isinstance(component, dict) or set(component) != {"name", "version", "license", "notice_marker"}:
            raise ReleaseError("static component needs name, version, license and notice_marker")
        if any(not isinstance(value, str) or not value.strip() for value in component.values()):
            raise ReleaseError("static component fields must be nonempty strings")
        if component["name"] in names or component["notice_marker"] not in text:
            raise ReleaseError("duplicate static component or missing notice marker")
        names.add(component["name"])
        versions[component["name"]] = component["version"]
    if build.get("cxx_id") != "GNU":
        raise ReleaseError("this release dependency profile currently requires a reviewed GNU compiler build")
    if versions.get("LLVM") != build.get("llvm_version") or versions.get("GCC") != build.get("cxx_version"):
        raise ReleaseError("reviewed LLVM/GCC versions differ from qualified build evidence")


def validate_vsix(path: Path, version: str) -> str:
    if not stat.S_ISREG(path.lstat().st_mode) or path.stat().st_size > 128 * 1024 * 1024:
        raise ReleaseError("VSIX must be a bounded regular archive")
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(entries) > 10000 or len(set(names)) != len(names):
            raise ReleaseError("VSIX has excessive or duplicate entries")
        if any(entry.file_size > MAX_JSON for entry in entries) or sum(entry.file_size for entry in entries) > 128 * 1024 * 1024:
            raise ReleaseError("VSIX exceeds individual or total decompressed size bounds")
        for entry in entries:
            name = entry.filename
            components = name.rstrip("/").split("/")
            if ("\\" in name or ":" in components[0] or any(part in ("", ".", "..") for part in components)
                    or stat.S_ISLNK(entry.external_attr >> 16) or entry.flag_bits & 1):
                raise ReleaseError("VSIX has an unsafe, linked or encrypted entry")
        for name in ("extension/package.json", "extension.vsixmanifest", "extension/THIRD_PARTY_NOTICES.txt"):
            if name not in names or archive.getinfo(name).file_size > MAX_JSON:
                raise ReleaseError("VSIX is missing bounded metadata/notices: " + name)
        package = json.loads(archive.read("extension/package.json"), object_pairs_hook=_unique_object)
        if not isinstance(package, dict) or (package.get("name"), package.get("publisher"), package.get("version")) != (
                "ny-language-support", "exec-software", version):
            raise ReleaseError("VSIX package identity differs from the NIL Framework release")
        declarations = [package.get("main")]
        contributions = package.get("contributes")
        if not isinstance(contributions, dict):
            raise ReleaseError("VSIX is missing Ny language contributions")
        for category, field in (("languages", "configuration"), ("grammars", "path"), ("snippets", "path")):
            values = contributions.get(category)
            if not isinstance(values, list) or not values or any(not isinstance(item, dict) for item in values):
                raise ReleaseError("VSIX is missing Ny " + category)
            declarations.extend(item.get(field) for item in values)
        for declaration in declarations:
            if not isinstance(declaration, str) or not declaration:
                raise ReleaseError("VSIX has an absent or invalid declared asset path")
            relative = declaration[2:] if declaration.startswith("./") else declaration
            if ("\\" in relative or ":" in relative or
                    any(part in ("", ".", "..") for part in relative.split("/"))):
                raise ReleaseError("VSIX has an unsafe declared asset path")
            declared_name = "extension/" + relative
            if declared_name not in names or archive.getinfo(declared_name).is_dir():
                raise ReleaseError("VSIX is missing a declared asset: " + declared_name)
        xml = archive.read("extension.vsixmanifest")
        if b"<!DOCTYPE" in xml.upper() or b"<!ENTITY" in xml.upper():
            raise ReleaseError("VSIX identity XML must not contain declarations/entities")
        identity = ET.fromstring(xml).find(".//{*}Identity")
        if identity is None or any(identity.get(key) != value for key, value in {
                "Id": "ny-language-support", "Publisher": "exec-software", "Version": version}.items()):
            raise ReleaseError("VSIX XML identity differs from the NIL Framework release")
        if not archive.read("extension/THIRD_PARTY_NOTICES.txt").strip():
            raise ReleaseError("VSIX notices are empty")
        if archive.testzip() is not None:
            raise ReleaseError("VSIX failed CRC verification")
    return "ny-language-support-" + version + ".vsix"


def rename_no_replace(source: Path, destination: Path) -> None:
    """Linux-only release profile: fail closed unless atomic no-replace exists."""
    library = ctypes.CDLL(None, use_errno=True)
    rename = getattr(library, "renameat2", None)
    if rename is None:
        raise ReleaseError("atomic no-replace release staging requires Linux renameat2")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1) != 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number), str(destination))


class _HashingReader:
    def __init__(self, stream: Any):
        self.stream = stream
        self.digest = hashlib.sha256()

    def read(self, size: int = -1) -> bytes:
        data = self.stream.read(size)
        self.digest.update(data)
        return data


def write_archive(destination: Path, root_name: str, files: dict[str, Path],
                  expected: dict[str, tuple[int, str]], epoch: int) -> None:
    """Stable ordering, zero ownership, fixed modes/times, no links or host paths."""
    directories = {root_name}
    for relative in files:
        directories.update(str(parent) for parent in (Path(root_name) / relative).parents if str(parent) != ".")
    with destination.open("xb") as output, gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for directory in sorted(directories):
                info = tarfile.TarInfo(directory)
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                info.mtime = epoch
                archive.addfile(info)
            for relative, source in sorted(files.items()):
                if not stat.S_ISREG(source.lstat().st_mode):
                    raise ReleaseError("SDK file changed to a non-regular entry: " + relative)
                size, digest = expected[relative]
                info = tarfile.TarInfo(root_name + "/" + relative)
                info.size = size
                info.mode = 0o755 if relative in TOOLS else 0o644
                info.mtime = epoch
                with source.open("rb") as stream:
                    reader = _HashingReader(stream)
                    archive.addfile(info, reader)
                    if reader.digest.hexdigest() != digest or stream.read(1):
                        raise ReleaseError("SDK file changed during archive assembly: " + relative)


def verify_assets(directory: Path) -> None:
    manifest = load_json(directory / "release-manifest.json")
    names = manifest.get("publishedAssets")
    if not isinstance(names, list) or any(not isinstance(name, str) for name in names):
        raise ReleaseError("release manifest has no usable asset allowlist")
    if names != sorted(set(names)) or any(SONAME.fullmatch(name) is None for name in names):
        raise ReleaseError("release asset allowlist is not canonical")
    present = {path.name for path in directory.iterdir() if stat.S_ISREG(path.lstat().st_mode)}
    if present != set(names) or len(list(directory.iterdir())) != len(names):
        raise ReleaseError("release directory does not contain exactly the allowed regular assets")
    checksums: dict[str, str] = {}
    for line in (directory / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_+.-]+)", line)
        if match is None or match[2] in checksums:
            raise ReleaseError("malformed or duplicate release checksum")
        checksums[match[2]] = match[1]
    if set(checksums) != set(names) - {"SHA256SUMS"}:
        raise ReleaseError("checksums must cover exactly all other release assets")
    if any(sha256(directory / name) != digest for name, digest in checksums.items()):
        raise ReleaseError("release checksum mismatch")
    records = manifest.get("assets")
    if not isinstance(records, list) or len(records) != len(names) - 2:
        raise ReleaseError("release manifest must identify every non-self/non-checksum asset")
    observed: set[str] = set()
    for record in records:
        if not isinstance(record, dict) or set(record) != {"name", "bytes", "sha256"}:
            raise ReleaseError("release manifest asset record is malformed")
        name = record["name"]
        if not isinstance(name, str) or name not in checksums or name == "release-manifest.json" or name in observed:
            raise ReleaseError("release manifest asset identity is duplicated or unexpected")
        if (type(record["bytes"]) is not int or record["bytes"] != (directory / name).stat().st_size
                or record["sha256"] != checksums[name]):
            raise ReleaseError("release manifest asset hash/size differs from checksums")
        observed.add(name)
    if observed != set(names) - {"release-manifest.json", "SHA256SUMS"}:
        raise ReleaseError("release manifest asset records do not exactly cover the payload")


def assemble(args: argparse.Namespace) -> Path:
    repo = args.repo_root.resolve(strict=True)
    if repo != SCRIPTS.parent.resolve():
        raise ReleaseError("--repo-root must match this assembler/helper checkout")
    sdk = args.sdk.resolve(strict=True)
    destination = args.output.absolute()
    if destination.exists() or destination.is_symlink():
        raise ReleaseError("refusing to replace an existing release output")
    if sdk == Path(sdk.anchor) or not sdk.is_dir():
        raise ReleaseError("SDK must be a non-root directory")
    if destination == sdk or sdk in destination.parents:
        raise ReleaseError("release output must not be inside the SDK")
    readelf = shutil.which("readelf")
    if readelf is None:
        raise ReleaseError("readelf is required for measured ELF dependency/ABI inventory")
    version, epoch = source_identity(repo, args.expected_revision)
    initial_manifest_identity = ((sdk / "manifest.json").stat().st_size, sha256(sdk / "manifest.json"))
    manifest, files = validate_sdk(sdk, args.expected_revision, version)
    if initial_manifest_identity != ((sdk / "manifest.json").stat().st_size, sha256(sdk / "manifest.json")):
        raise ReleaseError("sealed SDK manifest changed during validation")
    policy = load_json(args.dependency_policy)
    policy_bytes = args.dependency_policy.read_bytes()
    if not stat.S_ISREG(args.notices.lstat().st_mode) or args.notices.stat().st_size > MAX_JSON:
        raise ReleaseError("notices input must be a bounded regular file")
    notices = args.notices.read_bytes()
    for name, expected in (("THIRD_PARTY_NOTICES.txt", notices), ("dependency-policy.json", policy_bytes)):
        bundled = sdk / "docs" / "release" / name
        if not bundled.is_file() or bundled.is_symlink() or bundled.read_bytes() != expected:
            raise ReleaseError("sealed SDK release documentation differs from reviewed input: " + name)
    inventory = elf_inventory(sdk, readelf)
    validate_dependency_policy(policy, notices, inventory, manifest["qualification"]["evidence"]["build"])
    vsix_name = validate_vsix(args.vsix, version) if args.vsix else None
    vsix_digest = sha256(args.vsix) if args.vsix else None
    for name in TOOLS:
        if capture([str(sdk / name), "--version"]).strip() != name + " " + version:
            raise ReleaseError(name + " --version does not match committed VERSION")
    # Mandatory installed-tool execution, not merely trusted historical evidence.
    capture([sys.executable, str(SCRIPTS / "validate-sdk.py"), str(sdk), "--require-rc-checks",
             "--expected-revision", args.expected_revision], timeout=1800)
    root_name = "nil-sdk-" + version + "-linux-x86_64"
    archive_name = root_name + ".tar.gz"
    records = {item["path"]: (item["bytes"], item["sha256"]) for item in manifest["payload"]["files"]}
    records["manifest.json"] = initial_manifest_identity
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ny-release-stage-", dir=destination.parent) as temporary:
        stage = Path(temporary)
        write_archive(stage / archive_name, root_name, files, records, epoch)
        (stage / "runtime-requirements.json").write_bytes(json_bytes(inventory))
        (stage / "dependency-policy.json").write_bytes(policy_bytes)
        (stage / "THIRD_PARTY_NOTICES.txt").write_bytes(notices)
        if vsix_name:
            shutil.copyfile(args.vsix, stage / vsix_name)
            if sha256(stage / vsix_name) != vsix_digest:
                raise ReleaseError("VSIX changed during assembly")
        assets = sorted(ASSET_NAMES | {archive_name} | ({vsix_name} if vsix_name else set()))
        release = {
            "schemaVersion": 1, "name": "NIL Framework", "version": version, "tag": "v" + version,
            "channel": "rc" if "-rc." in version else "stable", "commit": args.expected_revision,
            "platform": inventory["platform"], "sourceDateEpoch": epoch,
            "status": "local-candidate-not-published", "publishedAssets": assets,
            "sdk": {"archive": archive_name, "archiveRoot": root_name,
                    "manifestSha256": records["manifest.json"][1], "qualification": "rc-checks"},
            "assets": [{"name": path.name, "bytes": path.stat().st_size, "sha256": sha256(path)}
                       for path in sorted(stage.iterdir())],
            "verification": {"installedSdkSmoke": "passed", "allToolVersions": version,
                             "archiveNormalization": "nil.sdk-tar.v1",
                             "dependencyReview": "explicit policy assertion; not automatic legal or transitive dependency verification",
                             "builddCompatibility": "not yet qualified; local assembly is not publication"},
        }
        (stage / "release-manifest.json").write_bytes(json_bytes(release))
        (stage / "SHA256SUMS").write_text("".join(sha256(stage / name) + "  " + name + "\n"
                                                    for name in assets if name != "SHA256SUMS"), encoding="ascii")
        verify_assets(stage)
        # Re-check inputs after archive/smoke work. Never emit stale provenance.
        if source_identity(repo, args.expected_revision) != (version, epoch):
            raise ReleaseError("source identity changed during assembly")
        validate_sdk(sdk, args.expected_revision, version)
        if initial_manifest_identity != ((sdk / "manifest.json").stat().st_size, sha256(sdk / "manifest.json")):
            raise ReleaseError("sealed SDK manifest changed during assembly")
        rename_no_replace(stage, destination)
    return destination


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=SCRIPTS.parent)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--dependency-policy", type=Path, required=True,
                        help="reviewed ny.release-dependencies.v1 policy, with exact needed SONAMEs and ABI ceilings")
    parser.add_argument("--notices", type=Path, required=True,
                        help="complete reviewed UTF-8 third-party notices matching the policy SHA-256")
    parser.add_argument("--vsix", type=Path, help="optional matching-version Ny VS Code extension, with its own notices")
    parser.add_argument("--output", type=Path, required=True, help="new local directory; existing output is never replaced")
    return parser.parse_args()


def main() -> int:
    try:
        output = assemble(arguments())
    except (ReleaseError, OSError, UnicodeError, json.JSONDecodeError, subprocess.SubprocessError,
            zipfile.BadZipFile, ET.ParseError) as error:
        print("NIL Framework release assembly refused: " + str(error), file=sys.stderr)
        return 1
    print("Local NIL Framework release candidate assembled: " + str(output))
    print("No tag, SDK activation, upload, or publication was performed.")
    print("release-manifest.json SHA-256: " + sha256(output / "release-manifest.json"))
    print("SHA256SUMS SHA-256: " + sha256(output / "SHA256SUMS"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
