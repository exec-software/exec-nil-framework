#!/usr/bin/env python3
"""Reject forged database intrinsic ABI metadata before publishing native code."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    env = dict(os.environ, NIL_STDLIB_PATH=str(args.stdlib.resolve()))
    with tempfile.TemporaryDirectory(prefix='nil-db-contract-') as directory:
        root = Path(directory)
        source = root / 'valid.nil'
        compiled = subprocess.run([str(args.compiler.resolve()), '--quiet', '--no-bundle', '--output', str(source), str(repo / 'tests/language/database-sqlite-runtime.ny')], env=env, capture_output=True, text=True, timeout=30)
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        original = source.read_text()
        output = root / 'probe.o'
        def assemble(payload: str) -> subprocess.CompletedProcess[str]:
            source.write_text(payload)
            return subprocess.run([str(args.assembler.resolve()), '-o', str(output), str(source)], env=env, capture_output=True, text=True, timeout=30)
        accepted = assemble(original)
        if accepted.returncode:
            if 'LLVM backend' in accepted.stderr and 'unavailable' in accepted.stderr:
                return 77
            raise AssertionError(accepted.stderr)
        cases = []

        # Split at the exact declaration line instead of interpreting enum
        # names: canonical namespace prefixes contain colons of their own.
        for operation in ('available', 'open', 'execute', 'rows', 'columns', 'affected', 'is_null', 'column_name', 'cell', 'release', 'close'):
            match = re.search(r'(@intrinsic\("db2\.' + operation + r'"\)\n(?:#[^\n]*\n)*)(function [^\n]+)', original)
            if not match:
                raise AssertionError(f'missing {operation} declaration')
            line = match[2]
            arguments = line[line.index('(') + 1:line.index(')')].split(', ')
            for index, argument in enumerate(arguments):
                if not argument:
                    continue
                name, kind = argument.split(': ', 1)
                alternatives = ('i64', 'array<i64>') if kind == 'text' else ('text', 'f64') if kind == 'i64' else ('array<i64>', 'text')
                for alternative in alternatives:
                    mutated_arguments = list(arguments)
                    mutated_arguments[index] = name + ': ' + alternative
                    changed = line[:line.index('(') + 1] + ', '.join(mutated_arguments) + line[line.index(')'):]
                    cases.append((f'{operation}-arg{index}-{alternative}', original[:match.start(2)] + changed + original[match.end(2):]))
            changed = line[:line.index(') -> ') + 5] + 'i64:'
            cases.append((f'{operation}-return', original[:match.start(2)] + changed + original[match.end(2):]))
        sentinel = b'previous object is preserved\n'
        for name, payload in cases:
            output.write_bytes(sentinel)
            rejected = assemble(payload)
            if rejected.returncode == 0 or 'invalid native ABI' not in rejected.stderr:
                raise AssertionError(f'{name}: not rejected at database boundary\n{rejected.stderr}')
            if output.read_bytes() != sentinel:
                raise AssertionError(f'{name}: rejected NIL replaced native output')
        print(f'database ABI: valid control and {len(cases)} forged signatures checked')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
