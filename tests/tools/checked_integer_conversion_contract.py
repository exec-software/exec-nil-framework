#!/usr/bin/env python3
"""Native checked integer providers require exact parameter and Result layouts."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    if args.llvm == 'OFF':
        print('Native conversion ABI checks require LLVM')
        return 77
    compiler, stdlib = args.compiler.resolve(), args.stdlib.resolve()
    environment = os.environ.copy()
    for name in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_STDLIB_PATH',
                 'NIL_STDLIB_PATH', 'NY_LEX_DUMP', 'NY_PARSE_TRACE'):
        environment.pop(name, None)
    # The ordinary canonical Result comes from core. Each malformed provider
    # changes one boundary independently, with a well-typed call that reaches
    # LLVM validation rather than failing ordinary argument/type checking.
    cases = (
        ('wrong-input', 'i64', 'value: i32', 'Result<i64, text>', '1'),
        ('wrong-success', 'i64', 'value: text', 'Result<i32, text>', '"1"'),
        ('wrong-error', 'i64', 'value: text', 'Result<i64, i64>', '"1"'),
        ('wrong-arity', 'i64', 'first: text, second: text', 'Result<i64, text>', '"1", "2"'),
        ('wrong-return', 'i64', 'value: text', 'i64', '"1"'),
        ('i32-wrong-success', 'i32', 'value: text', 'Result<i64, text>', '"1"'),
    )
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-checked-integer-abi-') as directory:
        root = Path(directory)
        sources = []
        for name, width, parameters, result, arguments in cases:
            path = root / f'{name}.ny'
            path.write_text('import nil.std.core\n'
                            f'@intrinsic("convert.try_to_{width}")\n'
                            f'fn malformed({parameters}) -> {result}\nend fn\n'
                            'fn main() -> i32\n'
                            f'let parsed = malformed({arguments})\n'
                            'return 0\nend fn\n')
            sources.append((path, f'convert.try_to_{width} requires the exact '
                                  f'(text) -> Result<{width},text> ABI'))
        # Native text has no byte-length argument at this ABI. Reachable NUL
        # constants must be rejected before a C string could truncate them.
        for language, source in (
            ('ny', 'import nil.std.core\nfn main() -> i32\n'
                   'let parsed = Convert.try_to_i64("12\0x")\nreturn 0\nend fn\n'),
            ('matrix', 'book tests.checked-integer-nul\nread everything from nil.std.core\n'
                       'main returns override integer begin paragraph\n'
                       'value parsed is Convert.try-to-i64 with "12\0x"\n'
                       'give 0\nend paragraph\n'),
        ):
            path = root / f'embedded-nul.{language}'
            # Matrix quoted text is raw, so this is an actual NUL source byte.
            path.write_text(source)
            sources.append((path, 'contains an embedded NUL byte'))
        for path, message in sources:
            for flags in (('--native',), ('--native', '--jit')):
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                command = [compiler, '--quiet', '--no-stdlib', '--sdk-path', stdlib,
                           *flags, '--output', output, path]
                result = subprocess.run(command, cwd=root, env=environment,
                                        capture_output=True, text=True, timeout=30)
                assert result.returncode == 1 and not result.stdout and message in result.stderr, (
                    command, result.returncode, result.stdout, result.stderr, message)
                assert output.read_bytes() == b'previous artifact', command
                checks += 1
    print(f'{checks} checked integer ABI and embedded-NUL rejection checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
