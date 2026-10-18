#!/usr/bin/env python3
"""Malformed/out-of-range floats fail before artifact writes and in editor analysis."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    environment = os.environ.copy()
    for name in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_LEX_DUMP',
                 'NY_PARSE_TRACE', 'NY_STDLIB_PATH'):
        environment.pop(name, None)
    flags = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil'),
             (), ('--emit-binary-nil',), ('--no-bundle', '--no-typecheck')]
    if args.llvm == 'ON':
        flags += [('--native',), ('--native', '--jit')]
    cases = [(literal, 'exponent requires at least one digit')
             for literal in ('1e', '1e+', '1e-', '1.5E+')]
    cases += [(literal, 'outside the representable')
              for literal in ('1e309', '1e999999999999999999999999',
                              '1e-324', '1e-999999999999999999999999',
                              '9' * 400 + '.0')]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-scientific-literals-') as directory:
        root = Path(directory)
        for language in ('ny', 'matrix'):
            for index, (literal, message) in enumerate(cases):
                # Numeric patterns bypass ordinary expression type inference;
                # test those too, with and without semantic checking enabled.
                for pattern in (False, True):
                    path = root / f'invalid-{index}-{pattern}.{language}'
                    if language == 'ny':
                        body = (f'match 1.0\n {literal} -> println(1)\nend match'
                                if pattern else f'let invalid = {literal}')
                        source = f'fn main() -> i32\n{body}\nreturn 0\nend fn\n'
                    else:
                        body = (f'consider 1.0\n when {literal} then say 1\nend consider'
                                if pattern else f'value invalid is {literal}')
                        source = ('book tests.invalid-literal\n'
                                  'main returns override integer begin paragraph\n'
                                  f'{body}\ngive 0\nend paragraph\n')
                    path.write_text(source)
                    for route_flags in flags:
                        output = root / 'protected-output'
                        output.write_bytes(b'previous artifact')
                        command = [compiler, '--quiet', '--no-stdlib', *route_flags,
                                   '--output', output, path]
                        result = subprocess.run(command, cwd=root, env=environment,
                                                capture_output=True, text=True, timeout=30)
                        assert result.returncode == 1 and not result.stdout and message in result.stderr, (
                            command, result.returncode, result.stdout, result.stderr, message)
                        assert output.read_bytes() == b'previous artifact', command
                        checks += 1
                    result = subprocess.run([compiler, '--analyze', '--no-stdlib', path],
                                            cwd=root, env=environment, capture_output=True,
                                            text=True, timeout=30)
                    assert result.returncode == 1 and not result.stderr, result
                    analysis = json.loads(result.stdout)
                    assert not analysis['success']
                    assert any(message in diagnostic['message'] and diagnostic['severity'] == 'error'
                               for diagnostic in analysis['diagnostics']), analysis
                    checks += 1
    print(f'{checks} paired scientific-literal rejection/analysis checks passed')


if __name__ == '__main__':
    main()
