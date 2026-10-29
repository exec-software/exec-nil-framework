#!/usr/bin/env python3
"""Initialized dynamic values, Boolean aliases, and checked failure parity."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-anything-') as directory:
        work = Path(directory)

        def run(command, status=0):
            result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                    capture_output=True, text=True, timeout=120)
            if result.returncode != status:
                for line in result.stdout.splitlines():
                    if line.startswith('Report: '):
                        report = json.loads(Path(line[8:]).read_text())
                        for route in report['results']:
                            if route['status'] == 'FAIL':
                                print(json.dumps(route['commands'], indent=2), file=sys.stderr)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def routes(source, expected, stdlib=False, native=True):
            nonlocal checks
            modes = 'all' if args.llvm == 'ON' and native else 'text-interpreter,binary-interpreter,bundle-text,bundle-binary'
            command = [sys.executable, ROOT / 'tests/routes/ny_route_matrix.py', '--compiler', compiler,
                       '--interpreter', interpreter, '--source', source, '--expected-stdout', expected,
                       '--expected-exit', '0', '--routes', modes,
                       '--work-dir', work / (source.name + '-routes'), '--strict']
            command += ['--compiler-arg=--sdk-path', '--compiler-arg=' + str(ROOT / 'nilstd' / 'src')] if stdlib else ['--compiler-arg=--no-stdlib']
            run(command)
            checks += 6 if args.llvm == 'ON' and native else 4

        for language in ('ny', 'matrix'):
            routes(ROOT / ('tests/language/anything-booleans.' + language),
                   ROOT / 'tests/language/anything-booleans.stdout')
        routes(ROOT / 'tests/language/anything-aggregates.ny',
               ROOT / 'tests/language/anything-aggregates.stdout')
        routes(ROOT / 'tests/language/anything-binding-reference.ny',
               ROOT / 'tests/language/anything-binding-reference.stdout')
        routes(ROOT / 'tests/language/anything-functions.ny',
               ROOT / 'tests/language/anything-functions.stdout', native=False)
        if args.llvm == 'ON':
            protected = work / 'unsupported-function.native'
            protected.write_bytes(b'previous artifact')
            result = run([compiler, '--quiet', '--no-stdlib', '--native', '--output', protected,
                          ROOT / 'tests/language/anything-functions.ny'], status=1)
            assert 'function' in result.stderr.lower(), result.stderr
            assert protected.read_bytes() == b'previous artifact'
            checks += 1

        optional = work / 'optional.ny'
        optional.write_text('''import nil.std.core.*
fn main() -> i32
 let value: anything = "ready"
 let present = try_as<text>(value)
 let missing = try_as<i32>(value)
 println(present.unwrap())
 println(missing.is_none())
 let absent: Option<anything> = Option.None
 println(absent.is_none())
 return 0
end fn
''')
        expected = work / 'optional.stdout'
        expected.write_text('ready\n1\n1\n')
        routes(optional, expected, stdlib=True)

        ny = lambda body: 'fn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
        invalid = {
            'uninitialized': (ny('let value: anything'), 'initial value'),
            'arithmetic': (ny('let value: anything = 1\nprintln(value + 1)'), 'anything'),
            'member': (ny('let value: anything = "text"\nprintln(value.length())'), 'anything'),
            'call': (ny('let value: anything = 1\nvalue()'), 'anything'),
            'implicit-extraction': (ny('let value: anything = 1\nlet number: i32 = value'), 'checked cast or try_as'),
            'malformed-assert': (ny('assert(yes)'), "Expected '.' after 'assert'"),
            'missing-field': ('struct Box\nitem: anything\nend struct\n' + ny('let value = Box {}'), 'anything'),
            'missing-record': ('struct Box\nitem: anything\nend struct\n' + ny('let value: Box'), 'initial'),
            'owned-erasure': (ny('let owner = owned<i32>(1)\nlet value: anything = owner'), 'anything'),
            'borrow-erasure': (ny('let owner = owned<i32>(1)\nlet view = owned_borrow(owner)\nlet value: anything = view'), 'anything'),
            'bool-two': (ny('let value: flag = 2'), '0 or 1'),
            'bool-negative': (ny('let value: boolean = -1'), '0 or 1'),
            'bool-fraction': (ny('println(0.5 as bool)'), '0 or 1'),
        }
        for name, (source, diagnostic) in invalid.items():
            path = work / (name + '.ny')
            path.write_text(source)
            for flags in (['--no-bundle'], *([['--native']] if args.llvm == 'ON' else [])):
                destination = work / 'protected-artifact'
                destination.write_bytes(b'previous artifact')
                result = run([compiler, '--quiet', '--no-stdlib', *flags,
                              '--output', destination, path], status=1)
                assert diagnostic in result.stderr, (name, result.stderr)
                assert destination.read_bytes() == b'previous artifact', name
                checks += 1

        for name in ('uninitialized', 'missing-field', 'missing-record', 'owned-erasure', 'borrow-erasure'):
            for flags in (['--no-bundle'], *([['--native']] if args.llvm == 'ON' else [])):
                destination = work / 'unchecked-protected-artifact'
                destination.write_bytes(b'previous artifact')
                result = run([compiler, '--quiet', '--no-stdlib', '--no-typecheck', *flags,
                              '--output', destination, work / (name + '.ny')], status=1)
                assert 'anything' in result.stderr, (name, result.stderr)
                assert destination.read_bytes() == b'previous artifact', name
                checks += 1

        # Computed numeric conversions must take the runtime guard as well.
        cases = {
            'wrong-type': (ny('let value: anything = "text"\nprintln(value as i32)'), ('i32', 'text')),
            'bool-computed': (ny('let value = 1 + 1\nprintln(value as bool)'), ('0 or 1',)),
            'bool-float': (ny('let value = 0.25 + 0.25\nprintln(value as bool)'), ('0 or 1',)),
            'bool-assignment': (ny('let value = 1 + 1\nlet flag: bool = value\nprintln(flag)'), ('0 or 1',)),
            'bool-array': (ny('let value = 1 + 1\nlet flags: array<bool> = [no]\nflags[0] = value'), ('0 or 1',)),
            'bool-pointer': (ny('let value = 1 + 1\nlet flags = allocate<bool>(1)\nflags[0] = value'), ('0 or 1',)),
            'bool-method': ('class Switcher\n pub fn accept(self, input: bool) -> i32\nreturn 0\nend fn\nend class\n' +
                            ny('let instance = Switcher {}\nlet value = 1 + 1\nprintln(instance.accept(value))'), ('0 or 1',)),
            'bool-worker-integer': ('fn worker(v: i32) -> bool\nreturn v as bool\nend fn\n' +
                                    ny('let result = parallel_map(worker, [0, 2])'), ('worker at input 1', '0 or 1')),
            'bool-worker-floating': ('fn worker(v: f64) -> bool\nreturn v as bool\nend fn\n' +
                                     ny('let result = parallel_map(worker, [0.0, 0.5])'), ('worker at input 1', '0 or 1')),
            'bool-nan': ('@intrinsic("math.sqrt")\nfn root(v: f64) -> f64\nreturn 0.0\nend fn\n' +
                         ny('println(root(-1.0) as bool)'), ('0 or 1',)),
            'bool-infinity': ('@intrinsic("math.exp")\nfn exp(v: f64) -> f64\nreturn 0.0\nend fn\n' +
                              ny('println(exp(1000.0) as bool)'), ('0 or 1',)),
        }
        for name, (source, diagnostics) in cases.items():
            path = work / (name + '.ny')
            path.write_text(source)
            for binary in (False, True):
                nil = work / (name + '.nil')
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle',
                     *(['--emit-binary-nil'] if binary else []), '--output', nil, path])
                result = run([interpreter, '--no-ai', nil], status=1)
                assert all(s in result.stderr for s in diagnostics), (name, result.stderr)
                checks += 1
            if args.llvm == 'ON':
                for jit in (False, True):
                    output = work / (name + '.native')
                    command = [compiler, '--quiet', '--no-stdlib', '--native',
                               *(['--jit'] if jit else []), '--output', output, path]
                    result = run(command, status=1 if jit else 0)
                    if not jit:
                        result = run([output], status=1)
                    assert all(s in result.stderr for s in diagnostics), (name, result.stderr)
                    checks += 1
    print(f'anything and Boolean contracts passed: {checks} route checks')


if __name__ == '__main__':
    main()
