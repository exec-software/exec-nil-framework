#!/usr/bin/env python3
"""End-to-end contract tests for the real Ny native object assembler."""

from __future__ import annotations

import argparse
import os
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def legacy_nil_v1(body: list[tuple[str, list[str]]]) -> bytes:
    """A genuine old executable layout, independent of today's v3 writer."""
    def word(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        encoded = value.encode("utf-8")
        return word(len(encoded)) + encoded

    function = (word(1) + string("main") + word(0) + word(0) + string("i32")
                + word(len(body)))
    for opcode, operands in body:
        function += (string(opcode) + word(len(operands))
                     + b"".join(string(value) for value in operands) + word(1))
    sections = [word(0), function, word(0), word(0), string("legacy") + word(0)]
    header = b"NIL\0" + word(1) + word(0) + word(5)
    offset = 116
    for section_type, section in enumerate(sections, 1):
        header += struct.pack("<IQQ", section_type, offset, len(section))
        offset += len(section)
    return header + b"".join(sections)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assembler", required=True, type=Path)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--linker-driver", required=True)
    return parser.parse_args()


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
    )


def expect_failure(process: subprocess.CompletedProcess[str], label: str) -> None:
    if process.returncode == 0:
        raise AssertionError(
            f"{label} unexpectedly succeeded: stdout={process.stdout!r} "
            f"stderr={process.stderr!r}"
        )


def compile_nil(
    compiler: Path, source: Path, output: Path, *, binary: bool, library: bool = False
) -> None:
    command = [
        str(compiler),
        "--no-stdlib",
        "--no-bundle",
        "--quiet",
        "--output",
        str(output),
    ]
    if binary:
        command.append("--emit-binary-nil")
    if library:
        command.append("--emit-library")
    command.append(str(source))
    process = run(command, source.parent)
    if process.returncode != 0 or process.stdout or process.stderr or not output.is_file():
        raise AssertionError(
            f"NIL fixture compilation failed: exit={process.returncode} "
            f"stdout={process.stdout!r} stderr={process.stderr!r}"
        )


def assert_real_object(path: Path) -> None:
    payload = path.read_bytes()
    if len(payload) < 64:
        raise AssertionError(f"object is implausibly small: {len(payload)} bytes")
    if payload.startswith((b"NYOBJ", b"NIL\0", b"MIL\0")):
        raise AssertionError("assembler published a prototype or NIL container")
    if sys.platform.startswith("linux") and not payload.startswith(b"\x7fELF"):
        raise AssertionError("Linux assembler output is not an ELF object")
    if sys.platform == "darwin" and payload[:4] not in {
        b"\xcf\xfa\xed\xfe",
        b"\xce\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xfe\xed\xfa\xce",
    }:
        raise AssertionError("macOS assembler output is not a Mach-O object")


def link_and_run(
    linker_driver: str,
    runtime: Path,
    object_path: Path,
    executable: Path,
    cwd: Path,
    expected_stdout: str = "nilassembler route\n",
) -> None:
    if os.name == "nt":
        return
    command = [linker_driver]
    if sys.platform.startswith("linux"):
        command.append("-no-pie")
    elif sys.platform == "darwin":
        command.append("-Wl,-no_pie")
    command.extend(["-o", str(executable), str(object_path), str(runtime)])
    linked = run(command, cwd)
    if linked.returncode != 0:
        raise AssertionError(
            f"host linker rejected assembler output: stdout={linked.stdout!r} "
            f"stderr={linked.stderr!r}"
        )
    executed = run([str(executable)], cwd)
    if executed.returncode != 0 or executed.stdout != expected_stdout or executed.stderr:
        raise AssertionError(
            f"linked assembler route failed: exit={executed.returncode} "
            f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
        )


def assert_no_staging_leaks(root: Path) -> None:
    leaks = sorted(
        path.name
        for path in root.iterdir()
        if path.name.startswith((".nilassembler-", ".ny-write-"))
    )
    if leaks:
        raise AssertionError(f"nilassembler leaked private staging paths: {leaks}")


def assert_alias_rejected(
    assembler: Path, input_path: Path, output_path: Path, cwd: Path, label: str
) -> None:
    observed_input = input_path if input_path.is_absolute() else cwd / input_path
    observed_output = output_path if output_path.is_absolute() else cwd / output_path
    input_bytes = observed_input.read_bytes()
    output_bytes = observed_output.read_bytes()
    input_stat = observed_input.stat()
    output_stat = observed_output.stat()
    process = run([str(assembler), "-o", str(output_path), str(input_path)], cwd)
    expect_failure(process, label)
    if "input and output paths must differ" not in process.stderr:
        raise AssertionError(
            f"{label} did not identify the input/output alias: {process.stderr!r}"
        )
    if (
        observed_input.read_bytes() != input_bytes
        or observed_output.read_bytes() != output_bytes
    ):
        raise AssertionError(f"{label} modified the input or previous output bytes")
    for path, before in ((observed_input, input_stat), (observed_output, output_stat)):
        after = path.stat()
        if (after.st_dev, after.st_ino) != (before.st_dev, before.st_ino):
            raise AssertionError(f"{label} replaced an existing filesystem entry")


def main() -> int:
    args = arguments()
    assembler = args.assembler.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    runtime = args.runtime.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="nilassembler-test-") as temporary:
        root = Path(temporary)
        source = root / "route.ny"
        source.write_text(
            "module assembler_route\n\n"
            "fn main() -> i32\n"
            "    println(\"nilassembler route\")\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        text_nil = root / "route-text.nil"
        binary_nil = root / "route-binary.nil"
        library_nil = root / "route-library.nil"
        compile_nil(compiler, source, text_nil, binary=False)
        compile_nil(compiler, source, binary_nil, binary=True)
        compile_nil(compiler, source, library_nil, binary=True, library=True)

        typed_struct_source = root / "typed-struct-route.ny"
        typed_struct_source.write_text(
            "module assembler_typed_struct\n\n"
            "struct branch_record\n"
            "    value: i32\n"
            "end struct\n\n"
            "fn choose(flag: bool) -> branch_record\n"
            "    if flag then\n"
            "        return branch_record { value: 21 }\n"
            "    else\n"
            "        return branch_record { value: 9 }\n"
            "    end if\n"
            "end fn\n\n"
            "fn main() -> i32\n"
            "    let selected = choose(true)\n"
            "    println(selected.value)\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        typed_struct_text = root / "typed-struct-text.nil"
        typed_struct_binary = root / "typed-struct-binary.nil"
        compile_nil(compiler, typed_struct_source, typed_struct_text, binary=False)
        compile_nil(compiler, typed_struct_source, typed_struct_binary, binary=True)

        annotation_source = root / "annotation-route.ny"
        annotation_source.write_text(
            "module assembler_annotation\n\n"
            "@intrinsic(\"console.writeln\")\n"
            "fn out(value: text)\n"
            "end fn\n\n"
            "fn main() -> i32\n"
            "    out(\"annotation route\")\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        annotation_text = root / "annotation-text.nil"
        annotation_binary = root / "annotation-binary.nil"
        compile_nil(compiler, annotation_source, annotation_text, binary=False)
        compile_nil(compiler, annotation_source, annotation_binary, binary=True)

        typed_branch_source = root / "typed-branch-route.ny"
        typed_branch_source.write_text(
            "module assembler_typed_branch\n\n"
            "fn choose(flag: bool) -> text\n"
            "    if flag then\n"
            "        return \"typed branch\"\n"
            "    else\n"
            "        return \"wrong branch\"\n"
            "    end if\n"
            "end fn\n\n"
            "fn main() -> i32\n"
            "    println(choose(true))\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        typed_branch_text = root / "typed-branch-text.nil"
        typed_branch_binary = root / "typed-branch-binary.nil"
        compile_nil(compiler, typed_branch_source, typed_branch_text, binary=False)
        compile_nil(compiler, typed_branch_source, typed_branch_binary, binary=True)

        dead_metadata_source = root / "dead-metadata-route.ny"
        dead_metadata_source.write_text(
            "module assembler_dead_metadata\n\n"
            "enum direction\n"
            "    left\n"
            "    right\n"
            "end enum\n\n"
            "impl direction\n"
            "    fn reverse(self) -> direction\n"
            "        match self\n"
            "            direction.left -> return direction.right\n"
            "            direction.right -> return direction.left\n"
            "        end match\n"
            "    end fn\n"
            "\n"
            "    fn borrow(self: ref<Self>) -> ref<Self>\n"
            "        return self\n"
            "    end fn\n"
            "end impl\n\n"
            "fn main() -> i32\n"
            "    println(\"dead metadata\")\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        dead_metadata_text = root / "dead-metadata-text.nil"
        dead_metadata_binary = root / "dead-metadata-binary.nil"
        compile_nil(compiler, dead_metadata_source, dead_metadata_text, binary=False)
        compile_nil(
            compiler, dead_metadata_source, dead_metadata_binary, binary=True
        )
        dead_metadata_nil = dead_metadata_text.read_text(encoding="utf-8")
        expected_receiver = (
            "function assembler_dead_metadata__direction__reverse("
            "self: assembler_dead_metadata::direction) -> assembler_dead_metadata::direction:"
        )
        expected_nested_self = (
            "function assembler_dead_metadata__direction__borrow("
            "self: ref<assembler_dead_metadata::direction>) -> "
            "ref<assembler_dead_metadata::direction>:"
        )
        if expected_receiver not in dead_metadata_nil:
            raise AssertionError("implicit self lacked an exact serialized NIL type")
        if expected_nested_self not in dead_metadata_nil:
            raise AssertionError("nested Self types were not recursively substituted")

        builtin_collision_source = root / "builtin-collision-route.ny"
        builtin_collision_source.write_text(
            "struct hidden_record\n"
            "    value: i64\n"
            "end struct\n\n"
            "fn alloc(size: i64) -> hidden_record\n"
            "    return hidden_record { value: size }\n"
            "end fn\n\n"
            "fn main() -> i32\n"
            "    alloc(8)\n"
            "    println(\"builtin collision\")\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        builtin_collision_text = root / "builtin-collision-text.nil"
        builtin_collision_binary = root / "builtin-collision-binary.nil"
        compile_nil(
            compiler, builtin_collision_source, builtin_collision_text, binary=False
        )
        compile_nil(
            compiler, builtin_collision_source, builtin_collision_binary, binary=True
        )
        collision_nil = builtin_collision_text.read_text(encoding="utf-8")
        if "function alloc(size: i64) -> hidden_record:" not in collision_nil:
            raise AssertionError("builtin collision fixture did not retain alloc")
        if "NEW_STRUCT   hidden_record" not in collision_nil:
            raise AssertionError(
                "builtin collision fixture did not contain schema-dependent dead code"
            )

        generic_ref_source = root / "generic-ref-boundary.ny"
        generic_ref_source.write_text(
            "struct holder<T>\n"
            "    value: T\n"
            "end struct\n\n"
            "impl<T> holder<T>\n"
            "    fn get(self: ref<Self>) -> T\n"
            "        return self.value\n"
            "    end fn\n"
            "end impl\n\n"
            "fn main() -> i32\n"
            "    let item: holder<i32> = holder<i32> { value: 42 }\n"
            "    println(item.get())\n"
            "    return 0\n"
            "end fn\n",
            encoding="utf-8",
        )
        generic_ref_text = root / "generic-ref-boundary.nil"
        compile_nil(compiler, generic_ref_source, generic_ref_text, binary=False)
        generic_ref_binary = root / "generic-ref-binary.nil"
        compile_nil(compiler, generic_ref_source, generic_ref_binary, binary=True)

        help_process = run([str(assembler), "--help"], root)
        if help_process.returncode != 0 or "Usage: nilassembler" not in help_process.stdout:
            raise AssertionError("--help did not provide the canonical usage")
        version_process = run([str(assembler), "--version"], root)
        if version_process.returncode != 0 or not version_process.stdout.startswith(
            "nilassembler "
        ):
            raise AssertionError("--version did not identify nilassembler")

        for command, label in (
            ([str(assembler)], "missing input"),
            ([str(assembler), "--unknown"], "unknown option"),
            ([str(assembler), str(text_nil), str(binary_nil)], "extra input"),
            ([str(assembler), "--help", str(text_nil)], "help with input"),
            ([str(assembler), "-o"], "missing -o operand"),
            (
                [str(assembler), "-o", str(root / "one.o"), "-o", str(root / "two.o"), str(text_nil)],
                "duplicate output option",
            ),
            ([str(assembler), "--unmanaged", "--unmanaged", str(text_nil)], "duplicate unmanaged option"),
        ):
            process = run(command, root)
            expect_failure(process, label)

        default_process = run([str(assembler), str(text_nil)], root)
        if "no LLVM backend" in default_process.stderr:
            expect_failure(default_process, "LLVM-disabled capability path")
            if text_nil.with_suffix(".o" if os.name != "nt" else ".obj").exists():
                raise AssertionError("LLVM-disabled assembler published an object")
            assert_no_staging_leaks(root)
            return 0
        if default_process.returncode != 0:
            raise AssertionError(
                f"textual NIL assembly failed: stdout={default_process.stdout!r} "
                f"stderr={default_process.stderr!r}"
            )
        text_object = text_nil.with_suffix(".o" if os.name != "nt" else ".obj")
        assert_real_object(text_object)

        binary_object = root / ("binary.obj" if os.name == "nt" else "binary.o")
        binary_process = run(
            [str(assembler), "-o", str(binary_object), str(binary_nil)], root
        )
        if binary_process.returncode != 0 or binary_process.stderr:
            raise AssertionError(
                f"binary NIL assembly failed: stdout={binary_process.stdout!r} "
                f"stderr={binary_process.stderr!r}"
            )
        assert_real_object(binary_object)
        if os.name != "nt" and stat.S_IMODE(binary_object.stat().st_mode) != 0o600:
            raise AssertionError("atomically published object is not mode 0600")

        unmanaged_object = root / ("unmanaged.obj" if os.name == "nt" else "unmanaged.o")
        unmanaged = run(
            [
                str(assembler),
                "--unmanaged",
                "--output",
                str(unmanaged_object),
                str(text_nil),
            ],
            root,
        )
        if unmanaged.returncode != 0 or not unmanaged_object.is_file():
            raise AssertionError("--unmanaged did not emit a host object")

        link_and_run(
            args.linker_driver,
            runtime,
            text_object,
            root / ("text-route.exe" if os.name == "nt" else "text-route"),
            root,
        )

        typed_struct_object = root / (
            "typed-struct.obj" if os.name == "nt" else "typed-struct.o"
        )
        typed_struct_process = run(
            [str(assembler), "-o", str(typed_struct_object), str(typed_struct_text)],
            root,
        )
        if typed_struct_process.returncode != 0 or typed_struct_process.stderr:
            raise AssertionError(
                "textual struct schema / typed branch assembly failed: "
                f"stdout={typed_struct_process.stdout!r} "
                f"stderr={typed_struct_process.stderr!r}"
            )
        assert_real_object(typed_struct_object)
        link_and_run(
            args.linker_driver,
            runtime,
            typed_struct_object,
            root / ("typed-struct.exe" if os.name == "nt" else "typed-struct"),
            root,
            "21\n",
        )

        annotation_object = root / (
            "annotation.obj" if os.name == "nt" else "annotation.o"
        )
        annotation_process = run(
            [str(assembler), "-o", str(annotation_object), str(annotation_text)],
            root,
        )
        if annotation_process.returncode != 0 or annotation_process.stderr:
            raise AssertionError(
                "textual annotation-dependent assembly failed: "
                f"stdout={annotation_process.stdout!r} "
                f"stderr={annotation_process.stderr!r}"
            )
        assert_real_object(annotation_object)
        link_and_run(
            args.linker_driver,
            runtime,
            annotation_object,
            root / ("annotation.exe" if os.name == "nt" else "annotation"),
            root,
            "annotation route\n",
        )

        for typed_branch_input, suffix in (
            (typed_branch_text, "text"),
            (typed_branch_binary, "binary"),
        ):
            typed_branch_object = root / (
                f"typed-branch-{suffix}.obj"
                if os.name == "nt"
                else f"typed-branch-{suffix}.o"
            )
            typed_branch_process = run(
                [
                    str(assembler),
                    "-o",
                    str(typed_branch_object),
                    str(typed_branch_input),
                ],
                root,
            )
            if typed_branch_process.returncode != 0 or typed_branch_process.stderr:
                raise AssertionError(
                    f"{suffix} typed branch assembly failed: "
                    f"stdout={typed_branch_process.stdout!r} "
                    f"stderr={typed_branch_process.stderr!r}"
                )
            assert_real_object(typed_branch_object)
            link_and_run(
                args.linker_driver,
                runtime,
                typed_branch_object,
                root
                / (
                    f"typed-branch-{suffix}.exe"
                    if os.name == "nt"
                    else f"typed-branch-{suffix}"
                ),
                root,
                "typed branch\n",
            )
        link_and_run(
            args.linker_driver,
            runtime,
            binary_object,
            root / ("binary-route.exe" if os.name == "nt" else "binary-route"),
            root,
        )

        dead_metadata_object = root / (
            "dead-metadata.obj" if os.name == "nt" else "dead-metadata.o"
        )
        dead_metadata_process = run(
            [
                str(assembler),
                "-o",
                str(dead_metadata_object),
                str(dead_metadata_binary),
            ],
            root,
        )
        if dead_metadata_process.returncode != 0 or dead_metadata_process.stderr:
            raise AssertionError(
                "unreachable binary schema metadata constrained assembly: "
                f"stdout={dead_metadata_process.stdout!r} "
                f"stderr={dead_metadata_process.stderr!r}"
            )
        assert_real_object(dead_metadata_object)
        link_and_run(
            args.linker_driver,
            runtime,
            dead_metadata_object,
            root
            / (
                "dead-metadata.exe"
                if os.name == "nt"
                else "dead-metadata"
            ),
            root,
            "dead metadata\n",
        )

        builtin_collision_object = root / (
            "builtin-collision.obj" if os.name == "nt" else "builtin-collision.o"
        )
        builtin_collision_process = run(
            [
                str(assembler),
                "-o",
                str(builtin_collision_object),
                str(builtin_collision_binary),
            ],
            root,
        )
        if (
            builtin_collision_process.returncode != 0
            or builtin_collision_process.stderr
        ):
            raise AssertionError(
                "declared builtin-named function did not preserve its schema: "
                f"stdout={builtin_collision_process.stdout!r} "
                f"stderr={builtin_collision_process.stderr!r}"
            )
        assert_real_object(builtin_collision_object)
        link_and_run(
            args.linker_driver,
            runtime,
            builtin_collision_object,
            root
            / (
                "builtin-collision.exe"
                if os.name == "nt"
                else "builtin-collision"
            ),
            root,
            "builtin collision\n",
        )

        sentinel = root / ("sentinel.obj" if os.name == "nt" else "sentinel.o")
        sentinel.write_bytes(b"preserve-existing-object")

        assert_alias_rejected(assembler, text_nil, text_nil, root, "same input/output")
        assert_alias_rejected(
            assembler, text_nil, Path(text_nil.name), root, "relative input/output alias"
        )
        hardlink_output = root / "hardlink-output.o"
        os.link(text_nil, hardlink_output)
        assert_alias_rejected(
            assembler, text_nil, hardlink_output, root, "hard-linked input/output"
        )

        if os.name != "nt":
            physical = root / "physical"
            nested = physical / "nested"
            nested.mkdir(parents=True)
            directory_alias = root / "directory-alias"
            directory_alias.symlink_to(physical, target_is_directory=True)
            physical_input = nested / "input.nil"
            physical_input.write_bytes(text_nil.read_bytes())
            aliased_input = directory_alias / "nested" / "input.nil"
            assert_alias_rejected(
                assembler, physical_input, aliased_input, root,
                "intermediate output-directory symlink",
            )
            assert_alias_rejected(
                assembler, aliased_input, physical_input, root,
                "intermediate input-directory symlink",
            )

            parent_input = physical / "input.nil"
            parent_input.write_bytes(text_nil.read_bytes())
            apparent_parent = root / "apparent-parent"
            apparent_parent.mkdir()
            child_alias = apparent_parent / "child-alias"
            child_alias.symlink_to(nested, target_is_directory=True)
            assert_alias_rejected(
                assembler, parent_input, child_alias / ".." / "input.nil", root,
                "directory symlink followed by parent traversal",
            )

            # A lexical ".." collapse can also incorrectly reject two different
            # files. The operating system follows the symlink before its parent.
            apparent_input = apparent_parent / "input.nil"
            apparent_input.write_bytes(text_nil.read_bytes())
            distinct_parent_output = child_alias / ".." / "input.nil"
            distinct_parent_process = run(
                [str(assembler), "-o", str(distinct_parent_output), str(apparent_input)],
                root,
            )
            if distinct_parent_process.returncode != 0:
                raise AssertionError(
                    "lexically equal but distinct input/output files were rejected: "
                    f"stderr={distinct_parent_process.stderr!r}"
                )
            assert_real_object(parent_input)
            if apparent_input.read_bytes() != text_nil.read_bytes():
                raise AssertionError("parent-traversal output modified its input")

            distinct_output = directory_alias / "nested" / "distinct.o"
            distinct_process = run(
                [str(assembler), "-o", str(distinct_output), str(physical_input)], root
            )
            if distinct_process.returncode != 0:
                raise AssertionError(
                    "distinct file through an intermediate directory symlink failed: "
                    f"stderr={distinct_process.stderr!r}"
                )
            assert_real_object(distinct_output)
            if physical_input.read_bytes() != text_nil.read_bytes():
                raise AssertionError("distinct output modified its input")
            assert_no_staging_leaks(nested)
            assert_no_staging_leaks(physical)

            symlink_input = root / "symlink-input.nil"
            symlink_input.symlink_to(text_nil)
            symlink_input_process = run(
                [str(assembler), "-o", str(sentinel), str(symlink_input)], root
            )
            expect_failure(symlink_input_process, "symlink input")
            if not symlink_input.is_symlink():
                raise AssertionError("symlink input rejection changed its entry")

        if sentinel.read_bytes() != b"preserve-existing-object":
            raise AssertionError("alias/no-follow tests modified the existing output")

        for generic_input in (generic_ref_text, generic_ref_binary):
            generic_object = root / (generic_input.stem + ".o")
            generic_ref_process = run(
                [str(assembler), "-o", str(generic_object), str(generic_input)], root
            )
            if generic_ref_process.returncode or generic_ref_process.stderr:
                raise AssertionError(f"generic ref<Self> assembly failed: {generic_ref_process.stderr}")
            assert_real_object(generic_object)
            link_and_run(args.linker_driver, runtime, generic_object,
                         root / (generic_input.stem + "-run"), root, "42\n")

        library_process = run(
            [str(assembler), "-o", str(sentinel), str(library_nil)], root
        )
        expect_failure(library_process, "NIL v2 library input")
        if "NIL v2 libraries" not in library_process.stderr:
            raise AssertionError("library rejection did not identify the format boundary")
        if sentinel.read_bytes() != b"preserve-existing-object":
            raise AssertionError("library rejection modified the existing output")

        # Current executable NIL retains schemas and annotations through the
        # complete object/link/run route. Old v1 artifacts still fail closed
        # when the omitted metadata is needed.
        for binary_metadata_input, expected_output in (
            (typed_struct_binary, "21\n"),
            (annotation_binary, "annotation route\n"),
        ):
            output = root / (binary_metadata_input.stem + ".o")
            process = run([str(assembler), "-o", str(output),
                           str(binary_metadata_input)], root)
            if process.returncode or process.stderr:
                raise AssertionError(f"v3 metadata assembly failed: {process.stderr}")
            link_and_run(args.linker_driver, runtime, output,
                         root / (binary_metadata_input.stem + "-run"),
                         root, expected_output)

        legacy_struct = root / "legacy-struct.nil"
        legacy_struct.write_bytes(legacy_nil_v1([
            ("NEW_STRUCT", ["missing_schema"]), ("POP", []),
            ("PUSH_CONST", ["0"]), ("RET", [])]))
        legacy_annotation = root / "legacy-annotation.nil"
        legacy_annotation.write_bytes(legacy_nil_v1([
            ("INTEROP_CALL", ["intrinsic", "console.writeln", "0", "void"]),
            ("PUSH_CONST", ["0"]), ("RET", [])]))
        for binary_metadata_input, expected_boundary, label in (
            (
                legacy_struct,
                "binary NIL version 1 does not serialize struct/enum schema metadata",
                "binary v1 struct schema boundary",
            ),
            (
                legacy_annotation,
                "binary NIL version 1 does not serialize function annotation metadata",
                "binary v1 annotation boundary",
            ),
        ):
            boundary_process = run(
                [str(assembler), "-o", str(sentinel), str(binary_metadata_input)],
                root,
            )
            expect_failure(boundary_process, label)
            if expected_boundary not in boundary_process.stderr:
                raise AssertionError(
                    f"{label} did not explain the metadata boundary: "
                    f"stderr={boundary_process.stderr!r}"
                )
            if sentinel.read_bytes() != b"preserve-existing-object":
                raise AssertionError(f"{label} modified the existing output")

        malformed = root / "malformed.nil"
        malformed.write_text(
            "module malformed\nfunction main -> i32:\nUNKNOWN_OPCODE\nend function\n",
            encoding="utf-8",
        )
        malformed_process = run(
            [str(assembler), "-o", str(sentinel), str(malformed)], root
        )
        expect_failure(malformed_process, "malformed textual NIL")
        if sentinel.read_bytes() != b"preserve-existing-object":
            raise AssertionError("malformed NIL modified the existing output")

        truncated = root / "truncated.nil"
        truncated.write_bytes(binary_nil.read_bytes()[:-1])
        truncated_process = run(
            [str(assembler), "-o", str(sentinel), str(truncated)], root
        )
        expect_failure(truncated_process, "truncated binary NIL")
        if sentinel.read_bytes() != b"preserve-existing-object":
            raise AssertionError("truncated NIL modified the existing output")

        if os.name != "nt":
            victim = root / "victim"
            victim.write_bytes(b"victim-content")
            symlink_output = root / "symlink.o"
            symlink_output.symlink_to(victim)
            symlink_process = run(
                [str(assembler), "-o", str(symlink_output), str(text_nil)], root
            )
            expect_failure(symlink_process, "symlink output")
            if victim.read_bytes() != b"victim-content" or not symlink_output.is_symlink():
                raise AssertionError("symlink output rejection changed filesystem state")

            full_process = run(
                [str(assembler), "-o", "/dev/full", str(text_nil)], root
            )
            expect_failure(full_process, "/dev/full output")

        assert_no_staging_leaks(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
