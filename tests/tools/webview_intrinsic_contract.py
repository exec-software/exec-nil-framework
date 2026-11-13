#!/usr/bin/env python3
"""Reject forged WebView declarations before loading any native provider."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--interpreter', required=True, type=Path)
    parser.add_argument('--llvm', required=True, choices=('ON', 'OFF'))
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-webview-abi-') as directory:
        work = Path(directory)
        base = [compiler, '--quiet', '--sdk-path', ROOT/'nilstd/src']

        def run(command, status=0):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work, env=environment,
                stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
            assert result.returncode == status, (result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        imported = 'import nil.std.core.result\n'
        entry = 'fn main() -> i32\n return 0\nend fn\n'
        invalid = {
            'unknown': ('@intrinsic("webview.v1.unknown")\nfn provider() -> bool\n', 'Unknown WebView intrinsic'),
            'arity': (imported+'@intrinsic("webview.v1.configure")\nfn provider() -> Result<i64,text>\n', 'exact WebView ABI'),
            'width': (imported+'@intrinsic("webview.v1.show")\nfn provider(handle:i32) -> Result<i64,text>\n', 'exact WebView ABI'),
            'number-result': (imported+'@intrinsic("webview.v1.poll")\nfn provider(handle:i64) -> Result<bool,text>\n', 'exact WebView ABI'),
            'text-result': (imported+'@intrinsic("webview.v1.event_text")\nfn provider(handle:i64,field:text) -> Result<i64,text>\n', 'exact WebView ABI'),
            'error-payload': (imported+'@intrinsic("webview.v1.show")\nfn provider(handle:i64) -> Result<i64,i64>\n', 'exact WebView ABI'),
            'reference': (imported+'@intrinsic("webview.v1.show")\nfn provider(ref handle:i64) -> Result<i64,text>\n', 'exact WebView ABI'),
            'default': (imported+'@intrinsic("webview.v1.show")\nfn provider(handle:i64=0) -> Result<i64,text>\n', 'exact WebView ABI'),
            'variadic': (imported+'@intrinsic("webview.v1.show")\nfn provider(..handles:i64) -> Result<i64,text>\n', 'exact WebView ABI'),
            'generic': (imported+'@intrinsic("webview.v1.show")\nfn provider<T>(handle:i64) -> Result<i64,text>\n', 'exact WebView ABI'),
            'impostor': ('''module impostor
enum Result<T,E>
 Ok(T)
 Error(E)
end enum
@intrinsic("webview.v1.show")
fn provider(handle:i64) -> Result<i64,text>
''', 'exact WebView ABI'),
        }
        for name, (declaration, diagnostic) in invalid.items():
            source, output = work/(name+'.ny'), work/(name+'.nil')
            source.write_text(declaration+entry, newline='\n')
            output.write_bytes(b'previous artifact')
            result = run([*base, '--no-bundle', '-o', output, source], 1)
            assert diagnostic in result.stderr
            assert output.read_bytes() == b'previous artifact'

        for name, declaration, expression in (
            ('arity', imported+'@intrinsic("webview.v1.configure")\nfn provider() -> Result<i64,text>\n', 'provider()'),
            ('return', '@intrinsic("webview.v1.poll")\nfn provider(handle:i64) -> i64\n', 'provider(0)'),
        ):
            source, output = work/('unchecked-'+name+'.ny'), work/('unchecked-'+name+'.nil')
            source.write_text(declaration+'fn main() -> i32\n let value = '+expression+'\n return 0\nend fn\n', newline='\n')
            run([*base, '--no-typecheck', '--no-bundle', '-o', output, source])
            result = run([interpreter, '--no-ai', output], 1)
            assert 'WebView ABI' in result.stderr
            if args.llvm == 'ON':
                for flags in ([], ['--jit']):
                    output.write_bytes(b'previous artifact')
                    result = run([*base, '--no-typecheck', '--native', *flags, '-o', output, source], 1)
                    assert 'WebView ABI' in result.stderr
                    assert output.read_bytes() == b'previous artifact'

        source = work/'checked.ny'
        source.write_text(imported+'''@intrinsic("webview.v1.configure")
fn configure(path:text) -> Result<i64,text>
@intrinsic("webview.v1.event_text")
fn event_text(snapshot:i64,field:text) -> Result<text,text>
fn main() -> i32
 match configure("relative-provider")
  Result.Ok(value) -> return 8
  Result.Error(message) -> println("checked WebView configuration failure")
 end match
 match event_text(0,"payload_json")
  Result.Ok(value) -> return 9
  Result.Error(message) -> println("checked WebView event failure")
 end match
 return 0
end fn
''', newline='\n')
        for route in ['text', 'binary', 'bundle'] + (['native', 'jit'] if args.llvm == 'ON' else []):
            output = work/('checked-'+route)
            flags = (['--native', '--jit'] if route == 'jit' else ['--native'] if route == 'native'
                     else [] if route == 'bundle' else ['--no-bundle'])
            if route == 'binary': flags += ['--emit-binary-nil']
            result = run([*base, *flags, '-o', output, source])
            if route != 'jit':
                executable = output.with_suffix('.exe') if os.name == 'nt' and route in ('native', 'bundle') else output
                command = [interpreter, '--no-ai', output] if route in ('text', 'binary') else [executable]
                result = run(command)
            assert result.stdout == 'checked WebView configuration failure\nchecked WebView event failure\n'
            assert not result.stderr
    print(f'WebView intrinsic ABI: {checks} checks passed')


if __name__ == '__main__':
    main()
