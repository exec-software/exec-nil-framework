#!/usr/bin/env python3
"""Checked conversion providers preserve Result ownership across source and NIL."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SENTINEL = b'previous artifact must survive invalid conversion declarations\n'


def wire(binary, owner='nil::std::core::Result', *, instruction_owner=None,
         declared_owner=None, variants=None, duplicate=False, missing=False,
         legacy_operand=False, parameters=None):
    result = owner + '<i32,text>'
    declaration = (declared_owner or owner) + '<i32,text>'
    operand = 'i64' if legacy_operand else (instruction_owner or owner) + '<i32,text>'
    variants = variants or [('Ok', 'i32'), ('Error', 'text')]
    enums = [] if missing else [(result, variants)] * (2 if duplicate else 1)
    functions = [
        ('provider', ['value:text'] if parameters is None else parameters, declaration, ['@intrinsic("convert.try_to_i32")'], [
            ('STORE_LOCAL', ['value']), ('LOAD_LOCAL', ['value']),
            ('INTEROP_CALL', ['intrinsic', 'convert.try_to_i32', '1', operand]), ('RET', [])]),
        ('main', [], 'i32', [], [('PUSH_CONST_STR', ['value']), ('CALL', ['provider', '1']),
            ('IS_VARIANT', ['Result::Ok', result, 'i32']), ('CALL', ['println', '1']),
            ('PUSH_CONST', ['0']), ('RET', [])]),
    ]
    if not binary:
        lines = ['module checked_conversion', 'const value = "7"']
        for name, values in enums:
            lines += ['enum ' + name, *(f'{variant}: {kind}' for variant, kind in values), 'end enum']
        for name, parameters, returned, annotations, body in functions:
            lines += annotations + [f'function {name}({", ".join(parameters)}) -> {returned}:']
            lines += [' '.join([opcode, *(json.dumps(value) for value in arguments)])
                      for opcode, arguments in body]
            lines += ['end function']
        return ('\n'.join(lines) + '\n').encode()

    def u32(value):
        return struct.pack('<I', value)

    def string(value):
        data = value.encode()
        return u32(len(data)) + data

    types = u32(0) + u32(len(enums))
    for name, values in enums:
        types += string(name) + u32(len(values))
        types += b''.join(string(variant) + string(kind) for variant, kind in values) + u32(0)
    code = u32(len(functions))
    for name, parameters, returned, annotations, body in functions:
        code += string(name) + u32(len(parameters)) + b''.join(map(string, parameters))
        code += u32(0) + string(returned) + u32(0) + u32(0)
        code += u32(len(annotations)) + b''.join(map(string, annotations)) + u32(len(body))
        for opcode, arguments in body:
            code += string(opcode) + u32(len(arguments)) + b''.join(map(string, arguments)) + u32(0)
    sections = [(1, types), (2, code), (3, u32(1) + string('value') + string('7')),
                (4, u32(0)), (5, string('checked_conversion') + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b''
    for kind, data in sections:
        table += struct.pack('<IQQ', kind, offset, len(data))
        offset += len(data)
    return b'NIL\0' + struct.pack('<III', 3, 2, len(sections)) + table + b''.join(data for _, data in sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'assembler'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter, assembler = (getattr(args, name).resolve()
                                       for name in ('compiler', 'interpreter', 'assembler'))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-conversion-abi-') as directory:
        work = Path(directory)
        base = [compiler, '--quiet', '--sdk-path', ROOT / 'nilstd/src', '-I', work]
        analysis_base = [part for part in base if part != '--quiet']

        def run(command, status=0):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work, env=environment,
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        custom = 'module impostor\npub enum Result<T,E>\n Ok(T)\n Error(E)\nend enum\n'
        (work / 'impostor.ny').write_text(custom, newline='\n')
        matrix_types = {'i32': 'override integer', 'i64': 'override long', 'f64': 'floating', 'bool': 'boolean'}
        for value, matrix_type in matrix_types.items():
            provider = 'convert.try_to_' + value
            sources = {
                'ny': 'import impostor\n@intrinsic("' + provider + '")\n' +
                    'fn provider(value: text) -> impostor::Result<' + value + ',text>\nend fn\n' +
                    'fn main() -> i32\n return 0\nend fn\n',
                'matrix': 'book rejected-conversion\nread impostor\nnote intrinsic "' + provider + '"\n' +
                    'provider receives input-text as text returns impostor.Result of ' + matrix_type + ', text\n' +
                    'main returns override integer begin paragraph\n give 0\nend paragraph\n',
            }
            for language, contents in sources.items():
                source = work / ('invalid-' + value + '.' + language)
                source.write_text(contents, newline='\n')
                analysis = run([*analysis_base, '--analyze', source], 1)
                report = json.loads(analysis.stdout)
                assert not report['success'] and 'standard Result ABI' in analysis.stdout, report
                for flags in ([], ['--no-typecheck']):
                    output = work / 'protected.nil'
                    output.write_bytes(SENTINEL)
                    result = run([*base, *flags, '--no-bundle', '-o', output, source], 1)
                    assert 'standard Result ABI' in result.stderr, result.stderr
                    assert output.read_bytes() == SENTINEL
                if args.llvm == 'ON':
                    for route in (['--native'], ['--native', '--jit']):
                        output = work / ('protected-native.exe' if os.name == 'nt' else 'protected-native')
                        output.write_bytes(SENTINEL)
                        result = run([*base, '--no-typecheck', *route, '-o', output, source], 1)
                        assert 'standard Result ABI' in result.stderr, result.stderr
                        assert output.read_bytes() == SENTINEL

        # The same leaf name, an alias, and matching variant shape cannot grant
        # a user enum the runtime's standard Result tag identity.
        source = work / 'alias.ny'
        source.write_text('import impostor\ntype Converted = impostor::Result<i32,text>\n'
            '@intrinsic(convert.try_to_i32)\nfn provider(value: text) -> Converted\nend fn\n'
            'fn main() -> i32\n return 0\nend fn\n', newline='\n')
        assert 'standard Result ABI' in run([*analysis_base, '--analyze', source], 1).stdout

        for language, declaration in (
            ('ny', '@intrinsic("convert.try_to_i32")\n'
             'fn provider(input: text) -> nil::std::core::Result<i32,text>\nend fn\n'),
            ('matrix', 'book missing-conversion-import\nnote intrinsic "convert.try_to_i32"\n'
             'provider receives input-text as text returns nil.std.core.Result of override integer, text\n'),
        ):
            source = work / ('missing-import.' + language)
            source.write_text(declaration, newline='\n')
            result = run([*analysis_base, '--analyze', source], 1)
            assert 'standard Result ABI' in result.stdout, result

        for owner in ('nil.std.core', 'ny.lang.core'):
            for language in ('ny', 'matrix'):
                if language == 'ny':
                    contents = 'import ' + owner + '.result\ntype Parsed = Result<i32,text>\n'
                    for value in ('i32', 'i64'):
                        returned = 'Parsed' if value == 'i32' else 'Result<i64,text>'
                        contents += '@intrinsic("convert.try_to_' + value + '")\n' +\
                            'fn parse_' + value + '(value: text) -> ' + returned + '\nend fn\n'
                    contents += 'fn main() -> i32\n'
                    for value in ('i32', 'i64'):
                        contents += ' match parse_' + value + '("7")\n' +\
                            '  Result.Ok(value) -> println(value)\n  Result.Error(error) -> return 1\n end match\n' +\
                            ' match parse_' + value + '("bad")\n' +\
                            '  Result.Ok(value) -> return 2\n  Result.Error(error) -> println("rejected")\n end match\n'
                    contents += ' return 0\nend fn\n'
                else:
                    contents = 'book accepted-conversion\nread ' + owner + '.result\nParsed means Result of override integer, text\n'
                    for value in ('i32', 'i64'):
                        returned = 'Parsed' if value == 'i32' else 'Result of override long, text'
                        contents += 'note intrinsic "convert.try_to_' + value + '"\n' +\
                            'parse-' + value + ' receives input-text as text returns ' + returned + '\n'
                    contents += 'main returns override integer begin paragraph\n'
                    for value in ('i32', 'i64'):
                        contents += ' consider parse-' + value + ' with "7"\n' +\
                            '  when Result.Ok holding parsed-value then say parsed-value\n  when Result.Error holding message then give 1\n end consider\n' +\
                            ' consider parse-' + value + ' with "bad"\n' +\
                            '  when Result.Ok holding parsed-value then give 2\n  when Result.Error holding message then say "rejected"\n end consider\n'
                    contents += ' give 0\nend paragraph\n'
                source = work / ('valid-' + owner.replace('.', '-') + '.' + language)
                source.write_text(contents, newline='\n')
                assert json.loads(run([*analysis_base, '--analyze', source]).stdout)['success']
                for route in ('text', 'binary', 'native', 'jit') if args.llvm == 'ON' else ('text', 'binary'):
                    output = work / ('run-' + owner + '-' + language + '-' + route + ('.exe' if os.name == 'nt' else ''))
                    flags = ['--native', '--jit'] if route == 'jit' else ['--native'] if route == 'native' else ['--no-bundle']
                    if route == 'binary': flags += ['--emit-binary-nil']
                    result = run([*base, *flags, '-o', output, source])
                    if route != 'jit':
                        result = run([output] if route == 'native' else [interpreter, '--no-ai', output])
                    assert result.stdout == '7\nrejected\n7\nrejected\n', result

        invalid_wire = {
            'custom-owner': {'owner': 'impostor::Result'},
            'custom-call-owner': {'instruction_owner': 'impostor::Result'},
            'custom-declaration-owner': {'declared_owner': 'impostor::Result'},
            'missing-schema': {'missing': True},
            'duplicate-schema': {'duplicate': True},
            'wrong-payload': {'variants': [('Ok', 'i64'), ('Error', 'text')]},
            'wrong-error': {'variants': [('Ok', 'i32'), ('Error', 'i64')]},
            'wrong-variants': {'variants': [('Ok', 'i32'), ('Failure', 'text')]},
            'wrong-input': {'parameters': ['value:i64']},
            'wrong-arity': {'parameters': ['value:text', 'other:text']},
        }
        for binary in (False, True):
            for owner in ('nil::std::core::Result', 'ny::lang::core::Result', 'Result'):
                for legacy in (False, True):
                    path = work / 'wire-valid.nil'
                    path.write_bytes(wire(binary, owner, legacy_operand=legacy))
                    assert run([interpreter, '--no-ai', path]).stdout == '1\n'
            for name, changes in invalid_wire.items():
                path = work / (name + '.nil')
                path.write_bytes(wire(binary, **changes))
                result = run([interpreter, '--no-ai', path], 1)
                assert not result.stdout and 'standard Result ABI' in result.stderr, result
                if args.llvm == 'ON':
                    output = work / 'protected.o'
                    output.write_bytes(SENTINEL)
                    result = run([assembler, '-o', output, path], 1)
                    assert 'standard Result ABI' in result.stderr, result
                    assert output.read_bytes() == SENTINEL

        # Textual NIL admits initializer instructions outside a function.
        # The CLI enters main; embedding execution is covered by nil.loader.
        # Both paths need the same owner/schema proof as wrapped calls.
        for owner, status in (('nil::std::core::Result', 0), ('impostor::Result', 1)):
            kind = owner + '<i32,text>'
            path = work / 'module-initializer.nil'
            path.write_text('module initializer\nconst value = "7"\n'
                f'enum {kind}\nOk: i32\nError: text\nend enum\n'
                'PUSH_CONST_STR value\n'
                f'INTEROP_CALL intrinsic convert.try_to_i32 1 {kind}\n'
                f'IS_VARIANT Result::Ok {kind} i32\nCALL println 1\n'
                'function main() -> i32:\nPUSH_CONST_STR value\n'
                f'INTEROP_CALL intrinsic convert.try_to_i32 1 {kind}\n'
                f'IS_VARIANT Result::Ok {kind} i32\nCALL println 1\n'
                'PUSH_CONST 0\nRET\nend function\n', newline='\n')
            result = run([interpreter, '--no-ai', path], status)
            if status:
                assert not result.stdout and 'standard Result ABI' in result.stderr, result
            else:
                assert result.stdout == '1\n', result
    print(f'Checked conversion intrinsic ABI: {checks} checks passed')


if __name__ == '__main__':
    main()
