#!/usr/bin/env python3
"""Prove bounded native asm survives source, NIL, object, and link boundaries."""

import argparse
import os
import platform
from pathlib import Path
import subprocess
import tempfile


def run(argv, root):
    return subprocess.run([str(a) for a in argv], cwd=root, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=60)


def require(result, expected=""):
    assert result.returncode == 0 and result.stdout == expected and not result.stderr, (
        result.args, result.returncode, result.stdout, result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("compiler", "interpreter", "assembler", "linker", "runtime", "source"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    expected = ("578437695752307201\n768\n90\n258\n2\n259\n"
                "-9223372036854775808\n9223372036854775807\n-1\n1\n251\n42\n251\n")
    base = [args.compiler, "--no-stdlib", "--quiet"]
    native = args.llvm == "ON" and platform.machine().lower() in ("x86_64", "amd64")
    with tempfile.TemporaryDirectory(prefix="ny-inline-asm-") as temporary:
        root = Path(temporary)
        for binary in (False, True):
            nil = root / ("binary.nil" if binary else "text.nil")
            require(run([*base, "--no-bundle", *(["--emit-binary-nil"] if binary else []),
                         "--output", nil, args.source], root))
            if not binary:
                assert "INLINE_ASM" in nil.read_text()
            interpreted = run([args.interpreter, "--no-ai", nil], root)
            assert interpreted.returncode > 0 and not interpreted.stdout
            assert "INLINE_ASM requires a matching LLVM native route" in interpreted.stderr
            if native:
                obj = root / ("binary.o" if binary else "text.o")
                exe = root / ("binary.exe" if binary else "text.exe")
                assembled = run([args.assembler, "-o", obj, nil], root)
                # NY_ENABLE_LLVM=ON can still configure without an installed
                # LLVM package. Require native execution only when available.
                if assembled.returncode != 0 and "no LLVM backend" in assembled.stderr:
                    native = False
                else:
                    require(assembled, f"Assembled {nil} to {obj}\n")
                    linked = run([args.linker, "--runtime", args.runtime, "-o", exe, obj], root)
                    require(linked, f"Linked 1 host object to {exe}\n")
                    require(run([exe], root), expected)

        # A bundled interpreter must reject native code, too, never erase it.
        if os.name != "nt":
            for binary in (False, True):
                bundle = root / ("bundle-binary" if binary else "bundle-text")
                require(run([*base, *(["--emit-binary-nil"] if binary else []),
                             "--output", bundle, args.source], root))
                interpreted = run([bundle], root)
                assert interpreted.returncode > 0 and not interpreted.stdout
                assert "INLINE_ASM requires a matching LLVM native route" in interpreted.stderr

        if native:
            require(run([*base, "--native", "--jit", "--output", root / "jit.nil",
                         args.source], root), expected)
            exe = root / "aot.exe"
            require(run([*base, "--native", "--output", exe, args.source], root))
            require(run([exe], root), expected)

        # Negative source contracts must preserve any previous destination.
        cases = {
            "target": ('asm "aarch64", "nop" () end asm', "explicit target"),
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
        }
        for name, (body, diagnostic) in cases.items():
            source = root / (name + ".ny")
            source.write_text("fn main() -> i32\n" + body + "\nreturn 0\nend fn\n")
            output = root / (name + ".nil")
            output.write_bytes(b"previous artifact")
            result = run([*base, "--no-bundle", "--output", output, source], root)
            assert result.returncode > 0 and diagnostic in result.stderr, (name, result.stderr)
            assert output.read_bytes() == b"previous artifact", name

        if native:
            original = (root / "text.nil").read_text()
            # Reject serialized metadata before it can reach LLVM assertions.
            for name, text in {
                "count": original.replace("INLINE_ASM   x86_64", "INLINE_ASM   wrong_target"),
                "missing_constant": original.replace("INLINE_ASM   x86_64 S", "INLINE_ASM   x86_64 absent_S"),
                "hidden_control_flow": original.replace('= "nop"', '= "ret"'),
            }.items():
                assert text != original, name
                nil = root / (name + ".nil")
                nil.write_text(text)
                obj = root / (name + ".o")
                obj.write_bytes(b"previous object")
                result = run([args.assembler, "-o", obj, nil], root)
                assert result.returncode > 0 and result.stderr, name
                assert obj.read_bytes() == b"previous object", name
    print("inline assembly contracts passed" + (" (native routes unavailable)" if not native else ""))


if __name__ == "__main__":
    main()
