#!/usr/bin/env python3
"""Mutable copies and caller-binding references agree across both frontends/routes."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPOSITORY = Path(__file__).resolve().parents[2]

# Matching fixtures make aliasing observable before a function returns. Merely
# copying parameter values back afterward cannot satisfy this contract.
MATRIX = '''book parameter_contract
Changed begin record
 field code as number
end record
write-then-raise receives reference target as number begin paragraph
 set target to 90
 raise Changed with code being 1
end paragraph
local-copy receives mutable amount as number and variable adjustment as number returns number begin paragraph
 set amount to amount plus adjustment
 set adjustment to 100
 give amount
end paragraph
replace-rest receives mutable first as number and more mutable rest as number returns number begin paragraph
 set first to first plus 1
 set rest to [8 becomes number, 9 becomes number]
 give first plus (rest at 0) plus (rest at 1)
end paragraph
legacy-rest receives more variable rest as number returns number begin paragraph
 set rest to [7 becomes number]
 give rest at 0
end paragraph
bump receives reference target as number returns nothing begin paragraph
 set target to target plus 1
end paragraph
aliases receives reference first as number and reference second as number returns number begin paragraph
 set first to first plus 1
 set second to second plus 10
 set first to first plus second
 give second
end paragraph
forward receives reference target as number returns nothing begin paragraph
 evaluate bump with reference to target
end paragraph
recurse receives count as number and reference target as number returns nothing begin paragraph
 when count is greater than 0 then
  set target to target plus 1
  evaluate recurse with target being reference to target, count being count minus 1
  set target to target plus 10
 end when
end paragraph
mutable-forward receives mutable target as number returns number begin paragraph
 evaluate forward with reference to target
 give target
end paragraph
shadow receives reference target as number returns nothing begin paragraph
 evaluate bump with reference to target
 when true then
  variable target as number is 100
  evaluate bump with reference to target
  say target
 end when
 set target to target plus 1
end paragraph
scalars receives reference integer-value as override integer and reference byte-value as override byte and reference truth as boolean and reference fraction as floating and reference label as text returns nothing begin paragraph
 set integer-value to integer-value plus 2
 set byte-value to 255 becomes override byte
 set truth to not truth
 set fraction to fraction plus 0.25
 set label to label plus "!"
end paragraph
main returns override integer begin paragraph
 variable total as number is 5
 say local-copy with adjustment being 2, amount being total
 say total
 say replace-rest with 2, 10, 20
 say legacy-rest with more 1, 2
 say aliases with second being reference to total, first being reference to total
 say total
 evaluate forward with target being reference to total
 evaluate recurse with count being 3, target being reference to total
 say total
 say mutable-forward with total
 say total
 when true then
  variable total as number is 40
  evaluate bump with reference to total
  say total
 end when
 say total
 evaluate shadow with reference to total
 say total
 set total to total plus 1
 say local-copy with total, 3
 say total
 variable small as override integer is 4
 variable octet as override byte is 0 becomes override byte
 variable truth as boolean is false
 variable fraction as floating is 1.25
 variable label as text is "updated"
 evaluate scalars with integer-value being reference to small, byte-value being reference to octet, truth being reference to truth, fraction being reference to fraction, label being reference to label
 say small
 say octet
 say truth
 say fraction
 say label
 attempt
  evaluate write-then-raise with reference to total
 caught Changed as problem
  say total
 end attempt
 say total
 give 0
end paragraph
'''

NY = '''module parameter_contract
struct Changed
 code: i64
end struct
fn write_then_raise(ref target: i64)
 target = 90
 raise Changed { code: 1 }
end fn
fn local_copy(mut amount: i64, mut adjustment: i64) -> i64
 amount = amount + adjustment
 adjustment = 100
 return amount
end fn
fn replace_rest(mut first: i64, mut ..rest: i64) -> i64
 first = first + 1
 rest = [8 as i64, 9 as i64]
 return first + rest[0] + rest[1]
end fn
fn legacy_rest(mut ..rest: i64) -> i64
 rest = [7 as i64]
 return rest[0]
end fn
fn bump(ref target: i64)
 target = target + 1
end fn
fn aliases(ref first: i64, ref second: i64) -> i64
 first = first + 1
 second = second + 10
 first = first + second
 return second
end fn
fn forward(ref target: i64)
 bump(ref target)
end fn
fn recurse(count: i64, ref target: i64)
 if count > 0 then
  target = target + 1
  recurse(count - 1, ref target)
  target = target + 10
 end if
end fn
fn mutable_forward(mut target: i64) -> i64
 forward(ref target)
 return target
end fn
fn shadow(ref target: i64)
 bump(ref target)
 if true then
  let mut target: i64 = 100
  bump(ref target)
  println(target)
 end if
 target = target + 1
end fn
fn scalars(ref integer_value: i32, ref byte_value: u8, ref truth: bool, ref fraction: f64, ref label: text)
 integer_value = integer_value + 2
 byte_value = 255 as u8
 truth = not truth
 fraction = fraction + 0.25
 label = label + "!"
end fn
fn main() -> i32
 let mut total: i64 = 5
 println(local_copy(total, 2))
 println(total)
 println(replace_rest(2, 10, 20))
 println(legacy_rest(1, 2))
 println(aliases(ref total, ref total))
 println(total)
 forward(ref total)
 recurse(3, ref total)
 println(total)
 println(mutable_forward(total))
 println(total)
 if true then
  let mut total: i64 = 40
  bump(ref total)
  println(total)
 end if
 println(total)
 shadow(ref total)
 println(total)
 total = total + 1
 println(local_copy(total, 3))
 println(total)
 let mut small: i32 = 4
 let mut octet: u8 = 0 as u8
 let mut truth: bool = false
 let mut fraction: f64 = 1.25
 let mut label: text = "updated"
 scalars(ref small, ref octet, ref truth, ref fraction, ref label)
 println(small)
 println(octet)
 println(truth)
 println(fraction)
 println(label)
 attempt
  write_then_raise(ref total)
 fault Changed as problem
  println(total)
 end attempt
 println(total)
 return 0
end fn
'''
EXPECTED = '7\n5\n20\n7\n32\n32\n66\n67\n66\n41\n66\n101\n68\n72\n69\n6\n255\n1\n1.5\nupdated!\n90\n90\n'

CALLBACK_NY = '''module reference_callbacks
fn bump(ref target: i64)
 target = target + 1
end fn
fn ordinary(amount: i64) -> i64
 return amount + 10
end fn
fn invoke(worker: fn(i64) -> i64, ref target: i64)
 target = worker(target)
end fn
fn choose_callback(worker: fn(i64) -> i64) -> fn(i64) -> i64
 return worker
end fn
fn main() -> i32
 let mut total: i64 = 5
 bump(ref total)
 let callback = ordinary
 println(callback(total))
 invoke(ordinary, ref total)
 println(total)
 let returned = choose_callback(ordinary)
 let copied = returned
 println(copied(2))
 return 0
end fn
'''

CALLBACK_MATRIX = '''book reference-callbacks
read Callback from parameter_callbacks
bump receives reference target as number begin paragraph
 set target to target plus 1
end paragraph
ordinary receives amount as number returns number begin paragraph
 give amount plus 10
end paragraph
invoke receives worker as Callback and reference target as number begin paragraph
 set target to worker with target
end paragraph
choose-callback receives worker as Callback returns Callback begin paragraph
 give worker
end paragraph
main returns override integer begin paragraph
 variable total as number is 5
 evaluate bump with reference to total
 value operation is the paragraph ordinary
 say operation with total
 evaluate invoke with the paragraph ordinary, reference to total
 say total
 value returned is choose-callback with the paragraph ordinary
 value copied is the paragraph returned
 say copied with 2
 give 0
end paragraph
'''

TEXT_NY = '''module reference_text_operations
fn decorated(label: text) -> text
 return label + "!"
end fn
fn shorten(ref label: text)
 label = label.substr(1, 2)
end fn
fn from_array(ref label: text)
 let values: array<text> = ["new"]
 label = values[0]
end fn
fn from_function(ref label: text)
 label = decorated(label)
end fn
fn main() -> i32
 let mut label: text = "abcd"
 shorten(ref label)
 println(label)
 from_array(ref label)
 println(label)
 from_function(ref label)
 println(label)
 return 0
end fn
'''

TEXT_MATRIX = '''book reference-text-operations
decorated receives label as text returns text begin paragraph
 give label plus "!"
end paragraph
shorten receives reference label as text begin paragraph
 set label to label.substr with 1, 2
end paragraph
from-array receives reference label as text begin paragraph
 value values as array of text is ["new"]
 set label to values at 0
end paragraph
from-function receives reference label as text begin paragraph
 set label to decorated with label
end paragraph
main returns override integer begin paragraph
 variable label as text is "abcd"
 evaluate shorten with reference to label
 say label
 evaluate from-array with reference to label
 say label
 evaluate from-function with reference to label
 say label
 give 0
end paragraph
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_LIBRARY_PATH'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-parameter-contract-') as directory:
        root = Path(directory)
        common = [compiler, '--no-stdlib', '-I', root]
        if args.stdlib:
            common += ['--sdk-path', args.stdlib.resolve(strict=True)]

        def run(command, status=0):
            result = subprocess.run(command, cwd=root, env=environment,
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
            assert result.returncode == status, (list(map(str, command)), result.returncode,
                result.stdout[:6000], result.stderr[:6000])
            return result

        for language, source in (('ny', NY), ('matrix', MATRIX)):
            path = root / ('parameter-contract.' + language)
            path.write_text(source, encoding='utf-8')
            expected = path.with_suffix('.stdout')
            expected.write_text(EXPECTED, encoding='utf-8')
            routes = 'all' if args.llvm == 'ON' else 'text-interpreter,binary-interpreter,bundle-text,bundle-binary'
            run([sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                 '--compiler', compiler, '--interpreter', interpreter,
                 '--source', path, '--expected-stdout', expected, '--expected-exit', '0',
                 '--compiler-arg=--no-stdlib', '--routes', routes,
                 '--work-dir', root / (language + '-routes'), '--strict'])
            checks += 6 if args.llvm == 'ON' else 4
            if args.llvm == 'ON':
                executable = root / ('parameter-' + language + '-unmanaged')
                run([*common, '--quiet', '--native', '--unmanaged', '-o', executable, path])
                executed = run([executable])
                assert (executed.stdout, executed.stderr) == (EXPECTED, ''), (language, executed)
                checks += 1
            analysis = json.loads(run([*common, '--analyze', '--semantic-index', path]).stdout)
            assert analysis['success'] and analysis['semantic'], analysis
            checks += 1

        # Existing callbacks remain usable alongside addressed bindings and as
        # ordinary function-valued parameters of a direct reference callee.
        # Native indirect calls retain their pre-existing unsupported status.
        (root / 'parameter_callbacks.ny').write_text(
            'module parameter_callbacks\npub type Callback = fn(i64) -> i64\n',
            encoding='utf-8')
        for name, ny, matrix, stdout, native in (
            ('callbacks', CALLBACK_NY, CALLBACK_MATRIX, '16\n16\n12\n', False),
            ('text-operations', TEXT_NY, TEXT_MATRIX, 'bc\nnew\nnew!\n', True),
        ):
            for language, source in (('ny', ny), ('matrix', matrix)):
                path = root / (name + '.' + language)
                path.write_text(source, encoding='utf-8')
                expected = path.with_suffix('.stdout')
                expected.write_text(stdout, encoding='utf-8')
                native_enabled = native and args.llvm == 'ON'
                routes = 'all' if native_enabled else 'text-interpreter,binary-interpreter,bundle-text,bundle-binary'
                run([sys.executable, REPOSITORY / 'tests/routes/ny_route_matrix.py',
                     '--compiler', compiler, '--interpreter', interpreter,
                     '--source', path, '--expected-stdout', expected, '--expected-exit', '0',
                     '--compiler-arg=--no-stdlib', '--compiler-arg=-I',
                     '--compiler-arg=' + str(root), '--routes', routes,
                     '--work-dir', root / (name + '-' + language + '-routes'), '--strict'])
                checks += 6 if native_enabled else 4
                if native_enabled:
                    executable = root / (name + '-' + language + '-unmanaged')
                    run([*common, '--quiet', '--native', '--unmanaged', '-o', executable, path])
                    executed = run([executable])
                    assert (executed.stdout, executed.stderr) == (stdout, ''), (name, language, executed)
                    checks += 1

        # Both source imports and source-less binary libraries must preserve
        # the parameter mode while forwarding a binding across frontends.
        for provider_language in ('ny', 'matrix'):
            provider = root / ('provider.' + provider_language)
            provider.write_text(
                'module provider\npub fn bump(ref target: i64)\ntarget = target + 1\nend fn\n'
                if provider_language == 'ny' else
                'book provider\npublic bump receives reference target as number begin paragraph\n'
                'set target to target plus 1\nend paragraph\n', encoding='utf-8')
            consumer_language = 'matrix' if provider_language == 'ny' else 'ny'
            consumer = root / ('consumer.' + consumer_language)
            consumer.write_text(
                'import provider.*\nfn relay(ref target: i64)\nbump(ref target)\nend fn\n'
                'fn main() -> i32\nlet mut total: i64 = 41\nrelay(ref total)\nprintln(total)\nreturn 0\nend fn\n'
                if consumer_language == 'ny' else
                'book consumer\nread everything from provider\n'
                'relay receives reference target as number begin paragraph\n'
                'evaluate bump with target being reference to target\nend paragraph\n'
                'main returns override integer begin paragraph\nvariable total as number is 41\n'
                'evaluate relay with reference to total\nsay total\ngive 0\nend paragraph\n', encoding='utf-8')
            library = root / 'provider.nil'
            for dependency in ('source', 'library'):
                if dependency == 'library':
                    run([*common, '--quiet', '--no-bundle', '--emit-library',
                         '--emit-binary-nil', '-o', library, provider])
                    provider.unlink()
                for encoding, flags in (('text', []), ('binary', ['--emit-binary-nil']),
                                        *((('native', ['--native']),) if args.llvm == 'ON' else ())):
                    output = root / ('import-' + provider_language + '-' + dependency + '-' + encoding)
                    run([*common, '--quiet', '--no-bundle', *flags, '-o', output, consumer])
                    command = [output] if encoding == 'native' else [interpreter, '--no-ai', output]
                    result = run(command)
                    assert (result.stdout, result.stderr) == ('42\n', ''), result
                    checks += 1
            library.unlink()
            consumer.unlink()

        def program(language, body, declarations=''):
            if language == 'ny':
                return declarations + '\nfn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
            return 'book parameter_invalid\n' + declarations + '\nmain returns override integer begin paragraph\n' + body + '\ngive 0\nend paragraph\n'

        declarations = {
            'ny': 'fn bump(ref target: i64)\ntarget = target + 1\nend fn\n',
            'matrix': 'bump receives reference target as number begin paragraph\nset target to target plus 1\nend paragraph\n',
        }
        invalid = [
            ('immutable-local', 'let total: i64 = 1\nbump(ref total)',
             'value total as number is 1\nevaluate bump with reference to total'),
            ('wrong-width', 'let mut total: i32 = 1\nbump(ref total)',
             'variable total as override integer is 1\nevaluate bump with reference to total'),
            ('wrong-kind', 'let mut total: text = "1"\nbump(ref total)',
             'variable total as text is "1"\nevaluate bump with reference to total'),
            ('missing-reference', 'let mut total: i64 = 1\nbump(total)',
             'variable total as number is 1\nevaluate bump with total'),
            ('temporary', 'bump(ref (1 as i64))',
             'evaluate bump with reference to (1 becomes number)'),
            ('expression', 'let mut total: i64 = 1\nbump(ref (total + 1))',
             'variable total as number is 1\nevaluate bump with reference to (total plus 1)'),
            ('index', 'let mut totals: array<i64> = [1]\nbump(ref totals[0])',
             'variable totals as array of number is [1]\nevaluate bump with reference to (totals at 0)'),
            ('function-value', 'let callback = bump', 'value callback is the paragraph bump'),
        ]
        for name, ny, matrix in invalid:
            for language, body in (('ny', ny), ('matrix', matrix)):
                source = program(language, body, declarations[language])
                path = root / (name + '.' + language)
                path.write_text(source, encoding='utf-8')
                for flags in ([], ['--emit-binary-nil'], ['--no-typecheck'],
                              ['--no-typecheck', '--emit-binary-nil'],
                              *([['--native']] if args.llvm == 'ON' else [])):
                    output = root / 'protected-output'
                    output.write_bytes(b'previous successful artifact')
                    result = run([*common, '--quiet', '--no-bundle', *flags,
                                  '-o', output, path], 1)
                    assert result.stderr.strip(), (name, language, flags, result)
                    assert output.read_bytes() == b'previous successful artifact', (name, language, flags)
                    checks += 1

        # A callable expression is not a direct call to its nested function
        # identifiers. Callee traversal must not accidentally authorize a
        # reference-parameter function as an ordinary function-pointer value.
        for name, body, extra in [
            ('conditional-callee', '(if true then bump else bump end if)(1)', ''),
            ('indexed-callee', '[bump][0](1)', ''),
            ('nested-callee', 'choose(bump)(1)',
             'fn choose(worker: fn(i64) -> void) -> fn(i64) -> void\nreturn worker\nend fn\n'),
        ]:
            path = root / (name + '.ny')
            path.write_text(program('ny', body, declarations['ny'] + extra), encoding='utf-8')
            analysis = json.loads(run([*common, '--analyze', path], 1).stdout)
            assert not analysis['success'] and any('must be called directly' in item['message']
                for item in analysis['diagnostics']), (name, analysis)
            checks += 1
            for flags in ([], ['--no-typecheck']):
                output = root / 'protected-indirect-call'
                output.write_bytes(b'previous successful artifact')
                run([*common, '--quiet', '--no-bundle', *flags, '-o', output, path], 1)
                assert output.read_bytes() == b'previous successful artifact', (name, flags)
                checks += 1

        # Declaration-time restrictions and hidden aliases must be enforced
        # even when the function is never invoked or checking is disabled.
        invalid_declarations = [
            ('generic', 'fn invalid<T>(ref target: T)\nend fn\n',
             'invalid of type T receives reference target as T begin paragraph\nend paragraph\n'),
            ('readonly-value', 'fn readonly(target: i64)\ntarget = 2\nend fn\n',
             'readonly receives target as number begin paragraph\nset target to 2\nend paragraph\n'),
            ('readonly-forward', 'fn readonly(target: i64)\nbump(ref target)\nend fn\n',
             'readonly receives target as number begin paragraph\nevaluate bump with reference to target\nend paragraph\n'),
            ('global', 'let mut total: i64 = 1\nfn use_global()\nbump(ref total)\nend fn\n',
             'variable total as number is 1\nuse-global begin paragraph\nevaluate bump with reference to total\nend paragraph\n'),
            ('default', 'fn invalid(ref target: i64 = 1)\nend fn\n',
             'invalid receives reference target as number defaulting to 1 begin paragraph\nend paragraph\n'),
            ('variadic', 'fn invalid(ref ..target: i64)\nend fn\n',
             'invalid receives more reference target as number begin paragraph\nend paragraph\n'),
            ('array', 'fn invalid(ref target: array<i64>)\nend fn\n',
             'invalid receives reference target as array of number begin paragraph\nend paragraph\n'),
            ('record', 'struct Parcel\nitem: i64\nend struct\nfn invalid(ref target: Parcel)\nend fn\n',
             'Parcel begin record\nfield item as number\nend record\ninvalid receives reference target as Parcel begin paragraph\nend paragraph\n'),
            ('field', 'struct Parcel\nitem: i64\nend struct\nfn invalid()\nlet mut record = Parcel { item: 1 }\nbump(ref record.item)\nend fn\n',
             'Parcel begin record\nfield item as number\nend record\ninvalid begin paragraph\nvariable sample is build Parcel with item being 1\nevaluate bump with reference to sample.item\nend paragraph\n'),
            ('lambda', 'fn invalid()\nlet callback = fn(ref target: i64)\ntarget = 1\nend fn\nend fn\n',
             'invalid begin paragraph\nvalue callback is anonymous receives reference target as number begin paragraph\nset target to 1\nend paragraph\nend paragraph\n'),
            ('method', 'struct Parcel\nitem: i64\nend struct\nimpl Parcel\nfn invalid(self, ref target: i64)\ntarget = 1\nend fn\nend impl\n',
             'Parcel begin record\nfield item as number\nend record\nParcel begin implementation\ninvalid receives reference target as number begin paragraph\nset target to 1\nend paragraph\nend implementation\n'),
        ]
        for name, ny, matrix in invalid_declarations:
            for language, declaration in (('ny', ny), ('matrix', matrix)):
                path = root / ('declaration-' + name + '.' + language)
                path.write_text(program(language, '', declarations[language] + declaration), encoding='utf-8')
                for flags in ([], ['--no-typecheck']):
                    output = root / 'protected-declaration'
                    output.write_bytes(b'previous successful artifact')
                    result = run([*common, '--quiet', '--no-bundle', *flags, '-o', output, path], 1)
                    assert result.stderr.strip(), (name, language, result)
                    assert output.read_bytes() == b'previous successful artifact', (name, language, flags)
                    checks += 1
    print(f'PASS: {checks} mutable/reference parameter checks')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
