#!/usr/bin/env python3
"""Link and run a freestanding program with no C library at all.

`nilassembler --freestanding` emits an object importing only the five native
runtime hooks, and nilruntime_freestanding supplies them. This builds the two
together with -nostdlib and its own `_start`, so nothing from a hosted
toolchain can satisfy a symbol by accident: if either side still needed libc,
the link fails rather than quietly succeeding on the build host.

Running it then proves the entry sequence itself works -- runtime ABI check,
context begin, user main, context end -- because the process exit status is
the value Ny returned.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

# A value with no special meaning, chosen so a zero from some other path
# cannot be mistaken for success.
expected_exit = 7

driver_source = """
extern "C" int __ny_entry();

// No C runtime is linked, so this is the process entry. Exit through the
// kernel directly; returning would have nowhere to go.
extern "C" [[noreturn]] void _start() {
  const long code = __ny_entry();
  asm volatile("syscall" :: "a"(60L), "D"(code) : "rcx", "r11", "memory");
  __builtin_unreachable();
}
"""

# Linking succeeds with the weak default in place; this one replaces it, which
# is how a host reports a failure instead of stopping silently.
host_fail_source = """
extern "C" int __ny_entry();
extern "C" void __ny_host_fail(const char *message) { (void)message; }

extern "C" [[noreturn]] void _start() {
  const long code = __ny_entry();
  asm volatile("syscall" :: "a"(60L), "D"(code) : "rcx", "r11", "memory");
  __builtin_unreachable();
}
"""


def run(command, timeout=180):
    result = subprocess.run(list(map(str, command)), stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=timeout)
    return result


def checked(command, timeout=180):
    result = run(command, timeout)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result


def build(compiler, assembler, archive, cxx, root: Path, name: str, driver: str) -> Path:
    source = root / f'{name}.ny'
    source.write_text(f'fn main() -> i64 {{\n    return {expected_exit};\n}}\n', newline='\n')
    nil = root / f'{name}.nil'
    obj = root / f'{name}.o'
    checked([compiler, '--no-stdlib', '--no-bundle', '--unmanaged', '-o', nil, source])
    checked([assembler, '--unmanaged', '--freestanding', '-o', obj, nil])
    driver_path = root / f'{name}_driver.cpp'
    driver_path.write_text(driver, newline='\n')
    program = root / name
    checked([cxx, '-nostdlib', '-static', '-fno-exceptions', '-fno-rtti',
             '-o', program, driver_path, obj, archive])
    return program


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--cxx', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'linux':
        print('SKIP: the freestanding link contract is exercised on ELF hosts')
        return 77

    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-freestanding-') as directory:
        root = Path(directory)

        program = build(args.compiler, args.assembler, args.archive, args.cxx,
                        root, 'plain', driver_source)
        checks += 1  # linking with -nostdlib at all is the first assertion

        result = run([program], timeout=60)
        assert result.returncode == expected_exit, (
            'the freestanding program must run and return what Ny returned; '
            f'got {result.returncode}, stderr: {result.stderr!r}')
        checks += 1

        # The failure reporter is weak so the archive links alone, and
        # replaceable so a host can observe a failure.
        overridden = build(args.compiler, args.assembler, args.archive, args.cxx,
                           root, 'reporter', host_fail_source)
        checks += 1
        result = run([overridden], timeout=60)
        assert result.returncode == expected_exit, (
            'defining the host failure reporter must not change ordinary execution; '
            f'got {result.returncode}')
        checks += 1

    print(f'Freestanding runtime: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
