#!/usr/bin/env python3
"""Merged source units retain their own root/module contexts on every route."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    compiler = str(args.compiler.resolve())
    interpreter = str(args.interpreter.resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-module-context-') as directory:
        root = Path(directory)
        common = [compiler, '--no-stdlib', '-I', str(root)]

        def run(command, status=0):
            result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                    stdin=subprocess.DEVNULL, timeout=45)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        (root / 'helper.ny').write_text('module helper\npub let shared_value: i32 = 40\n'
            'fn hidden() -> i32\nreturn 2\nend fn\n'
            'pub fn visible(input: i32) -> i32\nreturn shared_value + hidden() + input\nend fn\n')
        fixtures = {
            'root.ny': 'import helper.*\nlet shared_value: i32 = 100\nfn main() -> i32\n'
                       'println(visible(0))\nprintln(shared_value)\nreturn 0\nend fn\n',
            'root.matrix': 'book entry\nread everything from helper\nvalue shared-value as override integer is 100\n'
                           'main returns override integer begin paragraph\nsay visible with 0\n'
                           'say shared-value\ngive 0\nend paragraph\n',
        }
        for name, source in fixtures.items():
            path = root / name
            path.write_text(source)
            result = json.loads(run([*common, '--analyze', '--semantic-index', str(path)]).stdout)
            assert result['success'] and result['semantic'], result
            checks += 1
            for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                artifact = root / (name + '.' + encoding + '.nil')
                run([*common, '--no-bundle', *flags, '-o', str(artifact), str(path)])
                output = run([interpreter, str(artifact)])
                assert output.stdout == '42\n100\n' and not output.stderr, output
                checks += 1
            if args.llvm == 'ON':
                executable = root / (name + '.native')
                run([*common, '--native', '-o', str(executable), str(path)])
                output = run([str(executable)])
                assert output.stdout == '42\n100\n' and not output.stderr, output
                checks += 1

        # This used to succeed without a module/book because helper's context
        # leaked into the root source. Analysis and ordinary compilation must
        # both reject the private call, with or without an explicit root module.
        for language in ('ny', 'matrix'):
            for explicit in (False, True):
                if language == 'matrix' and not explicit:
                    continue  # Matrix requires a book/library declaration.
                source = ((('module entry\n' if explicit else '') +
                           'import helper.*\nfn main() -> i32\nreturn hidden()\nend fn\n')
                          if language == 'ny' else
                          (('book entry\n' if explicit else '') +
                           'read everything from helper\nmain returns override integer begin paragraph\n'
                           'give (hidden with)\nend paragraph\n'))
                # A no-argument paragraph value is still checked for privacy;
                # the extra return-type diagnostic does not hide that failure.
                if language == 'matrix':
                    source = source.replace('(hidden with)', 'the paragraph hidden')
                path = root / (f'private_{explicit}.' + language)
                path.write_text(source)
                analysis = json.loads(run([*common, '--analyze', str(path)], 1).stdout)
                assert not analysis['success'] and any('private to module' in d['message'] for d in analysis['diagnostics']), analysis
                checks += 1
                ordinary = run([*common, '--no-bundle', '-o', str(root / 'rejected.nil'), str(path)], 1)
                assert 'private to module' in ordinary.stderr, ordinary
                checks += 1
    print(f'module context contract: {checks} checks passed')


if __name__ == '__main__':
    main()
