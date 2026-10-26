#!/usr/bin/env python3
"""Public prose patterns survive cross-language source and binary imports."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def declaration(language: str, name: str, body: str, public: bool = False) -> str:
    if language == 'ny':
        header = ('pub ' if public else '') + 'pattern ' + name
    else:
        header = ('public ' if public else '') + name.replace('_', '-') + ' begin pattern'
    return header + '\n' + body + '\nend pattern\n'


def consumer(language: str) -> str:
    if language == 'ny':
        return '''import fixture_patterns.*
import nil.std.text
fn main() -> i32
    let price = compile_pattern(dollar_amount).unwrap()
    let found = first_match(price, "x12 USD").unwrap().unwrap()
    println(capture_named(found, "amount").unwrap())
    println(capture_index(found, 0).unwrap())
    println(capture_start(found, 0).unwrap())
    println(capture_length(found, 0).unwrap())
    let braced = compile_pattern(braced_code).unwrap()
    let code = first_match(braced, $"a{{123}}b").unwrap().unwrap()
    println(capture_named(code, "code").unwrap())
    println(capture_index(code, 0).unwrap())
    println(full_match(braced, $"{{123}}").unwrap().is_some())
    return 0
end fn
'''
    return '''book pattern_consumer
read everything from fixture_patterns
read everything from nil.std.text
main returns override integer begin paragraph
    value price is (compile-pattern with dollar-amount).unwrap with nothing
    value found is ((first-match with price, "x12 USD").unwrap with nothing).unwrap with nothing
    say (capture-named with found, "amount").unwrap with nothing
    say (capture-index with found, 0).unwrap with nothing
    say (capture-start with found, 0).unwrap with nothing
    say (capture-length with found, 0).unwrap with nothing
    value braced is (compile-pattern with braced-code).unwrap with nothing
    value code is ((first-match with braced, "a{123}b").unwrap with nothing).unwrap with nothing
    say (capture-named with code, "code").unwrap with nothing
    say (capture-index with code, 0).unwrap with nothing
    say ((full-match with braced, "{123}").unwrap with nothing).is-some with nothing
    give 0
end paragraph
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'stdlib'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler, interpreter, stdlib = (
        path.resolve(strict=True) for path in (args.compiler, args.interpreter, args.stdlib))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil pattern declarations ') as directory:
        root = Path(directory)

        def run(command, *, status=0, stdout=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=120)
            assert result.returncode == status, (command, result)
            if stdout is not None:
                assert result.stdout == stdout and not result.stderr, (command, result, stdout)
            checks += 1
            return result

        common = [compiler, '--quiet', '--no-stdlib', '--sdk-path', stdlib]
        expected = '12\n12\n1\n2\n123\n{123}\n1\n'

        def routes(source: Path, search: Path, name: str):
            for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                artifact = root / (name + '-' + encoding + '.nil')
                run([*common, '-I', search, '--no-bundle', *flags, '-o', artifact, source])
                run([interpreter, '--no-ai', artifact], stdout=expected)
            if args.llvm == 'ON':
                executable = root / (name + '.native')
                run([*common, '-I', search, '--native', '-o', executable, source])
                run([executable], stdout=expected)
                run([*common, '-I', search, '--native', '--jit', '-o', root / (name + '.jit'),
                     source], stdout=expected)

        for language in ('ny', 'matrix'):
            provider_root = root / ('source-' + language)
            provider_root.mkdir()
            provider = provider_root / ('fixture_patterns.' + language)
            header = 'module fixture_patterns\n' if language == 'ny' else 'book fixture_patterns\n'
            provider.write_text(header + declaration(language, 'dollar_amount',
                'capture amount matching one or more ascii digits\n'
                'require next to match literal " USD"', True) +
                declaration(language, 'braced_code',
                'literal "{"\ncapture code matching exactly 3 ascii digits\nliteral "}"', True))
            other = 'matrix' if language == 'ny' else 'ny'
            source = root / ('consumer-' + language + '.' + other)
            source.write_text(consumer(other))
            routes(source, provider_root, 'source-' + language)

            libraries = root / ('binary-' + language)
            libraries.mkdir()
            artifact = libraries / 'fixture_patterns.nil'
            run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                 '--emit-library', '-o', artifact, provider])
            provider.unlink()
            provider_root.rmdir()
            assert artifact.is_file() and not provider.exists()
            routes(source, libraries, 'binary-' + language)

        # Syntax and declaration validation must fail before artifact creation,
        # even when semantic type checking is explicitly disabled.
        invalid = {
            'unknown-phrase': 'match something mysterious',
            'repetition-limit': 'exactly 1001 ascii digits',
            'reversed-range': 'character from "z" through "a"',
            'capture-name': 'capture 7name matching literal "a"',
            'duplicate-capture': 'capture word matching literal "a"\ncapture word matching literal "b"',
            'missing-atom': 'one or more',
        }
        flags = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil')]
        if args.llvm == 'ON':
            flags += [('--native',), ('--native', '--jit')]
        for language in ('ny', 'matrix'):
            prefix = '' if language == 'ny' else 'book malformed_pattern\n'
            main_source = ('fn main() -> i32\nreturn 0\nend fn\n' if language == 'ny' else
                           'main returns override integer begin paragraph\ngive 0\nend paragraph\n')
            cases = dict(invalid)
            if language == 'matrix':
                cases['capture-injection'] = 'capture `x>a)(?<b` matching literal "z"'
            for name, body in cases.items():
                source = root / (name + '.' + language)
                source.write_text(prefix + declaration(language, 'invalid', body) + main_source)
                for unchecked in (False, True):
                    for route_flags in flags:
                        artifact = root / 'protected-output'
                        artifact.write_bytes(b'previous pattern artifact\x00\xff')
                        result = run([compiler, '--quiet', '--no-stdlib', *route_flags,
                                      *(['--no-typecheck'] if unchecked else []), '-o', artifact,
                                      source], status=1)
                        assert result.stderr and not result.stdout, (name, result)
                        assert artifact.read_bytes() == b'previous pattern artifact\x00\xff', name

    print(f'pattern declaration contract: {checks} executions/checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
