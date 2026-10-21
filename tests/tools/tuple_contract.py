#!/usr/bin/env python3
"""Structural tuples preserve ordered types, evaluation order, and immutable slots."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_LIBRARY_PATH'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-tuple-contract-') as directory:
        root = Path(directory)
        def run(command, status=0):
            result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                                    text=True, stdin=subprocess.DEVNULL, timeout=120)
            assert result.returncode == status, (list(map(str, command)), result.returncode,
                                                result.stdout[:4000], result.stderr[:4000])
            return result
        for language in ('ny', 'matrix'):
            source = REPOSITORY / 'tests/language' / ('tuples.ny' if language == 'ny' else 'tuples-matrix.matrix')
            command = [sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                       '--compiler', compiler, '--interpreter', interpreter, '--source', source,
                       '--expected-stdout', source.with_suffix('.stdout'), '--expected-exit', '0',
                       '--compiler-arg=--no-stdlib', '--routes',
                       'all' if args.llvm == 'ON' else
                       'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                       '--work-dir', root / language, '--strict']
            run(command)
            checks += 6 if args.llvm == 'ON' else 4
            analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', '--semantic-index', source]).stdout)
            assert analysis['success'], analysis['diagnostics']
            checks += 1

        # Each frontend consumes the other's public tuple signature, both
        # from source and from a binary NIL library with its original source files removed.
        for provider_language in ('ny', 'matrix'):
            provider_root = root / ('provider-' + provider_language)
            provider_root.mkdir()
            provider = provider_root / ('provider.' + provider_language)
            provider.write_text(
                'module provider\npub fn make() -> (i64, text)\nreturn (42,"cross")\nend fn\n'
                if provider_language == 'ny' else
                'book provider\npublic make returns tuple of (number,text) begin paragraph\n'
                'give (42,"cross")\nend paragraph\n')
            consumer_language = 'matrix' if provider_language == 'ny' else 'ny'
            consumer = root / ('consumer-' + provider_language + '.' + consumer_language)
            consumer.write_text(
                'import provider.*\nfn main() -> i32\nlet (amount,label) = make()\n'
                'println(amount)\nprintln(label)\nreturn 0\nend fn\n'
                if consumer_language == 'ny' else
                'book consumer\nread everything from provider\n'
                'main returns override integer begin paragraph\n'
                'value (amount,label) is make with nothing\nsay amount\nsay label\ngive 0\nend paragraph\n')
            libraries = root / ('libraries-' + provider_language)
            libraries.mkdir()
            for dependency_mode in ('source', 'binary'):
                if dependency_mode == 'binary':
                    run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                         '--emit-library', '-o', libraries / 'provider.nil', provider])
                    provider.unlink()
                    search = libraries
                else:
                    search = provider_root
                for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                    artifact = root / ('consumer-' + provider_language + '-' + dependency_mode + '-' + encoding + '.nil')
                    run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-I', search,
                         *flags, '-o', artifact, consumer])
                    output = run([interpreter, '--no-ai', artifact])
                    assert output.stdout == '42\ncross\n' and not output.stderr, output
                    checks += 1
                if args.llvm == 'ON':
                    executable = root / ('consumer-' + provider_language + '-' + dependency_mode)
                    run([compiler, '--quiet', '--no-stdlib', '--native', '-I', search,
                         '-o', executable, consumer])
                    output = run([executable])
                    assert output.stdout == '42\ncross\n' and not output.stderr, output
                    checks += 1

        invalid = [
            ('write', 'let mut pair = (1, "a")\npair.0 = 3',
             'variable pair is (1, "a")\nset pair.0 to 3', 'immutable'),
            ('arity', 'let (a,b,c) = (1,2)', 'value (a,b,c) is (1,2)', 'exactly 3'),
            ('non-tuple', 'let (a,b) = [1,2]', 'value (a,b) is [1,2]', 'requires a tuple'),
            ('out-of-range', 'let pair = (1,2)\nprintln(pair.2)',
             'value pair is (1,2)\nsay pair.2', 'member'),
            ('huge-position', 'let pair = (1,2)\nprintln(pair.999999999999999999999999999999)',
             'value pair is (1,2)\nsay pair.999999999999999999999999999999', 'range'),
            ('leading-zero', 'let pair = (1,2)\nprintln(pair.00)',
             'value pair is (1,2)\nsay pair.00', 'member'),
            ('literal-arity', 'let pair: (i64,text) = (1,2,3)',
             'value pair as tuple of (number,text) is (1,2,3)', 'Type mismatch'),
            ('literal-type', 'let pair: (i64,text) = (1,2)',
             'value pair as tuple of (number,text) is (1,2)', 'Type mismatch'),
            ('invariant', 'let original = (1,2)\nlet wrong: (i64,i64) = original',
             'value original is (1,2)\nvalue wrong as tuple of (number,number) is original', 'Type mismatch'),
            ('dynamic-index', 'let pair = (1,2)\nprintln(pair[0])',
             'value pair is (1,2)\nsay pair at 0', 'cannot be indexed'),
            ('duplicate', 'let (same,same) = (1,2)', 'value (same,same) is (1,2)', 'already declared'),
            ('pattern-arity', 'match (1,2)\n(a,b,c) -> println(a)\nend match',
             'consider (1,2)\nwhen (a,b,c) then say a\nend consider', 'arity'),
            ('scalar-loop', 'for (a,b) in [1,2]\nprintln(a)\nend for',
             'repeat for each (a,b) in [1,2]\nsay a\nend repeat', 'require tuple'),
            ('scalar-singleton-loop', 'for (a,) in [1,2]\nprintln(a)\nend for',
             'repeat for each (a,) in [1,2]\nsay a\nend repeat', 'require tuple'),
            ('scalar-range-loop', 'for (a,) in range(2)\nprintln(a)\nend for',
             'repeat for each (a,) in range with 2\nsay a\nend repeat', 'require tuple'),
            ('loop-arity', 'for (a,b,c) in [(1,2)]\nprintln(a)\nend for',
             'repeat for each (a,b,c) in [(1,2)]\nsay a\nend repeat', 'arity'),
            ('named-loop-arity', 'let items = [(1,2)]\nfor (a,b,c) in items\nprintln(a)\nend for',
             'value items is [(1,2)]\nrepeat for each (a,b,c) in items\nsay a\nend repeat', 'arity'),
        ]
        for name, ny, matrix, message in invalid:
            for language, body in [('ny', ny), ('matrix', matrix)]:
                source = ('fn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
                          if language == 'ny' else
                          'book tuple_invalid\nmain returns override integer begin paragraph\n' +
                          body + '\ngive 0\nend paragraph\n')
                path = root / (name + '.' + language)
                path.write_text(source)
                for flags in (['--no-bundle'], ['--no-bundle', '--emit-binary-nil'],
                              ['--no-bundle', '--no-typecheck'],
                              *([['--native']] if args.llvm == 'ON' else [])):
                    output = root / 'protected.nil'
                    output.write_bytes(b'keep previous artifact')
                    result = run([compiler, '--quiet', '--no-stdlib', *flags, '-o', output, path], 1)
                    expected_message = ('tuple' if '--no-typecheck' in flags and name != 'huge-position' else message)
                    assert expected_message.lower() in result.stderr.lower(), (name, language, result.stderr)
                    assert output.read_bytes() == b'keep previous artifact'
                    checks += 1
                result = json.loads(run([compiler, '--no-stdlib', '--analyze', path], 1).stdout)
                assert not result['success'] and any(message.lower() in d['message'].lower()
                    for d in result['diagnostics']), (name, result['diagnostics'])
                checks += 1

        # Structural tuple identity cannot be rebound to a mutable user record
        # or alias, and declaration destructuring cannot silently create globals.
        reserved = [
            ('struct', 'struct tuple<T>\nfield: T\nend struct\n',
             '`tuple` of type T begin record\nfield item as T\nend record\n', 'reserved'),
            ('alias', 'type tuple = i64\n', '`tuple` means number\n', 'reserved'),
            ('global', 'let (first, second) = (1,2)\n',
             'value (first, second) is (1,2)\n', None),
        ]
        for name, ny, matrix, message in reserved:
            for language, declaration in [('ny', ny), ('matrix', matrix)]:
                path = root / ('reserved-' + name + '.' + language)
                path.write_text((declaration + 'fn main() -> i32\nreturn 0\nend fn\n')
                    if language == 'ny' else 'book reserved_tuple\n' + declaration +
                    'main returns override integer begin paragraph\ngive 0\nend paragraph\n')
                for unchecked in (False, True):
                    output = root / 'protected-reserved.nil'
                    output.write_bytes(b'unchanged')
                    result = run([compiler, '--quiet', '--no-stdlib', '--no-bundle',
                        *(['--no-typecheck'] if unchecked else []), '-o', output, path], 1)
                    if message: assert message in result.stderr.lower(), result.stderr
                    assert output.read_bytes() == b'unchanged'
                    checks += 1

        # Tuple-array iteration uses typed field projection in the interpreter;
        # native aggregate-array support remains a separate, explicit boundary.
        for language in ('ny', 'matrix'):
            for name, values, bindings, prints, expected in [
                ('pair', '[(1,"a"),(2,"b")]', '(amount,label)',
                 ['amount', 'label'], '1\na\n2\nb\n'),
                ('singleton', '[(1,),(2,)]', '(amount,)', ['amount'], '1\n2\n'),
            ]:
                for named in (False, True):
                    iterable = 'items' if named else values
                    if language == 'ny':
                        declaration = f'let items = {values}\n' if named else ''
                        body = declaration + f'for {bindings} in {iterable}\n'
                        body += ''.join(f'println({value})\n' for value in prints)
                        source = 'fn main() -> i32\n' + body + 'end for\nreturn 0\nend fn\n'
                    else:
                        declaration = f'value items is {values}\n' if named else ''
                        body = declaration + f'repeat for each {bindings} in {iterable}\n'
                        body += ''.join(f'say {value}\n' for value in prints)
                        source = ('book tuple_loop\nmain returns override integer begin paragraph\n' +
                                  body + 'end repeat\ngive 0\nend paragraph\n')
                    path = root / f'loop-{name}-{named}.{language}'
                    path.write_text(source)
                    artifact = root / 'loop.nil'
                    run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', artifact, path])
                    result = run([interpreter, '--no-ai', artifact])
                    assert result.stdout == expected and not result.stderr, result
                    assert '__ritual_tuple_get' not in artifact.read_text()
                    checks += 1
        if args.llvm == 'ON':
            # A direct construction isolates the layout boundary from the
            # separately unsupported generic native iterator fallback.
            path = root / 'tuple-array.ny'
            path.write_text('fn main() -> i32\nlet items = [(1,"a"),(2,"b")]\nreturn 0\nend fn\n')
            output = root / 'tuple-array.native'
            output.write_bytes(b'keep previous executable')
            result = run([compiler, '--quiet', '--no-stdlib', '--native', '-o', output, path], 1)
            assert "array element type 'tuple<" in result.stderr.lower(), result.stderr
            assert output.read_bytes() == b'keep previous executable'
            checks += 1
    print(f'tuple contract: {checks} checks passed')


if __name__ == '__main__':
    main()
