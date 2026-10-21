#!/usr/bin/env python3
"""Formatted text rejects malformed holes and values without a text contract."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    routes = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil'),
              ('--no-bundle', '--no-typecheck')]
    if args.llvm == 'ON':
        routes += [('--native',), ('--native', '--jit')]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-formatted-contract-') as directory:
        root = Path(directory)
        for language, opening in [('ny', '$'), ('matrix', 'formatted ')]:
            for index, expression in enumerate([
                    '"{}"', '"{1 +}"' if language == 'ny' else '"{1 plus}"',
                    '"{1"', '"text }"', '"{unknown}"', '"{[1, 2]}"',
                    '"{(1, true)}"', '"{\n1}"', '"line\nbreak"']):
                if language == 'ny':
                    source = f'fn main() -> i32\nlet result = {opening}{expression}\nreturn 0\nend fn\n'
                else:
                    source = ('book invalid_formatted\nmain returns override integer begin paragraph\n'
                              f'value result is {opening}{expression}\ngive 0\nend paragraph\n')
                path = root / f'invalid-{index}.{language}'
                path.write_text(source)
                for flags in routes:
                    output = root / 'protected-output'
                    output.write_bytes(b'previous artifact')
                    result = subprocess.run([compiler, '--quiet', '--no-stdlib',
                                             *flags, '--output', output, path],
                                            capture_output=True, text=True, timeout=30)
                    assert result.returncode != 0, (path, flags, result.stdout)
                    assert output.read_bytes() == b'previous artifact', (path, flags)
                    assert result.stderr.strip(), (path, flags)
                    checks += 1
        # A function identifier has a return type, but is not a scalar value.
        path = root / 'function.ny'
        path.write_text('fn number() -> i32\nreturn 1\nend fn\n'
                        'fn main() -> i32\nprintln($"{number}")\nreturn 0\nend fn\n')
        result = subprocess.run([compiler, '--quiet', '--no-stdlib', '--no-bundle',
                                 '--output', root / 'invalid.nil', path],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, result.stdout
        checks += 1

        def generic_case(name, language, source, valid, dependencies=()):
            nonlocal checks
            path = root / f'{name}.{language}'
            path.write_text(source)
            for dependency, text in dependencies:
                (root / dependency).write_text(text)
            command = [compiler, '--no-stdlib', '-I', root]
            result = subprocess.run([*command, '--analyze', '--semantic-index', path],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == (0 if valid else 1), (name, result.stdout, result.stderr)
            response = json.loads(result.stdout)
            assert response['success'] is valid, (name, response)
            if not valid:
                assert response['semantic'] is None, (name, response)
                assert any('interpolates generic type' in item['message'] for item in response['diagnostics']), (name, response)
                assert any(item.get('path') == str(path) and 'interpolates generic type' in item['message']
                           for item in response['diagnostics']), (name, response)
            checks += 1
            output = root / 'generic-output.nil'
            output.write_bytes(b'previous artifact')
            result = subprocess.run([*command, '--quiet', '--no-bundle', '--output', output, path],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == (0 if valid else 1), (name, result.stdout, result.stderr)
            if not valid:
                assert 'interpolates generic type' in result.stderr, (name, result.stderr)
                assert output.read_bytes() == b'previous artifact', name
            checks += 1

        # Requirements are attached to checked declaration identity, including
        # functions declared after the caller and transitive generic wrappers.
        ny_definitions = ('struct Packet\nitem: i32\nend struct\n'
                          'fn render<T>(input: T) -> text\nreturn $"{input}"\nend fn\n'
                          'fn relay<U>(input: U) -> text\nreturn render(input)\nend fn\n'
                          'fn selective<T, U>(ignored: T, input: U) -> text\nreturn $"{input}"\nend fn\n')
        matrix_definitions = ('packet begin record\nfield item as number\nend record\n'
                              'render of type T receives input as T returns text begin paragraph\ngive formatted "{input}"\nend paragraph\n'
                              'relay of type U receives input as U returns text begin paragraph\ngive render with input being input\nend paragraph\n'
                              'selective of type T, type U receives ignored as T, input as U returns text begin paragraph\ngive formatted "{input}"\nend paragraph\n')
        for language, definitions, expressions in [
                ('ny', ny_definitions, [
                    ('render(7)', True), ('render<f64>(1.25)', True), ('relay("ready")', True),
                    ('selective(Packet { item: 1 }, 7)', True),
                    ('render(Packet { item: 1 })', False), ('render([1, 2])', False),
                    ('render((1, true))', False), ('relay(Packet { item: 1 })', False),
                    ('relay([1, 2])', False), ('render<Packet>(Packet { item: 1 })', False)]),
                ('matrix', matrix_definitions, [
                    ('render with input being 7', True), ('render of floating with input being 1.25', True),
                    ('relay with input being "ready"', True),
                    ('selective with ignored being (build packet with item being 1), input being 7', True),
                    ('render with input being (build packet with item being 1)', False),
                    ('render with input being [1, 2]', False), ('render with input being (1, true)', False),
                    ('relay with input being (build packet with item being 1)', False),
                    ('relay with input being [1, 2]', False),
                    ('render of packet with input being (build packet with item being 1)', False)])]:
            for index, (expression, valid) in enumerate(expressions):
                entry = (f'fn main() -> i32\nprintln({expression})\nreturn 0\nend fn\n' if language == 'ny' else
                         f'book generic-format\nmain returns override integer begin paragraph\nsay {expression}\ngive 0\nend paragraph\n')
                generic_case(f'generic-{index}', language, entry + definitions, valid)

        # Instance methods inherit substitutions from their generic receiver.
        for language in ('ny', 'matrix'):
            for aggregate in (False, True):
                if language == 'ny':
                    concrete = 'Packet' if aggregate else 'i32'
                    argument = 'Packet { item: 1 }' if aggregate else '7'
                    source = (ny_definitions + 'struct Box<T>\nitem: T\nend struct\n'
                              'impl<T> Box<T>\nfn render(self: Box<T>) -> text\nreturn $"{self.item}"\nend fn\nend impl\n'
                              f'fn main() -> i32\nlet box = Box<{concrete}> {{ item: {argument} }}\nprintln(box.render())\nreturn 0\nend fn\n')
                else:
                    concrete = 'packet' if aggregate else 'number'
                    argument = '(build packet with item being 1)' if aggregate else '7'
                    source = ('book generic-method\n' + matrix_definitions +
                              'box of type T begin record\nfield item as T\nend record\n'
                              'box of type T begin implementation\nrender returns text begin paragraph\ngive formatted "{my item}"\nend paragraph\nend implementation\n'
                              'main returns override integer begin paragraph\n'
                              f'value instance is build box of {concrete} with item being {argument}\nsay instance.render with nothing\ngive 0\nend paragraph\n')
                generic_case('generic-method-' + str(aggregate), language, source, not aggregate)

        # An imported callee's requirement is reported at the native callsite,
        # rather than borrowing the callee's source path or relying on order.
        dependency = 'module formatting\npub fn render<T>(input: T) -> text\nreturn $"{input}"\nend fn\n'
        generic_case('generic-import', 'ny', 'import formatting.*\nfn main() -> i32\nprintln(render([1, 2]))\nreturn 0\nend fn\n',
                     False, [('formatting.ny', dependency)])
    print(f'{checks} formatted-text rejection checks passed')


if __name__ == '__main__':
    main()
