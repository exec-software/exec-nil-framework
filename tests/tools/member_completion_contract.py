#!/usr/bin/env python3
"""Source-backed member completion follows checked receivers and visibility."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    compiler = str(Path(args.compiler).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-member-completion-') as directory:
        root = Path(directory)

        def run(command, *, payload=None, status=0):
            nonlocal checks
            result = subprocess.run(command, input=payload, text=True, capture_output=True,
                                    timeout=30, cwd=root)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        def query(marked, *, name='main.ny', dependencies=None, status=1, available=True):
            assert marked.count('|') == 1
            before, after = marked.split('|')
            source = before + after
            cursor = len(before.encode())
            sources = {name: source, **(dependencies or {})}
            payload = json.dumps({'schema': 'nil.analysis.workspace.v1', 'sources': [
                {'path': str(root / path), 'text': text} for path, text in sources.items()]})
            result = run([compiler, '--analyze', '--no-stdlib', '-I', str(root),
                          '--workspace-stdin', '--completion-at', str(cursor), str(root / name)],
                         payload=payload, status=status)
            assert not result.stderr, result.stderr
            analysis = json.loads(result.stdout)
            value = analysis['completion']
            if not available:
                assert value is None, analysis
                return value
            assert value is not None, analysis
            assert value['schema'] == 'nil.completion.v1' and value['partial'] is True
            assert value['path'] == str(root / name) and value['cursor'] == cursor
            assert 0 <= value['start'] < cursor <= value['start'] + value['length'] <= len(source.encode())
            source.encode()[value['start']:value['start'] + value['length']].decode()
            assert {item['path']: item['text'] for item in value['sources']} == {
                str(root / path): text for path, text in sources.items()}
            assert all(item['kind'] in ('field', 'method') for item in value['items']), value
            assert len({item['insert_text'] for item in value['items']}) == len(value['items'])
            return value

        def items(value):
            return {item['name']: item for item in value['items']}

        def apply(marked, value, candidate, *, name='applied.ny', dependencies=None):
            source = marked.replace('|', '').encode()
            insertion = candidate['insert_text'].encode()
            updated = (source[:value['start']] + insertion + source[value['start'] + value['length']:]).decode()
            for path, text in {name: updated, **(dependencies or {})}.items():
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(text)
            result = run([compiler, '--no-stdlib', '-I', str(root), '--no-bundle',
                          '-o', str(root / (name + '.nil')), str(root / name)])
            assert not result.stderr, result.stderr

        record = 'struct Sample\nitem: i32\nother: String\nend struct\n'
        entry = 'fn main() -> i32\nlet value = Sample { item: 7, other: "word" }\nreturn value.it|\nend fn\n'
        result = query(record + entry)
        assert items(result) == {'item': {'name': 'item', 'kind': 'field', 'type': 'i32', 'insert_text': 'item'}}, result
        apply(record + entry, result, items(result)['item'])
        middle = (record + entry).replace('it|', 'it|em_missing')
        result = query(middle)
        assert result['length'] == len('item_missing'), result
        apply(middle, result, items(result)['item'])
        assert not query((record + entry).replace('it|', 'IT|'))['items']
        query((record + entry).replace('value.it|', 'value.|'), available=False)
        query((record + entry).replace('value.it|', 'missing.it|'), available=False)
        query((record + entry).replace('value.it|', 'Sample.it|'), status=0, available=False)
        query((record + entry).replace('return value.it|', 'println(missing)\nreturn value.it|'), available=False)

        # Same-spelled locals and fields select the receiver's actual type.
        shadow = ('struct Textual\nitem: String\nend struct\n' + record +
                  'fn main() -> i32\nlet value = Sample { item: 7, other: "word" }\n'
                  'if true then\nlet value = Textual { item: "inner" }\nprintln(value.it|)\nend if\nreturn 0\nend fn\n')
        assert items(query(shadow))['item']['type'] == 'String'
        outer = shadow.replace('println(value.it|)', 'println(value.item)').replace('return 0', 'return value.it|')
        assert items(query(outer))['item']['type'] == 'i32'

        generic = ('struct Box<T>\nitem: T\nend struct\nimpl<T> Box<T>\n'
                   'pub fn sample(self) -> T\nreturn self.item\nend fn\nend impl\n')
        generic_entry = ('fn main() -> i32\nlet value = Box<i32> { item: 7 }\nreturn value.it|\nend fn\n')
        result = query(generic + generic_entry)
        assert items(result)['item']['type'] == 'i32', result
        apply(generic + generic_entry, result, items(result)['item'], name='generic_field.ny')
        method = generic + generic_entry.replace('value.it|', 'value.sam|()')
        result = query(method)
        assert items(result)['sample']['kind'] == 'method' and items(result)['sample']['type'] == 'i32', result
        apply(method, result, items(result)['sample'], name='generic_method.ny')
        pointer = generic + 'fn inspect(value: ptr<Box<i32>>) -> i32\nreturn value.it|\nend fn\n'
        assert items(query(pointer))['item']['type'] == 'i32'
        assert not query(pointer.replace('value.it|', 'value.sam|()'))['items']
        reference = pointer.replace('ptr<Box<i32>>', 'ref<Box<i32>>')
        assert items(query(reference))['item']['type'] == 'i32'
        assert not query(reference.replace('value.it|', 'value.sam|()'))['items']

        # The dependency's canonical declaration/source identity participates in
        # freshness, and private inherent methods remain private after checking.
        dependency = ('module helper\npub ' + generic +
                      'impl<T> Box<T>\nfn secret(self) -> i32\nreturn 9\nend fn\nend impl\n')
        imported = 'import helper.*\n' + generic_entry.replace('value.it|', 'value.sam|()')
        dependencies = {'helper.ny': dependency}
        result = query(imported, dependencies=dependencies)
        assert set(items(result)) == {'sample'}, result
        apply(imported, result, items(result)['sample'], name='imported_applied.ny', dependencies=dependencies)
        assert not query(imported.replace('sam|', 'sec|'), dependencies=dependencies)['items']
        for caller in (imported.replace('sam|', 'secret'),
                       imported.replace('sam|', 'secret').replace('import helper.*', 'module entry\nimport helper.*'),
                       imported.replace('value.sam|()', 'helper::Box__secret(value)')):
            (root / 'private.ny').write_text(caller)
            (root / 'helper.ny').write_text(dependency)
            result = run([compiler, '--no-stdlib', '-I', str(root), '--no-bundle',
                          '-o', str(root / 'private.nil'), str(root / 'private.ny')], status=1)
            assert 'private to module' in result.stderr, result.stderr
        legacy = ('module legacy\npub struct Record\nitem: i32\nend struct\n'
                  'fn Record__secret(self: Record) -> i32\nreturn self.item\nend fn\n')
        (root / 'legacy.ny').write_text(legacy)
        (root / 'legacy_private.ny').write_text('import legacy.*\nfn main() -> i32\n'
            'let input = Record { item: 7 }\nreturn input.secret()\nend fn\n')
        result = run([compiler, '--no-stdlib', '-I', str(root), '--no-bundle',
                      '-o', str(root / 'legacy_private.nil'), str(root / 'legacy_private.ny')], status=1)
        assert 'private to module' in result.stderr, result.stderr

        # Static dot dispatch has separate ordinary and generic call paths.
        # Neither may bypass access checks because its receiver is a type.
        for is_generic in (False, True):
            parameters = '<T>' if is_generic else ''
            field_type = 'T' if is_generic else 'i32'
            dependency = (f'module static_helper\npub struct Record{parameters}\nitem: {field_type}\nend struct\n'
                          f'impl{parameters} Record{parameters}\nfn secret() -> i32\nreturn 7\nend fn\n'
                          'pub fn public_value() -> i32\nreturn 7\nend fn\nend impl\n')
            (root / 'static_helper.ny').write_text(dependency)
            receiver = 'Record<i32>' if is_generic else 'Record'
            for explicit_module in (False, True):
                for method, status in (('secret', 1), ('public_value', 0)):
                    source = (('module entry\n' if explicit_module else '') + 'import static_helper.*\n'
                              f'fn main() -> i32\nreturn {receiver}.{method}()\nend fn\n')
                    path = root / f'static_{is_generic}_{explicit_module}_{method}.ny'
                    path.write_text(source)
                    signature_at = len(source[:source.index(method + '(') + len(method) + 1].encode())
                    analysis = json.loads(run([compiler, '--analyze', '--no-stdlib', '-I', str(root),
                        '--signature-at', str(signature_at), str(path)], status=status).stdout)
                    if status:
                        assert any('private to module' in d['message'] for d in analysis['diagnostics']), analysis
                        assert analysis['signature'] is None, analysis
                    else:
                        assert analysis['success'], analysis
                    artifact = path.with_suffix('.nil')
                    compiled = run([compiler, '--no-stdlib', '-I', str(root), '--no-bundle',
                                    '-o', str(artifact), str(path)], status=status)
                    if status:
                        assert 'private to module' in compiled.stderr and not artifact.exists(), compiled.stderr

        # Required methods of an exported trait retain the trait's public
        # contract; an additional inherent helper does not inherit that access.
        contract = ('module contract\npub trait Reading\nfn read(self) -> i32\nend trait\n'
                    'pub struct Record\nitem: i32\nend struct\nimpl Reading for Record\n'
                    'fn read(self) -> i32\nreturn self.item\nend fn\n'
                    'fn secret(self) -> i32\nreturn self.item\nend fn\nend impl\n')
        trait_entry = ('import contract.*\nfn main() -> i32\nlet input = Record { item: 7 }\n'
                       'return input.rea|()\nend fn\n')
        result = query(trait_entry, dependencies={'contract.ny': contract})
        assert set(items(result)) == {'read'}, result
        apply(trait_entry, result, items(result)['read'], name='trait_applied.ny', dependencies={'contract.ny': contract})
        assert not query(trait_entry.replace('rea|', 'sec|'), dependencies={'contract.ny': contract})['items']

        matrix = ('book members\r\n!!! é😀\r\nsample begin record\r\nfield item-value as number\r\n'
                  'field `value` as number\r\nend record\r\n'
                  'compute receives input as sample returns number begin paragraph\r\n'
                  'give input.IT_EM|_missing\r\nend paragraph\r\n')
        result = query(matrix, name='main.matrix')
        assert items(result)['item-value']['insert_text'] == 'item-value', result
        assert result['length'] == len('IT_EM_missing'), result
        apply(matrix, result, items(result)['item-value'], name='matrix_applied.matrix')
        escaped = matrix.replace('IT_EM|_missing', '`val|x`')
        result = query(escaped, name='escaped.matrix')
        assert result['length'] == len('`valx`') and items(result)['value']['insert_text'] == '`value`'
        apply(escaped, result, items(result)['value'], name='escaped_applied.matrix')
        keyword = matrix.replace('IT_EM|_missing', 'val|ue')
        result = query(keyword, name='keyword.matrix', status=0)
        assert items(result)['value']['insert_text'] == '`value`'
        apply(keyword, result, items(result)['value'], name='keyword_applied.matrix')
        # A page-wide declaration with the same fold does not redirect members.
        collision = matrix.replace('sample begin record', 'value Item_Value is 1\r\nsample begin record')
        result = query(collision, name='collision.matrix')
        apply(collision, result, items(result)['item-value'], name='collision_applied.matrix')
        imported_matrix = ('book client\nread everything from helper\n'
                           'compute receives input as Box of override integer returns override integer begin paragraph\n'
                           'give input.sam| with nothing\nend paragraph\n')
        result = query(imported_matrix, name='client.matrix', dependencies=dependencies)
        assert items(result)['sample']['type'] == 'override integer', result
        apply(imported_matrix, result, items(result)['sample'], name='client_applied.matrix', dependencies=dependencies)

        classes = ('class Parent\nitem: i32\npub fn sample(self) -> i32\nreturn self.item\nend fn\n'
                   'fn secret(self) -> i32\nreturn self.item\nend fn\nend class\n'
                   'class Child inherits Parent\nother: i32\nend class\n')
        child = classes + 'fn inspect(value: Child) -> i32\nreturn value.it|\nend fn\n'
        assert items(query(child))['item']['type'] == 'i32'
        child_method = child.replace('value.it|', 'value.sam|()')
        result = query(child_method)
        assert items(result)['sample']['kind'] == 'method'
        apply(child_method, result, items(result)['sample'], name='class_applied.ny')
        assert not query(child.replace('value.it|', 'value.sec|()'))['items']
        my = ('book my-members\nreader begin class\nfield item as number\n'
              'public sample returns number begin paragraph\ngive my it|\nend paragraph\nend class\n')
        result = query(my, name='my.matrix')
        assert items(result)['item']['kind'] == 'field'
        apply(my, result, items(result)['item'], name='my_applied.matrix')
    print(f'member completion contract: {checks} checks passed')


if __name__ == '__main__':
    main()
