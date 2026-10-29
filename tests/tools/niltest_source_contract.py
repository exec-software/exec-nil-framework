#!/usr/bin/env python3
"""Exercise co-located Ny/Matrix tests and assertion semantics on both routes."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    repository = Path(__file__).resolve().parents[2]
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0

    with tempfile.TemporaryDirectory(prefix='niltest-source-') as directory:
        work = Path(directory)
        base = [compiler, '--quiet', '--no-stdlib']
        routes = ['vm'] + (['native'] if args.llvm == 'ON' else [])

        def run(command, status=0):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work,
                                    env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=120)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        def write(name, content):
            path = work / name
            path.write_text(content, encoding='utf-8')
            return path

        def discover(source, options=None):
            result = run([*(base if options is None else options), '--test-list', source])
            manifest = json.loads(result.stdout)
            assert manifest['schema'] == 'nil.tests.v1', manifest
            return manifest['tests']

        def compile_route(source, route, options=None, tests=True):
            artifact = work / (source.name + '-' + route + ('-tests' if tests else '-production'))
            run([*(base if options is None else options), *(['--test'] if tests else []),
                 '--native' if route == 'native' else '--no-bundle', '-o', artifact, source])
            return [artifact] if route == 'native' else [interpreter, '--no-ai', artifact, '--']

        ny = write('lexical.ny', '''fn echo(input: i32) -> i32
 let observed = input
 test "lexical checks"
  assert.equal(observed, input)
  let observed = 99
  assert.equal(observed, 99)
  for cycle in range(1)
   break
  end for
  println("INLINE_ONLY")
 end test
 return observed
end fn
@suite("arithmetic")
@test
fn positive() -> void
 assert.equal(echo(7), 7, "lexical scope")
 assert.true(yes)
 assert.false(no)
 assert.not_equal("A", "B")
 assert.greater_than(2, 1)
 assert.greater_or_equal(2, 2)
 assert.less_than(1.5, 2)
 assert.less_or_equal(2, 2.0)
end fn
@suite("attached")
fn anchor() -> void
end fn
test "attached case"
 let mut storage = owned<i32>(1)
 storage[0] = 42
 let view = owned_borrow(storage)
 assert.equal(view[0], 42)
 assert.equal(echo(9), 9)
end test
@suite("ownership")
@integration
test "owned buffer"
 let mut storage = owned<i32>(1)
 storage[0] = 42
 let view = owned_borrow(storage)
 assert.equal(view[0], 42)
end test
fn main() -> i32
 println(echo(3))
 return 0
end fn
''')
        matrix = write('lexical.matrix', '''book testing.lexical
echo receives input as number returns number begin paragraph
 value observed is input
 lexical-checks begin test
  expect observed equals input
  value observed is 99
  expect observed equals 99
  repeat forever
   stop repeating
  end repeat
  say "INLINE_ONLY"
 end test
 give observed
end paragraph
note suite "arithmetic"
note test
positive begin paragraph
 expect (echo with 7) equals 7
 expect yes
 expect not no
 expect "A" does not equal "B"
 expect 2 is greater than 1
 expect 2 is at least 2
 expect 1.5 is less than 2
 expect 2 is at most 2.0
end paragraph
note suite "standalone"
standalone-case begin test
 expect (echo with 9) equals 9
end test
note suite "ownership"
note integration
owned-buffer begin test
 own variable storage as array of number sized 1
 set storage at 0 to 42
 borrow view-value from storage
 expect (view-value at 0) equals 42
end test
main returns number begin paragraph
 say echo with 3
 give 0
end paragraph
''')
        for source in (ny, matrix):
            cases = discover(source)
            assert len(cases) == 3, cases  # Inline checks are lexical, not independent cases.
            assert {case['suite'] for case in cases} == (
                {'arithmetic', 'attached', 'ownership'} if source == ny else
                {'arithmetic', 'standalone', 'ownership'}), cases
            assert any(case['name'] == 'positive' for case in cases), cases
            assert [case['kind'] for case in cases if case['suite'] == 'ownership'] == ['integration'], cases
            for route in routes:
                production = compile_route(source, route, tests=False)
                assert run(production).stdout == '3\n'
                artifact = Path(production[0] if route == 'native' else production[2])
                assert b'INLINE_ONLY' not in artifact.read_bytes()
                command = compile_route(source, route)
                for case in cases:
                    expected = '' if case['suite'] == 'ownership' else 'INLINE_ONLY\n'
                    assert run([*command, case['id']], 77).stdout == expected

        # Assertion operands and the optional message are each evaluated once.
        # Failure text names the assertion line, and includes the actual values.
        failure_ny = write('failures.ny', '''fn actual() -> i32
 println("actual evaluated")
 return 3
end fn
fn expected() -> i32
 println("expected evaluated")
 return 4
end fn
fn message() -> text
 println("message evaluated")
 return "comparison detail"
end fn
test "equal-values"
 assert.equal(actual(), expected(), message())
end test
test "boolean-value"
 assert.true(no)
end test
test "ordered-values"
 assert.greater_than(1, 2)
end test
test "text-values"
 assert.equal("alpha", "beta")
end test
test "floating-values"
 assert.equal(1.25, 2.5)
end test
''')
        failure_matrix = write('failures.matrix', '''book testing.failures
actual returns number begin paragraph
 say "actual evaluated"
 give 3
end paragraph
expected returns number begin paragraph
 say "expected evaluated"
 give 4
end paragraph
equal-values begin test
 expect (actual with nothing) equals (expected with nothing)
end test
boolean-value begin test
 expect no
end test
ordered-values begin test
 expect 1 is greater than 2
end test
text-values begin test
 expect "alpha" equals "beta"
end test
floating-values begin test
 expect 1.25 equals 2.5
end test
''')
        details = {
            'equal-values': ('3', '4'), 'boolean-value': ('false', 'true'),
            'ordered-values': ('1', '2'), 'text-values': ('alpha', 'beta'),
            'floating-values': ('1.25', '2.5'),
        }
        for source in (failure_ny, failure_matrix):
            cases = discover(source)
            assert {case['name'] for case in cases} == set(details), cases
            source_lines = source.read_text().splitlines()
            for route in routes:
                command = compile_route(source, route)
                for case in cases:
                    result = run([*command, case['id']], 1)
                    actual, expected = details[case['name']]
                    assert f'actual={actual}; expected={expected}' in result.stderr, result
                    assertion_line = next(index + 1 for index in range(case['line'], len(source_lines))
                                          if source_lines[index].lstrip().startswith(('assert.', 'expect ')))
                    assert f'{source}:{assertion_line}:' in result.stderr, result
                    if case['name'] == 'equal-values':
                        output = 'actual evaluated\nexpected evaluated\n'
                        if source == failure_ny:
                            output += 'message evaluated\n'
                            assert 'comparison detail' in result.stderr, result
                        assert result.stdout == output, result
                    else:
                        assert result.stdout == '', result

        def ny_test(body):
            return 'test "invalid"\n' + body + '\nend test\n'

        def matrix_test(body):
            return 'book testing.invalid\ninvalid-case begin test\n' + body + '\nend test\n'

        invalid = [
            ('arity.ny', ny_test('assert.equal(1)'), 'requires 2 argument'),
            ('extra-arity.ny', ny_test('assert.true(yes, "detail", "extra")'), 'at most one'),
            ('numeric-condition.ny', ny_test('assert.true(2)'), 'requires a boolean condition'),
            ('message-type.ny', ny_test('assert.equal(1, 1, 7)'), 'message must be text'),
            ('operand-type.ny', ny_test('assert.equal(1, "1")'), 'requires comparable'),
            ('ordered-type.ny', ny_test('assert.less_than("A", "B")'), 'requires comparable'),
            ('unknown-assertion.ny', ny_test('assert.contains("A", "B")'), 'Unsupported assertion'),
            ('function-arguments.ny', '@test\nfn invalid(value: i32) -> void\nend fn\n', 'zero-argument'),
            ('function-result.ny', '@test\nfn invalid() -> i32\nreturn 1\nend fn\n', 'returning void'),
            ('suite-type.ny', '@suite(7)\n' + ny_test('assert.true(yes)'), 'nonempty text'),
            ('property.ny', 'property "unsupported"\nassert.true(yes)\nend property\n', 'Property generators'),
            ('generator.ny', '@generate("values")\n' + ny_test('assert.true(yes)'), 'Property generators'),
            ('numeric-condition.matrix', matrix_test('expect 2'), 'requires a boolean condition'),
            ('operand-type.matrix', matrix_test('expect 1 equals "1"'), 'requires comparable'),
            ('ordered-type.matrix', matrix_test('expect "A" is less than "B"'), 'requires comparable'),
            ('suite-type.matrix', 'book testing.invalid\nnote suite 7\ninvalid-case begin test\nexpect yes\nend test\n', 'nonempty text'),
            ('generator.matrix', 'book testing.invalid\nnote generate "values"\ninvalid-case begin test\nexpect yes\nend test\n', 'Property generators'),
            ('inline-return.ny', 'fn probe() -> i32\ntest "inline"\nreturn 2\nend test\nreturn 1\nend fn\n', 'return cannot leave an inline'),
            ('inline-break.ny', 'fn probe() -> void\nfor index in range(2)\ntest "inline"\nbreak\nend test\nend for\nend fn\n', 'outside of'),
            ('inline-continue.ny', 'fn probe() -> void\nfor index in range(2)\ntest "inline"\ncontinue\nend test\nend for\nend fn\n', 'outside of'),
            ('inline-return.matrix', 'book testing.invalid\nprobe returns number begin paragraph\ninline-case begin test\ngive 2\nend test\ngive 1\nend paragraph\n', 'return cannot leave an inline'),
            ('inline-break.matrix', 'book testing.invalid\nprobe begin paragraph\nrepeat forever\ninline-case begin test\nstop repeating\nend test\nend repeat\nend paragraph\n', 'outside of'),
            ('borrowed-move.ny', ny_test('let mut storage = owned<i32>(1)\nlet view = owned_borrow(storage)\nlet moved = owned_move(storage)'), 'borrow'),
            ('inline-borrowed-move.ny', 'fn probe() -> void\nlet mut storage = owned<i32>(1)\nlet view = owned_borrow(storage)\ntest "inline"\nlet moved = owned_move(storage)\nend test\nend fn\n', 'borrow'),
            ('borrowed-move.matrix', matrix_test('own variable storage as array of number sized 1\nborrow view-value from storage\nmove storage into transferred'), 'borrow'),
        ]
        sentinel = b'previous successful test artifact\n'
        output = work / 'unchanged.nil'
        for name, content, diagnostic in invalid:
            source = write(name, content)
            for flags in (['--test-list'], ['--test', '--no-bundle', '-o', output]):
                output.write_bytes(sentinel)
                result = run([*base, *flags, source], 1)
                assert result.stdout == '', result
                assert diagnostic.lower() in result.stderr.lower(), (name, result)
                assert 'Parse error' not in result.stderr and 'Syntax error' not in result.stderr, (name, result)
                assert output.read_bytes() == sentinel, (name, result)

        # Test-only helpers must disappear before ordinary semantic checking.
        for extension, content in (
            ('ny', '@if_test_mode\nfn helper() -> void\nend fn\nfn main() -> i32\nhelper()\nreturn 0\nend fn\n'),
            ('matrix', 'book testing.visibility\nnote if_test_mode\nhelper begin paragraph\nend paragraph\nmain returns number begin paragraph\nevaluate helper with nothing\ngive 0\nend paragraph\n'),
        ):
            source = write('test-only.' + extension, content)
            result = run([*base, '--no-bundle', '-o', output, source], 1)
            assert 'helper' in result.stderr, result
            assert discover(source) == []

        # An invalid test marker must never silently erase an ordinary type.
        for extension, content in (
            ('ny', '@test\nstruct TestingRecord\n field_value: i32\nend struct\n'),
            ('matrix', 'book testing.invalid\nnote test\nTestingRecord begin record\nfield field_value as number\nend record\n'),
        ):
            source = write('test-on-record.' + extension, content)
            for flags in (['--test-list'], ['--test', '--no-bundle', '-o', output],
                          ['--no-bundle', '-o', output]):
                output.write_bytes(sentinel)
                result = run([*base, *flags, source], 1)
                assert 'only annotate functions or test blocks' in result.stderr, result
                assert output.read_bytes() == sentinel, result

        # An attached case is independent of its generic function's type
        # parameters and is discovered once, without first calling that function.
        generic = write('generic-attachment.ny', '''fn identity<T>(input: T) -> T
 return input
end fn
test "generic attachment"
 assert.equal(identity<i32>(42), 42)
end test
''')
        generic_cases = discover(generic)
        assert len(generic_cases) == 1 and generic_cases[0]['name'] == 'generic attachment', generic_cases
        for route in routes:
            command = compile_route(generic, route)
            assert run([*command, generic_cases[0]['id']], 77).stdout == ''

        # Method attachment has no implicit receiver construction contract.
        # Diagnose its placement rather than silently dropping a checked case.
        method = write('method-attachment.ny', '''class Probe
 fn inspect(self) -> i32
  return 1
 end fn
 test "method attachment"
  assert.true(yes)
 end test
end class
''')
        result = run([*base, '--test-list', method], 1)
        assert 'Attached tests on methods are not supported' in result.stderr, result
        assert 'construct the receiver explicitly' in result.stderr, result

        # The shipped testing package uses its own public API in co-located tests.
        library = repository / 'nilstd/src/testing/package.matrix'
        library_options = [compiler, '--quiet', '--sdk', repository]
        cases = discover(library, library_options)
        package_cases = [case for case in cases if Path(case['file']) == library]
        assert len(package_cases) >= 2, cases
        assert all(case['suite'] == 'nil.std.testing' for case in package_cases), package_cases
        for route in routes:
            command = compile_route(library, route, library_options)
            for case in package_cases:
                assert run([*command, case['id']], 77).stdout == ''

        # Recovering an absent/error payload must stop the selected case with
        # the caller's explanation, rather than returning an invented value.
        # Source imports also discover the package's own tests; select only the
        # two cases owned by each fixture.
        recovery_ny = write('recovery-failures.ny', '''import nil.std.testing
import nil.std.core.option
import nil.std.core.result

test "option_failure"
 let input: Option<i32> = Option.None
 println("before recovery")
 let actual = require_some<i32>(input, "required option payload is absent")
 println("UNREACHABLE_AFTER_REQUIRE_SOME")
 println(actual)
end test

test "result_failure"
 let input: Result<i32, text> = Result.Error("underlying failure")
 println("before recovery")
 let actual = require_ok<i32, text>(input, "required result did not succeed")
 println("UNREACHABLE_AFTER_REQUIRE_OK")
 println(actual)
end test
''')
        recovery_matrix = write('recovery-failures.matrix', '''book testing.recovery
read nil.std.testing
read nil.std.core.option
read nil.std.core.result

option_failure begin test
 value input as Option of number is Option.None
 say "before recovery"
 value actual is require-some of number with input being input, message being "required option payload is absent"
 say "UNREACHABLE_AFTER_REQUIRE_SOME"
 say actual
end test

result_failure begin test
 value input as Result of number, text is Result.Error holding "underlying failure"
 say "before recovery"
 value actual is require-ok of number, text with input being input, message being "required result did not succeed"
 say "UNREACHABLE_AFTER_REQUIRE_OK"
 say actual
end test
''')
        recovery_messages = {
            'option_failure': 'required option payload is absent',
            'result_failure': 'required result did not succeed',
        }
        for source in (recovery_ny, recovery_matrix):
            cases = [case for case in discover(source, library_options)
                     if Path(case['file']) == source and case['name'] in recovery_messages]
            assert {case['name'] for case in cases} == set(recovery_messages), cases
            for route in routes:
                command = compile_route(source, route, library_options)
                for case in cases:
                    result = run([*command, case['id']], 1)
                    assert result.stdout == 'before recovery\n', result
                    assert recovery_messages[case['name']] in result.stderr, result
                    assert 'UNREACHABLE_AFTER_REQUIRE_' not in result.stdout + result.stderr, result

        # Keep the published Matrix recovery example executable, including the
        # generic Result spelling and the returned payload's ordinary binding.
        guide = (repository / 'docs/tools/testing.md').read_text(encoding='utf-8')
        recovery_examples = [example for example in re.findall(r'```matrix\n(.*?)\n```', guide, re.S)
                             if 'recover_values begin test' in example]
        assert len(recovery_examples) == 1, recovery_examples
        documented = write('documented-recovery.matrix', recovery_examples[0] + '\n')
        documented_cases = [case for case in discover(documented, library_options)
                            if Path(case['file']) == documented and case['name'] == 'recover_values']
        assert len(documented_cases) == 1, documented_cases
        for route in routes:
            command = compile_route(documented, route, library_options)
            result = run([*command, documented_cases[0]['id']], 77)
            assert result.stdout == '' and result.stderr == '', result

    print(f'niltest Ny/Matrix source semantics: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
