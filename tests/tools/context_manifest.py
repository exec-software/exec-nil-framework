#!/usr/bin/env python3
"""Qualify opt-in inert context manifests across installed Ny tool boundaries."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


SOURCE = r'''module context_fixture

@ai_context("unused declaration remains visible")
fn z_unused(value: i64) -> i64
    return value
end fn

@ai_contextual("not an exact context annotation")
fn misleading() -> i32
    return 0
end fn

@ai_context("a shape on screen")
struct rectangle
    width: i64
end struct

@doc("not an intent")
struct untold
    width: i64
end struct

@ai_context("what a window is doing")
enum window_state
    Hidden
    Visible
end enum

@ai_context("a metered thing")
pub class Meter
    value: i64
    @ai_context("class method metadata")
    pub fn read(self) -> i64
        return self.value
    end fn
end class

@ai_context("entry π 😀 with \"quote\" and \\path; newline\nnext; carriage\rreturn; tab\tend")
fn main() -> i32
    let meter = Meter{value: 42}
    println(meter.read())
    return 0
end fn
'''
EXPECTED = "42\n"
SENTINEL = b"previous output must survive\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "assembler", "linker", "runtime"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    tools = {name: str(getattr(args, name).resolve(strict=True))
             for name in ("compiler", "assembler", "linker", "runtime")}
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    checks = 0
    native_executions = 0

    with tempfile.TemporaryDirectory(prefix="ny-context-manifest-") as directory:
        root = Path(directory)

        def run(command: list[str | Path]) -> subprocess.CompletedProcess[str]:
            return subprocess.run([str(value) for value in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60, check=False)

        def success(result: subprocess.CompletedProcess[str], label: str,
                    stdout: str | None = None) -> None:
            nonlocal checks
            checks += 1
            if result.returncode != 0 or result.stderr or (stdout is not None and result.stdout != stdout):
                raise AssertionError(f"{label}: {result}")

        def failure(result: subprocess.CompletedProcess[str], label: str,
                    output: Path | None = None, diagnostic: str | None = None) -> None:
            nonlocal checks
            checks += 1
            if result.returncode != 1 or (output is not None and output.read_bytes() != SENTINEL):
                raise AssertionError(f"{label}: failed to reject/preserve output: {result}")
            if diagnostic and diagnostic.lower() not in (result.stdout + result.stderr).lower():
                raise AssertionError(f"{label}: missing {diagnostic!r} diagnostic: {result}")

        def inspect(artifact: Path) -> dict[str, object]:
            result = run([tools["compiler"], "--inspect-context", artifact])
            success(result, "inspect " + artifact.name)
            value = json.loads(result.stdout)
            if not isinstance(value, dict):
                raise AssertionError("context inspection must return a JSON object")
            return value

        source = root / "context.ny"
        source.write_text(SOURCE, encoding="utf-8")
        base = [tools["compiler"], "--quiet", "--no-stdlib", "--no-bundle"]
        nils = []
        manifests = []
        for binary in (False, True):
            artifact = root / ("binary.nil" if binary else "text.nil")
            success(run([*base, *(["--emit-binary-nil"] if binary else []),
                         "-o", artifact, source]), "compile NIL context")
            nils.append(artifact)
            manifests.append(inspect(artifact))
        manifest = manifests[0]
        # Both NIL encodings state the same thing, types included: the binary
        # type sections carry their annotations behind the header's
        # type-annotation flag.
        if manifests[1] != manifest:
            raise AssertionError("text and binary NIL context differ")
        if manifest.get("schema") != "ny.context.v2" or manifest.get("profile") != "annotations":
            raise AssertionError(f"wrong context schema/profile: {manifest}")

        types = manifest.get("types")
        if not isinstance(types, list) or len(types) != 3:
            raise AssertionError(f"expected a record, choice and class statement: {types}")
        for declaration in types:
            if set(declaration) != {"kind", "name", "annotations"}:
                raise AssertionError(f"unexpected declaration disclosure: {declaration}")
        identities = [(d["kind"], d["name"]) for d in types]
        if identities != sorted(identities):
            raise AssertionError(f"declarations not sorted by kind then name: {identities}")
        if [kind for kind, _ in identities] != ["class", "enum", "struct"]:
            raise AssertionError(f"a record, choice and class must each appear: {identities}")
        if any("untold" in name for _, name in identities):
            raise AssertionError(f"only exact ai_context annotations select a type: {identities}")
        stated = " ".join(a for d in types for a in d["annotations"])
        for text in ("a shape on screen", "what a window is doing", "a metered thing"):
            if text not in stated:
                raise AssertionError(f"type intent missing from manifest: {text}")
        functions = manifest.get("functions")
        if not isinstance(functions, list) or len(functions) != 3:
            raise AssertionError(f"expected entry, unused function, and class method: {manifest}")
        names = [function["name"] for function in functions]
        if names != sorted(names) or any("misleading" in name for name in names):
            raise AssertionError(f"function filtering/order incorrect: {names}")
        if not any("z_unused" in name for name in names) or not any("Meter" in name and "read" in name for name in names):
            raise AssertionError(f"unused or class context missing: {names}")
        for function in functions:
            if set(function) != {"name", "declaration_line", "parameters", "return_type", "annotations"}:
                raise AssertionError(f"unexpected source/environment disclosure: {function}")
            if not isinstance(function["declaration_line"], int) or function["declaration_line"] <= 0:
                raise AssertionError("source declaration line not retained")
        annotations = " ".join(annotation for function in functions for annotation in function["annotations"])
        if "π" not in annotations or "😀" not in annotations or "quote" not in annotations or "path" not in annotations:
            raise AssertionError("Unicode/escaped annotation content lost")
        if "newline\nnext" not in annotations or "carriage\rreturn" not in annotations or "tab\tend" not in annotations:
            raise AssertionError("decoded annotation control characters lost")
        if str(source) in json.dumps(manifest) or SOURCE in json.dumps(manifest):
            raise AssertionError("source paths or source bodies leaked into context")

        # Read-only inspection must neither execute unfamiliar artifacts nor
        # accept mixed compile/inspection invocations.
        marker = root / "must-not-exist"
        executable_text = root / "not-a-ny-artifact"
        executable_text.write_text("#!/bin/sh\nprintf executed > '" + str(marker) + "'\n", encoding="utf-8")
        executable_text.chmod(0o700)
        failure(run([tools["compiler"], "--inspect-context", executable_text]), "inspect does not execute")
        if marker.exists():
            raise AssertionError("inspecting an artifact executed it")
        for index, contents in enumerate(("", "# ordinary comment\n", "module empty\n")):
            nonexecutable = root / f"nonexecutable-{index}.nil"
            nonexecutable.write_text(contents, encoding="utf-8")
            failure(run([tools["compiler"], "--inspect-context", nonexecutable]),
                    "reject NIL context inspection without function records")
        metadata_only = root / "metadata-only.nil"
        metadata_only.write_text("module only_functions\n@ai_context\nfunction helper() -> i32:\n"
                                 "PUSH_CONST 0\nRET\nend function\n", encoding="utf-8")
        metadata_manifest = inspect(metadata_only)
        if [function["name"] for function in metadata_manifest["functions"]] != ["helper"]:
            raise AssertionError("function-bearing metadata inspection incorrectly requires main")
        for flags in (["--inspect-context"], ["--inspect-context", nils[0], "extra"],
                      ["--inspect-context", nils[0], "--native"],
                      ["--native", "--inspect-context", nils[0]]):
            failure(run([tools["compiler"], *flags]), "exclusive inspection CLI")
        output = root / "preserved-output"
        output.write_bytes(SENTINEL)
        for flags in (["--embed-context"], ["--native", "--jit", "--embed-context"]):
            failure(run([*base, *flags, "-o", output, source]), "invalid embedding mode", output)
        library = root / "library.nil"
        success(run([*base, "--emit-library", "--emit-binary-nil", "-o", library, source]),
                "compile library rejection fixture")
        failure(run([tools["compiler"], "--inspect-context", library]), "reject library context inspection")

        if os.name != "nt":
            symlink = root / "link.nil"
            symlink.symlink_to(nils[0])
            failure(run([tools["compiler"], "--inspect-context", symlink]), "reject symlink snapshot")
            fifo = root / "fifo.nil"
            os.mkfifo(fifo)
            failure(run([tools["compiler"], "--inspect-context", fifo]), "reject FIFO without blocking")

        oversize = root / "oversize.ny"
        oversize.write_text('@ai_context("' + "x" * 16384 + '")\nfn main() -> i32\nreturn 0\nend fn\n', encoding="utf-8")
        for binary in (False, True):
            artifact = root / ("oversize-binary.nil" if binary else "oversize-text.nil")
            success(run([*base, *(["--emit-binary-nil"] if binary else []),
                         "-o", artifact, oversize]), "produce oversized annotation NIL")
            failure(run([tools["compiler"], "--inspect-context", artifact]), "reject oversized context", diagnostic="context")
        malformed = root / "malformed.nil"
        malformed.write_bytes(b'module bad\n@ai_context("\xc0\xaf")\nfunction main() -> i32:\nPUSH_CONST 0\nRET\nend function\n')
        failure(run([tools["compiler"], "--inspect-context", malformed]), "reject malformed UTF-8 context")

        if args.llvm == "OFF":
            failure(run([*base, "--native", "--embed-context", "-o", output, source]),
                    "LLVM OFF native embedding unavailable", output, "LLVM")
            print(f"context manifest: {checks} inspection/option checks passed; native execution explicitly skipped (LLVM OFF)")
            return 0

        default_native = root / "default-native"
        success(run([*base, "--native", "-o", default_native, source]), "default native compilation")
        failure(run([tools["compiler"], "--inspect-context", default_native]), "native context defaults off", diagnostic="context")
        success(run([default_native]), "default native execution", EXPECTED)
        native_executions += 1
        embedded = root / "embedded-native"
        success(run([*base, "--native", "--embed-context", "-o", embedded, source]), "embedded compiler AOT")
        if inspect(embedded) != manifest:
            raise AssertionError("compiler AOT context differs from NIL")
        success(run([embedded]), "embedded compiler execution", EXPECTED)
        native_executions += 1
        # Each artifact is compared against its own NIL's context: the
        # assembler's job is to preserve exactly what it was handed.
        for artifact, source_manifest in zip(nils, manifests):
            obj = root / (artifact.stem + ".o")
            linked = root / (artifact.stem + "-linked")
            success(run([tools["assembler"], "--embed-context", "-o", obj, artifact]), "embedded assembly")
            if inspect(obj) != source_manifest:
                raise AssertionError("standalone object context differs from NIL")
            success(run([tools["linker"], "--runtime", tools["runtime"], "-o", linked, obj]), "embedded linking")
            if inspect(linked) != source_manifest:
                raise AssertionError("linker did not retain context exactly")
            success(run([linked]), "standalone context execution", EXPECTED)
            native_executions += 1
            default_obj = root / (artifact.stem + "-default.o")
            success(run([tools["assembler"], "-o", default_obj, artifact]), "default assembly")
            failure(run([tools["compiler"], "--inspect-context", default_obj]), "assembler context defaults off", diagnostic="context")
        failure(run([*base, "--native", "--embed-context", "-o", output, oversize]),
                "oversized AOT metadata preserves output", output, "context")
        for artifact in (root / "oversize-text.nil", root / "oversize-binary.nil", malformed):
            failure(run([tools["assembler"], "--embed-context", "-o", output, artifact]),
                    "bad assembler metadata preserves output", output)

    print(f"context manifest: {checks} CLI checks passed, including {native_executions} native executions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
