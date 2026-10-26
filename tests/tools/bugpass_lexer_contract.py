#!/usr/bin/env python3
"""Incomplete Ny comments and blocks fail before writes and in editor analysis."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    flags = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil'),
             (), ('--emit-binary-nil',), ('--no-bundle', '--no-typecheck')]
    if args.llvm == 'ON':
        flags += [('--native',), ('--native', '--jit')]
    complete = 'fn main() -> i32\nprintln(42)\nreturn 0\nend fn\n'
    # Match the opening line too: a comment's EOF can be many lines later.
    invalid = []
    for label, opening, closing in [('slash', '/*', '*/'), ('hash', '###', '###')]:
        for ending in ('', '\nmore comment\n'):
            invalid.append((f'comment-{label}-{len(ending)}',
                complete + opening + ' never closed' + ending,
                f"Unterminated block comment; expected '{closing}'", 5))
        invalid.append((f'comment-in-body-{label}',
            'fn main() -> i32\n' + opening + '\nreturn 0\nend fn\n',
            f"Unterminated block comment; expected '{closing}'", 2))

    invalid += [
        ('function', 'fn main() -> i32\nreturn 0\n', 'Unterminated function body', 1),
        ('function-alias', 'function main() -> i32\nreturn 0\n',
         'Unterminated function body', 1),
        ('while', 'fn main() -> i32\nwhile false\nprintln(42)\n',
         'Unterminated while statement', 2),
        ('for', 'fn main() -> i32\nfor item in [1]\nprintln(item)\n',
         'Unterminated for statement', 2),
        ('loop', 'fn main() -> i32\nloop\nbreak\n',
         'Unterminated loop statement', 2),
        ('struct', complete + 'struct Example\nx: i32\n',
         "Unterminated struct declaration; expected 'end struct'", 5),
        ('struct-brace', complete + 'struct Example {\nx: i32\n',
         "Unterminated struct declaration; expected '}'", 5),
        ('enum', complete + 'enum Example\nA\n',
         'Unterminated enum declaration', 5),
        ('class', complete + 'class Example\nx: i32\n',
         "Unterminated class declaration; expected 'end class'", 5),
        ('class-brace', complete + 'class Example {\nx: i32\n',
         "Unterminated class declaration; expected '}'", 5),
        ('anonymous-function',
         'fn main() -> i32\nlet callback = function() -> i32\nreturn 0\n',
         'Unterminated anonymous function', 2),
        # These already required terminators; keep their fail-closed behavior.
        ('if', 'fn main() -> i32\nif true then\nreturn 0\n',
         "Expected 'end if'", None),
        ('match', 'fn main() -> i32\nmatch 0\n_ -> return 0\n',
         "Expected 'end' to close match statement", None),
        ('function-brace', 'fn main() -> i32 {\nreturn 0\n',
         "Expected '}' after block", None),
        ('trait', complete + 'trait Example\nfn read(self) -> i32\n',
         'Unterminated trait declaration', 5),
        ('impl', complete + 'impl i32\n', 'Unterminated impl declaration', None),
    ]
    valid = [
        ('closed-eof', complete.rstrip(), '42\n'),
        ('function-short-end', complete.replace('end fn', 'end').rstrip(), '42\n'),
        ('function-alias', complete.replace('fn ', 'function ').replace('end fn', 'end function'), '42\n'),
        ('function-brace', 'fn main() -> i32 { println(42) return 0 }', '42\n'),
        ('closed-slash-eof', complete + '/* closed\ncomment */', '42\n'),
        ('closed-hash-eof', complete + '### closed\ncomment ###', '42\n'),
        ('empty-slash-eof', complete + '/**/', '42\n'),
        ('empty-hash-eof', complete + '######', '42\n'),
        ('forward-eof', complete + 'fn declared(value: i32) -> i32', '42\n'),
        ('intrinsic-eof', complete + '@intrinsic("math.sqrt")\nfn root(value: f64) -> f64', '42\n'),
        ('abstract', 'abstract class Example\nabstract fn value(self) -> i32\n'
         'end fn\nend class\n' + complete, '42\n'),
        ('class-short-end', 'class Example\nx: i32\nend\n' + complete, '42\n'),
        ('closed-declarations', 'struct Point { x: i32 }\nstruct Pair\nx: i32\nend struct\n'
         'enum Choice\nA\nend enum\nclass Box\nx: i32\nend class\n'
         'class Boxed { x: i32 }\n' + complete, '42\n'),
        ('closed-loops', 'fn main() -> i32\nwhile false\nprintln(0)\nend while\n'
         'for item in [42]\nprintln(item)\nend for\nloop\nbreak\nend loop\nreturn 0\nend fn', '42\n'),
        ('literal-markers', 'fn main() -> i32\nprintln("/* ### */")\n'
         'println($"### {{literal}} /* */")\nprintln(\'#\' == (35 as u8))\n'
         'println(\'/\' == (47 as u8))\nreturn 0\nend fn', '/* ### */\n### {literal} /* */\n1\n1\n'),
    ]

    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-lexer-block-contract-') as directory:
        root = Path(directory)

        def run(command):
            return subprocess.run(list(map(str, command)), cwd=root, env=environment,
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)

        for name, source, message, line in invalid:
            path = root / (name + '.ny')
            path.write_text(source)
            for route_flags in flags:
                artifact = root / 'protected-output'
                artifact.write_bytes(b'previous artifact')
                result = run([compiler, '--quiet', '--no-stdlib', *route_flags,
                              '-o', artifact, path])
                assert result.returncode == 1 and not result.stdout and message in result.stderr, (
                    name, route_flags, result)
                assert artifact.read_bytes() == b'previous artifact', (name, route_flags)
                if line is not None:
                    assert f'{path}:{line}: {message}' in result.stderr, result.stderr
                checks += 1
            result = run([compiler, '--analyze', '--no-stdlib', path])
            assert result.returncode == 1 and not result.stderr, (name, result)
            analysis = json.loads(result.stdout)
            errors = [item for item in analysis['diagnostics']
                      if item['severity'] == 'error' and message in item['message']]
            assert not analysis['success'] and errors, (name, analysis)
            if line is not None:
                opening = sum(len(item) for item in source.splitlines(keepends=True)[:line - 1])
                assert any(item.get('start') == opening for item in errors), (name, errors)
            checks += 1

        for name, source, expected in valid:
            path = root / (name + '.ny')
            path.write_text(source)
            artifact = path.with_suffix('.nil')
            result = run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', artifact, path])
            assert result.returncode == 0 and not result.stdout and not result.stderr, (name, result)
            result = run([interpreter, '--no-ai', artifact])
            assert result.returncode == 0 and result.stdout == expected and not result.stderr, (name, result)
            result = run([compiler, '--analyze', '--no-stdlib', path])
            assert result.returncode == 0 and not result.stderr, (name, result)
            assert json.loads(result.stdout)['success'], (name, result.stdout)
            checks += 3
    print(f'{checks} Ny comment/block diagnostic, artifact preservation, and valid-form checks passed')


if __name__ == '__main__':
    main()
