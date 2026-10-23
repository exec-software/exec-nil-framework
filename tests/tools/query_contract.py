#!/usr/bin/env python3
"""Eager query binding, ordering, effects and collection terminals on both routes."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]
FIXTURES = REPOSITORY / 'tests/tools/fixtures/query'


def program(language, body, declarations='', imports=''):
    if language == 'ny':
        return imports + declarations + 'fn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
    return ('book query_contract\n' + imports + declarations +
            'main returns override integer begin paragraph\n' + body + '\ngive 0\nend paragraph\n')


def query(language, source='[1,2,3]', clauses='', projection='row', binder='row'):
    return (('collect query\nfrom ' if language == 'ny' else 'collect begin query\nfrom each ') +
            binder + ' in ' + source + '\n' + clauses +
            ('select ' if language == 'ny' else 'give ') + projection + '\nend query')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler, interpreter, stdlib = (p.resolve() for p in (args.compiler, args.interpreter, args.stdlib))
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil query contract ') as directory:
        root = Path(directory)

        def run(command, status=0, stdout=None, diagnostic=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root, env=environment,
                                    capture_output=True, text=True, stdin=subprocess.DEVNULL,
                                    timeout=120)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            if stdout is not None:
                assert result.stdout == stdout, (command, result.stdout, stdout, result.stderr)
            if diagnostic:
                assert diagnostic.lower() in result.stderr.lower(), (command, diagnostic, result.stderr)
            checks += 1
            return result

        def routes(source, expected, name, extra=('--no-stdlib',)):
            nonlocal checks
            command = [sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                       '--compiler', compiler, '--interpreter', interpreter, '--source', source,
                       '--expected-stdout', expected, '--expected-exit', '0', '--strict',
                       '--routes', 'all' if args.llvm == 'ON' else
                       'text-interpreter,binary-interpreter,bundle-text,bundle-binary',
                       '--work-dir', root / name]
            command += ['--compiler-arg=' + str(value) for value in extra]
            run(command)
            checks += 5 if args.llvm == 'ON' else 3

        for language in ('ny', 'matrix'):
            for fixture in ('eager', 'terminals'):
                routes(FIXTURES / (fixture + '.' + language), FIXTURES / (fixture + '.stdout'),
                       fixture + '-' + language,
                       ('--sdk-path', stdlib) if fixture == 'terminals' else ('--no-stdlib',))

            # Exact binder spans and query-local references reach the semantic
            # index. Query counts resolve the outer lexical name instead.
            prefix = 'let selected = ' if language == 'ny' else 'value selected is '
            body = query(language, clauses='where row > 1\n' if language == 'ny' else
                         'where row is greater than 1\n')
            source = root / ('binding.' + language)
            source.write_text(program(language, prefix + body))
            analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', '--semantic-index', source]).stdout)
            assert analysis['success'], analysis
            semantic = analysis['semantic']
            binder_offset = source.read_text().index('from row' if language == 'ny' else 'from each row')
            binder_offset += len('from ' if language == 'ny' else 'from each ')
            declarations = [s for s in semantic['symbols'] if s['start'] == binder_offset and s['name'] == 'row']
            assert len(declarations) == 1 and declarations[0]['length'] == 3, semantic
            references = [r for r in semantic['references'] if r['symbol_id'] == declarations[0]['id']]
            assert len(references) == 2, semantic
            checks += 1

            # Completion uses the same lexical scopes as query execution.
            # A partial identifier is enough to capture suggestions even though
            # ordinary analysis correctly reports the unfinished expression.
            outer = ('let sample_outer: i64 = 2\n' if language == 'ny' else
                     'value sample_outer as number is 2\n')
            comparison = 'sample_row > 1' if language == 'ny' else 'sample_row is greater than 1'
            expression = query(language, binder='sample_row', projection='sample_row',
                               clauses='where ' + comparison + '\nskip sample_outer\ntake 2\n')
            after = 'println(sample_outer)' if language == 'ny' else 'say sample_outer'
            complete_source = program(language, outer + prefix + expression + '\n' + after)
            row_keyword = 'select' if language == 'ny' else 'give'
            completion_cases = {
                'predicate': (complete_source.replace(comparison, comparison.replace('sample_row', 'sam|', 1)),
                              {'sample_outer': 'i64', 'sample_row': 'i32'}),
                'projection': (complete_source.replace(row_keyword + ' sample_row\n', row_keyword + ' sam|\n'),
                               {'sample_outer': 'i64', 'sample_row': 'i32'}),
                'count': (complete_source.replace('skip sample_outer', 'skip sam|'), {'sample_outer': 'i64'}),
                'after': (complete_source.replace(after, after.replace('sample_outer', 'sam|')),
                          {'sample_outer': 'i64'}),
            }
            for position, (marked, expected_items) in completion_cases.items():
                before, following = marked.split('|')
                snapshot = before + following
                cursor = len(before.encode())
                source = root / ('completion-' + position + '.' + language)
                source.write_text(snapshot)
                result = json.loads(run([compiler, '--no-stdlib', '--analyze', '--completion-at',
                                         cursor, source], 1).stdout)
                completion = result['completion']
                assert not result['success'] and completion is not None, result
                assert completion['schema'] == 'nil.completion.v1' and completion['partial'] is True
                assert completion['cursor'] == cursor and completion['path'] == str(source)
                assert snapshot.encode()[completion['start']:completion['start'] + completion['length']] == b'sam'
                assert {item['path']: item['text'] for item in completion['sources']}[str(source)] == snapshot
                if language == 'matrix':
                    expected_items = {name: {'i64': 'number', 'i32': 'override integer'}[type_name]
                                      for name, type_name in expected_items.items()}
                assert {item['name']: item['type'] for item in completion['items']} == expected_items, completion
                assert all(item['kind'] == 'variable' and item['insert_text'] == item['name']
                           for item in completion['items']), completion

            # With identical spellings, a count and a post-query use refer to
            # the outer i64 declaration; row clauses refer to the inner i32.
            shadow_source = complete_source.replace('sample_outer', 'sample_row')
            source = root / ('semantic-shadow.' + language)
            source.write_text(shadow_source)
            analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', '--semantic-index', source]).stdout)
            assert analysis['success'], analysis
            semantic = analysis['semantic']
            symbols = {item['id']: item for item in semantic['symbols']}
            shadow_references = {item['start']: item['symbol_id'] for item in semantic['references']
                                 if item['path'] == str(source)}
            def referred_at(context):
                offset = shadow_source.index(context) + context.index('sample_row')
                return symbols[shadow_references[len(shadow_source[:offset].encode())]]
            inner = referred_at('where sample_row')
            outer = referred_at('skip sample_row')
            assert inner['type'] == 'i32' and outer['type'] == 'i64' and inner['id'] != outer['id'], semantic
            assert referred_at(row_keyword + ' sample_row')['id'] == inner['id'], semantic
            assert referred_at('println(sample_row)' if language == 'ny' else 'say sample_row')['id'] == outer['id'], semantic

            for clause in ('skip', 'take'):
                source = root / ('negative-' + clause + '.' + language)
                source.write_text(program(language, prefix + query(language, clauses=clause + ' -1\n')))
                diagnostic = 'Query ' + clause + ' count must be nonnegative'
                for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                    artifact = root / ('negative-' + clause + '-' + language + '-' + encoding + '.nil')
                    run([compiler, '--quiet', '--no-stdlib', '--no-bundle', *flags, '-o', artifact, source])
                    run([interpreter, '--no-ai', artifact], 1, stdout='', diagnostic=diagnostic)
                if args.llvm == 'ON':
                    artifact = root / ('negative-' + clause + '-' + language + '.native')
                    run([compiler, '--quiet', '--no-stdlib', '--native', '-o', artifact, source])
                    run([artifact], 1, stdout='', diagnostic=diagnostic)
                    run([compiler, '--quiet', '--no-stdlib', '--native', '--jit', '-o',
                         root / ('negative-' + clause + '-' + language + '.jit'), source], 1,
                        stdout='', diagnostic=diagnostic)

            invalid = [
                ('source-scalar', query(language, source='7'), 'requires an array'),
                ('source-nested', query(language, source='[[1],[2]]'), 'requires an array'),
                ('source-empty-untyped', query(language, source='[]'), 'requires an array'),
                ('predicate-number', query(language, clauses='where 1\n'), 'must be boolean'),
                ('key-tuple', query(language, clauses='order by (row,row)\n'), 'order key'),
                ('count-float', query(language, clauses='take 1.5\n'), 'integer count'),
                ('count-bool', query(language, clauses='skip true\n'), 'integer count'),
                ('count-binder', query(language, clauses='take row\n'), 'Undefined identifier'),
                ('projection-array', query(language, projection='[row]'), 'projection requires'),
                ('duplicate', query(language, clauses='take 1\ntake 2\n'), 'clauses must appear once'),
                ('wrong-order', query(language, clauses='take 1\nwhere true\n'), 'clauses must appear once'),
                ('missing-projection', query(language).replace('select row\n' if language == 'ny' else 'give row\n', ''),
                 'requires a final'),
            ]
            for name, expression, diagnostic in invalid:
                source = root / (name + '.' + language)
                source.write_text(program(language, prefix + expression))
                for unchecked in (False, True):
                    for flags in (['--no-bundle'], ['--no-bundle', '--emit-binary-nil'],
                                  *([['--native']] if args.llvm == 'ON' else [])):
                        artifact = root / 'protected-output'
                        artifact.write_bytes(b'keep previous artifact')
                        result = run([compiler, '--quiet', '--no-stdlib', *flags,
                                      *(['--no-typecheck'] if unchecked else []), '-o', artifact, source], 1)
                        # Parser failures remain parser failures; unchecked
                        # semantic failures must fail closed during lowering.
                        expected = diagnostic if not unchecked else None
                        if expected:
                            assert expected.lower() in result.stderr.lower(), (name, result.stderr)
                        assert artifact.read_bytes() == b'keep previous artifact', name
                analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', source], 1).stdout)
                assert not analysis['success'] and analysis['diagnostics'], (name, analysis)

            record = ('struct Row\nvalue: i32\nend struct\n' if language == 'ny' else
                      'Row begin record\nfield value as override integer\nend record\n')
            record_value = 'Row { value: 1 }' if language == 'ny' else '(build Row with value being 1)'
            for name, source_expression, projection in (
                    ('record-source', '[' + record_value + ']', 'row'),
                    ('record-tuple', '[(' + record_value + ',1)]', 'row'),
                    ('record-projection', '[1]', record_value)):
                source = root / (name + '.' + language)
                source.write_text(program(language, prefix + query(language, source_expression, projection=projection), record))
                result = run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', root / (name + '.nil'), source], 1)
                assert 'query' in result.stderr.lower(), result.stderr

            for name, body, diagnostic in (
                    ('binder-escape', prefix + query(language) +
                     ('\nprintln(row)' if language == 'ny' else '\nsay row'), 'Undefined identifier'),
                    ('binder-immutable', prefix + query(language, projection='row = 3') if language == 'ny' else
                     prefix + query(language, clauses='where (row becomes text)\n'),
                     'immutable' if language == 'ny' else 'must be boolean')):
                source = root / (name + '.' + language)
                source.write_text(program(language, body))
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', root / (name + '.nil'), source], 1,
                    diagnostic=diagnostic)

        # Row expressions may run zero or many times. Match block arms are
        # currently rejected by lowering, but analysis must independently
        # enforce affine ownership before that unrelated restriction changes.
        for place, empty, zero in [('where', False, False), ('order by', False, False),
                                   ('select', False, False), ('select', True, False),
                                   ('select', False, True)]:
            value = 'true' if place == 'where' else '1'
            moving = 'match row { _ -> { let next = owned_move(owner); ' + value + '; } }'
            clauses = (place + ' ' + moving + '\n') if place != 'select' else ''
            if zero:
                clauses += 'take 0\n'
            body = ('let mut owner = owned<i32>(2)\nlet empty: array<i32> = []\nlet selected = ' +
                    query('ny', 'empty' if empty else '[1,2]', clauses,
                          moving if place == 'select' else 'row'))
            source = root / ('ownership-' + place.replace(' ', '-') + str(empty) + str(zero) + '.ny')
            source.write_text(program('ny', body))
            analysis = json.loads(run([compiler, '--no-stdlib', '--analyze', source], 1).stdout)
            assert any('cannot move an outer owner across a query boundary' in d['message']
                       for d in analysis['diagnostics']), analysis
            for unchecked in (False, True):
                artifact = root / 'ownership-protected-output'
                artifact.write_bytes(b'previous query artifact')
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle',
                     *(['--no-typecheck'] if unchecked else []), '-o', artifact, source], 1,
                    diagnostic=None if unchecked else 'query boundary')
                assert artifact.read_bytes() == b'previous query artifact'

        # Grammar words must be syntax, not quoted strings, escaped names or
        # Matrix name-folding aliases. Keep contextual identifiers available
        # outside the complete introducer (the eager fixture checks those).
        for language in ('ny', 'matrix'):
            valid = query(language, clauses='where true\norder by row ascending\n')
            malformed = {
                'quoted-open': valid.replace('query', '"query"', 1),
                'quoted-close': valid.replace('end query', 'end "query"'),
                'quoted-from': valid.replace('from ', '"from" ', 1),
                'quoted-clause': valid.replace('where true', '"where" true'),
                'quoted-direction': valid.replace('ascending', '"ascending"'),
                'escaped-open': valid.replace('query', '`query`', 1),
                'escaped-close': valid.replace('end query', 'end `query`'),
                'escaped-clause': valid.replace('where true', '`where` true'),
                'short-direction': valid.replace('ascending', 'asc'),
            }
            if language == 'matrix':
                malformed.update({
                    'hyphen-open': valid.replace('query', 'qu-ery', 1),
                    'underscore-close': valid.replace('end query', 'end que_ry'),
                    'hyphen-clause': valid.replace('where true', 'w-here true'),
                    'hyphen-direction': valid.replace('ascending', 'as-cending'),
                })
            for name, expression in malformed.items():
                source = root / ('grammar-' + name + '.' + language)
                prefix = 'let selected = ' if language == 'ny' else 'value selected is '
                source.write_text(program(language, prefix + expression))
                artifact = root / 'grammar-protected-output'
                artifact.write_bytes(b'previous query artifact')
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', artifact, source], 1)
                assert artifact.read_bytes() == b'previous query artifact'

        # Query bodies survive independent source and binary library imports;
        # each consumer uses the other language frontend.
        for language in ('ny', 'matrix'):
            provider_root = root / ('provider-' + language)
            provider_root.mkdir()
            provider = provider_root / ('provider.' + language)
            expression = query(language, clauses='order by row descending\n', projection='row')
            provider.write_text(('module provider\npub fn selected() -> array<i32>\nreturn ' + expression + '\nend fn\n')
                                if language == 'ny' else
                                ('book provider\npublic selected returns array of override integer begin paragraph\ngive ' + expression + '\nend paragraph\n'))
            consumer_language = 'matrix' if language == 'ny' else 'ny'
            consumer = root / ('consumer-' + language + '.' + consumer_language)
            consumer.write_text(program(consumer_language,
                'let values = selected()\nprintln(values[0])\nprintln(values[2])' if consumer_language == 'ny' else
                'value values is selected with nothing\nsay values at 0\nsay values at 2',
                imports='import provider.*\n' if consumer_language == 'ny' else 'read everything from provider\n'))
            expected = root / 'consumer.stdout'
            expected.write_text('3\n1\n')
            routes(consumer, expected, 'source-provider-' + language, ('--no-stdlib', '-I', provider_root))
            libraries = root / ('binary-' + language)
            libraries.mkdir()
            run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil', '--emit-library',
                 '-o', libraries / 'provider.nil', provider])
            provider.unlink()
            routes(consumer, expected, 'binary-provider-' + language, ('--no-stdlib', '-I', libraries))

    print(f'query contract: {checks} checks/routes passed')


if __name__ == '__main__':
    main()
