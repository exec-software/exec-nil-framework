#!/usr/bin/env python3
"""Serialized NILData interop checks types before entering the shared engine."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('interpreter', 'assembler', 'linker', 'runtime'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil data ABI ') as directory:
        root = Path(directory)

        def run(command, success=True):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, timeout=30)
            assert (result.returncode == 0) == success, (command, result)
            checks += 1
            return result

        declarations = '''module data_contract
const operation = "from_json"
const source = "[null,9007199254740993]"
const empty = ""
'''
        operands = 'PUSH_CONST_STR operation\nPUSH_CONST_STR source\nPUSH_CONST_STR empty\nPUSH_CONST 0\n'
        call = 'INTEROP_CALL intrinsic nildata.request 4 array<text>\n'
        for mode in ('safe', 'unsafe'):
            source = root / (mode + '.nil')
            source.write_text(declarations + 'function main() -> i32:\n' + operands + call +
                              'PUSH_CONST 1\nGET_INDEX\nCALL println 1\nPUSH_CONST 0\nRET\nend function\n')
            result = run([args.interpreter.resolve(), '--no-ai', '--memory-mode=' + mode, source])
            assert result.stdout == '[null,9007199254740993]\n' and not result.stderr, result
            if args.llvm == 'ON':
                output, executable = root / (mode + '.o'), root / mode
                flags = ['--unmanaged'] if mode == 'unsafe' else []
                run([args.assembler.resolve(), *flags, '-o', output, source])
                run([args.linker.resolve(), '--runtime', args.runtime.resolve(), '-o', executable, output])
                result = run([executable])
                assert result.stdout == '[null,9007199254740993]\n' and not result.stderr, result

        invalid = {
            'wrong-result': operands + call.replace('array<text>', 'array<i64>'),
            'wrong-count': operands + call.replace('request 4', 'request 3'),
            'extra-metadata': operands + call.replace('array<text>', 'array<text> i64'),
            'unknown-operation': operands + call.replace('nildata.request', 'nildata.unknown'),
            'wrong-text-operand': operands.replace('PUSH_CONST_STR source', 'PUSH_CONST 7') + call,
            'wrong-index-operand': operands.replace('PUSH_CONST 0', 'PUSH_CONST_STR empty') + call,
        }
        for name, body in invalid.items():
            source, output = root / (name + '.nil'), root / (name + '.o')
            source.write_text(declarations + 'function main() -> i32:\n' + body +
                              'POP\nPUSH_CONST 0\nRET\nend function\n')
            result = run([args.interpreter.resolve(), '--no-ai', source], success=False)
            assert 'nildata.request' in result.stderr and not result.stdout, result
            if args.llvm == 'ON':
                result = run([args.assembler.resolve(), '-o', output, source], success=False)
                assert 'nildata.request' in result.stderr and not output.exists(), result

        if args.llvm == 'ON':
            for name, signature in [('index', 'index: text'), ('source', 'source: i64')]:
                parameters = 'operation: text, source: text, argument: text, index: i64'
                parameters = parameters.replace('index: i64' if name == 'index' else 'source: text', signature)
                source, output = root / ('wrapper-' + name + '.nil'), root / ('wrapper-' + name + '.o')
                source.write_text(declarations + '@intrinsic("nildata.request")\nfunction wrong(' + parameters +
                                  ') -> array<text>:\nend function\nfunction main() -> i32:\n' +
                                  operands + 'CALL wrong 4\nPOP\nPUSH_CONST 0\nRET\nend function\n')
                result = run([args.assembler.resolve(), '-o', output, source], success=False)
                assert 'nildata.request' in result.stderr and not output.exists(), result
    print(f'NILData intrinsic contract: {checks} checks passed')


if __name__ == '__main__':
    main()
