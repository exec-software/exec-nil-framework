#!/usr/bin/env python3
"""Tuple arrays preserve schemas, identity, and bounds across both frontends."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]


def program(language: str, body: str, declarations: str = '', imports: str = '') -> str:
    if language == 'ny':
        return (imports + declarations + 'fn main() -> i32\n' + body +
                '\nreturn 0\nend fn\n')
    return ('book tuple_array_contract\n' + imports + declarations +
            'main returns override integer begin paragraph\n' + body +
            '\ngive 0\nend paragraph\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler, interpreter, stdlib = (path.resolve() for path in
                                    (args.compiler, args.interpreter, args.stdlib))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_LIBRARY_PATH'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-tuple-array-contract-') as directory:
        root = Path(directory)

        def run(command, status=0):
            result = subprocess.run(command, cwd=root, env=environment,
                                    capture_output=True, text=True,
                                    stdin=subprocess.DEVNULL, timeout=120)
            assert result.returncode == status, (list(map(str, command)), result.returncode,
                                                result.stdout[:4000], result.stderr[:4000])
            return result

        for language in ('ny', 'matrix'):
            source = REPOSITORY / 'tests/language' / (
                'tuple-arrays.ny' if language == 'ny' else 'tuple-arrays-matrix.matrix')
            run([sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                 '--compiler', compiler, '--interpreter', interpreter, '--source', source,
                 '--expected-stdout', source.with_suffix('.stdout'), '--expected-exit', '0',
                 '--compiler-arg=--sdk-path', '--compiler-arg=' + str(stdlib),
                 '--routes', 'all' if args.llvm == 'ON' else
                 'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                 '--work-dir', root / language, '--strict'])
            checks += 6 if args.llvm == 'ON' else 4
            analysis = json.loads(run([compiler, '--sdk-path', stdlib, '--analyze',
                                      '--semantic-index', source]).stdout)
            assert analysis['success'], analysis['diagnostics']
            checks += 1

        # Cross-frontend consumers keep tuple schemas and array element identity,
        # including a binary provider whose original source file is absent.
        for provider_language in ('ny', 'matrix'):
            provider_root = root / ('provider-' + provider_language)
            provider_root.mkdir()
            provider = provider_root / ('provider.' + provider_language)
            provider.write_text(
                'module provider\npub fn make() -> array<(i64,text)>\n'
                'let pair: (i64,text) = (42,"cross")\nreturn [pair,pair]\nend fn\n'
                'pub fn identity<T>(items: array<T>) -> array<T>\nreturn items\nend fn\n'
                if provider_language == 'ny' else
                'book provider\npublic make returns array of tuple of (number,text) begin paragraph\n'
                'value pair as tuple of (number,text) is (42,"cross")\ngive [pair,pair]\nend paragraph\n'
                'public identity of type T receives items as array of T returns array of T begin paragraph\n'
                'give items\nend paragraph\n')
            consumer_language = 'matrix' if provider_language == 'ny' else 'ny'
            consumer = root / ('consumer-' + provider_language + '.' + consumer_language)
            consumer.write_text(program(consumer_language,
                'let values = make()\nlet alias = identity<(i64,text)>(values)\n'
                'alias[1] = (43 as i64,"updated")\n'
                'println(values[0].0)\nprintln(values[0].1)\n'
                'println(values[1].0)\nprintln(values[1].1)'
                if consumer_language == 'ny' else
                'value values is make with nothing\n'
                'value alias is identity of tuple of (number,text) with items being values\n'
                'set alias at 1 to ((43 becomes number),"updated")\n'
                'say (values at 0).0\nsay (values at 0).1\n'
                'say (values at 1).0\nsay (values at 1).1',
                imports='import provider.*\n' if consumer_language == 'ny' else
                        'read everything from provider\n'))
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
                    assert output.stdout == '42\ncross\n43\nupdated\n' and not output.stderr, output
                    checks += 1
                if args.llvm == 'ON':
                    executable = root / ('consumer-' + provider_language + '-' + dependency_mode)
                    run([compiler, '--quiet', '--no-stdlib', '--native', '-I', search,
                         '-o', executable, consumer])
                    output = run([executable])
                    assert output.stdout == '42\ncross\n43\nupdated\n' and not output.stderr, output
                    checks += 1

        invalid = [
            ('literal-schema', 'let items = [(1,"a"),(2,true)]',
             'value items is [(1,"a"),(2,true)]'),
            ('declared-schema', 'let items: array<(i32,text)> = [(1,true)]',
             'value items as array of tuple of (override integer,text) is [(1,true)]'),
            ('set-schema', 'let items = [(1,"a")]\nitems[0] = (2,true)',
             'value items is [(1,"a")]\nset items at 0 to (2,true)'),
            ('push-schema', 'let items = [(1,"a")]\nitems.push((2,true))',
             'value items is [(1,"a")]\nappend (2,true) to items'),
            ('set-arity', 'let items = [(1,"a")]\nitems[0] = (2,"b",true)',
             'value items is [(1,"a")]\nset items at 0 to (2,"b",true)'),
            ('push-width', 'let items = [(1,"a")]\nlet wrong = (2 as i64,"b")\nitems.push(wrong)',
             'value items is [(1,"a")]\nvalue wrong is ((2 becomes number),"b")\nappend wrong to items'),
            ('mutable-slot', 'let items = [(1,"a")]\nitems[0].0 = 2',
             'value items is [(1,"a")]\nset (items at 0).0 to 2'),
        ]
        for name, ny, matrix in invalid:
            for language, body in [('ny', ny), ('matrix', matrix)]:
                path = root / (name + '.' + language)
                path.write_text(program(language, body))
                for unchecked in (False, True):
                    for flags in (['--no-bundle'], ['--no-bundle', '--emit-binary-nil'],
                                  *([['--native']] if args.llvm == 'ON' else [])):
                        output = root / 'protected-output'
                        output.write_bytes(b'keep previous artifact')
                        result = run([compiler, '--quiet', '--no-stdlib', *flags,
                                      *(['--no-typecheck'] if unchecked else []),
                                      '-o', output, path], 1)
                        assert 'tuple' in result.stderr.lower(), (name, language, result.stderr)
                        assert output.read_bytes() == b'keep previous artifact'
                        checks += 1
                analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', path], 1).stdout)
                assert not analysis['success'] and analysis['diagnostics'], (name, language, analysis)
                checks += 1

        # Function parameters obey the same tuple storage identity contract as
        # locals and indexed writes, including unchecked compilation.
        for language in ('ny', 'matrix'):
            ny = language == 'ny'
            declaration = (
                'fn first_value(items: array<(i64,text)>) -> i64\n'
                'return items[0].0\nend fn\n' if ny else
                'first-value receives items as array of tuple of (number,text) returns number begin paragraph\n'
                'give (items at 0).0\nend paragraph\n')
            for name, values in [('schema', '[(1,true)]'),
                                 ('width', '[(1,"wrong-width")]'),
                                 ('wildcard', '[]')]:
                body = (f'let wrong = {values}\nprintln(first_value(wrong))' if ny else
                        f'value wrong is {values}\nsay first-value with items being wrong')
                path = root / ('argument-' + name + '.' + language)
                path.write_text(program(language, body, declaration))
                for unchecked in (False, True):
                    for flags in (['--no-bundle'], ['--no-bundle', '--emit-binary-nil'],
                                  *([['--native']] if args.llvm == 'ON' else [])):
                        output = root / 'protected-argument'
                        output.write_bytes(b'keep previous artifact')
                        result = run([compiler, '--quiet', '--no-stdlib', *flags,
                                      *(['--no-typecheck'] if unchecked else []),
                                      '-o', output, path], 1)
                        assert 'tuple' in result.stderr.lower(), (name, language, result.stderr)
                        assert output.read_bytes() == b'keep previous artifact'
                        checks += 1
                analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', path], 1).stdout)
                assert not analysis['success'] and any('tuple' in item['message'].lower()
                    for item in analysis['diagnostics']), (name, language, analysis)
                checks += 1

            body = (
                'println(first_value([(1,"literal")]))\n'
                'let exact: array<(i64,text)> = [(2,"existing")]\n'
                'println(first_value(exact))' if ny else
                'say first-value with items being [(1,"literal")]\n'
                'value exact as array of tuple of (number,text) is [(2,"existing")]\n'
                'say first-value with items being exact')
            path = root / ('argument-positive.' + language)
            path.write_text(program(language, body, declaration))
            stdout = path.with_suffix('.stdout')
            stdout.write_text('1\n2\n')
            run([sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                 '--compiler', compiler, '--interpreter', interpreter, '--source', path,
                 '--expected-stdout', stdout, '--expected-exit', '0',
                 '--compiler-arg=--no-stdlib', '--routes',
                 'all' if args.llvm == 'ON' else
                 'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                 '--work-dir', root / ('argument-positive-' + language), '--strict'])
            checks += 6 if args.llvm == 'ON' else 4
            for flags, suffix in [([], 'text'), (['--emit-binary-nil'], 'binary'),
                                  *([(['--native'], 'native')] if args.llvm == 'ON' else [])]:
                artifact = root / ('argument-positive-' + language + '-' + suffix)
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--no-typecheck',
                     *flags, '-o', artifact, path])
                result = run([artifact] if suffix == 'native' else [interpreter, '--no-ai', artifact])
                assert result.stdout == stdout.read_text() and not result.stderr, result
                checks += 1

        # Tuple bounds failures must be explicit: no fabricated tuple schema or
        # zero-valued record may be returned to a subsequent field projection.
        for language in ('ny', 'matrix'):
            for name, ny, matrix in [
                ('negative-index', 'let items = [(1,"a")]\nlet missing = items[-1]',
                 'value items is [(1,"a")]\nvalue missing is items at -1'),
                ('past-end-index', 'let items = [(1,"a")]\nlet missing = items[1]',
                 'value items is [(1,"a")]\nvalue missing is items at 1'),
                ('empty-index', 'let items: array<(i32,text)> = []\nlet missing = items[0]',
                 'value items as array of tuple of (override integer,text) is []\nvalue missing is items at 0'),
                ('intrinsic-get', 'let items = [(1,"a")]\nlet missing = get<(i32,text)>(items, 1)',
                 'value items is [(1,"a")]\nvalue missing is get of tuple of (override integer,text) with arr being items, idx being 1'),
            ]:
                path = root / (name + '.' + language)
                path.write_text(program(language, ny if language == 'ny' else matrix,
                    imports='import nil.std.collections\n' if language == 'ny' else
                            'read everything from nil.std.collections\n'))
                for flags, suffix in [([], 'text'), (['--emit-binary-nil'], 'binary'),
                                      *([(['--native'], 'native')] if args.llvm == 'ON' else [])]:
                    artifact = root / (name + '-' + language + '-' + suffix)
                    run([compiler, '--quiet', '--sdk-path', stdlib, '--no-bundle',
                         *flags, '-o', artifact, path])
                    command = [artifact] if suffix == 'native' else [interpreter, '--no-ai', artifact]
                    output = subprocess.run(command, cwd=root, env=environment, text=True,
                                            capture_output=True, timeout=30)
                    assert output.returncode != 0 and 'tuple' in output.stderr.lower(), (
                        name, language, suffix, output.returncode, output.stdout, output.stderr)
                    assert not output.stdout, output.stdout
                    checks += 1

        if args.llvm == 'ON':
            unsupported = [
                ('records', 'struct Record\nvalue: i32\nend struct\n',
                 'record-value begin record\nfield item as override integer\nend record\n',
                 'let items = [Record { value: 1 }]',
                 'value items is [(build record-value with item being 1)]'),
                ('enums', 'enum Choice\nOne\nend enum\n',
                 'choice-value begin choice\nwhen One\nend choice\n',
                 'let items = [Choice.One]', 'value items is [choice-value.One]'),
                ('nested-arrays', '', '', 'let items = [[1,2]]', 'value items is [[1,2]]'),
            ]
            for name, ny_decl, matrix_decl, ny_body, matrix_body in unsupported:
                for language, declaration, body in [('ny', ny_decl, ny_body),
                                                     ('matrix', matrix_decl, matrix_body)]:
                    path = root / ('unsupported-' + name + '.' + language)
                    path.write_text(program(language, body, declaration))
                    output = root / 'protected-native'
                    output.write_bytes(b'keep previous executable')
                    result = run([compiler, '--quiet', '--no-stdlib', '--native', '-o', output, path], 1)
                    assert ('[NativeCodegen]' in result.stderr and
                            'array' in result.stderr.lower()), result.stderr
                    assert output.read_bytes() == b'keep previous executable'
                    checks += 1
    print(f'tuple array contract: {checks} checks passed')


if __name__ == '__main__':
    main()
