#!/usr/bin/env python3
"""Compiler-resolved definition locations for exact workspace source snapshots."""
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
    with tempfile.TemporaryDirectory(prefix='nil-semantic-') as directory:
        root = pathlib.Path(directory)

        def analyze(sources, primary, *, status=0, semantic=True, extra=(), mode="workspace"):
            nonlocal checks
            command = [compiler, '--analyze', '--no-stdlib', '-I', str(root), str(root / primary)]
            if mode == 'workspace':
                command.append('--workspace-stdin')
                payload = json.dumps({'schema': 'nil.analysis.workspace.v1',
                    'sources': [{'path': str(root / name), 'text': text} for name, text in sources.items()]})
            else:
                assert mode in ('stdin', 'disk')
                for name, text in sources.items():
                    if mode == 'disk' or name != primary:
                        path = root / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(text.encode())
                payload = sources[primary] if mode == 'stdin' else None
                if mode == 'stdin':
                    command.append('--source-stdin')
            if semantic:
                command.append('--semantic-index')
            result = subprocess.run(command + list(extra), input=payload,
                text=True, capture_output=True, timeout=20, cwd=root)
            assert result.returncode == status and not result.stderr, (result.returncode, result.stdout, result.stderr)
            value = json.loads(result.stdout)
            assert value['success'] == (status == 0)
            checks += 1
            if not semantic:
                assert 'semantic' not in value
                return value
            if status:
                assert value['semantic'] is None, value
                return value
            index = value['semantic']
            if index is None:
                return value
            assert index['schema'] == 'nil.semantic.v1' and index['partial'] is True
            snapshots = {item['path']: item['text'] for item in index['sources']}
            assert len(snapshots) == len(index['sources']) and str(root / primary) in snapshots
            for name, text in sources.items():
                if str(root / name) in snapshots:
                    assert snapshots[str(root / name)] == text
            assert all(type(item['id']) is int and item['id'] >= 0 for item in index['symbols'])
            ids = {item['id'] for item in index['symbols']}
            assert len(ids) == len(index['symbols'])
            for item in index['symbols'] + index['references']:
                text = snapshots[item['path']].encode()
                assert item['length'] > 0
                selected = text[item['start']:item['start'] + item['length']].decode()
                assert selected and len(selected.encode()) == item['length']
            assert all(item['symbol_id'] in ids for item in index['references'])
            assert len({(item['path'], item['start'], item['length']) for item in index['references']}) == len(index['references'])
            policy = index['rename_policy']
            assert policy['schema'] == 'nil.rename.v1'
            assert len({item['path'] for item in policy['sources']}) == len(policy['sources'])
            for item in policy['sources']:
                assert item['path'] in snapshots and item['language'] in ('ny', 'matrix')
                assert len(item['identifier_keys']) <= 32768
                for key in ('identifier_keys', 'reserved_keys'):
                    assert len(set(item[key])) == len(item[key])
                    assert all(isinstance(name, str) and name for name in item[key])
            assert sum(len(item['identifier_keys']) for item in policy['sources']) <= 131072
            for item in index['symbols']:
                if item.get('references_complete') or item.get('rename_safe'):
                    assert item['references_complete'] is True
                    assert item['kind'] == 'variable'
                    assert all(use['path'] == item['path'] for use in index['references'] if use['symbol_id'] == item['id'])
                    if item.get('rename_safe'):
                        assert item['rename_safe'] is True
                        assert item['path'] in {source['path'] for source in policy['sources']}
            return value

        def binding(result, file, context, token):
            index = result['semantic']
            assert index is not None, result
            source = next(item['text'] for item in index['sources'] if item['path'] == str(root / file))
            offset = source.index(context) + context.index(token)
            start = len(source[:offset].encode())
            references = [item for item in index['references'] if item['path'] == str(root / file) and item['start'] == start]
            assert len(references) == 1, (context, references, index)
            declaration = next(item for item in index['symbols'] if item['id'] == references[0]['symbol_id'])
            return declaration

        def rename_local(result, file, target, new_name):
            """Exercise emitted edits through a second independent compiler check."""
            index = result['semantic']
            assert target['references_complete'] and target['rename_safe']
            source = next(item['text'] for item in index['sources'] if item['path'] == str(root / file))
            uses = [item for item in index['references'] if item['symbol_id'] == target['id']]
            text = source.encode()
            for item in sorted([target] + uses, key=lambda item: item['start'], reverse=True):
                text = text[:item['start']] + new_name.encode() + text[item['start'] + item['length']:]
            renamed = analyze({file: text.decode()}, file)
            name = new_name.replace('-', '_') if file.endswith(('.mat', '.matrix')) else new_name
            declaration = next(item for item in renamed['semantic']['symbols'] if item['name'] == name)
            assert declaration['rename_safe'] and declaration['references_complete']
            assert len([item for item in renamed['semantic']['references'] if item['symbol_id'] == declaration['id']]) == len(uses)
            return renamed

        source = '# é😀\r\n \t\r\nfn compute(value: i32) -> i32\r\n let mut item = value\r\n if true then\r\n  let item = 2\r\n  println(item)\r\n end if\r\n item = item + 1\r\n return item\r\nend fn'
        value = analyze({'main.ny': source}, 'main.ny')
        parameter = binding(value, 'main.ny', '= value', 'value')
        inner = binding(value, 'main.ny', 'println(item)', 'item')
        outer = binding(value, 'main.ny', 'return item', 'item')
        assignment = binding(value, 'main.ny', 'item = item', 'item')
        assert parameter['kind'] == 'parameter' and parameter['start'] == len(source[:source.index('value:')].encode())
        assert inner['id'] != outer['id'] and assignment['id'] == outer['id']
        assert inner['start'] == len(source[:source.index('item = 2')].encode())
        assert inner['references_complete'] and inner['rename_safe']
        assert outer['references_complete'] and outer['rename_safe']
        assert not parameter.get('rename_safe') and not parameter.get('references_complete')
        assert not next(item for item in value['semantic']['symbols'] if item['kind'] == 'function').get('rename_safe')
        policy = value['semantic']['rename_policy']['sources'][0]
        assert {'compute', 'value', 'item', 'i32'} <= set(policy['identifier_keys'])
        assert {'fn', 'function', 'fun', 'use', 'using', 'asm', 'and', 'or', '_', 'owned_move', 'parallel_map'} <= set(policy['reserved_keys'])
        renamed = rename_local(value, 'main.ny', outer, 'updated_item')
        assert binding(renamed, 'main.ny', 'println(item)', 'item')['name'] == 'item'
        assert binding(renamed, 'main.ny', 'return updated_item', 'updated_item')['name'] == 'updated_item'
        analyze({'main.ny': source}, 'main.ny', semantic=False)

        ny = 'module helper\npub fn fetch_value(input: i32) -> i32\n return input\nend fn\n'
        matrix = 'book helper\npublic fetch-value receives value-item as override integer returns override integer begin paragraph\n give value-item\nend paragraph\n'
        for language, dependency in [('matrix', ny), ('ny', matrix)]:
            entry = ('book entry\nread everything from helper\nmain returns override integer begin paragraph\n give (fetch-value with input being 7)\nend paragraph\n' if language == 'matrix' else
                     'import helper.*\nfn main() -> i32\n return fetch_value(7)\nend fn\n')
            name = 'main.' + language
            dep = 'helper.' + ('ny' if language == 'matrix' else 'matrix')
            result = analyze({name: entry, dep: dependency}, name)
            spelling = 'fetch-value' if language == 'matrix' else 'fetch_value'
            target = binding(result, name, spelling, spelling)
            assert target['path'] == str(root / dep) and target['kind'] == 'function'
            assert not (root / dep).exists()

        forward = 'fn main() -> i32\n return later(7)\nend fn\nfn later(value: i32) -> i32\n return value\nend fn\n'
        result = analyze({'forward.ny': forward}, 'forward.ny')
        assert binding(result, 'forward.ny', 'return later', 'later')['start'] == forward.index('later(value')
        bad = forward.replace('return later(7)', 'return missing(7)')
        analyze({'forward.ny': bad}, 'forward.ny', status=1)

        # Loop variables and function-valued calls resolve to lexical bindings,
        # including lambda parameters whose scope disappears after checking.
        loops = ('fn loop_values() -> i32\nlet mut total=0\nfor index in [1,2]\n'
                 ' total = total + index\nend for\nreturn total\nend fn\n')
        result = analyze({'loops.ny': loops}, 'loops.ny')
        target = binding(result, 'loops.ny', '+ index', 'index')
        assert target['kind'] == 'variable' and target['start'] == loops.index('index in')
        assert target['rename_safe'] and target['references_complete']
        closures = ('fn run(callback: fn(i32) -> i32) -> i32\nreturn callback(7)\nend fn\n'
                    'fn main() -> i32\nlet local = fn(item: i32) -> i32\nreturn item\nend fn\n'
                    'return run(local)\nend fn\n')
        result = analyze({'closures.ny': closures}, 'closures.ny')
        assert binding(result, 'closures.ny', 'return callback', 'callback')['kind'] == 'parameter'
        assert binding(result, 'closures.ny', 'return item', 'item')['start'] == closures.index('item:')
        assert binding(result, 'closures.ny', 'run(local)', 'local')['kind'] == 'variable'
        assert binding(result, 'closures.ny', 'run(local)', 'local')['rename_safe']
        assert not binding(result, 'closures.ny', 'return item', 'item').get('rename_safe')

        # Repeated generic specialization must retain the source declaration,
        # never expose a generated mangled name or duplicate source occurrence.
        generic = ('fn identity<T>(input: T) -> T\nreturn input\nend fn\n'
                   'fn main() -> i32\nlet first=identity<i32>(7)\n'
                   'let second=identity<i64>(9)\nreturn first\nend fn\n')
        result = analyze({'generic.ny': generic}, 'generic.ny')
        first = binding(result, 'generic.ny', 'first=identity', 'identity')
        second = binding(result, 'generic.ny', 'second=identity', 'identity')
        assert first['id'] == second['id'] and first['start'] == generic.index('identity<T>')
        assert [symbol['name'] for symbol in result['semantic']['symbols']].count('identity') == 1

        # Bare Matrix names fold ASCII case and separators, while reported
        # byte ranges still select each exact UTF-8 spelling, without backticks.
        folded = ('book fold-check\r\n!!! é😀\r\n'
                  'compute receives café-value as number returns number begin paragraph\r\n'
                  ' value local-item is CAFé_VALUE\r\n give LOCALITEM\r\nend paragraph\r\n')
        result = analyze({'folded.matrix': folded}, 'folded.matrix')
        parameter = binding(result, 'folded.matrix', 'is CAFé_VALUE', 'CAFé_VALUE')
        assert parameter['start'] == len(folded[:folded.index('café-value')].encode())
        assert parameter['length'] == len('café-value'.encode())
        target = binding(result, 'folded.matrix', 'give LOCALITEM', 'LOCALITEM')
        assert target['start'] == len(folded[:folded.index('local-item')].encode())
        assert target['rename_safe']
        policy = result['semantic']['rename_policy']['sources'][0]
        assert policy['language'] == 'matrix' and 'localitem' in policy['identifier_keys']
        assert 'local-item' not in policy['identifier_keys'] and 'LOCALITEM' not in policy['identifier_keys']
        assert {'value', 'paragraph', 'receives', 'ownedmove', 'negative', 'tuple'} <= set(policy['reserved_keys'])
        rename_local(result, 'folded.matrix', target, 'updated-count')
        escaped = ('book escaped-check\ncompute receives `value` as number returns number begin paragraph\n'
                   ' give `value`\nend paragraph\n')
        result = analyze({'escaped.matrix': escaped}, 'escaped.matrix')
        target = binding(result, 'escaped.matrix', 'give `value`', 'value')
        assert target['start'] == escaped.index('`value`') + 1 and target['length'] == 5

        # Backtick delimiters remain outside the editable range, while an
        # unused local still has a complete empty reference set.
        escaped_local = ('book escaped-local\ncompute returns number begin paragraph\n'
                         ' value `value` is 3\n value spare-item is 5\n give `value`\nend paragraph\n')
        result = analyze({'escaped_local.matrix': escaped_local}, 'escaped_local.matrix')
        target = binding(result, 'escaped_local.matrix', 'give `value`', 'value')
        renamed = rename_local(result, 'escaped_local.matrix', target, 'updated-count')
        assert '`updated-count`' in renamed['semantic']['sources'][0]['text']
        spare = next(item for item in result['semantic']['symbols'] if item['name'] == 'spare_item')
        assert spare['rename_safe'] and not any(item['symbol_id'] == spare['id'] for item in result['semantic']['references'])

        # Globals and named parameters have potential callers outside this
        # snapshot; neither is certified as a closed lexical editing target.
        globals_source = ('let shared = 3\nfn compute(input: i32) -> i32\n'
                          'let local = shared + input\nreturn local\nend fn\n')
        result = analyze({'globals.ny': globals_source}, 'globals.ny')
        assert not binding(result, 'globals.ny', 'local = shared', 'shared').get('references_complete')
        assert not binding(result, 'globals.ny', '+ input', 'input').get('rename_safe')
        assert binding(result, 'globals.ny', 'return local', 'local')['rename_safe']
        forward_global = ('fn main() -> i32\nlet local = later\nreturn local\nend fn\n'
                          'let later: i32 = 5\n')
        result = analyze({'forward_global.ny': forward_global}, 'forward_global.ny')
        assert not binding(result, 'forward_global.ny', 'local = later', 'later').get('rename_safe')
        assert binding(result, 'forward_global.ny', 'return local', 'local')['rename_safe']

        # An inline-assembly output is a real checker use without a retained
        # name span. Exhaustive token coverage catches it and withholds edits.
        assembly = ('fn main() -> i32\nlet mut result: i64 = 0\nlet other = 3\n'
                    'asm "x86_64", "movq $1, $0" (42 as i64) -> result end asm\n'
                    'println(result)\nreturn other\nend fn\n')
        result = analyze({'assembly.ny': assembly}, 'assembly.ny')
        assert not binding(result, 'assembly.ny', 'println(result)', 'result').get('rename_safe')
        assert not binding(result, 'assembly.ny', 'return other', 'other').get('rename_safe')
        assert result['semantic']['rename_policy']['sources'] == []

        # Matrix named arguments follow the checked callable, so certified
        # callback locals may be renamed even when shadowing paragraph names.
        signature_shadow = ('book order-check\n'
            'operation receives first-part as number, second-part as number returns number begin paragraph\n'
            ' give first-part times 100 plus second-part\nend paragraph\n'
            'alternate receives first-part as number, second-part as number returns number begin paragraph\n'
            ' give first-part times 100 plus second-part\nend paragraph\n'
            'main returns number begin paragraph\n value operation is the paragraph alternate\n'
            ' say operation with second-part being 2, first-part being 1\n give 0\nend paragraph\n')
        result = analyze({'signature_shadow.matrix': signature_shadow}, 'signature_shadow.matrix')
        target = binding(result, 'signature_shadow.matrix', 'say operation', 'operation')
        assert target['kind'] == 'variable' and target['references_complete']
        assert target['rename_safe']
        assert len(result['semantic']['rename_policy']['sources']) == 1

        # Unindexed field/type tokens sharing a spelling cannot be mistaken
        # for a complete local reference set, even when ordinary reads work.
        fields = ('struct Box\nitem: i32\nend struct\nfn main() -> i32\n'
                  'let item = 3\nlet box = Box { item: item }\nreturn item\nend fn\n')
        result = analyze({'fields.ny': fields}, 'fields.ny')
        assert not binding(result, 'fields.ny', 'return item', 'item').get('references_complete')
        assert next(item for item in result['semantic']['symbols'] if item['name'] == 'box')['rename_safe']

        hidden = ('fn main() -> i32\nlet item = 7\nlet other = 9\n'
                  'println("{item}")\nreturn other\nend fn\n')
        result = analyze({'hidden.ny': hidden}, 'hidden.ny')
        assert not any(item.get('rename_safe') for item in result['semantic']['symbols'])
        assert result['semantic']['rename_policy']['sources'] == []

        # Lookup must honor actual imported identity across qualified paths,
        # selective imports and aliases, not match the first similar spelling.
        dependency = 'module library.helpers\npub fn answer() -> i32\n return 9\nend fn\n'
        for ordinal, (imports, call) in enumerate([
                ('import library.helpers', 'library.helpers::answer'),
                ('import library.helpers::{answer}', 'answer'),
                ('import library.helpers\nnamespace short = library.helpers', 'short::answer')]):
            name = f'import{ordinal}.ny'
            source = imports + '\nfn main() -> i32\n return ' + call + '()\nend fn\n'
            result = analyze({name: source, 'library/helpers.ny': dependency}, name)
            target = binding(result, name, 'return ' + call, 'answer')
            assert target['path'] == str(root / 'library/helpers.ny')
            assert target['start'] == dependency.index('answer()')

        # The optional index is also valid through existing disk/raw-buffer
        # analysis entry points. A raw primary overrides deliberately bad disk.
        route_dependency = 'module routehelper\npub fn route_value() -> i32\nreturn 5\nend fn\n'
        for mode in ('stdin', 'disk'):
            name = mode + '_route.ny'
            source = 'import routehelper.*\nfn main() -> i32\nreturn route_value()\nend fn\n'
            if mode == 'stdin':
                (root / name).write_text('this disk source is invalid')
            result = analyze({name: source, 'routehelper.ny': route_dependency}, name, mode=mode)
            assert binding(result, name, 'return route_value', 'route_value')['path'] == str(root / 'routehelper.ny')
            if mode == 'stdin':
                assert (root / name).read_text() == 'this disk source is invalid'

        # Explicit formatting participates in the ordinary source binding and
        # closed-local rename proof, including Unicode and decoded prefixes.
        for suffix, formatted in [
                ('ny', '# é😀\r\nfn message() -> i32\r\nlet local_value = 7\r\n'
                 'println($"\\n{{local_value}} {local_value + 1}")\r\nreturn local_value\r\nend fn\r\n'),
                ('matrix', '!!! é😀\r\nbook formatted-editing\r\nmessage returns number begin paragraph\r\nvalue local-value is 7\r\n'
                 'say formatted "\\n{{local-value}} {local-value plus 1}"\r\ngive local-value\r\nend paragraph\r\n')]:
            name = 'formatted.' + suffix
            result = analyze({name: formatted}, name)
            spelling = 'local_value' if suffix == 'ny' else 'local-value'
            context = '{' + spelling + (' +' if suffix == 'ny' else ' plus')
            target = binding(result, name, context, spelling)
            assert target['rename_safe'] and target['references_complete'], target
            assert len([use for use in result['semantic']['references'] if use['symbol_id'] == target['id']]) == 2
            renamed = rename_local(result, name, target, 'renamed_value' if suffix == 'ny' else 'renamed-value')
            snapshot = next(item['text'] for item in renamed['semantic']['sources'] if item['path'] == str(root / name))
            assert '{{' + spelling + '}}' in snapshot

        interpolation = ('fn message(input: i32) -> i32\nprintln("\\n{input}")\nreturn input\nend fn\n')
        result = analyze({'interpolation.ny': interpolation}, 'interpolation.ny')
        assert len(result['semantic']['references']) == 1
        assert binding(result, 'interpolation.ny', 'return input', 'input')['kind'] == 'parameter'

        # Parser failure, checker failure and an analyzer-policy failure all
        # withhold navigation rather than exporting a partly checked graph.
        analyze({'parse_error.ny': 'fn main( -> i32'}, 'parse_error.ny', status=1)
        policy = 'fn main() -> i32 { return 0 }  \n'
        failed = analyze({'policy.ny': policy}, 'policy.ny', status=1, extra=('--warnings-as-errors',))
        assert failed['reflection']['semantic_checked']
        assert any(item['code'] == 'NIL1001' for item in failed['diagnostics'])

        expanded = '@define VALUE 7\nfn main() -> i32\n let value = VALUE\n return value\nend fn\n'
        result = analyze({'expanded.ny': expanded}, 'expanded.ny')
        assert not result['semantic']['symbols'] and not result['semantic']['references']
        # Input snapshots include a reached source even if it contributes no
        # supported declaration/name span to the initial index.
        include_text = 'fn included() -> i32\r\n return 7\r\nend fn\r\n'
        (root / 'hidden.inc').write_bytes(include_text.encode())
        for mode in ('workspace', 'stdin', 'disk'):
            name = mode + '_includes.ny'
            result = analyze({name: '@include "hidden.inc"\nfn main() -> i32\n return included()\nend fn\n'}, name, mode=mode)
            snapshots = {item['path']: item['text'] for item in result['semantic']['sources']}
            assert snapshots[str(root / 'hidden.inc')] == include_text
            assert not result['semantic']['symbols'] and not result['semantic']['references']

        # Binary libraries lack a native-source snapshot for their artifact.
        # Even an import with no exported symbols must disable the optional
        # index, while leaving ordinary checking and source precedence intact.
        library_directory = root / 'libraries'
        library_directory.mkdir()
        binary_source = 'module binary_dependency\npub fn binary_answer() -> i32\nreturn 17\nend fn\n'
        for module, contents in [('binary_dependency', binary_source),
                                 ('empty_dependency', 'module empty_dependency\n')]:
            source_path = root / ('build_' + module + '.ny')
            source_path.write_text(contents)
            compiled = subprocess.run([compiler, '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                '--emit-library', '-q', '-o', str(library_directory / (module + '.nil')), str(source_path)],
                text=True, capture_output=True, timeout=20, cwd=root)
            assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
            checks += 1
        binary_entry = 'import binary_dependency.*\nfn main() -> i32\nreturn binary_answer()\nend fn\n'
        for mode in ('workspace', 'stdin', 'disk'):
            name = mode + '_binary.ny'
            result = analyze({name: binary_entry}, name, mode=mode, extra=('-I', str(library_directory)))
            assert result['semantic'] is None and result['reflection']['semantic_checked']
        empty_entry = 'import empty_dependency\nfn main() -> i32\nreturn 0\nend fn\n'
        result = analyze({'empty_binary.ny': empty_entry}, 'empty_binary.ny', extra=('-I', str(library_directory)))
        assert result['semantic'] is None and result['reflection']['semantic_checked']
        result = analyze({'source_precedence.ny': binary_entry,
                          'libraries/binary_dependency.ny': binary_source},
                         'source_precedence.ny', extra=('-I', str(library_directory)))
        assert binding(result, 'source_precedence.ny', 'return binary_answer', 'binary_answer')['path'] == str(library_directory / 'binary_dependency.ny')

        # An index too large for its independent budget must not suppress
        # diagnostics or truncate its input set silently.
        large = '# ' + 'x' * (1500 * 1024) + '\n'
        sources = {'large.ny': 'import a.*\nimport b.*\nfn main() -> i32\n return first() + second()\nend fn\n',
                   'a.ny': large + 'module a\npub fn first() -> i32\n return 1\nend fn\n',
                   'b.ny': large + 'module b\npub fn second() -> i32\n return 2\nend fn\n'}
        sources['large.ny'] = large + sources['large.ny']
        assert analyze(sources, 'large.ny')['semantic'] is None
    print(f'semantic index contract: {checks} compiler checks passed')


if __name__ == '__main__':
    main()
