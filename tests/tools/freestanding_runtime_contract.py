#!/usr/bin/env python3
"""Link and run a freestanding program with no C library at all.

`nilassembler --freestanding` emits an object importing the five native
runtime hooks, plus three more if it allocates, and nilruntime_freestanding
supplies them. This builds the two together with -nostdlib and its own
`_start`, so nothing from a hosted toolchain can satisfy a symbol by accident:
if either side still needed libc, the link fails rather than quietly
succeeding on the build host.

Running it then proves the entry sequence itself works -- runtime ABI check,
context begin, user main, context end -- because the process exit status is
the value Ny returned.

The archive is also required to use general registers only. It has no floating
point of its own, but at -O2 the compiler vectorizes its zero and copy loops
into SSE, and a kernel profile that denies inherited x87/SIMD state faults on
the first such instruction -- a consumer cannot opt out of what is already in
the archive. The compile flag is the guarantee; the check below is the
tripwire, and it needs a disassembler, so it reports rather than skips the
whole test when none is installed.

A freestanding consumer links at a fixed address with no dynamic linker, so
the link is `-no-pie` and the result must contain no global offset table. That
is not cosmetic: under PIC the weak host failure hook is reached through a GOT,
and a GOT is a section a kernel or vessel profile rejects outright, so an
archive that quietly became position-independent would link here and be
refused by the consumer it exists for.

Allocation is checked the same way: by running it. The host owns the memory,
so the driver installs its own array and afterwards reads the concatenated
bytes back out of it, which no amount of symbol inspection could show. The
case with no region installed is checked too, because an allocator that
cannot say no is worse than none.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# A value with no special meaning, chosen so a zero from some other path
# cannot be mistaken for success.
expected_exit = 7

# Distinct from expected_exit and from each other, so a wrong answer names
# which assertion inside the freestanding program failed rather than just
# differing from 7.
missing_bytes_exit = 91
heap_not_reset_exit = 92
reached_user_code_exit = 93
reported_failure_exit = 23
failure_without_message_exit = 24

concat_program = (
    'fn main() -> i64 {\n'
    '    let a: text = "left";\n'
    '    let b: text = "right";\n'
    '    let joined: text = a + b;\n'
    f'    return {expected_exit};\n'
    '}\n'
)

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


# The host owns the memory: it installs a region of its own and the runtime
# bumps through it. Afterwards the driver reads the region back, so the check
# is that the concatenated bytes really landed in host memory -- not merely
# that an allocator was called. It also requires the region to be empty again,
# because the context ended and nothing allocated inside it outlives that.
heap_source = f"""
extern "C" int __ny_entry();
extern "C" void nyrt_heap_init(void *base, unsigned long long size);
extern "C" unsigned long long nyrt_heap_used();

static unsigned char arena[4096];

static bool arena_holds(const char *needle) {{
  unsigned long long length = 0;
  while (needle[length] != '\\0') ++length;
  if (length == 0 || length > sizeof arena) return false;
  for (unsigned long long start = 0; start + length <= sizeof arena; ++start) {{
    unsigned long long index = 0;
    while (index < length &&
           arena[start + index] == static_cast<unsigned char>(needle[index]))
      ++index;
    if (index == length) return true;
  }}
  return false;
}}

extern "C" [[noreturn]] void _start() {{
  nyrt_heap_init(arena, sizeof arena);
  long code = __ny_entry();
  if (!arena_holds("leftright")) code = {missing_bytes_exit};
  else if (nyrt_heap_used() != 0) code = {heap_not_reset_exit};
  asm volatile("syscall" :: "a"(60L), "D"(code) : "rcx", "r11", "memory");
  __builtin_unreachable();
}}
"""

# The same program with no region installed. nyrt_alloc has nothing to hand
# out, returns null, and the generated null check reports it through nyrt_fail
# with a message naming the operation. A vessel must not reach the code after
# a failed allocation, so arriving at the exit below at all is the failure.
starved_source = f"""
extern "C" int __ny_entry();

[[noreturn]] static void leave(long code) {{
  asm volatile("syscall" :: "a"(60L), "D"(code) : "rcx", "r11", "memory");
  __builtin_unreachable();
}}

extern "C" void __ny_host_fail(const char *message) {{
  leave(message ? {reported_failure_exit} : {failure_without_message_exit});
}}

extern "C" [[noreturn]] void _start() {{
  __ny_entry();
  leave({reached_user_code_exit});
}}
"""


def run(command, timeout=180):
    result = subprocess.run(list(map(str, command)), stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=timeout)
    return result


def checked(command, timeout=180):
    result = run(command, timeout)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result


def build(compiler, assembler, archive, cxx, root: Path, name: str, driver: str,
          program_source: str | None = None) -> Path:
    source = root / f'{name}.ny'
    if program_source is None:
        program_source = f'fn main() -> i64 {{\n    return {expected_exit};\n}}\n'
    source.write_text(program_source, newline='\n')
    nil = root / f'{name}.nil'
    obj = root / f'{name}.o'
    checked([compiler, '--no-stdlib', '--no-bundle', '--unmanaged', '-o', nil, source])
    checked([assembler, '--unmanaged', '--freestanding', '-o', obj, nil])
    driver_path = root / f'{name}_driver.cpp'
    driver_path.write_text(driver, newline='\n')
    program = root / name
    checked([cxx, '-nostdlib', '-static', '-no-pie', '-fno-exceptions',
             '-fno-rtti', '-o', program, driver_path, obj, archive])
    return program


def indirection_sections(program: Path) -> list[str]:
    """Names of any GOT/PLT sections in a linked ELF program.

    Read from the section header table directly, so the check needs no
    binutils where an SDK is built.
    """
    data = program.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise SystemExit(f'expected a little-endian 64-bit program: {program}')
    section_offset, = struct.unpack_from('<Q', data, 0x28)
    section_size, section_count, names_index = struct.unpack_from('<HHH', data, 0x3a)

    def name_offset(index: int) -> int:
        return struct.unpack_from('<I', data, section_offset + index * section_size)[0]

    names_base, = struct.unpack_from(
        '<Q', data, section_offset + names_index * section_size + 0x18)

    def text(index: int) -> str:
        start = names_base + name_offset(index)
        end = data.index(b'\0', start)
        return data[start:end].decode('utf-8', 'replace')

    found = []
    for index in range(section_count):
        section = text(index)
        if section.startswith('.got') or section.startswith('.plt') or section == '.iplt':
            found.append(section)
    return found


def non_general_register_instructions(archive: Path) -> list[str] | None:
    """Disassembled lines using x87, MMX or SIMD registers, or None.

    None means no disassembler was available, which is reported rather than
    treated as a pass.
    """
    for tool in ('objdump', 'llvm-objdump'):
        executable = shutil.which(tool)
        if executable is None:
            continue
        result = run([executable, '-d', archive], timeout=120)
        if result.returncode != 0:
            continue
        pattern = re.compile(r'%(?:x|y|z)mm[0-9]|%mm[0-7]|\b(?:f[il]?(?:ld|st|add|mul|div|sub)'
                             r'|emms|femms)\b')
        return [line.strip() for line in result.stdout.splitlines()
                if pattern.search(line)]
    return None


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

        # General registers only, so a profile that denies inherited x87 and
        # SIMD state can run what this archive contains.
        simd = non_general_register_instructions(args.archive)
        if simd is None:
            print('NOTE: no disassembler found; the general-register-only '
                  'guarantee rests on the compile flag alone')
        else:
            assert not simd, (
                'the freestanding runtime must use general registers only; '
                f'found {len(simd)}: {simd[:4]}')
            checks += 1

        # No GOT, no PLT. A consumer that forbids those sections is the reason
        # this archive exists, so the absence is pinned rather than assumed.
        indirections = indirection_sections(program)
        assert not indirections, (
            'a freestanding program must link with no indirection tables; '
            f'found: {indirections}')
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

        # Allocation over a region the host owns. The program concatenates,
        # and the driver finds the result inside its own array afterwards.
        allocating = build(args.compiler, args.assembler, args.archive, args.cxx,
                           root, 'heap', heap_source, program_source=concat_program)
        checks += 1
        result = run([allocating], timeout=60)
        assert result.returncode != missing_bytes_exit, (
            'the concatenated bytes must appear in the region the host installed')
        assert result.returncode != heap_not_reset_exit, (
            'ending the context must return the region to empty')
        assert result.returncode == expected_exit, (
            'an allocating freestanding program must run and return what Ny '
            f'returned; got {result.returncode}, stderr: {result.stderr!r}')
        checks += 1

        # And with no region installed, which is the default. Allocation has
        # to fail loudly rather than hand back memory it does not have.
        starved = build(args.compiler, args.assembler, args.archive, args.cxx,
                        root, 'starved', starved_source,
                        program_source=concat_program)
        checks += 1
        result = run([starved], timeout=60)
        assert result.returncode != reached_user_code_exit, (
            'a failed allocation must not return into the program')
        assert result.returncode != failure_without_message_exit, (
            'the failure must name the operation that could not allocate')
        assert result.returncode == reported_failure_exit, (
            'allocating with no region installed must report through nyrt_fail; '
            f'got {result.returncode}, stderr: {result.stderr!r}')
        checks += 1

    print(f'Freestanding runtime: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
