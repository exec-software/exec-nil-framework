#!/usr/bin/env python3
"""Resolved native callable signatures in exact Ny and Matrix source snapshots."""
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
    with tempfile.TemporaryDirectory(prefix='nil-signature-') as directory:
        root = pathlib.Path(directory)

        def query(marked, *, name='main.ny', dependencies=None, mode='workspace',
                  available=True, status=None, offset=None, extra=(), enabled=True):
            nonlocal checks
            assert marked.count('|') == 1
            before, after = marked.split('|')
            source = before + after
            cursor = len(before.encode()) if offset is None else offset
            sources = {name: source, **(dependencies or {})}
            command = [compiler, '--analyze', '--no-stdlib', '-I', str(root), str(root / name)]
            if mode == 'workspace':
                command += ['--workspace-stdin']
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
                    command += ['--source-stdin']
            if enabled:
                command += ['--signature-at', str(cursor)]
            result = subprocess.run(command + list(extra), input=payload, text=True,
                                    capture_output=True, timeout=30, cwd=root)
            assert not result.stderr and result.returncode in (0, 1), (result.returncode, result.stdout, result.stderr)
            if status is not None:
                assert result.returncode == status, (result.returncode, result.stdout)
            response = json.loads(result.stdout)
            checks += 1
            if not enabled:
                assert 'signature' not in response
                return response
            value = response['signature']
            if not available:
                assert value is None, value
                return response
            assert value is not None, response
            assert value['schema'] == 'nil.signature.v1' and value['partial'] is True
            assert value['path'] == str(root / name) and value['cursor'] == cursor
            assert 0 <= value['start'] <= cursor <= value['start'] + value['length'] <= len(source.encode())
            source.encode()[value['start']:value['start'] + value['length']].decode()
            snapshots = {item['path']: item['text'] for item in value['sources']}
            assert len(snapshots) == len(value['sources']) and snapshots[str(root / name)] == source
            for path, text in sources.items():
                if str(root / path) in snapshots:
                    assert snapshots[str(root / path)] == text
            assert len(value['parameters']) <= 256
            assert value['active_parameter'] is None or 0 <= value['active_parameter'] < len(value['parameters'])
            assert all(parameter['label'] in value['label'] for parameter in value['parameters'])
            assert value['return_type'] in value['label']
            return value

        ny = ('# é😀\r\nfn combine(first: i32, second: text) -> i32\r\nreturn first\r\nend fn\r\n'
              'fn main() -> i32\r\nreturn combine(7, |"second")\r\nend fn\r\n')
        for mode in ('workspace', 'stdin', 'disk'):
            value = query(ny, mode=mode, name=mode + '.ny', status=0)
            assert value['label'] == 'combine(first: i32, second: String) -> i32', value
            assert value['active_parameter'] == 1
        value = query(ny.replace('7, |"second"', '|'), status=1)
        assert value['length'] == 0 and value['active_parameter'] == 0
        query(ny.replace('7, |"second"', 'missing|, "second"'), status=1)
        value = query(ny.replace('7, |"second"', '7, "second", |9'), status=1)
        assert value['active_parameter'] is None
        query(ny.replace('return combine', 'let missing = unknown\r\nreturn combine'), available=False, status=1)
        query(ny.replace('combine(7, |"second")', 'unknown(7, |"second")'), available=False, status=1)
        query(ny.replace('combine(7, |"second")', 'combine(7, |'), available=False, status=1)
        query(ny.replace('7, |"second"', '7, "second"').replace('fn main()', '|fn main()'), available=False, status=0)
        query(ny, offset=len(ny.replace('|', '').encode()) + 1, available=False, status=0)
        query(ny, enabled=False, status=0)
        # Multibyte cursor boundaries are validated independently of parsed calls.
        unicode_text = ny.replace('7, |"second"', '7, "é|"')
        query(unicode_text, offset=len(unicode_text.split('|')[0].encode()) - 1, available=False, status=0)
        query(ny, extra=('--semantic-index',), status=0)

        # Exact source-only markers preserve declaration and argument byte
        # coordinates, including CRLF, BOM/Unicode, and no final newline.
        markers = '\ufeff# café 😀\r\n  @pragma once\t\r\n\t@schema \r\n'
        marked_ny = markers + ny.rstrip('\r\n')
        value = query(marked_ny, name='source-markers.ny', status=0)
        assert value['label'] == 'combine(first: i32, second: String) -> i32'
        assert value['active_parameter'] == 1
        assert query(marked_ny.replace('7, |"second"', '|'), name='empty-markers.ny', status=1)['length'] == 0
        for directive in ('@define UNUSED 1\n', '@pragma once_extra\n', '@schema extra\n', '@pragma link("example")\n'):
            query(directive + ny, name='other-directive.ny', available=False, status=0)
        # Nested calls choose the innermost parsed call, including when it is
        # unresolved; an outer callable must not be advertised in its place.
        nested = ('fn inside(item: i32) -> i32\nreturn item\nend fn\n'
                  'fn outside(left: i32, right: i32) -> i32\nreturn left\nend fn\n'
                  'fn main() -> i32\nreturn outside(inside(7|), 9)\nend fn\n')
        assert query(nested, status=0)['label'].startswith('inside(')
        assert query(nested.replace('7|), 9', '7), |9'), status=0)['active_parameter'] == 1
        query(nested.replace('inside(7|)', 'missing(7|)'), available=False, status=1)
        comma_text = ny.replace('7, |"second"', '7, "a,|b"')
        assert query(comma_text, status=0)['active_parameter'] == 1

        matrix_decl = ('book prose\ncombine receives first as number, second-value as number returns number begin paragraph\n'
                       'give first plus second-value\nend paragraph\n')
        matrix = matrix_decl + 'main returns number begin paragraph\ngive combine with second_value being |9, first being 7\nend paragraph\n'
        value = query(matrix, name='named.matrix', status=0)
        assert value['label'] == 'combine receives first as number, second-value as number returns number', value
        assert value['active_parameter'] == 1
        assert query(matrix.replace('|9', '9').replace('first being 7', 'first being |7'), name='first.matrix', status=0)['active_parameter'] == 0
        assert query(matrix.replace('second_value being |9', 'unknown being |9'), name='unknown.matrix', status=1)['active_parameter'] is None
        assert query(matrix.replace('second_value being |9, first being 7', '|nothing'), name='empty.matrix', status=1)['active_parameter'] == 0
        bare = matrix_decl + 'main returns number begin paragraph\ngive combine with |\nend paragraph\n'
        assert query(bare, name='bare.matrix', status=1)['active_parameter'] == 0
        query(bare, name='bare-normal.matrix', enabled=False, status=1)
        query(bare.replace('with |', 'with first being |'), name='unrecovered.matrix', available=False, status=1)
        query(bare.replace('with |', 'with \n|'), name='next-line.matrix', available=False, status=1)
        zero = ('book zero-test\nzero returns number begin paragraph\ngive 0\nend paragraph\n'
                'main returns number begin paragraph\ngive zero with |\nend paragraph\n')
        value = query(zero, name='zero.matrix', status=1)
        assert value['parameters'] == [] and value['active_parameter'] is None

        # Imports preserve all native inputs, and only the actual selected
        # function's declaration supplies its parameter names and types.
        helper_ny = 'module helper\npub fn imported(amount: i32) -> i32\nreturn amount\nend fn\n'
        helper_matrix = 'book helper\npublic imported receives amount as override integer returns override integer begin paragraph\ngive amount\nend paragraph\n'
        for extension, helper in [('ny', helper_ny), ('matrix', helper_matrix)]:
            entry = 'import helper::{imported}\nfn main() -> i32\nreturn imported(|7)\nend fn\n'
            value = query(entry, name='imported-' + extension + '.ny', dependencies={'helper.' + extension: helper}, status=0)
            assert value['label'] == 'imported(amount: i32) -> i32', value
            assert len(value['sources']) == 2
        entry = 'book entry\nread imported from helper\nmain returns override integer begin paragraph\ngive imported with amount being |7\nend paragraph\n'
        assert query(entry, name='mixed.matrix', dependencies={'helper.ny': helper_ny}, status=0)['parameters'][0]['name'] == 'amount'
        private = helper_ny.replace('pub fn', 'fn')
        query(entry, name='private.matrix', dependencies={'helper.ny': private}, available=False, status=1)

        marked_helper = '@schema\r\n@pragma once\r\n' + helper_ny.replace('\n', '\r\n').rstrip('\r\n')
        value = query(entry, name='marked-import.matrix', dependencies={'helper.ny': marked_helper}, status=0)
        assert value['parameters'][0]['name'] == 'amount'
        # Callback labels are provenance, not part of fn type identity. Only
        # immutable values whose checker contract is known expose labels.
        callback = ('fn combine(first: i32, second: i32) -> i32\nreturn first\nend fn\n'
                    'fn main() -> i32\nlet callback = combine\nreturn callback(7, |9)\nend fn\n')
        assert query(callback, status=0)['label'] == 'callback(first: i32, second: i32) -> i32'
        query(callback.replace('let callback', 'let mut callback'), available=False, status=0)
        parameter = 'fn invoke(callback: fn(i32) -> i32) -> i32\nreturn callback(|7)\nend fn\n'
        query(parameter, available=False, status=0)
        lambda_call = ('fn main() -> i32\nlet callback = fn(first: i32, second: i32) -> i32\n'
                       'return first\nend fn\nreturn callback(7, |9)\nend fn\n')
        assert query(lambda_call, status=0)['active_parameter'] == 1
        matrix_callback = matrix_decl + ('main returns number begin paragraph\nvalue callback is the paragraph combine\n'
                    'give callback with second-value being |9, first being 7\nend paragraph\n')
        assert query(matrix_callback, name='callback.matrix', status=0)['active_parameter'] == 1

        # Generic source signatures deliberately describe declarations rather
        # than inventing a specialization from a partly typed argument list.
        generic = 'fn identity<T>(item: T) -> T\nreturn item\nend fn\nfn main() -> i32\nreturn identity<i32>(|7)\nend fn\n'
        assert query(generic, status=0)['parameters'][0]['type'] == 'T'
        variadic = 'fn values(first: i32, ..rest: i32) -> i32\nreturn first\nend fn\nfn main() -> i32\nreturn values(1, 2, |3)\nend fn\n'
        value = query(variadic, status=0)
        assert value['active_parameter'] == 1 and value['parameters'][1]['variadic'] is True
        method = ('struct Counter\nvalue: i32\nend struct\nimpl Counter\n'
                  'fn add(self: Counter, amount: i32) -> i32\nreturn amount\nend fn\n'
                  'pub fn create(amount: i32) -> Counter\nreturn Counter { value: amount }\nend fn\nend impl\n'
                  'fn main() -> i32\nlet counter = Counter { value: 1 }\nreturn counter.add(|7)\nend fn\n')
        value = query(method, status=0)
        assert value['label'] == 'add(amount: i32) -> i32', value
        static = method.replace('let counter = Counter { value: 1 }', 'let counter = Counter.create(|1)').replace('counter.add(|7)', 'counter.add(7)')
        assert query(static, status=0)['parameters'][0]['name'] == 'amount'
        matrix_method = ('book methods\ncalculator begin record\nfield unused as number\nend record\n'
                         'calculator begin implementation\n'
                         'calculate receives first-part as number, second-part as number returns number begin paragraph\n'
                         'give first-part plus second-part\nend paragraph\nend implementation\n'
                         'main returns number begin paragraph\nvalue instance is build calculator with unused being 0\n'
                         'give instance.calculate with second-part being |2, first-part being 1\nend paragraph\n')
        value = query(matrix_method, name='method.matrix', status=0)
        assert len(value['parameters']) == 2 and value['active_parameter'] == 1, value
        assert value['parameters'][0]['name'] == 'first-part'
        # Quoted arguments inside a hole are ordinary language expressions;
        # signature ranges remain in the original, undecoded source snapshot.
        formatted_ny = ny.replace('return combine(7, |"second")', 'println($"\\n{{literal}} {combine(7, |"second")}")\r\nreturn 0')
        assert query(formatted_ny, name='formatted.ny', status=0)['active_parameter'] == 1
        formatted_matrix = ('book formatted-signature\r\ncombine receives first as number, second as text returns number begin paragraph\r\ngive first\r\nend paragraph\r\n'
                            'main returns number begin paragraph\r\nsay formatted "{{literal}} {combine with second being |"value", first being 7}"\r\ngive 0\r\nend paragraph\r\n')
        assert query(formatted_matrix, name='formatted.matrix', status=0)['active_parameter'] == 1

        limit = ', '.join(f'parameter_{index}: i32' for index in range(257))
        query('fn large(' + limit + ') -> i32\nreturn 0\nend fn\nfn main() -> i32\nreturn large(|)\nend fn\n',
              name='parameter-limit.ny', available=False, status=1)
        query('fn main() -> i32\nprintln("a")\nreturn panic(|"bad")\nend fn\n', available=False)
        # Imported trait signatures and binary/generated declarations without
        # native declaration provenance must never borrow the caller's source.
        trait = 'module factory\npub trait Factory\nfn create(value: i64) -> Self\nend trait\n'
        query('import factory::{Factory}\nfn make<T: Factory>() -> T\nreturn T.create(|1)\nend fn\n',
              name='trait.ny', dependencies={'factory.ny': trait}, available=False, status=0)
        libraries = root / 'libraries'
        libraries.mkdir()
        binary_source = root / 'build_binary.ny'
        binary_source.write_text('module binary_dependency\n')
        compiled = subprocess.run([compiler, '--no-stdlib', '--no-bundle', '--emit-binary-nil',
            '--emit-library', '-q', '-o', str(libraries / 'binary_dependency.nil'), str(binary_source)],
            text=True, capture_output=True, timeout=20, cwd=root)
        assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
        checks += 1
        query('import binary_dependency\n' + ny, name='binary.ny',
              extra=('-I', str(libraries)), available=False, status=0)
        # Query options remain strict and are never forwarded to codegen.
        source = root / 'options.ny'
        source.write_text(ny.replace('|', ''))
        for options in [('--signature-at', '-1'), ('--signature-at', 'x'),
                        ('--signature-at', '2097153'), ('--signature-at', '1', '--signature-at', '2')]:
            result = subprocess.run([compiler, '--analyze', '--no-stdlib', str(source), *options], capture_output=True, text=True)
            assert result.returncode == 2 and not result.stdout and 'signature' in result.stderr, result
            checks += 1
    print(f'{checks} signature contract checks passed')


if __name__ == '__main__':
    main()
