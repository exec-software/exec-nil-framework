#!/usr/bin/env python3
"""Renamed modules retain the checked callable owner across compilation routes."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    stdlib = args.stdlib.resolve(strict=True)
    environment = os.environ.copy()
    for name in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_STDLIB_PATH',
                 'NIL_STDLIB_PATH', 'NY_LEX_DUMP', 'NIL_LEX_DUMP',
                 'NY_PARSE_TRACE', 'NIL_PARSE_TRACE'):
        environment.pop(name, None)
    harness = Path(__file__).resolve().parents[1] / 'routes/ny_route_matrix.py'
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-module-alias-') as directory:
        root = Path(directory).resolve()

        def run(command, status=0):
            result = subprocess.run([str(item) for item in command], cwd=root,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=90)
            assert result.returncode == status, (command, result.returncode,
                                                 result.stdout, result.stderr)
            return result

        def routes(source, search, expected, name):
            nonlocal checks
            common = [compiler, '--no-stdlib', '-I', search]
            analysis = run([*common, '--analyze', source])
            assert not analysis.stderr and json.loads(analysis.stdout)['success'], analysis
            checks += 1
            expected_file = root / (name + '.stdout')
            expected_file.write_text(expected, encoding='utf-8', newline='\n')
            report = root / (name + '.json')
            selection = ('all' if args.llvm == 'ON' else
                         'text-interpreter,binary-interpreter,bundle-text,bundle-binary')
            run([sys.executable, harness, '--compiler', compiler,
                 '--interpreter', interpreter, '--source', source,
                 '--expected-stdout', expected_file, '--expected-exit', '0',
                 '--compiler-arg=-I', '--compiler-arg=' + str(search),
                 '--routes', selection, '--strict', '--timeout', '30',
                 '--work-dir', root / (name + '-routes'), '--report-json', report])
            checks += 6 if args.llvm == 'ON' else 4

        def source(language, body, imports=''):
            if language == 'ny':
                return ('module consumer\n' + imports +
                        'fn main() -> i32\n' + body + '\nreturn 0\nend fn\n')
            return ('book consumer\n' + imports +
                    'main returns override integer begin paragraph\n' + body +
                    '\ngive 0\nend paragraph\n')

        providers = {
            'ny': 'module helper\n'
                  'pub fn calculate(input: i32) -> i32\nreturn input + 20\nend fn\n'
                  'pub fn select<T>(chosen: T, ignored: i32) -> T\nreturn chosen\nend fn\n'
                  'pub fn ok(input: i32) -> i32\nreturn input + 40\nend fn\n'
                  'fn hidden(input: i32) -> i32\nreturn input\nend fn\n',
            'matrix': 'book helper\n'
                      'public calculate receives input as override integer returns override integer begin paragraph\n'
                      'give input plus 20\nend paragraph\n'
                      'public select of type T receives chosen as T, ignored as override integer returns T begin paragraph\n'
                      'give chosen\nend paragraph\n'
                      'public ok receives input as override integer returns override integer begin paragraph\n'
                      'give input plus 40\nend paragraph\n'
                      'hidden receives input as override integer returns override integer begin paragraph\n'
                      'give input\nend paragraph\n',
        }
        for provider_language, provider_text in providers.items():
            search = root / ('source-' + provider_language)
            search.mkdir()
            provider = search / ('helper.' + provider_language)
            provider.write_text(provider_text, encoding='utf-8', newline='\n')
            binary = root / ('binary-' + provider_language)
            binary.mkdir()
            run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                 '--emit-library', '-o', binary / 'helper.nil', provider])
            checks += 1
            for library_form, library in (('source', search), ('binary', binary)):
                if library_form == 'binary':
                    provider.unlink()
                    assert not provider.exists()
                for language in ('ny', 'matrix'):
                    if language == 'ny':
                        imports = 'import helper\nnamespace words = helper\n'
                        body = ('println(words::calculate(7))\nprintln(helper::calculate(8))\n'
                                'println(words::select(49, 0))\n'
                                'println(words::select<i32>(50, 0))\nprintln(words.calculate(6))\nprintln(words::ok(11))')
                    else:
                        # A read-as declaration must load its dependency itself.
                        imports = 'read helper as words\n'
                        body = ('say words::calculate with 7\nsay helper::calculate with 8\n'
                                'say words::select with ignored being 0, chosen being 49\n'
                                'say words::select of override integer with 50, 0\nsay words.calculate with 6\nsay words::ok with 11')
                    name = f'{provider_language}-{library_form}-{language}'
                    consumer = root / (name + '.' + language)
                    consumer.write_text(source(language, body, imports), encoding='utf-8', newline='\n')
                    routes(consumer, library, '27\n28\n49\n50\n26\n51\n', name)

                    # Aliased functions used as values need the same identity.
                    # Function-value execution has its own interpreter contract.
                    body = ('let callback = words::calculate\nprintln(callback(9))\n'
                            'let calculate = words.calculate\nprintln(calculate(10))'
                            if language == 'ny' else
                            'value callback is the paragraph words::calculate\nsay callback with 9\n'
                            'value calculate is the paragraph words.calculate\nsay calculate with 10')
                    consumer.write_text(source(language, body, imports), encoding='utf-8', newline='\n')
                    for encoding, flags in (('text', []), ('binary', ['--emit-binary-nil'])):
                        output = root / (name + '-value-' + encoding + '.nil')
                        run([compiler, '--quiet', '--no-stdlib', '-I', library,
                             '--no-bundle', *flags, '-o', output, consumer])
                        result = run([interpreter, '--no-ai', output])
                        assert result.stdout == '29\n30\n' and not result.stderr, result
                        checks += 1

            provider.write_text(provider_text, encoding='utf-8', newline='\n')
            # Privacy and argument checking must use the selected declaration.
            for language in ('ny', 'matrix'):
                imports = ('import helper\nnamespace words = helper\n' if language == 'ny'
                           else 'read helper as words\n')
                for case, ny_call, matrix_call, message in (
                    ('private', 'words::hidden(1)', 'words::hidden with 1', 'private to module'),
                    ('arity', 'words::calculate(1, 2)', 'words::calculate with 1, 2', 'argument'),
                    ('type', 'words::calculate("bad")', 'words::calculate with "bad"', 'type'),
                    ('unknown', 'words::missing(1)', 'words::missing with 1', 'Undefined identifier'),
                ):
                    path = root / f'reject-{provider_language}-{case}.{language}'
                    body = 'println(' + ny_call + ')' if language == 'ny' else 'say ' + matrix_call
                    path.write_text(source(language, body, imports), encoding='utf-8', newline='\n')
                    common = [compiler, '--no-stdlib', '-I', search]
                    analysis = run([*common, '--analyze', path], 1)
                    payload = json.loads(analysis.stdout)
                    assert not payload['success'] and any(
                        message.lower() in item['message'].lower() and item['severity'] == 'error'
                        for item in payload['diagnostics']), payload
                    output = root / 'protected.nil'
                    output.write_bytes(b'previous artifact')
                    rejected = run([*common, '--no-bundle', '-o', output, path], 1)
                    assert message.lower() in rejected.stderr.lower(), rejected
                    assert output.read_bytes() == b'previous artifact'
                    checks += 2

        # Identical alias spellings in distinct modules must not leak. A same-
        # named local function still takes precedence over imported functions.
        scope = root / 'scope'
        scope.mkdir()
        for module, offset in (('left', 10), ('right', 20)):
            (scope / (module + '.ny')).write_text(
                f'module {module}\npub fn calculate(value: i32) -> i32\n'
                f'return value + {offset}\nend fn\n', encoding='utf-8', newline='\n')
        for module, target in (('first', 'left'), ('second', 'right')):
            (scope / (module + '.ny')).write_text(
                f'module {module}\nimport {target}\nnamespace words = {target}\n'
                'pub fn invoke() -> i32\nreturn words::calculate(1)\nend fn\n',
                encoding='utf-8', newline='\n')
        path = root / 'scope.ny'
        path.write_text('module entry\nimport first\nimport second\nimport left\n'
                        'namespace words = left\nnamespace chained = words\n'
                        'fn calculate(value: i32) -> i32\nreturn value + 30\nend fn\n'
                        'fn main() -> i32\nprintln(first::invoke())\nprintln(second::invoke())\n'
                        'println(chained::calculate(2))\nprintln(calculate(3))\nreturn 0\nend fn\n',
                        encoding='utf-8', newline='\n')
        routes(path, scope, '11\n21\n12\n33\n', 'module-scope')

        path = root / 'object-shadow.ny'
        path.write_text('module shadow\nimport left\nnamespace words = left\n'
                        'struct Holder\n value: i32\nend struct\n'
                        'impl Holder\n fn calculate(self, input: i32) -> i32\n'
                        'return self.value + input\nend fn\nend impl\n'
                        'fn main() -> i32\nif true then\n'
                        'let words = Holder { value: 100 }\nprintln(words.calculate(4))\nend if\n'
                        'println(words.calculate(5))\nreturn 0\nend fn\n',
                        encoding='utf-8', newline='\n')
        routes(path, scope, '104\n15\n', 'object-shadow')

        # Canonical and legacy standard-library aliases resolve to one owner.
        for language in ('ny', 'matrix'):
            imports = ('import nil.std.text\nnamespace words = ny.lang.text\n'
                       if language == 'ny' else 'read ny.lang.text as words\n')
            body = ('println(words::substring("alpha", 1, 2))' if language == 'ny'
                    else 'say words::substring with "alpha", 1, 2')
            path = root / ('standard.' + language)
            path.write_text(source(language, body, imports), encoding='utf-8', newline='\n')
            routes(path, stdlib, 'lp\n', 'standard-' + language)

        # The bare name is the canonical identity of a root declaration, not
        # an expendable compatibility alias for the last similarly named
        # module function. Both input orders must specialize the root body.
        entry = root / 'root-function.ny'
        entry.write_text('fn choose<T>(input: T) -> T\nreturn input\nend fn\n'
                         'fn main() -> i32\nprintln(choose<i32>(7))\n'
                         'println(choose(8))\nreturn 0\nend fn\n',
                         encoding='utf-8', newline='\n')
        unrelated = root / 'unrelated-function.ny'
        unrelated.write_text('module unrelated\npub fn choose(input: i32) -> i32\n'
                             'return input + 90\nend fn\n', encoding='utf-8', newline='\n')
        for order, inputs in enumerate(((entry, unrelated), (unrelated, entry))):
            variants = [('text', []), ('binary', ['--emit-binary-nil'])]
            if args.llvm == 'ON':
                variants += [('aot', ['--native']), ('jit', ['--native', '--jit'])]
            for route, flags in variants:
                output = root / (f'root-function-{order}-{route}' +
                                 ('.exe' if route == 'aot' and os.name == 'nt' else '.nil'))
                compiled = run([compiler, '--quiet', '--no-stdlib', '--no-bundle',
                                *flags, '-o', output, *inputs])
                result = (compiled if route == 'jit' else run([output]) if route == 'aot'
                          else run([interpreter, '--no-ai', output]))
                assert result.stdout == '7\n8\n' and not result.stderr, (order, route, result)
                checks += 1

    print(f'module alias contract: {checks} checks passed')


if __name__ == '__main__':
    main()
