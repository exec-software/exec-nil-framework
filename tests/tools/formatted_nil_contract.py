#!/usr/bin/env python3
"""Typed and legacy TO_STRING preserve parity across raw NIL consumers."""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def binary_module(body):
    """Executable-v3 wire input deliberately bypasses source type checking."""
    def u32(value):
        return struct.pack('<I', value)

    def string(value):
        encoded = value.encode('utf-8')
        return u32(len(encoded)) + encoded

    function = (u32(1) + string('main') + u32(0) + u32(0) + string('i32') +
                u32(0) + u32(0) + u32(0) + u32(len(body)))
    for opcode, arguments in body:
        function += string(opcode) + u32(len(arguments))
        function += b''.join(string(value) for value in arguments) + u32(0)
    types = u32(0) + u32(1) + string('Probe') + u32(1) + string('Only') + string('')
    sections = [(1, types), (2, function),
                (3, u32(1) + string('message') + string('héllo text')),
                (4, u32(0)), (5, string('formatted_nil') + u32(0))]
    table = b''
    offset = 16 + 20 * len(sections)
    for kind, payload in sections:
        table += struct.pack('<IQQ', kind, offset, len(payload))
        offset += len(payload)
    return b'NIL\0' + struct.pack('<III', 3, 0, len(sections)) + table + b''.join(
        payload for _, payload in sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('interpreter', 'assembler', 'linker', 'runtime'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    for name in ('interpreter', 'assembler', 'linker', 'runtime'):
        setattr(args, name, getattr(args, name).resolve())
    environment = os.environ.copy()
    for key in ('NY_LIBRARY_PATH', 'NIL_LIBRARY_PATH', 'NY_LEX_DUMP',
                'NY_PARSE_TRACE', 'NY_STDLIB_PATH'):
        environment.pop(key, None)
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-formatted-wire-') as temporary:
        root = Path(temporary)

        def run(command):
            return subprocess.run([str(value) for value in command], cwd=root,
                                  env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60)

        def successful(result, output=None):
            assert result.returncode == 0 and not result.stderr, result
            assert output is None or result.stdout == output, result

        def write(name, body, binary):
            path = root / (name + ('.binary.nil' if binary else '.text.nil'))
            if binary:
                path.write_bytes(binary_module(body))
            else:
                instructions = '\n'.join(' '.join([opcode, *(
                    json.dumps(value, ensure_ascii=False) for value in arguments)])
                    for opcode, arguments in body)
                path.write_text('module formatted_nil\nconst message = "héllo text"\n'
                                'enum Probe\nOnly\nend enum\n'
                                'function main() -> i32:\n' + instructions +
                                '\nend function\n')
            return path

        # CAST and comparisons preserve bool metadata natively; unlike plain
        # PUSH_CONST 1, these expose an accidental true/false legacy conversion.
        body = []
        expected = []
        cases = [
            ([('PUSH_CONST', ['1']), ('CAST', ['bool'])], 'bool', 'true'),
            ([('PUSH_CONST', ['0']), ('CAST', ['bool'])], 'bool', 'false'),
            ([('PUSH_CONST', ['1']), ('CAST', ['bool'])], '', '1'),
            ([('PUSH_CONST', ['0']), ('CAST', ['bool'])], '', '0'),
            ([('PUSH_CONST', ['-1']), ('PUSH_CONST', ['-1']), ('CMP_EQ', [])], '', '1'),
            ([('PUSH_CONST', ['-9223372036854775808'])], 'i64', '-9223372036854775808'),
            ([('PUSH_CONST', ['-42']), ('CAST', ['i32'])], 'i32', '-42'),
            ([('PUSH_CONST', ['255']), ('CAST', ['u8'])], 'u8', '255'),
            ([('PUSH_CONST', ['-42'])], '', '-42'),
            ([('PUSH_CONST', ['-0.0'])], 'f64', '-0'),
            ([('PUSH_CONST', ['1.2345678901234567'])], '', '1.2345678901234567'),
            ([('PUSH_CONST_STR', ['message'])], 'text', 'héllo text'),
            ([('PUSH_CONST_STR', ['message'])], '', 'héllo text'),
        ]
        for prefix, kind, output in cases:
            body += prefix + [('TO_STRING', [kind] if kind else []), ('CALL', ['println', '1'])]
            expected.append(output)
        body += [('PUSH_CONST', ['0']), ('RET', [])]
        for binary in (False, True):
            source = write('scalars', body, binary)
            successful(run([args.interpreter, '--no-ai', source]), '\n'.join(expected) + '\n')
            checks += len(cases)
            if args.llvm == 'ON':
                object_path = root / ('binary.o' if binary else 'text.o')
                executable = root / ('binary' if binary else 'text')
                successful(run([args.assembler, '-o', object_path, source]))
                successful(run([args.linker, '--runtime', args.runtime, '-o', executable, object_path]))
                successful(run([executable]), '\n'.join(expected) + '\n')
                checks += len(cases)

        invalid = [
            [('PUSH_CONST', ['1.5']), ('TO_STRING', ['bool'])],
            [('PUSH_CONST', ['1']), ('TO_STRING', ['text'])],
            [('PUSH_CONST_STR', ['message']), ('TO_STRING', ['f64'])],
            [('PUSH_CONST', ['1']), ('TO_STRING', ['array<i32>'])],
            [('PUSH_CONST', ['1']), ('TO_STRING', ['i32', 'i64'])],
            [('NEW_VARIANT', ['Probe::Only', '0']), ('TO_STRING', ['i64'])],
            [('NEW_VARIANT', ['Probe::Only', '0']), ('TO_STRING', [])],
        ]
        for index, invalid_body in enumerate(invalid):
            for binary in (False, True):
                source = write(f'invalid-{index}', invalid_body + [('POP', []), ('PUSH_CONST', ['0']), ('RET', [])], binary)
                result = run([args.interpreter, '--no-ai', source])
                assert result.returncode != 0 and not result.stdout and 'TO_STRING' in result.stderr, result
                checks += 1
                if args.llvm == 'ON':
                    output = root / 'protected.o'
                    output.write_bytes(b'previous artifact')
                    result = run([args.assembler, '-o', output, source])
                    assert result.returncode != 0 and 'TO_STRING' in result.stderr, result
                    assert output.read_bytes() == b'previous artifact'
                    checks += 1
    print(f'{checks} raw NIL scalar formatting and rejection checks passed')


if __name__ == '__main__':
    main()
