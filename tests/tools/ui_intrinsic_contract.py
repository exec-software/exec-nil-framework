#!/usr/bin/env python3
"""UI provider declarations must preserve their exact checked cross-route ABI."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


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
    with tempfile.TemporaryDirectory(prefix='nil-ui-intrinsic-') as directory:
        work = Path(directory)
        base = [compiler, '--quiet', '--sdk-path', ROOT / 'nilstd/src']

        def run(command, status=0):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, timeout=120)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        entry = 'fn main() -> i32\n return 0\nend fn\n'
        imported = 'import nil.std.core.result\n'
        invalid = {
            'unknown': ('@intrinsic("ui.v1.unknown")\nfn provider() -> bool\n', 'Unknown UI intrinsic'),
            'arity': ('@intrinsic("ui.v1.available")\nfn provider(unused: i64) -> bool\n', 'exact UI ABI'),
            'scalar-width': (imported + '@intrinsic("ui.v1.read_text")\nfn provider(handle: i32) -> Result<text,text>\n', 'exact UI ABI'),
            'capability-result': ('@intrinsic("ui.v1.available")\nfn provider() -> i64\n', 'exact UI ABI'),
            'destroy-result': (imported + '@intrinsic("ui.v1.destroy_widget")\nfn provider(handle: i64) -> Result<bool,text>\n', 'exact UI ABI'),
            'number-result': (imported + '@intrinsic("ui.v1.poll")\nfn provider(handle: i64) -> Result<bool,text>\n', 'exact UI ABI'),
            'text-result': (imported + '@intrinsic("ui.v1.read_text")\nfn provider(handle: i64) -> Result<i64,text>\n', 'exact UI ABI'),
            'error-payload': (imported + '@intrinsic("ui.v1.poll")\nfn provider(handle: i64) -> Result<i64,i64>\n', 'exact UI ABI'),
            'reference': (imported + '@intrinsic("ui.v1.read_text")\nfn provider(ref handle: i64) -> Result<text,text>\n', 'exact UI ABI'),
            'default': (imported + '@intrinsic("ui.v1.read_text")\nfn provider(handle: i64 = 0) -> Result<text,text>\n', 'exact UI ABI'),
            'variadic': (imported + '@intrinsic("ui.v1.read_text")\nfn provider(..handles: i64) -> Result<text,text>\n', 'exact UI ABI'),
            'generic': ('@intrinsic("ui.v1.available")\nfn provider<T>() -> bool\n', 'exact UI ABI'),
            'lookalike-result': ('''module impostor
enum Result<T,E>
 Ok(T)
 Error(E)
end enum
@intrinsic("ui.v1.read_text")
fn provider(handle: i64) -> Result<text,text>
''', 'exact UI ABI'),
        }
        for name, (declaration, diagnostic) in invalid.items():
            source = work / (name + '.ny')
            source.write_text(declaration + entry)
            output = work / (name + '.nil')
            output.write_bytes(b'previous artifact')
            result = run([*base, '--no-bundle', '-o', output, source], 1)
            assert diagnostic in result.stderr, (name, result.stderr)
            assert output.read_bytes() == b'previous artifact', name

        matrix = work / 'wrong-width.matrix'
        matrix.write_text('''book invalid-ui
read nil.std.core.result
note intrinsic "ui.v1.read_text"
provider receives handle as i32 returns Result of text, text
main returns number begin paragraph
 give 0
end paragraph
''')
        result = run([*base, '--no-bundle', '-o', work / 'matrix.nil', matrix], 1)
        assert 'exact UI ABI' in result.stderr, result.stderr

        # Matrix folding applies equally to public and private declarations,
        # and neither source order may silently turn a record into its factory.
        for reverse in (False, True):
            for record_visibility in ("", "public "):
                for function_visibility in ("", "public "):
                    record = record_visibility + "TextModel begin record\n field content as text\nend record\n"
                    function = function_visibility + "text-model returns number begin paragraph\n give 0\nend paragraph\n"
                    source = work / 'folded-collision.matrix'
                    source.write_text('book collisions\n' + (function + record if reverse else record + function) +
                                      'main returns number begin paragraph\n give 0\nend paragraph\n')
                    result = run([*base, '--no-bundle', '-o', work / 'collision.nil', source], 1)
                    assert 'Matrix names fold case and separators' in result.stderr, result.stderr
                    assert 'text-model' in result.stderr and 'TextModel' in result.stderr, result.stderr

        # Ny does not fold spelling, but the shared checker must also prevent
        # a function from replacing a type with the exact same module identity.
        for reverse in (False, True):
            record = 'struct Shared\n content: text\nend struct\n'
            function = 'fn Shared() -> i32\n return 0\nend fn\n'
            source = work / 'cross-kind.ny'
            source.write_text((function + record if reverse else record + function) + entry)
            result = run([*base, '--no-bundle', '-o', work / 'cross-kind.nil', source], 1)
            assert 'conflicts with another declaration kind' in result.stderr, result.stderr

        # Implementations refer to an existing declaration, while named tests
        # carry display metadata. Neither is another module-level value symbol.
        source = work / 'non-collisions.matrix'
        source.write_text('''book valid-declarations
TextModel begin record
 field content as text
end record
TextModel begin implementation
end implementation
TextModel begin test
 expect yes
end test
main returns number begin paragraph
 give 0
end paragraph
''')
        run([*base, '--no-bundle', '-o', work / 'valid-declarations.nil', source])

        # Disabling source checking must not permit an incompatible declaration
        # to reinterpret its return value in either backend.
        for name, declaration, expression in (
            ('unchecked-arity', '@intrinsic("ui.v1.available")\nfn provider(unused: i64) -> bool\n', 'provider(0)'),
            ('unchecked-return', '@intrinsic("ui.v1.available")\nfn provider() -> i64\n', 'provider()'),
        ):
            source = work / (name + '.ny')
            source.write_text(declaration + 'fn main() -> i32\n println(' + expression + ')\n return 0\nend fn\n')
            output = work / (name + '.nil')
            compiled = subprocess.run(list(map(str, [*base, '--no-typecheck', '--no-bundle', '-o', output, source])),
                                      cwd=work, env=env, capture_output=True, text=True, timeout=120)
            checks += 1
            if compiled.returncode == 0:
                result = run([interpreter, '--no-ai', output], 1)
            else:
                assert compiled.returncode == 1, compiled
                result = compiled
            assert 'UI ABI' in result.stderr, (name, result.stderr)
            if args.llvm == 'ON':
                output.write_bytes(b'previous artifact')
                result = run([*base, '--no-typecheck', '--native', '-o', output, source], 1)
                assert 'UI ABI' in result.stderr, result.stderr
                assert output.read_bytes() == b'previous artifact'

        # A valid declaration works without a display. Read failures are typed
        # Result errors, and obtaining a capability cannot open a window.
        source = work / 'checked.ny'
        source.write_text(imported + '''@intrinsic("ui.v1.available")
fn provider_available() -> flag
@intrinsic("ui.v1.read_text")
fn provider_text(handle: i64) -> Result<text,text>
@intrinsic("ui.v1.destroy_widget")
fn provider_destroy(handle: i64) -> Result<i64,text>
fn main() -> i32
 let capability: bool = provider_available()
 let destroyed = provider_destroy(0)
 match destroyed
  Result.Ok(status) -> return 8
  Result.Error(message) -> println("checked destroy failure")
 end match
 let result = provider_text(0)
 match result
  Result.Ok(value) -> return 9
  Result.Error(message) -> println("checked UI failure")
 end match
 return 0
end fn
''')
        for route in ['text', 'binary', 'bundle'] + (['native', 'jit'] if args.llvm == 'ON' else []):
            output = work / ('checked-' + route)
            flags = ['--native', '--jit'] if route == 'jit' else ['--native'] if route == 'native' else [] if route == 'bundle' else ['--no-bundle']
            if route == 'binary':
                flags += ['--emit-binary-nil']
            result = run([*base, *flags, '-o', output, source])
            if route != 'jit':
                command = [interpreter, '--no-ai', output] if route in ('text', 'binary') else [output]
                result = run(command)
            assert result.stdout == 'checked destroy failure\nchecked UI failure\n', (route, result.stdout, result.stderr)
    print(f'UI intrinsic ABI: {checks} checks passed')


if __name__ == '__main__':
    main()
