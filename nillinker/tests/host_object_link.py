#!/usr/bin/env python3
"""End-to-end contract checks for the standalone NIL host-object linker."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import struct
import subprocess
import tempfile
from pathlib import Path


class SparseFilesUnavailable(RuntimeError):
    """The current Windows volume cannot create sparse limit fixtures."""


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linker", required=True, type=Path)
    parser.add_argument("--timeout-linker", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--install-libdir", required=True)
    parser.add_argument("--work-dir", required=True, type=Path)
    return parser.parse_args()


def run(
    command: list[str], cwd: Path, *, timeout: float = 30.0
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            "process exceeded {:.1f}s timeout: {}".format(
                timeout, " ".join(str(argument) for argument in command[:8])
            )
        ) from error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def process_description(process: subprocess.CompletedProcess[str]) -> str:
    return "exit={} stdout={!r} stderr={!r}".format(
        process.returncode, process.stdout, process.stderr
    )


def compile_object(cxx: str, source: Path, output: Path, cwd: Path) -> None:
    compiler_name = Path(cxx).name.lower()
    if compiler_name in ("cl", "cl.exe"):
        command = [cxx, "/nologo", "/c", str(source), "/Fo" + str(output)]
    else:
        command = [cxx, "-std=c++17", "-c", str(source), "-o", str(output)]
    compiled = run(command, cwd)
    require(
        compiled.returncode == 0 and output.is_file(),
        "host fixture compilation failed: " + process_description(compiled),
    )


def assert_no_staging(root: Path, description: str) -> None:
    leftovers = sorted(path.name for path in root.iterdir() if path.name.startswith(".ny-link-"))
    require(not leftovers, "{} leaked private staging: {}".format(description, leftovers))


def reject_preserving_output(
    command: list[str],
    *,
    root: Path,
    output: Path,
    sentinel: bytes,
    description: str,
    timeout: float = 30.0,
    diagnostic_fragments: tuple[str, ...] = (),
) -> subprocess.CompletedProcess[str]:
    output.write_bytes(sentinel)
    process = run(command, root, timeout=timeout)
    require(
        process.returncode != 0,
        description + " was accepted: " + process_description(process),
    )
    require(
        bool(process.stderr.strip()),
        description
        + " did not report a diagnostic: "
        + process_description(process),
    )
    if diagnostic_fragments:
        diagnostic = process.stderr.lower()
        require(
            any(fragment.lower() in diagnostic for fragment in diagnostic_fragments),
            description
            + " reported an unrelated diagnostic: "
            + process_description(process),
        )
    require(
        output.read_bytes() == sentinel,
        description + " replaced the caller's output",
    )
    assert_no_staging(root, description)
    return process


def sparse_prefixed_file(path: Path, prefix: bytes, size: int) -> None:
    require(size >= len(prefix), "sparse fixture is smaller than its prefix")
    with path.open("wb") as output:
        if os.name == "nt":
            import ctypes
            import msvcrt

            bytes_returned = ctypes.c_ulong()
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            marked_sparse = kernel32.DeviceIoControl(
                ctypes.c_void_p(msvcrt.get_osfhandle(output.fileno())),
                0x000900C4,  # FSCTL_SET_SPARSE
                None,
                0,
                None,
                0,
                ctypes.byref(bytes_returned),
                None,
            )
            if not marked_sparse:
                raise SparseFilesUnavailable(
                    "Windows volume rejected FSCTL_SET_SPARSE with error {}".format(
                        ctypes.get_last_error()
                    )
                )
        output.write(prefix)
        output.truncate(size)
    require(
        path.stat().st_size == size,
        "sparse fixture has the wrong logical size",
    )


def sparse_object(source: Path, output: Path, size: int) -> None:
    with source.open("rb") as input_file:
        header = input_file.read(64)
    require(len(header) >= 20, "compiled host fixture has no usable object header")
    sparse_prefixed_file(output, header, size)


def verify_program(executable: Path, root: Path) -> None:
    require(executable.is_file() and not executable.is_symlink(), "linked output is not a regular file")
    if os.name == "posix":
        require(
            stat.S_IMODE(executable.stat().st_mode) == 0o700,
            "linked output must have exact POSIX mode 0700",
        )
    executed = run([str(executable), "argument with spaces"], root)
    require(
        executed.returncode == 0
        and executed.stdout == "NYLINK_OK:1:argument with spaces\n"
        and not executed.stderr,
        "linked executable contract failed: " + process_description(executed),
    )


def archive_member(name: str, payload: bytes) -> bytes:
    encoded_name = name.encode("ascii")
    require(len(encoded_name) <= 16, "test archive member name is too long")
    header = (
        encoded_name.ljust(16, b" ")
        + b"0".ljust(12, b" ")
        + b"0".ljust(6, b" ")
        + b"0".ljust(6, b" ")
        + b"100644".ljust(8, b" ")
        + str(len(payload)).encode("ascii").ljust(10, b" ")
        + b"`\n"
    )
    require(len(header) == 60, "test archive member header has the wrong size")
    return header + payload + (b"\n" if len(payload) & 1 else b"")


def invalid_runtime_archives(root: Path) -> list[Path]:
    empty = root / "empty-runtime.a"
    empty.write_bytes(b"")

    magic_only = root / "magic-only-runtime.a"
    magic_only.write_bytes(b"!<arch>\n")

    no_index = root / "unindexed-runtime.a"
    no_index.write_bytes(b"!<arch>\n" + archive_member("fake.o/", b"x"))

    # A symbol index is not evidence of definitions unless every referenced
    # member offset resolves to a real non-metadata archive member.
    names = b"nyrt_abi_version\0nyrt_context_begin\0nyrt_context_end\0"
    forged_index = struct.pack(">I", 3) + struct.pack(">III", 999, 999, 999) + names
    forged = root / "forged-index-runtime.a"
    forged.write_bytes(
        b"!<arch>\n"
        + archive_member("/", forged_index)
        + archive_member("fake.o/", b"x")
    )
    return [empty, magic_only, no_index, forged]


def verify_default_layout(
    *,
    label: str,
    linker: Path,
    runtime: Path,
    linker_directory: Path,
    runtime_directory: Path,
    object_files: list[Path],
    root: Path,
    invalid_shadow: bool = False,
) -> None:
    linker_directory.mkdir(parents=True, exist_ok=True)
    runtime_directory.mkdir(parents=True, exist_ok=True)
    copied_linker = linker_directory / linker.name
    shutil.copy2(linker, copied_linker)
    shutil.copy2(runtime, runtime_directory / runtime.name)
    if invalid_shadow:
        shadow_directory = linker_directory / "nillib" / "native"
        shadow_directory.mkdir(parents=True, exist_ok=True)
        (shadow_directory / runtime.name).write_bytes(b"!<arch>\n")
    requested_output = root / (label + " program")
    expected_output = (
        requested_output.with_suffix(".exe") if os.name == "nt" else requested_output
    )
    linked = run(
        [
            str(copied_linker),
            "-o",
            str(requested_output),
            *(str(path) for path in object_files),
        ],
        root,
    )
    require(
        linked.returncode == 0,
        label + " runtime discovery failed: " + process_description(linked),
    )
    verify_program(expected_output, root)
    if os.name == "nt":
        require(
            not requested_output.exists(),
            "Windows extensionless -o unexpectedly published an extensionless PE",
        )
    assert_no_staging(root, label + " runtime discovery")


def check_cli(linker: Path, root: Path) -> None:
    help_process = run([str(linker), "--help"], root)
    require(
        help_process.returncode == 0
        and "Usage: nillinker" in help_process.stdout
        and "--runtime" in help_process.stdout
        and "--driver-timeout-ms" in help_process.stdout
        and not help_process.stderr,
        "--help contract failed: " + process_description(help_process),
    )
    version = run([str(linker), "--version"], root)
    require(
        version.returncode == 0
        and version.stdout.startswith("nillinker ")
        and not version.stderr,
        "--version contract failed: " + process_description(version),
    )
    for command, fragment in (
        ([str(linker)], "at least one host object"),
        ([str(linker), "--unknown"], "unknown option"),
        ([str(linker), "-o"], "requires a path argument"),
        ([str(linker), "--help", "extra"], "must be used by itself"),
        ([str(linker), "-o", "one", "--output", "two", "object"], "more than once"),
    ):
        rejected = run(command, root)
        require(
            rejected.returncode == 2
            and fragment in rejected.stderr
            and "Usage: nillinker" in rejected.stderr,
            "strict CLI rejection failed for {}: {}".format(
                command, process_description(rejected)
            ),
        )

    timeout_output = root / (
        "timeout CLI sentinel.exe" if os.name == "nt" else "timeout CLI sentinel"
    )
    timeout_sentinel = b"invalid timeout options must preserve this output\n"
    for options, fragment, label in (
        (["--driver-timeout-ms", "99"], "100 through 3600000", "below minimum"),
        (
            ["--driver-timeout-ms=3600001"],
            "100 through 3600000",
            "above maximum",
        ),
        (
            ["--driver-timeout-ms", "soon"],
            "100 through 3600000",
            "nonnumeric",
        ),
        (
            [
                "--driver-timeout-ms=100",
                "--driver-timeout-ms",
                "200",
            ],
            "more than once",
            "duplicate",
        ),
    ):
        rejected = reject_preserving_output(
            [
                str(linker),
                "-o",
                str(timeout_output),
                *options,
                "object",
            ],
            root=root,
            output=timeout_output,
            sentinel=timeout_sentinel,
            description="{} driver-timeout option rejection".format(label),
            diagnostic_fragments=(fragment,),
        )
        require(
            rejected.returncode == 2 and "Usage: nillinker" in rejected.stderr,
            "{} driver-timeout option was not a CLI usage error: {}".format(
                label, process_description(rejected)
            ),
        )


def main() -> int:
    args = arguments()
    linker = args.linker.resolve(strict=True)
    timeout_linker = args.timeout_linker.resolve(strict=True)
    runtime = args.runtime.resolve(strict=True)
    args.work_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="nillinker-host-", dir=args.work_dir) as temporary:
        root = Path(temporary)
        check_cli(linker, root)

        main_source = root / "main source.cpp"
        helper_source = root / "helper source.cpp"
        main_object = root / "main object.o"
        helper_object = root / "helper object.o"
        if os.name == "nt":
            main_object = main_object.with_suffix(".obj")
            helper_object = helper_object.with_suffix(".obj")
        main_source.write_text(
            """#include <cstdio>
extern "C" int nyrt_abi_version();
extern "C" int ny_link_helper();

int main(int argc, char **argv) {
    if (nyrt_abi_version() != 1 || ny_link_helper() != 37) {
        return 91;
    }
    std::printf("NYLINK_OK:%d:%s\\n", argc - 1,
                argc > 1 ? argv[1] : "missing");
    return 0;
}
""",
            encoding="utf-8",
        )
        helper_source.write_text(
            'extern "C" int ny_link_helper() { return 37; }\n', encoding="utf-8"
        )
        compile_object(args.cxx, main_source, main_object, root)
        compile_object(args.cxx, helper_source, helper_object, root)

        output = root / ("linked program.exe" if os.name == "nt" else "linked program")
        victim = root / "symlink victim"
        victim_bytes = b"successful publication must not follow this symlink\n"
        if os.name == "posix":
            victim.write_bytes(victim_bytes)
            output.symlink_to(victim.name)
        else:
            output.write_bytes(victim_bytes)
        linked = run(
            [
                str(linker),
                "--runtime",
                str(runtime),
                "-o",
                str(output),
                str(main_object),
                str(helper_object),
            ],
            root,
        )
        require(
            linked.returncode == 0 and "Linked 2 host objects" in linked.stdout and not linked.stderr,
            "explicit-runtime link failed: " + process_description(linked),
        )
        verify_program(output, root)
        if os.name == "posix":
            require(victim.read_bytes() == victim_bytes, "publication followed the output symlink")
        assert_no_staging(root, "successful explicit-runtime link")

        unicode_root = root / "Unicode 路径 δοκιμή"
        unicode_object_directory = unicode_root / "objects 对象"
        unicode_runtime_directory = unicode_root / "runtime 运行时"
        unicode_output_directory = unicode_root / "output 输出"
        unicode_object_directory.mkdir(parents=True)
        unicode_runtime_directory.mkdir()
        unicode_output_directory.mkdir()
        object_suffix = ".obj" if os.name == "nt" else ".o"
        unicode_main_object = unicode_object_directory / ("主程序" + object_suffix)
        unicode_helper_object = unicode_object_directory / ("βοηθός" + object_suffix)
        shutil.copy2(main_object, unicode_main_object)
        shutil.copy2(helper_object, unicode_helper_object)
        shutil.copy2(runtime, unicode_runtime_directory / runtime.name)
        unicode_output = unicode_output_directory / (
            "程序 αποτέλεσμα.exe" if os.name == "nt" else "程序 αποτέλεσμα"
        )
        unicode_link = run(
            [
                str(linker),
                "--runtime",
                str(unicode_runtime_directory),
                "--output=" + str(unicode_output),
                str(unicode_main_object),
                str(unicode_helper_object),
            ],
            unicode_root,
        )
        require(
            unicode_link.returncode == 0
            and "Linked 2 host objects" in unicode_link.stdout
            and str(unicode_output) in unicode_link.stdout
            and not unicode_link.stderr,
            "Unicode object/runtime/output path link failed: "
            + process_description(unicode_link),
        )
        verify_program(unicode_output, unicode_root)
        assert_no_staging(unicode_output_directory, "Unicode path link")

        actual_layout_output = root / (
            "actual build layout.exe" if os.name == "nt" else "actual build layout"
        )
        actual_layout_link = run(
            [
                str(linker),
                "-o",
                str(actual_layout_output),
                str(main_object),
                str(helper_object),
            ],
            root,
        )
        require(
            actual_layout_link.returncode == 0,
            "linker's own runtime layout was not discoverable: "
            + process_description(actual_layout_link),
        )
        verify_program(actual_layout_output, root)
        assert_no_staging(root, "actual linker/runtime layout")

        explicit_sdk = root / "explicit sdk"
        explicit_runtime_dir = explicit_sdk / "nillib" / "native"
        explicit_runtime_dir.mkdir(parents=True)
        shutil.copy2(runtime, explicit_runtime_dir / runtime.name)
        sdk_output = root / ("sdk path program.exe" if os.name == "nt" else "sdk path program")
        sdk_link = run(
            [
                str(linker),
                "--runtime=" + str(explicit_sdk),
                "--output=" + str(sdk_output),
                str(main_object),
                str(helper_object),
            ],
            root,
        )
        require(
            sdk_link.returncode == 0,
            "SDK-directory runtime resolution failed: " + process_description(sdk_link),
        )
        verify_program(sdk_output, root)
        assert_no_staging(root, "SDK-directory runtime link")

        copied_sdk = root / "copied sdk"
        copied_runtime_dir = copied_sdk / "nillib" / "native"
        copied_runtime_dir.mkdir(parents=True)
        copied_linker = copied_sdk / linker.name
        shutil.copy2(linker, copied_linker)
        shutil.copy2(runtime, copied_runtime_dir / runtime.name)
        default_link = run(
            [str(copied_linker), str(main_object), str(helper_object)], copied_sdk
        )
        default_output = copied_sdk / ("a.exe" if os.name == "nt" else "a.out")
        require(
            default_link.returncode == 0,
            "adjacent SDK runtime discovery failed: " + process_description(default_link),
        )
        verify_program(default_output, copied_sdk)
        assert_no_staging(copied_sdk, "default-output SDK link")

        build_layout = root / "build layout"
        verify_default_layout(
            label="build bin-lib",
            linker=linker,
            runtime=runtime,
            linker_directory=build_layout / "bin",
            runtime_directory=build_layout / "lib",
            object_files=[main_object, helper_object],
            root=root,
            invalid_shadow=True,
        )

        configured_libdir = Path(args.install_libdir)
        if configured_libdir.is_absolute() or ".." in configured_libdir.parts:
            # An absolute install libdir cannot be recreated under this isolated
            # prefix. The ordinary prefix/lib fallback remains testable.
            configured_libdir = Path("lib")
        install_layout = root / "install layout"
        verify_default_layout(
            label="conventional install",
            linker=linker,
            runtime=runtime,
            linker_directory=install_layout / "bin",
            runtime_directory=install_layout / configured_libdir,
            object_files=[main_object, helper_object],
            root=root,
        )

        multi_layout = root / "multi-config layout"
        verify_default_layout(
            label="multi-config bin-lib",
            linker=linker,
            runtime=runtime,
            linker_directory=multi_layout / "bin" / "RelWithDebInfo",
            runtime_directory=multi_layout / "lib" / "RelWithDebInfo",
            object_files=[main_object, helper_object],
            root=root,
        )

        invalid_object = root / "not an object.o"
        invalid_object.write_text("not a native object\n", encoding="utf-8")
        preserved_output = root / (
            "preserved output.exe" if os.name == "nt" else "preserved output"
        )
        preserved_bytes = b"failed links must preserve the caller's output\n"

        timed_out_link = reject_preserving_output(
            [
                str(timeout_linker),
                "--driver-timeout-ms=100",
                "--runtime",
                str(runtime),
                "-o",
                str(preserved_output),
                str(main_object),
                str(helper_object),
            ],
            root=root,
            output=preserved_output,
            sentinel=preserved_bytes,
            description="native linker-driver timeout",
            timeout=3.0,
            diagnostic_fragments=("timed out", "deadline"),
        )
        require(
            "100" in timed_out_link.stderr,
            "native linker-driver timeout omitted the enforced duration: "
            + process_description(timed_out_link),
        )

        too_many_objects = reject_preserving_output(
            [
                str(linker),
                "--runtime",
                str(runtime),
                "-o",
                str(preserved_output),
                *(["o"] * 4097),
            ],
            root=root,
            output=preserved_output,
            sentinel=preserved_bytes,
            description="host-object count rejection",
            diagnostic_fragments=("4096", "too many host object", "object limit"),
        )
        require(
            too_many_objects.returncode == 2
            and "does not exist" not in too_many_objects.stderr.lower()
            and "regular file" not in too_many_objects.stderr.lower(),
            "host-object count was not rejected as a CLI usage error before file access: "
            + process_description(too_many_objects),
        )

        maximum_object_bytes = 512 * 1024 * 1024
        oversized_object = root / ("large.obj" if os.name == "nt" else "large.o")
        try:
            sparse_object(main_object, oversized_object, maximum_object_bytes + 1)
        except SparseFilesUnavailable as error:
            oversized_object.unlink(missing_ok=True)
            print("SKIP: sparse linker-limit fixtures: {}".format(error))
        else:
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime),
                    "-o",
                    str(preserved_output),
                    str(oversized_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="oversized host-object rejection",
                diagnostic_fragments=("512 mib", "536870912", "size limit", "exceeds"),
            )

            aggregate_objects: list[Path] = []
            for index in range(8):
                aggregate_object = root / (
                    "large-{}.obj".format(index)
                    if os.name == "nt"
                    else "large-{}.o".format(index)
                )
                sparse_object(main_object, aggregate_object, maximum_object_bytes)
                aggregate_objects.append(aggregate_object)
            aggregate_bytes = sum(path.stat().st_size for path in aggregate_objects)
            aggregate_bytes += helper_object.stat().st_size
            require(
                aggregate_bytes > 4 * 1024 * 1024 * 1024
                and all(
                    path.stat().st_size <= maximum_object_bytes
                    for path in aggregate_objects
                ),
                "aggregate fixture does not cross only the aggregate boundary",
            )
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime),
                    "-o",
                    str(preserved_output),
                    *(str(path) for path in aggregate_objects),
                    str(helper_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="aggregate host-object size rejection",
                diagnostic_fragments=("aggregate", "4 gib", "combined", "total object"),
            )

        object_directory = root / "object directory"
        object_directory.mkdir()
        reject_preserving_output(
            [
                str(linker),
                "--runtime",
                str(runtime),
                "-o",
                str(preserved_output),
                str(object_directory),
            ],
            root=root,
            output=preserved_output,
            sentinel=preserved_bytes,
            description="host-object directory rejection",
            diagnostic_fragments=("directory", "regular file", "secure"),
        )

        object_symlink = root / (
            "object symlink.obj" if os.name == "nt" else "object symlink.o"
        )
        try:
            object_symlink.symlink_to(main_object)
        except (NotImplementedError, OSError):
            if os.name == "posix":
                raise
        else:
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime),
                    "-o",
                    str(preserved_output),
                    str(object_symlink),
                    str(helper_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="host-object symlink rejection",
                diagnostic_fragments=("symlink", "secure", "regular file"),
            )

        if os.name == "posix" and hasattr(os, "mkfifo"):
            object_fifo = root / "object fifo.o"
            os.mkfifo(object_fifo)
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime),
                    "-o",
                    str(preserved_output),
                    str(object_fifo),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="host-object FIFO rejection",
                timeout=3.0,
                diagnostic_fragments=("fifo", "regular file", "secure", "special file"),
            )

        runtime_symlink = root / (
            "runtime symlink.lib" if os.name == "nt" else "runtime symlink.a"
        )
        try:
            runtime_symlink.symlink_to(runtime)
        except (NotImplementedError, OSError):
            if os.name == "posix":
                raise
        else:
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime_symlink),
                    "-o",
                    str(preserved_output),
                    str(main_object),
                    str(helper_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="runtime symlink rejection",
                diagnostic_fragments=("symlink", "secure", "regular file"),
            )

        if os.name == "posix" and hasattr(os, "mkfifo"):
            runtime_fifo = root / "runtime fifo.a"
            os.mkfifo(runtime_fifo)
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(runtime_fifo),
                    "-o",
                    str(preserved_output),
                    str(main_object),
                    str(helper_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="runtime FIFO rejection",
                timeout=3.0,
                diagnostic_fragments=(
                    "fifo",
                    "regular file",
                    "secure",
                    "neither an archive",
                    "special file",
                ),
            )

        oversized_runtime = root / (
            "oversized runtime.lib" if os.name == "nt" else "oversized runtime.a"
        )
        maximum_runtime_bytes = 512 * 1024 * 1024
        try:
            sparse_prefixed_file(
                oversized_runtime, b"!<arch>\n", maximum_runtime_bytes + 1
            )
        except SparseFilesUnavailable as error:
            oversized_runtime.unlink(missing_ok=True)
            print("SKIP: sparse runtime-limit fixture: {}".format(error))
        else:
            reject_preserving_output(
                [
                    str(linker),
                    "--runtime",
                    str(oversized_runtime),
                    "-o",
                    str(preserved_output),
                    str(main_object),
                    str(helper_object),
                ],
                root=root,
                output=preserved_output,
                sentinel=preserved_bytes,
                description="oversized runtime rejection",
                diagnostic_fragments=(
                    "512 mib",
                    "536870912",
                    "size limit",
                    "exceeds",
                ),
            )

        preserved_output.write_bytes(preserved_bytes)
        malformed = run(
            [
                str(linker),
                "--runtime",
                str(runtime),
                "-o",
                str(preserved_output),
                str(invalid_object),
            ],
            root,
        )
        require(
            malformed.returncode != 0
            and "object" in malformed.stderr.lower()
            and preserved_output.read_bytes() == preserved_bytes,
            "malformed input was not rejected safely: " + process_description(malformed),
        )
        assert_no_staging(root, "malformed-object rejection")

        for invalid_runtime in invalid_runtime_archives(root):
            preserved_output.write_bytes(preserved_bytes)
            rejected_runtime = run(
                [
                    str(linker),
                    "--runtime",
                    str(invalid_runtime),
                    "-o",
                    str(preserved_output),
                    str(main_object),
                    str(helper_object),
                ],
                root,
            )
            require(
                rejected_runtime.returncode != 0
                and "runtime archive is invalid" in rejected_runtime.stderr.lower()
                and preserved_output.read_bytes() == preserved_bytes,
                "invalid runtime was not rejected safely for {}: {}".format(
                    invalid_runtime.name, process_description(rejected_runtime)
                ),
            )
            assert_no_staging(root, invalid_runtime.name + " rejection")

        unresolved_source = root / "unresolved.cpp"
        unresolved_object = root / ("unresolved.obj" if os.name == "nt" else "unresolved.o")
        unresolved_source.write_text(
            'extern "C" int missing_symbol();\nint main() { return missing_symbol(); }\n',
            encoding="utf-8",
        )
        compile_object(args.cxx, unresolved_source, unresolved_object, root)
        unresolved = run(
            [
                str(linker),
                "--runtime",
                str(runtime),
                "-o",
                str(preserved_output),
                str(unresolved_object),
            ],
            root,
        )
        require(
            unresolved.returncode != 0
            and "linker driver failed" in unresolved.stderr
            and preserved_output.read_bytes() == preserved_bytes,
            "link failure did not preserve output: " + process_description(unresolved),
        )
        assert_no_staging(root, "unresolved-symbol rejection")

        missing_runtime = run(
            [
                str(linker),
                "--runtime",
                str(root / "missing runtime"),
                "-o",
                str(preserved_output),
                str(main_object),
                str(helper_object),
            ],
            root,
        )
        require(
            missing_runtime.returncode != 0
            and "runtime" in missing_runtime.stderr.lower()
            and preserved_output.read_bytes() == preserved_bytes,
            "missing runtime did not fail safely: " + process_description(missing_runtime),
        )

        legacy_output = root / "legacy positional output"
        legacy = run(
            [
                str(linker),
                "--runtime",
                str(runtime),
                str(main_object),
                str(helper_object),
                str(legacy_output),
            ],
            root,
        )
        require(
            legacy.returncode != 0
            and "positional output paths are unsupported" in legacy.stderr
            and not legacy_output.exists(),
            "legacy positional output was not rejected: " + process_description(legacy),
        )

    print("nillinker host-object regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
