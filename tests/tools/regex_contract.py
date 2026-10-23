#!/usr/bin/env python3
"""Bounded regex captures and iteration agree across source and execution routes."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for tool in ('compiler', 'interpreter', 'stdlib'):
        parser.add_argument('--' + tool, type=Path, required=True)
    for tool in ('assembler', 'linker', 'runtime'):
        parser.add_argument('--' + tool, type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    tools = {key: str(value.resolve(strict=True)) for key, value in vars(args).items()
             if isinstance(value, Path)}
    env = {key: value for key, value in os.environ.items() if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil regex contract ') as directory:
        root = Path(directory)

        def run(command, *, data=b'', status=0, profile=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root,
                env=env if profile is None else {**env, 'NIL_MEMORY_PROFILE': str(profile)},
                input=data, capture_output=True, timeout=120)
            assert result.returncode == status, (command, result)
            checks += 1
            return result

        def native(nil, name, mode):
            obj, exe = root / (name + '.o'), root / name
            run([tools['assembler'], *(['--unmanaged'] if mode == 'unsafe' else []), '-o', obj, nil])
            run([tools['linker'], '--runtime', tools['runtime'], '-o', exe, obj])
            return [exe]

        base = [tools['compiler'], '--quiet', '--no-bundle', '--no-stdlib', '--sdk-path', tools['stdlib']]
        expected = ('alpha\nalpha42\n42\n1\n7\nbeta\n' + '1\n' * 10 + '3\nalpha42\n12\n').encode()
        for language in ('ny', 'matrix'):
            source = Path(__file__).resolve().parent / 'fixtures' / 'regex' / ('captures.' + language)
            for binary in (False, True):
                nil = root / (language + ('-binary' if binary else '-text') + '.nil')
                run([*base, '--debug-info', *(['--emit-binary-nil'] if binary else []), '-o', nil, source])
                for mode in ('safe', 'unsafe'):
                    routes = [('vm', [tools['interpreter'], '--no-ai', '--memory-mode=' + mode, nil])]
                    if args.llvm == 'ON':
                        routes.append(('native', native(nil, language + str(binary) + mode, mode)))
                    for route, command in routes:
                        report = root / (language + str(binary) + mode + route + '.json')
                        result = run(command, profile=report)
                        assert result.stdout == expected and not result.stderr, result
                        profile = json.loads(report.read_text())
                        assert profile['completed'] and not profile['errors'], profile
                        assert profile['totals']['allocated_objects'] >= 30000, profile
                        if mode == 'safe':
                            assert profile['collections']['count'] > 0, profile
                            assert profile['totals']['freed_objects'] > 0, profile
            if args.llvm == 'ON':
                for mode in ('safe', 'unsafe'):
                    result = run([*base, '--native', '--jit',
                                  *(['--unmanaged'] if mode == 'unsafe' else []),
                                  '-o', root / (language + mode + '.jit'), source])
                    assert result.stdout == expected and not result.stderr, result

        # Dynamic records cannot bypass compile-time validation. A public
        # Pattern assembled by the caller is validated again by the engine.
        guards = root / 'guards.ny'
        guards.write_text('''import nil.std.text
fn main() -> i32
    let forged = Pattern { source: "[" }
    println(first_match(forged, "a").is_error())
    let spans: array<i64> = [1 as i64, 99 as i64, 2 as i64]
    let bad = PatternMatch { input: "a", pattern: "a", spans: spans }
    println(capture_index(bad, 0).is_none())
    println(next_match(bad).is_error())
    let expensive = compile_pattern("^(a+)+$").unwrap()
    let exhausted = first_match(expensive, "aaaaaaaaaaaaaaaaaaaaaaaaaaaa!")
    println(exhausted.is_error())
    println(exhausted.unwrap_error())
    let over_limit = Pattern { source: "PATTERN_LIMIT_PLACEHOLDER" }
    println(first_match(over_limit, "a").unwrap_error())
    return 0
end fn
'''.replace('PATTERN_LIMIT_PLACEHOLDER', 'a' * 4097))
        nil = root / 'guards.nil'
        run([*base, '-o', nil, guards])
        commands = [[tools['interpreter'], '--no-ai', nil]]
        if args.llvm == 'ON': commands.append(native(nil, 'guards', 'safe'))
        for command in commands:
            result = run(command)
            assert result.stdout == (b'1\n1\n1\n1\n' +
                b'pattern matching budget exceeded\n' * 2) and not result.stderr, result

        direct = root / 'direct.nil'
        direct.write_text('''module direct_regex
const pattern = "(?<word>a+)"
const input = "!aaa!"
const name = "word"
function main() -> i32:
PUSH_CONST_STR pattern
INTEROP_CALL intrinsic regex.validate 1 text
CALL println 1
PUSH_CONST_STR pattern
PUSH_CONST_STR name
INTEROP_CALL intrinsic regex.group_index 2 i64
CALL println 1
PUSH_CONST_STR pattern
PUSH_CONST_STR input
PUSH_CONST 0
CAST i64
PUSH_CONST 0
CAST bool
INTEROP_CALL intrinsic regex.search 4 array<i64>
STORE_LOCAL found
LOAD_LOCAL found
PUSH_CONST 0
GET_INDEX
CALL println 1
LOAD_LOCAL found
PUSH_CONST 1
GET_INDEX
CALL println 1
LOAD_LOCAL found
PUSH_CONST 2
GET_INDEX
CALL println 1
PUSH_CONST 0
RET
end function
''')
        commands = [[tools['interpreter'], '--no-ai', direct]]
        if args.llvm == 'ON': commands.append(native(direct, 'direct', 'safe'))
        for command in commands:
            result = run(command)
            assert result.stdout == b'\n1\n1\n1\n3\n' and not result.stderr, result

        invalid = {
            'wrong-result': ('', 'PUSH_CONST_STR text\nINTEROP_CALL intrinsic regex.validate 1 i64\nPOP'),
            'wrong-input': ('', 'PUSH_CONST 1\nINTEROP_CALL intrinsic regex.validate 1 text\nPOP'),
            'wrong-count': ('', 'INTEROP_CALL intrinsic regex.search 0 array<i64>\nPOP'),
            'unknown-operation': ('', 'PUSH_CONST_STR text\nINTEROP_CALL intrinsic regex.unknown 1 text\nPOP'),
            'wrapper-wrong-input': ('@intrinsic("regex.validate")\nfunction wrong(input: i64) -> text:\nend function', 'PUSH_CONST 1\nCAST i64\nCALL wrong 1\nPOP'),
            'wrapper-wrong-result': ('@intrinsic("regex.search")\nfunction wrong(pattern: text, input: text, start: i64, full: bool) -> array<text>:\nend function', 'PUSH_CONST_STR text\nPUSH_CONST_STR text\nPUSH_CONST 0\nCAST i64\nPUSH_CONST 0\nCAST bool\nCALL wrong 4\nPOP'),
        }
        if args.llvm == 'ON':
            for name, (declaration, body) in invalid.items():
                nil, output = root / (name + '.nil'), root / (name + '.o')
                nil.write_text('module invalid_regex\nconst text = "a"\n' + declaration +
                    '\nfunction main() -> i32:\n' + body + '\nPUSH_CONST 0\nRET\nend function\n')
                result = run([tools['assembler'], '-o', output, nil], status=1)
                assert b'regex' in result.stderr and not output.exists(), result
    print(f'regex contract: {checks} executions/checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
