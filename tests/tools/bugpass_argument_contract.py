#!/usr/bin/env python3
"""Source-call argument contracts fail in analysis before either execution route."""
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
    parser.add_argument('--stdlib', type=Path, default=Path(__file__).resolve().parents[2] / 'nilstd/src')
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    parser.add_argument('--positive-only', action='store_true')
    args = parser.parse_args()
    compiler, interpreter = (p.resolve() for p in (args.compiler, args.interpreter))
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil argument contract ') as directory:
        root = Path(directory)

        def run(command):
            return subprocess.run(list(map(str, command)), cwd=root, env=environment,
                                  stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)

        def source(name, body, declarations='', language='ny'):
            path = root / (name + '.' + language)
            if language == 'ny':
                path.write_text(declarations + '\nfn main() -> i32\n' + body + '\nreturn 0\nend fn\n')
            else:
                path.write_text('book arguments\n' + declarations + '\nmain returns override integer begin paragraph\n' +
                                body + '\ngive 0\nend paragraph\n')
            return path

        def rejected(name, body, declarations='', language='ny', diagnostic='incompatible'):
            nonlocal checks
            path = source(name, body, declarations, language)
            result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--analyze', '--semantic-index', path])
            analysis = json.loads(result.stdout)
            assert result.returncode != 0 and not analysis['success'], (name, result)
            assert analysis['semantic'] is None, (name, analysis)
            assert any(diagnostic.lower() in d['message'].lower() for d in analysis['diagnostics']), (name, analysis)
            checks += 1
            for route, flags in [('text', []), ('binary', ['--emit-binary-nil'])] + (
                    [('native', ['--native'])] if args.llvm == 'ON' else []):
                output = root / (name + '-' + route)
                output.write_bytes(b'preserve existing artifact\n')
                result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--no-bundle', *flags, '-o', output, path])
                assert result.returncode != 0 and diagnostic.lower() in result.stderr.lower(), (name, result)
                assert output.read_bytes() == b'preserve existing artifact\n', (name, route)
                checks += 1

        declarations = '''fn identity<T>(item: T) -> T
 return item
end fn
fn forward<T>(item: T) -> T
 return identity(item)
end fn
fn text_count(items: array<text>) -> i32
 return items.len()
end fn
fn label(item: i32) -> text
 return "hello"
end fn
fn add_one(item: i32) -> i32
 return item + 1
end fn
fn invoke(callback: fn(i32) -> i32, item: i32) -> i32
 return callback(item)
end fn
fn invoke_generic<T>(callback: fn(T) -> T, item: T) -> T
 return callback(item)
end fn
fn first_integer(items: array<i32>) -> i32
 return items[0]
end fn
fn invoke_array<T>(callback: fn(array<T>) -> T, items: array<T>) -> T
 return callback(items)
end fn
fn count_tail(..items: text) -> i32
 return items.len()
end fn
struct Box<T>
 item: T
end struct
impl<T> Box<T>
 fn create(item: T) -> T
  return item
 end fn
 fn replace(self, item: T) -> T
  return item
 end fn
end impl
fn narrow(item: i32) -> i32
 return item
end fn
'''
        path = source('valid-ny', '''println(forward("hello"))
println(identity<i32>(7))
let box: Box<text> = Box<text> { item: "old" }
println(box.replace("new"))
println(Box<text>.create("static"))
println(text_count([]))
println(count_tail("a", "b", "c"))
println(narrow(4294967297 as i64))
let numbers = [4, 6]
println(numbers.len())
println(numbers.length())
println(numbers.get(1))
numbers.set(0, 9)
println(numbers[0])
println("hello".len())
println("hello".length())''', declarations)
        matrix_declarations = '''identity receives item as text returns text begin paragraph
 give item
end paragraph
increment receives item as number returns number begin paragraph
 give item plus 1
end paragraph
'''
        matrix = source('valid-matrix', '''say identity with item being "hello"
say increment with item being 8
value items is [4, 6]
say items.len with nothing
say items.get with 1
evaluate items.set with 0, 9
say items at 0''', matrix_declarations, 'matrix')
        for path, expected in [(path, 'hello\n7\nnew\nstatic\n0\n3\n1\n2\n2\n6\n9\n5\n5\n'),
                               (matrix, 'hello\n9\n2\n6\n9\n')]:
            analysis = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--analyze', path])
            assert analysis.returncode == 0 and json.loads(analysis.stdout)['success'], analysis
            checks += 1
            for route, flags in [('text', []), ('binary', ['--emit-binary-nil'])] + (
                    [('native', ['--native'])] if args.llvm == 'ON' else []):
                output = root / (path.stem + '-' + route + '.nil')
                result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--no-bundle', *flags, '-o', output, path])
                assert result.returncode == 0, result
                if route != 'jit':
                    result = run([output] if route == 'native' else [interpreter, output])
                assert result.returncode == 0 and result.stdout == expected, (path, route, result, expected)
                checks += 1

        # Indirect calls are supported by the interpreter. Native callback
        # lowering is independently unavailable and is not widened here.
        callback = source('valid-callback',
            'let callback = add_one\nprintln(invoke(callback, 8))\nprintln(callback(10))\n'
            'println(invoke_generic<i32>(callback, 41))\n'
            'let packed = count_tail\nprintln(packed(["a", "b"]))\n'
            'println(invoke_array<i32>(first_integer, [23, 47]))', declarations)
        analysis = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--analyze', callback])
        assert analysis.returncode == 0 and json.loads(analysis.stdout)['success'], analysis
        checks += 1
        for suffix, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
            output = root / ('callback-' + suffix + '.nil')
            result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--no-bundle', *flags, '-o', output, callback])
            assert result.returncode == 0, result
            result = run([interpreter, output])
            assert result.returncode == 0 and result.stdout == '9\n11\n42\n2\n23\n', result
            checks += 1

        for prefix, imports in [('direct', 'import nil.std.collections.*\n'),
                                ('transitive', 'import ny.lang.core.*\nimport ny.lang.io.*\n')]:
            vector = source('valid-vector-' + prefix,
                'let items: vec<text> = vec<text>.new()\nitems.push("a")\nitems.push("b")\nprintln(items.length())', imports)
            analysis = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--analyze', vector])
            assert analysis.returncode == 0 and json.loads(analysis.stdout)['success'], analysis
            checks += 1
            for suffix, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                output = root / ('vector-' + prefix + '-' + suffix + '.nil')
                result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--no-bundle', *flags, '-o', output, vector])
                assert result.returncode == 0, result
                result = run([interpreter, output])
                assert result.returncode == 0 and result.stdout == '2\n', result
                checks += 1
            if not args.positive_only:
                rejected('vector-wrong-element-' + prefix,
                    'let items: vec<text> = vec<text>.new()\nitems.push(42)', imports)

        mapped = source('valid-generic-method',
            'let item: Option<i32> = Option.Some(7)\nprintln(item.map(add_one).unwrap())\n'
            'println(item.map<text>(label).unwrap())',
            'import nil.std.core.*\n' + declarations)
        analysis = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--analyze', mapped])
        assert analysis.returncode == 0 and json.loads(analysis.stdout)['success'], analysis
        checks += 1
        for suffix, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
            output = root / ('mapped-' + suffix + '.nil')
            result = run([compiler, '--no-stdlib', '--sdk-path', args.stdlib.resolve(), '--no-bundle', *flags, '-o', output, mapped])
            assert result.returncode == 0, result
            result = run([interpreter, output])
            assert result.returncode == 0 and result.stdout == '8\nhello\n', result
            checks += 1

        if not args.positive_only:
            rejected('explicit-method-wrong-result',
                'let item: Option<i32> = Option.Some(7)\nprintln(item.map<text>(add_one).unwrap())',
                'import nil.std.core.*\n' + declarations)
            rejected('explicit-method-extra-types',
                'let item: Option<i32> = Option.Some(7)\nprintln(item.map<i32, text>(add_one).unwrap())',
                'import nil.std.core.*\n' + declarations, diagnostic='type argument')
            rejected('nongeneric-method-types',
                'let box: Box<text> = Box<text> { item: "old" }\nprintln(box.replace<i32>(42))',
                declarations, diagnostic='not generic')
            text = 'fn consume(item: text) -> text\nreturn item\nend fn\n'
            integer = 'fn consume(item: i64) -> i64\nreturn item\nend fn\n'
            array = 'fn consume(items: array<text>) -> i32\nreturn items.len()\nend fn\n'
            generic = 'fn consume<T>(item: T) -> T\nreturn item\nend fn\n'
            for name, body, declaration in [
                ('text-argument', 'println(consume(42))', text),
                ('integer-argument', 'println(consume("42"))', integer),
                ('fraction-argument', 'println(consume(1.5))', integer),
                ('array-argument', 'println(consume([1, 2]))', array),
                ('array-width-argument', 'println(consume([1, 2]))', array.replace('array<text>', 'array<i64>')),
                ('explicit-generic', 'println(consume<text>(42))', generic),
                ('method-argument', 'let box: Box<text> = Box<text> { item: "old" }\nprintln(box.replace(42))', declarations),
                ('static-generic-argument', 'println(Box<text>.create(42))', declarations),
                ('variadic-first', 'println(count_tail(1, "okay"))', declarations),
                ('variadic-later', 'println(count_tail("okay", 1))', declarations),
                ('callback-argument', 'let callback = consume\nprintln(callback(42))', text),
                ('callback-parameter', 'println(invoke(add_one, "wrong"))', declarations),
            ]:
                rejected(name, body, declaration)
            for name, body, declaration, diagnostic in [
                ('callback-missing', 'let callback = consume\nprintln(callback())', text, 'expects 1 arguments'),
                ('callback-extra', 'let callback = consume\nprintln(callback("a", "b"))', text, 'expects 1 arguments'),
                ('array-len-extra', 'let values = [1]\nprintln(values.len(99))', '', 'expects 0 arguments'),
                ('array-length-extra', 'let values = [1]\nprintln(values.length(99))', '', 'expects 0 arguments'),
                ('text-len-extra', 'println("text".len(99))', '', 'expects 0 arguments'),
                ('array-get-missing', 'let values = [1]\nprintln(values.get())', '', 'expects 1 arguments'),
                ('array-get-extra', 'let values = [1]\nprintln(values.get(0, 1))', '', 'expects 1 arguments'),
                ('array-get-text-index', 'let values = [1]\nprintln(values.get("zero"))', '', 'index must be an integer'),
                ('array-set-missing', 'let values = [1]\nvalues.set(0)', '', 'expects 2 arguments'),
                ('array-set-text-value', 'let values = [1]\nvalues.set(0, "wrong")', '', 'element type mismatch'),
                ('array-push-extra', 'let values = [1]\nvalues.push(2, 3)', '', 'expects 1 argument'),
            ]:
                rejected(name, body, declaration, diagnostic=diagnostic)
            rejected('matrix-named-argument', 'say identity with item being 42', matrix_declarations, 'matrix')
            rejected('matrix-callback-argument', 'value callback is the paragraph identity\nsay callback with item being 42',
                     matrix_declarations, 'matrix')
            rejected('matrix-array-len', 'value items is [1]\nsay items.len with 2', language='matrix', diagnostic='expects 0 arguments')
        print(json.dumps({'checks': checks, 'positive_only': args.positive_only}))


if __name__ == '__main__':
    main()
