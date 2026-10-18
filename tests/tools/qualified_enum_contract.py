#!/usr/bin/env python3
"""Reject invalid enum owners consistently in compilation and editor analysis."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, stdlib = args.compiler.resolve(), args.stdlib.resolve()
    environment = os.environ.copy()
    for name in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_LEX_DUMP',
                 'NY_PARSE_TRACE', 'NY_STDLIB_PATH'):
        environment.pop(name, None)

    with tempfile.TemporaryDirectory(prefix='nil-qualified-enum-') as directory:
        root = Path(directory)
        for owner in ('left', 'right'):
            payload, empty = (7, 9) if owner == 'left' else (8, 10)
            (root / (owner + '.ny')).write_text(
                f'module {owner}\npub enum Choice<T>\n Some(T)\n None\nend enum\n'
                'pub fn make(present: bool) -> Choice<i32>\n'
                f'if present then\n return Choice.Some({payload})\nend if\n'
                'return Choice.None\nend fn\n'
                'pub fn value(item: Choice<i32>) -> i32\nmatch item\n'
                f'{owner}::Choice::Some(payload) -> return payload\n'
                f'Choice.None -> return {empty}\nend match\nreturn 0\nend fn\n')
        common = [compiler, '--quiet', '--no-stdlib', '--sdk-path', stdlib, '-I', root]
        flags = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil'),
                 (), ('--emit-binary-nil',)]
        if args.llvm == 'ON':
            flags += [('--native',), ('--native', '--jit')]

        expected = root / 'owners.stdout'
        expected.write_text('7\n8\n9\n10\n')
        for language in ('ny', 'matrix'):
            if language == 'ny':
                source = 'import left\nimport right\nfn main() -> i32\n'
                for present in ('true', 'false'):
                    for owner in ('left', 'right'):
                        source += f'println({owner}::value({owner}::make({present})))\n'
                source += 'return 0\nend fn\n'
            else:
                source = ('book validation\nread everything from left\nread everything from right\n'
                          'main returns override integer begin paragraph\n')
                for present in ('true', 'false'):
                    for owner in ('left', 'right'):
                        source += f'say {owner}::value with item being ({owner}::make with present being {present})\n'
                source += 'give 0\nend paragraph\n'
            path = root / ('valid-owners.' + language)
            path.write_text(source)
            harness = Path(__file__).resolve().parents[1] / 'routes/ny_route_matrix.py'
            command = ['python3', harness, '--compiler', compiler,
                       '--interpreter', args.interpreter.resolve(), '--source', path,
                       '--expected-stdout', expected, '--expected-exit', '0',
                       '--compiler-arg=-I', '--compiler-arg=' + str(root),
                       '--routes', 'all' if args.llvm == 'ON' else
                       'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                       '--work-dir', root / ('routes-' + language), '--strict']
            result = subprocess.run(command, cwd=root, env=environment,
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, (command, result.stdout, result.stderr)

        cases = [
            ('wrong-owner', 'Result::Ok', 'Option<i32>', 'Option.Some(7)',
             'Option of override integer', 'Option.Some holding 7',
             "does not match target type"),
            ('unknown-owner', 'missing::Option::Some', 'Option<i32>', 'Option.Some(7)',
             'Option of override integer', 'Option.Some holding 7',
             "Unknown enum owner 'missing::Option'"),
            ('unit-unknown-owner', 'missing::Option::None', 'Option<i32>', 'Option.None',
             'Option of override integer', 'Option.None',
             "Unknown enum owner 'missing::Option'"),
            ('unit-unknown-variant', 'Option::Missing', 'Option<i32>', 'Option.None',
             'Option of override integer', 'Option.None', "Unknown variant 'Missing'"),
            ('unit-wrong-owner', 'Result::Error', 'Option<i32>', 'Option.None',
             'Option of override integer', 'Option.None', "does not match target type"),
            ('unknown-variant', 'nil::std::core::Option::Missing', 'Option<i32>', 'Option.Some(7)',
             'Option of override integer', 'Option.Some holding 7',
             "Unknown variant 'Missing'"),
            ('scalar-target', 'Option::Some', 'i32', '7', 'override integer', '7',
             "does not match target type"),
            ('tuple-target', 'Option::Some', 'tuple<i32,i32>', '',
             'tuple of override integer, override integer', '',
             "does not match target type"),
            ('same-leaf-owner', 'right::Choice::Some', 'left::Choice<i32>', 'left::Choice::Some(7)',
             'left.Choice of override integer', 'left::Choice.Some holding 7',
             "does not match target type"),
        ]
        checks = 0
        for language in ('ny', 'matrix'):
            for expression in (False, True):
                for name, pattern, ny_type, ny_value, matrix_type, matrix_value, message in cases:
                    path = root / f'{name}-{expression}.{language}'
                    unit = name.startswith('unit-')
                    ny_pattern = pattern if unit else pattern + '(payload)'
                    matrix_pattern = pattern.replace('::', '.') + ('' if unit else ' holding payload')
                    if expression and name in ('unknown-owner', 'unit-unknown-owner'):
                        message = "Undefined identifier 'missing::Option"
                    if language == 'ny':
                        arm = (f'println(match selected {{ {ny_pattern} -> 1; _ -> 0; }})'
                               if expression else
                               f'match selected\n {ny_pattern} -> println(1)\nend match')
                        source = ('module validation\nimport nil.std.core\nimport left\nimport right\n'
                                  f'fn main() -> i32\nlet selected: {ny_type} = {ny_value}\n'
                                  f'{arm}\nreturn 0\nend fn\n')
                    else:
                        arm = (f'say consider selected\n when {matrix_pattern} give 1\n otherwise give 0\nend consider'
                               if expression else
                               f'consider selected\n when {matrix_pattern} then say 1\nend consider')
                        source = ('book validation\nread everything from nil.std.core\n'
                                  'read everything from left\nread everything from right\n'
                                  'main returns override integer begin paragraph\n'
                                  f'value selected as {matrix_type} is {matrix_value}\n'
                                  f'{arm}\ngive 0\nend paragraph\n')
                    if name == 'tuple-target':
                        if language == 'ny':
                            source = source.replace(
                                f'fn main() -> i32\nlet selected: {ny_type} = \n',
                                f'fn reject(selected: {ny_type}) -> i32\n')
                            source += 'fn main() -> i32\nreturn 0\nend fn\n'
                        else:
                            source = source.replace(
                                'main returns override integer begin paragraph\n'
                                f'value selected as {matrix_type} is \n',
                                f'reject receives selected as {matrix_type} returns override integer begin paragraph\n')
                            source += 'main returns override integer begin paragraph\ngive 0\nend paragraph\n'
                    path.write_text(source)
                    for route_flags in flags:
                        output = root / 'protected-output'
                        output.write_bytes(b'previous artifact')
                        command = [*common, *route_flags, '--output', output, path]
                        result = subprocess.run(command, cwd=root, env=environment,
                                                capture_output=True, text=True, timeout=30)
                        assert result.returncode == 1 and not result.stdout and message in result.stderr, (
                            command, result.returncode, result.stdout, result.stderr, message)
                        assert output.read_bytes() == b'previous artifact', command
                        checks += 1
                    result = subprocess.run([compiler, '--analyze', '--no-stdlib', '-I', stdlib, '-I', root, path], cwd=root,
                                            env=environment, capture_output=True, text=True, timeout=30)
                    assert result.returncode == 1 and not result.stderr, result
                    analysis = json.loads(result.stdout)
                    assert not analysis['success'] and not analysis['reflection']['semantic_checked']
                    assert any(message in diagnostic['message'] and diagnostic['severity'] == 'error'
                               for diagnostic in analysis['diagnostics']), analysis
                    checks += 1
        print(f'paired same-name enum routes and {checks} rejection/analysis checks passed')


if __name__ == '__main__':
    main()
