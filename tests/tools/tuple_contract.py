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
            ('duplicate-loop', 'for (same,same) in [(1,2)]\nprintln(same)\nend for',
             'repeat for each (same,same) in [(1,2)]\nsay same\nend repeat', 'already declared'),
            ('array-wildcard-alias', 'let unknown = []\nlet items: array<(i32,text)> = unknown',
             'value unknown is []\nvalue items as array of tuple of (override integer,text) is unknown',
             'Type mismatch'),
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

        # Every typed array expression uses index-based iteration. Exercise the
        # same generated suite through the interpreter, bundles, JIT and AOT.
        for language in ('ny', 'matrix'):
            ny = language == 'ny'
            declarations = ['let mut calls: i32 = 0\n' if ny else
                            'book tuple_loops\nvariable calls as override integer is 0\n']
            body, expected = [], []
            cases = [
                ('pairs', '[(1,"a"),(2,"b")]', '(amount,label)',
                 ['amount', 'label'], ['1', 'a', '2', 'b'],
                 '(i32,text)', 'tuple of (override integer,text)'),
                ('singles', '[(3,),(4,)]', '(amount,)', ['amount'], ['3', '4'],
                 '(i32,)', 'tuple of (override integer)'),
                ('whole', '[(5,"c"),(6,"d")]', 'item', ['item.0', 'item.1'],
                 ['5', 'c', '6', 'd'], '(i32,text)', 'tuple of (override integer,text)'),
                ('wildcards', '[(7,8,9),(10,11,12)]', '(_,amount,_)', ['amount'],
                 ['8', '11'], '(i32,i32,i32)',
                 'tuple of (override integer,override integer,override integer)'),
                ('scalars', '[13,14]', 'amount', ['amount'], ['13', '14'],
                 'i32', 'override integer'),
            ]
            for name, values, bindings, prints, output, ny_type, matrix_type in cases:
                factory = 'make_' + name
                declarations.append(
                    f'fn {factory}() -> array<{ny_type}>\ncalls = calls + 1\n'
                    f'return {values}\nend fn\n' if ny else
                    f'{factory} returns array of {matrix_type} begin paragraph\n'
                    f'set calls to calls plus 1\ngive {values}\nend paragraph\n')
                for mode in ('direct', 'named', 'call'):
                    if mode == 'named':
                        body.append(f'let {name} = {values}\n' if ny else
                                    f'value {name} is {values}\n')
                    iterable = (values if mode == 'direct' else name if mode == 'named' else
                                factory + ('()' if ny else ' with nothing'))
                    body.append(f'for {bindings} in {iterable}\n' if ny else
                                f'repeat for each {bindings} in {iterable}\n')
                    body.extend(f'println({value})\n' if ny else f'say {value}\n' for value in prints)
                    body.append('end for\n' if ny else 'end repeat\n')
                    expected.extend(output)
            # A call returning an array is evaluated once per loop, including
            # singleton tuple destructuring and ordinary tuple value bindings.
            body.append('println(calls)\n' if ny else 'say calls\n')
            expected.append(str(len(cases)))
            body.append(
                'let items = [(15,"outer")]\n'
                'for items in items\nprintln(items.0)\nend for\n'
                'for (items,_) in items\nprintln(items)\nend for\n'
                'println(items[0].0)\n'
                'let mut source = [(16,),(17,)]\n'
                'for (amount,) in source\nsource = [(90,),(91,)]\n'
                'println(amount)\nend for\n'
                'for (amount,label) in [(1,"first"),(2,"skip"),(3,"third"),(4,"stop")]\n'
                'if amount == 2 then\ncontinue\nend if\n'
                'if amount == 4 then\nbreak\nend if\n'
                'let amount = amount + 100\nprintln(amount)\nprintln(label)\nend for\n'
                if ny else
                'value items is [(15,"outer")]\n'
                'repeat for each items in items\nsay items.0\nend repeat\n'
                'repeat for each (items,_) in items\nsay items\nend repeat\n'
                'say (items at 0).0\n'
                'variable source is [(16,),(17,)]\n'
                'repeat for each (amount,) in source\nset source to [(90,),(91,)]\n'
                'say amount\nend repeat\n'
                'repeat for each (amount,label) in [(1,"first"),(2,"skip"),(3,"third"),(4,"stop")]\n'
                'when amount equals 2 then\ncontinue repeating\nend when\n'
                'when amount equals 4 then\nstop repeating\nend when\n'
                'value amount is amount plus 100\nsay amount\nsay label\nend repeat\n')
            expected.extend(['15', '15', '15', '16', '17', '101', 'first', '103', 'third'])
            source = root / ('typed-loops.' + language)
            source.write_text(''.join(declarations) +
                              ('fn main() -> i32\n' if ny else
                               'main returns override integer begin paragraph\n') + ''.join(body) +
                              ('return 0\nend fn\n' if ny else 'give 0\nend paragraph\n'))
            stdout = source.with_suffix('.stdout')
            stdout.write_text('\n'.join(expected) + '\n')
            run([sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                 '--compiler', compiler, '--interpreter', interpreter, '--source', source,
                 '--expected-stdout', stdout, '--expected-exit', '0',
                 '--compiler-arg=--no-stdlib', '--routes',
                 'all' if args.llvm == 'ON' else
                 'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                 '--work-dir', root / ('typed-loops-' + language), '--strict'])
            checks += 6 if args.llvm == 'ON' else 4
            # Unchecked mode must preserve exact array/tuple metadata too.
            for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil']),
                                    *([('native', ['--native'])] if args.llvm == 'ON' else [])]:
                artifact = root / ('typed-loops-' + language + '-' + encoding)
                run([compiler, '--quiet', '--no-stdlib', '--no-typecheck', '--no-bundle',
                     *flags, '-o', artifact, source])
                result = run([artifact] if encoding == 'native' else [interpreter, '--no-ai', artifact])
                assert result.stdout == stdout.read_text() and not result.stderr, result
                if encoding == 'text':
                    nil = artifact.read_text()
                    assert '__ritual_iter' not in nil and '__ritual_tuple_get' not in nil, nil
                checks += 1
    print(f'tuple contract: {checks} checks passed')


if __name__ == '__main__':
    main()
