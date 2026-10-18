#!/usr/bin/env python3
"""Check integer literal rejection, editor diagnostics, and no-typecheck routes."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


REPOSITORY = Path(__file__).resolve().parents[2]
SENTINEL = b'previous artifact'


def source_with_literal(language, context, literal):
    """Keep unused defaults in the source-validation contract as well."""
    if language == 'ny':
        prefix = ''
        if context == 'default':
            prefix = f'fn unused(value: i64 = {literal}) -> i64\nreturn value\nend fn\n'
            body = 'println(0)'
        elif context == 'statement-pattern':
            body = f'match 1\n{literal} -> println(1)\nend match'
        elif context == 'expression-pattern':
            body = f'println(match 1 {{ {literal} -> 1; _ -> 0; }})'
        else:
            body = f'let invalid = {literal}'
        return f'{prefix}fn main() -> i32\n{body}\nreturn 0\nend fn\n'
    literal = 'minus ' + literal[1:] if literal.startswith('-') else literal
    prefix = 'book tests.invalid-integer\n'
    if context == 'default':
        # A Matrix default is a primary expression; parentheses admit unary minus.
        prefix += ('unused receives input as override long defaulting to '
                   f'({literal}) returns override long begin paragraph\n'
                   'give input\nend paragraph\n')
        body = 'say 0'
    elif context == 'statement-pattern':
        body = f'consider 1\nwhen {literal} then say 1\nend consider'
    elif context == 'expression-pattern':
        body = f'say consider 1 when {literal} give 1 otherwise give 0 end consider'
    else:
        body = f'value invalid is {literal}'
    return (prefix + 'main returns override integer begin paragraph\n'
            f'{body}\ngive 0\nend paragraph\n')


def rejected_source(compiler, path, flags, environment, root, expected_message=None):
    output = root / 'protected-output'
    output.write_bytes(SENTINEL)
    command = [compiler, '--quiet', '--no-stdlib', *flags, '--output', output, path]
    result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                            text=True, timeout=30)
    assert result.returncode == 1 and not result.stdout and result.stderr, (
        command, result.returncode, result.stdout, result.stderr)
    if expected_message is not None:
        assert expected_message in result.stderr, (command, result.stderr, expected_message)
    assert output.read_bytes() == SENTINEL, command


def rejected_analysis(compiler, path, environment, root, expected_message=None):
    result = subprocess.run([compiler, '--analyze', '--no-stdlib', path], cwd=root,
                            env=environment, capture_output=True, text=True, timeout=30)
    assert result.returncode == 1 and not result.stderr, result
    analysis = json.loads(result.stdout)
    assert not analysis['success'], analysis
    errors = [item for item in analysis['diagnostics'] if item['severity'] == 'error']
    assert errors, analysis
    if expected_message is not None:
        assert any(expected_message in item['message'] for item in errors), analysis
    return errors


def check_flag_routes(compiler, interpreter, llvm, root, environment):
    # This flag suppresses semantic errors but still runs checker inference.
    # Keep its consumer checks focused on values/array joins, not the separate
    # historical differences in untyped scalar arithmetic overflow.
    sources = {
        'ny': '''fn total(items: array<i64>) -> i64
    return items[0] + items[1]
end fn
fn main() -> i32
    let small = 7
    let wide = 4294967296
    println(total([1, 2147483648]))
    println(total([2147483648, 1]))
    println(total([small, wide]))
    println(total([wide, small]))
    println(-9223372036854775808)
    println(9223372036854775807)
    println(0x100000000)
    return 0
end fn
''',
        'matrix': '''book tests.integer-width-flag
total receives items as array of override long returns override long begin paragraph
    give (items at 0) plus (items at 1)
end paragraph
main returns override integer begin paragraph
    value small is 7
    value wide is 4294967296
    say total with items being [1, 2147483648]
    say total with items being [2147483648, 1]
    say total with items being [small, wide]
    say total with items being [wide, small]
    say minus 9223372036854775808
    say 9223372036854775807
    say 0x100000000
    give 0
end paragraph
''',
    }
    expected = root / 'flag.stdout'
    expected.write_text('2147483649\n2147483649\n4294967303\n4294967303\n'
                        '-9223372036854775808\n9223372036854775807\n4294967296\n')
    routes = ('all' if llvm == 'ON' else
              'text-interpreter,binary-interpreter,bundle-text,bundle-binary')
    for language, source in sources.items():
        path = root / f'flag.{language}'
        path.write_text(source)
        command = [sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                   '--compiler', compiler, '--interpreter', interpreter,
                   '--source', path, '--expected-stdout', expected, '--expected-exit', '0',
                   '--routes', routes, '--compiler-arg=--no-stdlib',
                   '--compiler-arg=--no-typecheck', '--work-dir', root / f'flag-{language}',
                   '--strict']
        result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                                text=True, timeout=180)
        assert result.returncode == 0, (command, result.stdout, result.stderr)
    return 12 if llvm == 'ON' else 8


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    environment = os.environ.copy()
    for name in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_LEX_DUMP',
                 'NY_PARSE_TRACE', 'NY_STDLIB_PATH'):
        environment.pop(name, None)
    flags = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil'),
             (), ('--emit-binary-nil',), ('--no-bundle', '--no-typecheck')]
    if args.llvm == 'ON':
        flags += [('--native',), ('--native', '--jit')]
    out_of_range = ('9223372036854775808', '18446744073709551616', '9' * 400,
                    '-9223372036854775809', '-' + '9' * 400,
                    '0x8000000000000000', '0xFFFFFFFFFFFFFFFF',
                    '0x10000000000000000', '-0x8000000000000001')
    malformed = ('0x', '0X', '0xG', '0x12G')
    # Matrix renders its public `number` spelling in place of the shared i64 name.
    cases = [(value, 'outside the representable signed') for value in out_of_range]
    cases += [(value, None) for value in malformed]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-integer-literals-') as directory:
        root = Path(directory)
        for language in ('ny', 'matrix'):
            for index, (literal, expected_message) in enumerate(cases):
                for context in ('expression', 'statement-pattern', 'expression-pattern', 'default'):
                    path = root / f'invalid-{index}-{context}.{language}'
                    path.write_text(source_with_literal(language, context, literal))
                    for route_flags in flags:
                        rejected_source(compiler, path, route_flags, environment, root,
                                        expected_message)
                        checks += 1
                    rejected_analysis(compiler, path, environment, root, expected_message)
                    checks += 1
            # A numeric join inside a fresh literal does not make existing
            # arrays covariant. Check both conversion directions explicitly.
            for source_type, destination_type in (('i32', 'i64'), ('i64', 'i32')):
                if language == 'ny':
                    source = ('fn main() -> i32\n'
                              f'let original: array<{source_type}> = '
                              f'[1 as {source_type}, 2 as {source_type}]\n'
                              f'let invalid: array<{destination_type}> = original\n'
                              'return 0\nend fn\n')
                else:
                    names = {'i32': 'override integer', 'i64': 'override long'}
                    source = ('book tests.array-invariance\n'
                              'main returns override integer begin paragraph\n'
                              f'value original as array of {names[source_type]} is [1, 2]\n'
                              f'value invalid as array of {names[destination_type]} is original\n'
                              'give 0\nend paragraph\n')
                path = root / f'invariant-{source_type}.{language}'
                path.write_text(source)
                display = ({'i32': 'i32', 'i64': 'i64'} if language == 'ny' else
                           {'i32': 'override integer', 'i64': 'number'})
                expected_message = (f"Expected 'array<{display[destination_type]}>', "
                                    f"got 'array<{display[source_type]}>'")
                for route_flags in flags:
                    if '--no-typecheck' not in route_flags:
                        rejected_source(compiler, path, route_flags, environment, root,
                                        expected_message)
                        checks += 1
                errors = rejected_analysis(compiler, path, environment, root, expected_message)
                invalid_start = source.index('let invalid' if language == 'ny' else 'value invalid')
                assert any(item['start'] == invalid_start and expected_message in item['message']
                           for item in errors), errors
                checks += 1
            # Revisiting the completed join is essential: bool is compatible
            # with i32 in some scalar contexts, but it is not an i64 element.
            for index, array in enumerate(('[1, true, 2147483648]',
                                           '[2147483648, true, 1]')):
                path = root / f'invalid-array-{index}.{language}'
                path.write_text(source_with_literal(language, 'expression', array))
                for route_flags in flags:
                    if '--no-typecheck' not in route_flags:
                        rejected_source(compiler, path, route_flags, environment, root,
                                        'Array element type mismatch')
                        checks += 1
                rejected_analysis(compiler, path, environment, root,
                                  'Array element type mismatch')
                checks += 1
        executions = check_flag_routes(compiler, interpreter, args.llvm, root, environment)
    print(f'{checks} paired integer-literal rejection/analysis checks and '
          f'{executions} no-typecheck route executions passed')


if __name__ == '__main__':
    main()
