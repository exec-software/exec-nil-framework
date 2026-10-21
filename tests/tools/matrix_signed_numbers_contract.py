#!/usr/bin/env python3
"""Matrix signs preserve literal validation and never introduce symbolic subtraction."""

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
    cases = [('- 1.6', 'numeric sign must touch'),
             ('-value', 'numeric sign must touch'),
             ('--1.6', 'numeric sign must touch'),
             ('-.5', 'numeric sign must touch'),
             ('1.6 - 2.0', 'numeric sign must touch'),
             ('1.6 -2.0', None), ('1.6 negative 2.0', None),
             ('negative', None), ('-1e-', 'exponent requires'),
             ('negative 1e+', 'exponent requires'),
             ('-1e309', 'outside the representable'),
             ('negative 1e-324', 'outside the representable'),
             ('-9223372036854775809', 'outside the representable signed'),
             ('negative 9223372036854775809', 'outside the representable signed'),
             ('-0x8000000000000001', 'outside the representable signed')]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-matrix-signs-') as directory:
        root = Path(directory)
        for index, (literal, message) in enumerate(cases):
            contexts = ('expression', 'statement-pattern', 'expression-pattern', 'default')
            for context in contexts:
                prefix = ''
                if context == 'statement-pattern':
                    body = f'consider 1.0\nwhen {literal} then say 1\nend consider'
                elif context == 'expression-pattern':
                    body = f'say consider 1.0 when {literal} give 1 otherwise give 0 end consider'
                elif context == 'default':
                    prefix = (f'unused receives input as floating defaulting to {literal} '
                              'returns floating begin paragraph\ngive input\nend paragraph\n')
                    body = 'say 0'
                else:
                    body = f'value invalid is {literal}'
                source = ('book tests.invalid-sign\n' + prefix +
                          'main returns override integer begin paragraph\n' +
                          body + '\ngive 0\nend paragraph\n')
                path = root / f'invalid-{index}-{context}.matrix'
                path.write_text(source)
                for route_flags in flags:
                    output = root / 'protected-output'
                    output.write_bytes(b'previous artifact')
                    result = subprocess.run([compiler, '--quiet', '--no-stdlib', *route_flags,
                                             '--output', output, path], cwd=root, env=environment,
                                            capture_output=True, text=True, timeout=30)
                    assert result.returncode == 1 and not result.stdout, (literal, context, result)
                    if message:
                        assert message in result.stderr, (literal, context, result.stderr)
                    assert output.read_bytes() == b'previous artifact', (literal, context)
                    checks += 1
                result = subprocess.run([compiler, '--analyze', '--no-stdlib', path],
                                        cwd=root, env=environment, capture_output=True,
                                        text=True, timeout=30)
                assert result.returncode == 1 and not result.stderr, (literal, context, result)
                analysis = json.loads(result.stdout)
                assert not analysis['success'], (literal, context, analysis)
                assert any(item['severity'] == 'error' and
                           (message is None or message in item['message'])
                           for item in analysis['diagnostics']), (literal, context, analysis)
                checks += 1
    print(f'{checks} Matrix signed-number rejection/analysis checks passed')


if __name__ == '__main__':
    main()
