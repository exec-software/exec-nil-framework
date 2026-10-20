#!/usr/bin/env python3
"""Visible compiler bindings at unfinished identifier expressions, in exact source snapshots."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    compiler = str(pathlib.Path(args.compiler).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-completion-') as directory:
        root = pathlib.Path(directory)

        def query(marked, *, name='main.ny', dependencies=None, mode='workspace', status=1,
                  available=True, extra=(), offset=None, enabled=True):
            nonlocal checks
            assert marked.count('|') == 1
            before, after = marked.split('|')
            source = before + after
            cursor = len(before.encode()) if offset is None else offset
            sources = {name: source, **(dependencies or {})}
            command = [compiler, '--analyze', '--no-stdlib', '-I', str(root), str(root / name)]
            if mode == 'workspace':
                command.append('--workspace-stdin')
                payload = json.dumps({'schema': 'nil.analysis.workspace.v1', 'sources': [
                    {'path': str(root / path), 'text': text} for path, text in sources.items()]})
            else:
                for path, text in sources.items():
                    if mode == 'disk' or path != name:
                        target = root / path
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(text.encode())
                payload = source if mode == 'stdin' else None
                if mode == 'stdin':
                    command.append('--source-stdin')
            if enabled:
                command += ['--completion-at', str(cursor)]
            result = subprocess.run(command + list(extra), input=payload, text=True,
                                    capture_output=True, timeout=30, cwd=root)
            assert result.returncode == status and not result.stderr, (result.returncode, result.stdout, result.stderr)
            value = json.loads(result.stdout)
            checks += 1
            assert value['success'] == (status == 0)
            if not enabled:
                assert 'completion' not in value
                return value
            completion = value['completion']
            if not available:
                assert completion is None, completion
                return value
            assert completion is not None, value
            assert completion['schema'] == 'nil.completion.v1' and completion['partial'] is True
            assert completion['path'] == str(root / name) and completion['cursor'] == cursor
            text = source.encode()
            assert 0 <= completion['start'] < cursor <= completion['start'] + completion['length'] <= len(text)
            text[completion['start']:completion['start'] + completion['length']].decode()
            snapshots = {item['path']: item['text'] for item in completion['sources']}
            assert len(snapshots) == len(completion['sources']) and snapshots[str(root / name)] == source
            for path, contents in sources.items():
                if str(root / path) in snapshots:
                    assert snapshots[str(root / path)] == contents
            assert len(completion['items']) <= 4096
            assert len({item['insert_text'] for item in completion['items']}) == len(completion['items'])
            assert all(item['kind'] in ('function', 'variable', 'parameter') for item in completion['items'])
            return completion

        def items(value):
            return {item['name']: item for item in value['items']}

        def applied(marked, completion, insertion):
            source = marked.replace('|', '').encode()
            updated = (source[:completion['start']] + insertion.encode() +
                       source[completion['start'] + completion['length']:]).decode()
            return updated[:len(updated)] + '|'

        # A partial unresolved name produces a completion snapshot even though
        # the normal analysis remains unsuccessful, on all existing input routes.
        source = ('# é😀\r\nfn compute(sample_parameter: i32) -> i32\r\n'
                  ' let sample_before = sample_parameter\r\n return sam|\r\n'
                  ' let sample_after = 4\r\nend fn\r\n')
        for mode in ('workspace', 'stdin', 'disk'):
            result = query(source, name=mode + '.ny', mode=mode)
            found = items(result)
            assert set(found) == {'sample_parameter', 'sample_before'}, found
            assert found['sample_parameter']['kind'] == 'parameter'
            query(applied(source, result, 'sample_before'), name=mode + '_applied.ny', status=0, available=False)

        # Lexical shadowing, leaving a block, own initializer order, loops,
        # and lambda parameters must use checker scope, never a file-wide list.
        shadow = ('fn compute(sample_parameter: i32) -> i32\nlet sample_value = 2\n'
                  'if true then\nlet sample_value = "inner"\nprintln(sam|)\nend if\nreturn 0\nend fn\n')
        found = items(query(shadow))
        assert found['sample_value']['type'] == 'String', found
        outer = shadow.replace('println(sam|)', 'println(sample_value)').replace('return 0', 'return sam|')
        assert items(query(outer))['sample_value']['type'] == 'i32'
        own = 'fn main() -> i32\nlet sample_new = sam|\nreturn 0\nend fn\n'
        assert not query(own)['items']
        loop = ('fn main() -> i32\nfor sample_index in [1,2]\nprintln(sam|)\nend for\nreturn 0\nend fn\n')
        assert items(query(loop))['sample_index']['kind'] == 'variable'
        after_loop = loop.replace('println(sam|)', 'println(sample_index)').replace('return 0', 'return sam|')
        assert not query(after_loop)['items']
        closure = ('fn main() -> i32\nlet sample_outer = 3\nlet callback = fn(sample_input: i32) -> i32\n'
                   'return sam|\nend fn\nreturn 0\nend fn\n')
        assert set(items(query(closure))) == {'sample_outer', 'sample_input'}
        forward = ('fn main() -> i32\nreturn sam|()\nend fn\n'
                   'fn sample_later() -> i32\nreturn 7\nend fn\n')
        assert items(query(forward))['sample_later']['kind'] == 'function'
        generic = ('fn identity<T>(sample_input: T) -> T\nreturn sam|ple_input\nend fn\n'
                   'fn main() -> i32\nlet first = identity<i32>(7)\n'
                   'let second = identity<i64>(9)\nreturn first\nend fn\n')
        assert items(query(generic, name='generic.ny', status=0))['sample_input']['type'] == 'T'

        # Mid-token edits replace the entire token, and Ny prefix matching is
        # case-sensitive. Builtins without a source identity are not invented.
        middle = 'fn main() -> i32\nlet sample_value = 4\nreturn sam|ple_missing\nend fn\n'
        result = query(middle)
        assert result['length'] == len('sample_missing') and set(items(result)) == {'sample_value'}
        query(applied(middle, result, 'sample_value'), name='mid_applied.ny', status=0, available=False)
        assert not query(middle.replace('sam|ple_missing', 'SAM|'))['items']
        assert not query(middle.replace('sam|ple_missing', 'pan|'))['items']

        # Both source frontends preserve native snapshots across imports and
        # filter private or selectively unimported functions.
        ny_dependency = ('module helper\npub fn sample_public() -> i32\nreturn 5\nend fn\n'
                         'fn sample_private() -> i32\nreturn 6\nend fn\n')
        matrix_dependency = ('book helper\npublic sample-public returns override integer begin paragraph\n'
                             'give 5\nend paragraph\nsample-private returns override integer begin paragraph\n'
                             'give 6\nend paragraph\n')
        for language, dependency_name, dependency in [
                ('matrix', 'helper.ny', ny_dependency), ('ny', 'helper.matrix', matrix_dependency)]:
            entry = ('book entry\nread everything from helper\nmain returns override integer begin paragraph\n'
                     'give sam|\nend paragraph\n' if language == 'matrix' else
                     'import helper.*\nfn main() -> i32\nreturn sam|()\nend fn\n')
            result = query(entry, name='imported.' + language, dependencies={dependency_name: dependency})
            expected = 'sample_public'
            assert set(items(result)) == {expected}, result
            assert {source['path'] for source in result['sources']} == {str(root / ('imported.' + language)), str(root / dependency_name)}
        selective = 'import helper::{sample_public}\nfn main() -> i32\nreturn sam|()\nend fn\n'
        dependency = ny_dependency + 'pub fn sample_other() -> i32\nreturn 8\nend fn\n'
        assert set(items(query(selective, dependencies={'helper.ny': dependency}))) == {'sample_public'}

        # Matrix matching folds ASCII case and separators but preserves the
        # original display name, including Unicode and escaped identifiers.
        matrix = ('book fold-check\r\n!!! é😀\r\ncompute receives café-value as number returns number begin paragraph\r\n'
                  'value sample-item is café-value\r\ngive SAM_P|\r\nend paragraph\r\n')
        result = query(matrix, name='folded.matrix')
        assert set(items(result)) == {'sample-item'} and items(result)['sample-item']['insert_text'] == 'sample-item'
        query(applied(matrix, result, 'sample-item'), name='fold_applied.matrix', status=0, available=False)
        escaped = ('book escaped\ncompute returns number begin paragraph\nvalue `value` is 4\n'
                   'give `val|x`\nend paragraph\n')
        result = query(escaped, name='escaped.matrix')
        assert items(result)['value']['insert_text'] == '`value`' and result['length'] == len('`valx`')
        query(applied(escaped, result, '`value`'), name='escaped_applied.matrix', status=0, available=False)
        unicode_query = matrix.replace('SAM_P|', 'CAFé|')
        assert set(items(query(unicode_query, name='unicode.matrix'))) == {'café-value'}
        cursor = len(unicode_query.split('|')[0].encode()) - 1
        query(unicode_query, name='unicode_boundary.matrix', available=False, offset=cursor)

        # Parse/import/earlier-checker failures and unsupported positions must
        # remain explicit nulls. No heuristic type/member/comment completion.
        query('fn main( sam|', name='parse.ny', available=False)
        query('import absent_dependency\nfn main() -> i32\nreturn sam|\nend fn\n', name='import_error.ny', available=False)
        query('fn main() -> i32\nprintln(missing)\nlet sample = 7\nreturn sam|\nend fn\n', name='prior_error.ny', available=False)
        query('fn main() -> i32\n# sam|\nreturn 0\nend fn\n', name='comment.ny', status=0, available=False)
        query('fn main() -> i32\nprintln("sam|")\nreturn 0\nend fn\n', name='literal.ny', status=0, available=False)
        query('fn main() -> sam|\nreturn 0\nend fn\n', name='type.ny', available=False)
        query('fn main() -> i32\nreturn namespace::sam|()\nend fn\n', name='qualified.ny', available=False)
        query('struct Sample\nitem: i32\nend struct\nfn main() -> i32\nlet box = Sample { item: 4 }\nreturn box.it|\nend fn\n', name='member.ny', available=False)
        query('@define SAMPLE 7\nfn main() -> i32\nlet sample = SAMPLE\nreturn sam|\nend fn\n', name='macro.ny', available=False)
        query('fn main() -> i32\nreturn 0|\nend fn\n', name='optional.ny', status=0, enabled=False)

        # Independent result budgets withhold the optional query rather than
        # return truncated candidates or omit contributing source snapshots.
        large = '# ' + 'x' * (1500 * 1024) + '\n'
        sources = {'a.ny': large + 'module a\npub fn sample_first() -> i32\nreturn 1\nend fn\n',
                   'b.ny': large + 'module b\npub fn sample_second() -> i32\nreturn 2\nend fn\n'}
        query(large + 'import a.*\nimport b.*\nfn main() -> i32\nreturn sam|()\nend fn\n',
              name='large.ny', dependencies=sources, available=False)
        many = ''.join(f'fn sample_{index}() -> i32\nreturn 0\nend fn\n' for index in range(4097))
        query(many + 'fn main() -> i32\nreturn sam|()\nend fn\n', name='many.ny', available=False)
        imports = ','.join(f'candidate_{index}' for index in range(32769))
        bounded = query('import named_budget::{' + imports + '}\nfn main() -> i32\nreturn sam|\nend fn\n',
                        name='import_budget.ny', dependencies={'named_budget.ny': 'module named_budget\n'}, available=False)
        assert bounded['diagnostics'] and all(item['code'] == 'NIL0002' for item in bounded['diagnostics']), bounded

        # A binary-only dependency cannot be freshness-validated through native
        # source snapshots, even when all visible candidates are local values.
        libraries = root / 'libraries'
        libraries.mkdir()
        binary_source = root / 'build_binary.ny'
        binary_source.write_text('module binary_dependency\n')
        compiled = subprocess.run([compiler, '--no-stdlib', '--no-bundle', '--emit-binary-nil',
            '--emit-library', '-q', '-o', str(libraries / 'binary_dependency.nil'), str(binary_source)],
            text=True, capture_output=True, timeout=20, cwd=root)
        assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
        checks += 1
        query('import binary_dependency\nfn main() -> i32\nlet sample = 4\nreturn sam|\nend fn\n',
              name='binary.ny', extra=('-I', str(libraries)), available=False)

        for spelling in ('-1', '1.0', 'invalid', '2097153'):
            result = subprocess.run([compiler, '--analyze', '--completion-at', spelling, str(root / 'main.ny')],
                                    text=True, capture_output=True, timeout=20, cwd=root)
            assert result.returncode == 2 and not result.stdout and 'completion' in result.stderr
            checks += 1
    print(f'completion contract: {checks} compiler checks passed')


if __name__ == '__main__':
    main()
