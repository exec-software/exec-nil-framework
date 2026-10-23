#!/usr/bin/env python3
"""Declared scalar arguments retain their representation at native dispatch."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter, assembler = (path.resolve() for path in
        (args.compiler, args.interpreter, args.assembler))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil native arguments ') as directory:
        root = Path(directory)

        def run(command, status=0, stdout='', diagnostic=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=120)
            assert (result.returncode == status and result.stdout == stdout and
                    (diagnostic in result.stderr if diagnostic else not result.stderr)), (
                command, result.returncode, result.stdout, result.stderr,
                status, stdout, diagnostic)
            checks += 1
            return result

        def program(language, body, declarations='', imports=''):
            if language == 'ny':
                return imports + declarations + '\nfn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
            return ('book consumer\n' + imports + declarations +
                    '\nmain returns override integer begin paragraph\n' + body +
                    '\ngive 0\nend paragraph\n')

        declarations = {
            'ny': '@interop("std_sys")\nfn exit(code: i32)\nend fn\n'
                  'fn returned() -> i64\nprintln("once")\nreturn 7\nend fn\n',
            'matrix': 'note interop "std_sys"\nexit receives code as override integer begin paragraph\nend paragraph\n'
                      'returned returns number begin paragraph\nsay "once"\ngive 7\nend paragraph\n',
        }
        cases = [
            ('literal', 'exit(7)', 'evaluate exit with code being 7', ''),
            ('variable', 'let code: i64 = 7\nexit(code)',
             'value code as number is 7\nevaluate exit with code being code', ''),
            ('exact', 'let code: i32 = 7\nexit(code)',
             'value code as override integer is 7\nevaluate exit with code being code', ''),
            ('returned', 'exit(returned())',
             'evaluate exit with code being (returned with nothing)', 'once\n'),
            ('explicit', 'exit(7 as i32)',
             'evaluate exit with code being (7 becomes override integer)', ''),
            ('floating', 'exit(7.9 as i32)',
             'evaluate exit with code being (7.9 becomes override integer)', ''),
            # Narrowing follows existing CAST/storage modulo semantics. It is
            # deliberately tested rather than accidentally relying on LLVM
            # coercion or OS truncation of the process exit code.
            ('narrowing', 'let code: i64 = 4294967303\nexit(code)',
             'value code as number is 4294967303\nevaluate exit with code being code', ''),
        ]

        def execute(source, name, search=None, status=7, stdout='', diagnostic=None,
                    native=True):
            base = [compiler, '--quiet', '--no-stdlib']
            if search:
                base += ['-I', search]
            for encoding, flags in (('text', []), ('binary', ['--emit-binary-nil'])):
                artifact = root / (name + '-' + encoding + '.nil')
                run([*base, '--no-bundle', *flags, '-o', artifact, source])
                run([interpreter, '--no-ai', artifact], status, stdout, diagnostic)
            if native and args.llvm == 'ON':
                artifact = root / (name + '.native')
                run([*base, '--native', '-o', artifact, source])
                run([artifact], status, stdout, diagnostic)
                run([*base, '--native', '--jit', '-o', root / (name + '.jit'), source],
                    status, stdout, diagnostic)

        for language in ('ny', 'matrix'):
            for name, ny, matrix, stdout in cases:
                source = root / (name + '.' + language)
                source.write_text(program(language, ny if language == 'ny' else matrix,
                                          declarations[language]))
                execute(source, name + '-' + language, stdout=stdout)

            # A scalar conversion must still check nonfinite/out-of-range
            # floating values before the process-exit provider can run.
            source = root / ('outside-range.' + language)
            source.write_text(program(language,
                'exit(1e30 as i32)' if language == 'ny' else
                'evaluate exit with code being (1e30 becomes override integer)', declarations[language]))
            execute(source, 'outside-range-' + language, status=1,
                    diagnostic='CAST: floating-point value is non-finite or outside i64 range')

            # Lexical callable values take precedence over the declaration
            # named exit. Native indirect calls are unavailable; VM values
            # must retain their i64 argument without the global i32 boundary.
            for kind in ('named', 'lambda'):
                if language == 'ny':
                    extra = 'fn wide(value: i64) -> i64\nreturn value\nend fn\n'
                    body = ('let exit = wide\n' if kind == 'named' else
                            'let exit = fn(value: i64) -> i64\nreturn value\nend fn\n')
                    body += 'println(exit(4294967303))'
                else:
                    extra = 'wide receives item as number returns number begin paragraph\ngive item\nend paragraph\n'
                    body = ('value exit is the paragraph wide\n' if kind == 'named' else
                            'value exit is anonymous receives item as number returns number begin paragraph\n'
                            'give item\nend paragraph\n')
                    body += 'say exit with item being 4294967303'
                source = root / ('shadow-' + kind + '.' + language)
                source.write_text(program(language, body, declarations[language] + extra))
                execute(source, 'shadow-' + kind + '-' + language, status=0,
                        stdout='4294967303\n', native=False)

        # Source and binary-only libraries retain the annotated parameter
        # declaration. Matrix named arguments have already been reordered by
        # the checker when codegen applies each declared scalar boundary.
        for provider_language in ('ny', 'matrix'):
            providers = root / ('provider-' + provider_language)
            libraries = root / ('libraries-' + provider_language)
            providers.mkdir()
            libraries.mkdir()
            provider = providers / ('boundary.' + provider_language)
            provider.write_text(
                'module boundary\n@interop("std_sys")\npub fn exit(code: i32)\nend fn\n'
                '@intrinsic("math.pow")\npub fn power(base: f64, exponent: f64) -> f64\nend fn\n'
                if provider_language == 'ny' else
                'book boundary\nnote interop "std_sys"\npublic exit receives code as override integer begin paragraph\nend paragraph\n'
                'note intrinsic "math.pow"\npublic power receives base as floating, exponent as floating returns floating\n')
            for dependency in ('source', 'binary'):
                if dependency == 'binary':
                    run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                         '--emit-library', '-o', libraries / 'boundary.nil', provider])
                    provider.unlink()
                search = providers if dependency == 'source' else libraries
                for language in ('ny', 'matrix'):
                    source = root / ('consumer-' + provider_language + '-' + dependency + '.' + language)
                    source.write_text(program(language,
                        'println(power(3,2))\nexit(7)' if language == 'ny' else
                        'say power with exponent being 2, base being 3\n'
                        'evaluate exit with code being 7', imports=
                        'import boundary.*\n' if language == 'ny' else
                        'read everything from boundary\n'))
                    execute(source, 'library-' + provider_language + '-' + dependency + '-' + language,
                            search, stdout='9\n')

        # Representation normalization must not become implicit numeric
        # parsing, float truncation or pointer casts. Preserve provider checks
        # on both checked and unchecked source paths. Raw VM pointers and
        # booleans are integer words, so only native code can reject their
        # exact ABI type.
        invalid = [
            ('text', 'exit("7")', 'evaluate exit with code being "7"'),
            ('pointer', 'let p = allocate<i32>(1)\nexit(p)',
             'allocate p as pointer to override integer sized 1\nevaluate exit with code being p'),
            ('array', 'exit([7])', 'evaluate exit with code being [7]'),
            ('function', 'exit(returned)',
             'evaluate exit with code being the paragraph returned'),
            ('boolean', 'let code: bool = true\nexit(code)',
             'value code as boolean is true\nevaluate exit with code being code'),
            ('fraction', 'exit(7.9)', 'evaluate exit with code being 7.9'),
            ('huge_float', 'exit(1e30)', 'evaluate exit with code being 1e30'),
        ]
        for language in ('ny', 'matrix'):
            for name, ny, matrix in invalid:
                source = root / ('invalid-' + name + '.' + language)
                source.write_text(program(language, ny if language == 'ny' else matrix,
                                          declarations[language]))
                for unchecked in ([], ['--no-typecheck']):
                    for encoding in ([], ['--emit-binary-nil']):
                        output = root / 'invalid.nil'
                        run([compiler, '--quiet', '--no-stdlib', '--no-bundle', *unchecked,
                             *encoding, '-o', output, source])
                        if name not in ('pointer', 'boolean'):
                            run([interpreter, '--no-ai', output], status=1,
                                diagnostic='std_sys.exit: expected one integer code')
                    if args.llvm == 'ON':
                        for flags in (['--native'], ['--native', '--jit']):
                            output = root / 'protected-output'
                            output.write_bytes(b'previous artifact')
                            run([compiler, '--quiet', '--no-stdlib', *unchecked, *flags,
                                 '-o', output, source], status=1,
                                diagnostic='NativeCodegen')
                            assert output.read_bytes() == b'previous artifact'

            source = root / ('missing.' + language)
            source.write_text(program(language, 'exit()' if language == 'ny' else
                                      'evaluate exit with nothing', declarations[language]))
            for flags in (['--no-bundle'], ['--no-bundle', '--emit-binary-nil'],
                          *([['--native'], ['--native', '--jit']] if args.llvm == 'ON' else [])):
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                run([compiler, '--quiet', '--no-stdlib', *flags, '-o', output, source],
                    status=1, diagnostic='expects 1 arguments')
                assert output.read_bytes() == b'previous artifact'

            # Sleep deliberately validates integer durations itself. An
            # implicit CAST here would silently turn 0.5 into a zero wait.
            for name, value in (('fraction', '0.5'), ('text', '"invalid"'),
                                ('huge', '9223372036854775808.0')):
                source = root / ('duration-' + name + '.' + language)
                declaration = ('@intrinsic("time.sleep")\nfn sleep_ms(value: i64)\nend fn\n'
                               if language == 'ny' else
                               'note intrinsic "time.sleep"\nsleep-ms receives item as number\n')
                body = 'sleep_ms(' + value + ')' if language == 'ny' else \
                       'evaluate sleep-ms with item being ' + value
                source.write_text(program(language, body, declaration))
                for unchecked in ([], ['--no-typecheck']):
                    for encoding in ([], ['--emit-binary-nil']):
                        output = root / 'invalid-duration.nil'
                        run([compiler, '--quiet', '--no-stdlib', '--no-bundle', *unchecked,
                             *encoding, '-o', output, source])
                        run([interpreter, '--no-ai', output], status=1,
                            diagnostic='TIME_SLEEP: expected an integer duration')
                    if args.llvm == 'ON':
                        output = root / 'protected-duration'
                        output.write_bytes(b'previous artifact')
                        run([compiler, '--quiet', '--no-stdlib', '--native', *unchecked,
                             '-o', output, source], status=1,
                            diagnostic='TIME_SLEEP requires one integer duration')
                        assert output.read_bytes() == b'previous artifact'

        if args.llvm == 'ON':
            # Source casts qualify operands without relaxing provider ABIs.
            # A forged declaration still produces a rejected i64/f64 operand.
            for language in ('ny', 'matrix'):
                for width in ('i64', 'f64'):
                    source = root / ('forged-' + width + '.' + language)
                    declaration = (
                        f'@interop("std_sys")\nfn exit(code: {width})\nend fn\n'
                        if language == 'ny' else
                        'note interop "std_sys"\nexit receives code as ' +
                        ('number' if width == 'i64' else 'floating') +
                        ' begin paragraph\nend paragraph\n')
                    source.write_text(program(language,
                        'exit(7)' if language == 'ny' else
                        'evaluate exit with code being 7', declaration))
                    for flags in (['--native'], ['--native', '--jit']):
                        output = root / 'protected-output'
                        output.write_bytes(b'previous artifact')
                        run([compiler, '--quiet', '--no-stdlib', *flags, '-o', output, source],
                            status=1, diagnostic='std_sys.exit requires one i32 operand')
                        assert output.read_bytes() == b'previous artifact'

            raw = root / 'raw.nil'
            output = root / 'raw.o'
            valid = ('module raw\nfunction main() -> i32:\n'
                     'PUSH_CONST 7\nCAST i32\nINTEROP_CALL std_sys exit 1 void\n'
                     'PUSH_CONST 0\nCAST i32\nRET\nend function\n')
            raw.write_text(valid)
            run([assembler, '-o', output, raw], stdout=f'Assembled {raw} to {output}\n')
            invalid_nil = [
                (valid.replace('CAST i32\nINTEROP_CALL', 'INTEROP_CALL'), 'requires one i32 operand'),
                (valid.replace('exit 1 void', 'exit 1 void i64'), 'malformed std_sys'),
                (valid.replace('exit 1 void', 'exit 2 void'), 'malformed std_sys'),
                (valid.replace('exit 1 void', 'exit 1 i32'), 'malformed std_sys'),
            ]
            for payload, diagnostic in invalid_nil:
                raw.write_text(payload)
                output.write_bytes(b'previous object')
                run([assembler, '-o', output, raw], status=1, diagnostic=diagnostic)
                assert output.read_bytes() == b'previous object'

    print(f'{checks} native argument conversion and ABI checks passed')


if __name__ == '__main__':
    main()
