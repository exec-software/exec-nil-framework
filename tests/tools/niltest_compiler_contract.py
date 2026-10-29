#!/usr/bin/env python3
"""Checked test discovery, production exclusion, and isolated compiler harnesses."""
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
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='niltest-compiler-') as directory:
        work = Path(directory)

        def run(command, status=0):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, timeout=120)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        source = work / 'suite "quoted".ny'
        source.write_text('''fn initialize() -> i32
 println("initialized")
 return 41
end fn
let state: i32 = initialize()
@interop("std_sys")
fn exit(code: i32) -> void
end fn
@if_test_mode
fn helper() -> void
 println("TEST_ONLY_SECRET")
end fn
fn main() -> i32
 println("application main")
 return 0
end fn
test "calls application"
 let result = main()
 println(state)
end test
test "plain"
 helper()
 println("test body")
end test
test "early exit"
 exit(0)
end test
test "failure"
 panic("intentional failure")
end test
''')
        base = [compiler, '--no-stdlib']
        manifest = json.loads(run([*base, '--test-list', source]).stdout)
        assert manifest['schema'] == 'nil.tests.v1', manifest
        cases = manifest['tests']
        assert len(cases) == 4 and len({case['id'] for case in cases}) == 4, manifest
        by_name = {case['name']: case for case in cases}
        assert set(by_name) == {'calls application', 'plain', 'early exit', 'failure'}, manifest
        for case in cases:
            assert set(case) == {'id', 'name', 'suite', 'kind', 'function', 'file', 'line'}, case
            assert Path(case['file']) == source and case['line'] > 0, case
            assert source.read_text().splitlines()[case['line'] - 1].startswith('test '), case
        assert json.loads(run([*base, '--test-list', source]).stdout) == manifest
        assert not list(work.glob('*.nil')), 'discovery created an artifact'

        production = work / 'production.nil'
        run([*base, '--quiet', '--no-bundle', '-o', production, source])
        assert 'TEST_ONLY_SECRET' not in production.read_text()
        assert 'intentional failure' not in production.read_text()
        result = run([interpreter, '--no-ai', production])
        assert result.stdout == 'initialized\napplication main\n', result

        routes = ['text', 'binary', 'bundle'] + (['native'] if args.llvm == 'ON' else [])
        for route in routes:
            artifact = work / ('tests-' + route + ('.nil' if route in ('text', 'binary') else ''))
            flags = ['--native'] if route == 'native' else [] if route == 'bundle' else ['--no-bundle']
            if route == 'binary':
                flags += ['--emit-binary-nil']
            run([*base, '--quiet', '--test', *flags, '-o', artifact, source])
            command = [interpreter, '--no-ai', artifact, '--'] if route in ('text', 'binary') else [artifact]
            for selection in ([], ['unknown-test-id'], [by_name['plain']['id'], 'extra']):
                result = run([*command, *selection], 2)
                assert result.stdout == '' and 'exactly one discovered test ID' in result.stderr, result
            result = run([*command, by_name['calls application']['id']], 77)
            assert result.stdout == 'initialized\napplication main\n41\n', result
            result = run([*command, by_name['plain']['id']], 77)
            assert result.stdout == 'initialized\nTEST_ONLY_SECRET\ntest body\n', result
            result = run([*command, by_name['early exit']['id']], 0)
            assert result.stdout == 'initialized\n', result
            result = run([*command, by_name['failure']['id']], 1)
            assert 'intentional failure' in result.stderr, result

        if args.llvm == 'ON':
            result = run([*base, '--quiet', '--test', '--native', '--jit', '-o', work / 'jit-unused',
                          source, '--', by_name['calls application']['id']], 77)
            assert result.stdout == 'initialized\napplication main\n41\n', result
            assert not (work / 'jit-unused').exists()

        matrix = work / 'suite.matrix'
        matrix.write_text('''book harness-matrix
sample begin test
 say "matrix body"
end test
''')
        matrix_manifest = json.loads(run([*base, '--test-list', matrix]).stdout)
        assert len(matrix_manifest['tests']) == 1, matrix_manifest
        matrix_case = matrix_manifest['tests'][0]
        for route in ['text'] + (['native'] if args.llvm == 'ON' else []):
            artifact = work / ('matrix-' + route)
            run([*base, '--quiet', '--test', '--native' if route == 'native' else '--no-bundle',
                 '-o', artifact, matrix])
            command = [artifact] if route == 'native' else [interpreter, '--no-ai', artifact, '--']
            assert run([*command, matrix_case['id']], 77).stdout == 'matrix body\n'

        # A compilation error in a stripped test cannot affect production, but
        # discovery checks it and failed test compilation preserves old output.
        invalid = work / 'invalid.ny'
        invalid.write_text('''fn main() -> i32
 return 0
end fn
test "bad test"
 missing_function()
end test
''')
        run([*base, '--quiet', '--no-bundle', '-o', production, invalid])
        result = run([*base, '--test-list', invalid], 1)
        assert result.stdout == '' and 'missing_function' in result.stderr, result
        production.write_bytes(b'previous artifact')
        run([*base, '--quiet', '--test', '--no-bundle', '-o', production, invalid], 1)
        assert production.read_bytes() == b'previous artifact'

        for flags in (['--test-list', '--test'], ['--test-list', '--test-list'],
                      ['--test', '--test'], ['--test-list', '--no-typecheck'],
                      ['--test', '--no-typecheck'], ['--test', '--emit-library'],
                      ['--test-list', '--native'], ['--test-list', '--no-bundle'],
                      ['--test-list', '-o', production]):
            result = run([*base, *flags, source], 1)
            assert result.stdout == '', result
            assert production.read_bytes() == b'previous artifact'

        empty = work / 'empty.ny'
        empty.write_text('fn helper() -> void\nend fn\n')
        assert json.loads(run([*base, '--test-list', empty]).stdout)['tests'] == []
    print(f'niltest compiler discovery/harness: {checks} checks passed')


if __name__ == '__main__':
    main()
