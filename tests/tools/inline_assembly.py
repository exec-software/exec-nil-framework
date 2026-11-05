#!/usr/bin/env python3
"""Prove bounded native asm survives source, NIL, object, and link boundaries."""

import argparse
import os
import platform
from pathlib import Path
import re
import subprocess
import tempfile
import time


DEADLINE = time.monotonic() + 290


def run(argv, root):
    remaining = DEADLINE - time.monotonic()
    assert remaining > 0, "inline assembly test deadline exceeded"
    return subprocess.run([str(a) for a in argv], cwd=root, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=min(60, remaining))


def require(result, expected=""):
    assert result.returncode == 0 and result.stdout == expected and not result.stderr, (
        result.args, result.returncode, result.stdout, result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "linker", "runtime", "source",
                 "matrix-source"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--target", choices=("x86_64", "aarch64"), required=True)
    args = parser.parse_args()
    expected = ("578437695752307201\n768\n90\n258\n2\n259\n"
                "-9223372036854775808\n9223372036854775807\n-1\n1\n251\n42\n251\n")
    if args.target == "aarch64":
        expected += ("36\n1\n-1\n-42\n-9223372036854775808\n"
                     "9223372036854775807\n6\n")
    base = [args.compiler, "--no-stdlib", "--quiet"]
    machine = platform.machine().lower()
    host = {"x86_64": "x86_64", "amd64": "x86_64",
            "aarch64": "aarch64", "arm64": "aarch64"}.get(machine)
    native = args.llvm == "ON"
    assert not native or host == args.target, ("fixture does not match native host", host, args.target)
    executions = 0
    with tempfile.TemporaryDirectory(prefix="ny-inline-asm-") as temporary:
        root = Path(temporary).resolve(strict=True)
        for language, source, stdout in (("ny", args.source, expected),
                                         ("matrix", args.matrix_source, "72057594037927936\n")):
            for binary in (False, True):
                nil = root / (language + ("-binary.nil" if binary else "-text.nil"))
                require(run([*base, "--no-bundle", *(["--emit-binary-nil"] if binary else []),
                             "--output", nil, source], root))
                if not binary:
                    assert re.search(r"\bINLINE_ASM\s+" + args.target + r"\s", nil.read_text())
                interpreted = run([args.interpreter, "--no-ai", nil], root)
                assert interpreted.returncode > 0 and not interpreted.stdout
                assert "INLINE_ASM requires a matching LLVM native route" in interpreted.stderr
                if native:
                    obj = nil.with_suffix(".o")
                    exe = nil.with_suffix(".exe")
                    assembled = run([args.assembler, "-o", obj, nil], root)
                    require(assembled, f"Assembled {nil} to {obj}\n")
                    linked = run([args.linker, "--runtime", args.runtime, "-o", exe, obj], root)
                    require(linked, f"Linked 1 host object to {exe}\n")
                    require(run([exe], root), stdout)
                    executions += 1

            # A bundled interpreter must reject native code, never erase it.
            if os.name != "nt":
                for binary in (False, True):
                    bundle = root / (language + ("-bundle-binary" if binary else "-bundle-text"))
                    require(run([*base, *(["--emit-binary-nil"] if binary else []),
                                 "--output", bundle, source], root))
                    interpreted = run([bundle], root)
                    assert interpreted.returncode > 0 and not interpreted.stdout
                    assert "INLINE_ASM requires a matching LLVM native route" in interpreted.stderr
            if native:
                require(run([*base, "--native", "--jit", "--output", root / (language + "-jit.nil"),
                             source], root), stdout)
                exe = root / (language + "-aot.exe")
                require(run([*base, "--native", "--output", exe, source], root))
                require(run([exe], root), stdout)
                executions += 2

        # Negative source contracts must preserve any previous destination.
        cases = {
            "target": ('asm "unknown", "nop" () end asm', "explicit target"),
            "missing_operand": ('asm "x86_64", "movq $1, $0" () end asm', "out of range"),
            "bad_placeholder": ('asm "x86_64", "$x" () end asm', "placeholders"),
            "float_input": ('asm "x86_64", "nop" (1.5) end asm', "inputs must have type i64"),
            "immutable": ('let result: i64 = 0\nasm "x86_64", "xorq $0, $0" () -> result end asm', "mutable i64 local"),
            "undefined": ('asm "x86_64", "xorq $0, $0" () -> missing end asm', "mutable i64 local"),
            "wrong_output": ('let mut result: f64 = 0.0\nasm "x86_64", "xorq $0, $0" () -> result end asm', "mutable i64 local"),
            "too_many": ('asm "x86_64", "nop" (' + ', '.join(['1 as i64'] * 9) + ') end asm', "at most eight"),
            "hidden_register": ('let mut result: i64 = 0\nasm "x86_64", "movq %rax, $0" () -> result end asm', "explicit registers"),
            "control_flow": ('asm "x86_64", "ret" () end asm', "outside the bounded"),
            "uninitialized_output": ('let mut result: i64 = 0\nasm "x86_64", "bswapq $0" () -> result end asm', "uninitialized output"),
            "arm_register": ('let mut result: i64 = 0\nasm "aarch64", "mov $0,x0" () -> result end asm', "explicit registers"),
            "arm_control_flow": ('asm "aarch64", "ret" () end asm', "outside the bounded"),
            "arm_flags": ('let mut result: i64 = 0\nasm "aarch64", "adds $0,$1,$1" (1 as i64) -> result end asm', "outside the bounded"),
            "arm_input_write": ('let mut result: i64 = 0\nasm "aarch64", "mov $1,$1" (1 as i64) -> result end asm', "inputs are read-only"),
            "arm_shift": ('let mut result: i64 = 0\nasm "aarch64", "lsl $0,$1,#64" (1 as i64) -> result end asm', "decimal #0..#63"),
            "arm_uninitialized": ('let mut result: i64 = 0\nasm "aarch64", "eor $0,$0,$0" () -> result end asm', "uninitialized output"),
            "arm_immediate": ('let mut result: i64 = 0\nasm "aarch64", "mov $0,#1" () -> result end asm', "register operand"),
            "arm_wrong_type": ('asm "aarch64", "nop" (1.5) end asm', "inputs must have type i64"),
        }
        for name, (body, diagnostic) in cases.items():
            source = root / (name + ".ny")
            source.write_text("fn main() -> i32\n" + body + "\nreturn 0\nend fn\n", newline='\n')
            output = root / (name + ".nil")
            output.write_bytes(b"previous artifact")
            result = run([*base, "--no-bundle", "--output", output, source], root)
            assert result.returncode > 0 and diagnostic in result.stderr, (name, result.stderr)
            assert output.read_bytes() == b"previous artifact", name

        if native:
            original = (root / "ny-text.nil").read_text()
            # Reject serialized metadata before it can reach LLVM assertions.
            for name, text in {
                "target": re.sub(r"(INLINE_ASM\s+)" + args.target, r"\g<1>wrong_target", original),
                "count": re.sub(r"(INLINE_ASM\s+\S+\s+\S+\s+)\d+", r"\g<1>9", original),
                "missing_constant": re.sub(r"(INLINE_ASM\s+\S+\s+)S", r"\g<1>absent_S", original),
                "hidden_control_flow": original.replace('= "nop"', '= "ret"'),
            }.items():
                assert text != original, name
                nil = root / (name + ".nil")
                nil.write_text(text, newline='\n')
                obj = root / (name + ".o")
                obj.write_bytes(b"previous object")
                result = run([args.assembler, "-o", obj, nil], root)
                assert result.returncode > 0 and result.stderr, name
                assert obj.read_bytes() == b"previous object", name

            # A valid template for the other architecture must be preserved in
            # NIL, then rejected by every native entry point before publication.
            foreign = "aarch64" if args.target == "x86_64" else "x86_64"
            for language in ("ny", "matrix"):
                source = root / ("foreign." + language)
                text = (f'fn main() -> i32\nasm "{foreign}", "nop" () end asm\nreturn 0\nend fn\n'
                        if language == "ny" else
                        f'book foreign.asm\nmain returns number begin paragraph\nassembly for {foreign} is "nop"\ngive 0\nend paragraph\n')
                source.write_text(text, newline='\n')
                for binary in (False, True):
                    nil = root / (f"foreign-{language}-{binary}.nil")
                    require(run([*base, "--no-bundle", *(["--emit-binary-nil"] if binary else []),
                                 "--output", nil, source], root))
                    obj = nil.with_suffix(".o")
                    obj.write_bytes(b"previous object")
                    result = run([args.assembler, "-o", obj, nil], root)
                    assert result.returncode > 0 and "does not match native target" in result.stderr, result.stderr
                    assert obj.read_bytes() == b"previous object"
                for jit in (False, True):
                    output = root / (f"foreign-{language}-{jit}.exe")
                    output.write_bytes(b"previous artifact")
                    result = run([*base, "--native", *(["--jit"] if jit else []),
                                  "--output", output, source], root)
                    assert result.returncode > 0 and "does not match native target" in result.stderr, result.stderr
                    assert output.read_bytes() == b"previous artifact"
    print(f"inline assembly contracts passed ({args.target}; {executions} Ny/Matrix native executions; "
          + ("LLVM disabled)" if not native else "text/binary objects, JIT and AOT)"))


if __name__ == "__main__":
    main()
