#!/usr/bin/env python3
"""Shared literal metadata, attachment, preprocessing and execution contract."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
ARGS = '"cache, warm", enabled=true, limit=-4, ratio=1.25, target=net::Peer'
NY = '''module annotation_contract
@policy({args})
fn answer() -> i32
 return 7
end fn
@doc("A record")
struct Parcel
 item: i32
end struct
@doc("A class")
class Worker
 @doc("A method")
 pub fn inspect(self) -> i32
  return 7
 end fn
end class
@doc("A choice")
enum State
 Ready
end enum
fn main() -> i32
 println(answer())
 return 0
end fn
'''
MATRIX = '''book annotation-contract
{annotation}
answer returns override integer begin paragraph
 give 7
end paragraph
@doc("A record")
Parcel begin record
 field item as override integer
end record
@doc("A class")
Worker begin class
 @doc("A method")
 public inspect returns override integer begin paragraph
  give 7
 end paragraph
end class
@doc("A choice")
State begin choice
 when Ready
end choice
main returns override integer begin paragraph
 say answer with nothing
 give 0
end paragraph
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--compiler', type=Path, required=True)
    ap.add_argument('--interpreter', type=Path, required=True)
    ap.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    opts = ap.parse_args()
    compiler, interpreter = opts.compiler.resolve(), opts.interpreter.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-annotation-') as directory:
        root = Path(directory)

        def run(command, *, status=0, source=None):
            result = subprocess.run([str(x) for x in command], cwd=root, env=env,
                                    input=source, text=True, capture_output=True, timeout=90)
            assert result.returncode == status, (command, result.stdout, result.stderr)
            return result

        def analyze(path, *, extra=(), payload=None):
            return json.loads(run([compiler, '--analyze', '--no-stdlib', *extra, path], source=payload).stdout)

        def no_editable_symbols(result):
            semantic = result['semantic']
            assert semantic['partial'] and not semantic['symbols'] and not semantic['references'], result

        declarations = None
        for name, source in (
            ('long.ny', NY.replace('{args}', ARGS)),
            ('at.matrix', MATRIX.replace('{annotation}', '@policy(\n' + ARGS + '\n)')),
            ('prose.matrix', MATRIX.replace('{annotation}',
                'note policy with "cache, warm", enabled being true, limit being negative 4, ratio being 1.25, target being net::Peer')),
        ):
            path = root / name
            path.write_text(source)
            reflected = analyze(path)['reflection']
            annotations = reflected['annotations']
            assert [a['name'] for a in annotations] == ['policy', 'doc', 'doc', 'doc', 'doc'], annotations
            policy = annotations[0]
            assert policy['arguments_parsed'] and policy['line'] == 2, policy
            expected = [(None, 'text', 'cache, warm'), ('enabled', 'boolean', 'true'),
                        ('limit', 'integer', '-4'), ('ratio', 'floating', '1.25'),
                        ('target', 'symbol', 'net::Peer')]
            assert [(a['name'], a['kind'], a['value']) for a in policy['parsed_arguments']] == expected, policy
            shape = [(d['kind'], d['name']) for d in reflected['declarations']]
            if declarations is None:
                declarations = shape
            assert shape == declarations, shape
            checks += 1
            expected_stdout = root / (name + '.stdout')
            expected_stdout.write_text('7\n')
            routes = 'all' if opts.llvm == 'ON' else 'text-interpreter,binary-interpreter,bundle-text,bundle-binary'
            run([sys.executable, REPO / 'tests/routes/ny_route_matrix.py', '--compiler', compiler,
                 '--interpreter', interpreter, '--source', path, '--expected-stdout', expected_stdout,
                 '--expected-exit', '0', '--compiler-arg=--no-stdlib', '--routes', routes,
                 '--work-dir', root / (name + '-routes'), '--strict'])
            checks += 6 if opts.llvm == 'ON' else 4
            inspected = []
            for flags in ([], ['--emit-binary-nil']):
                output = root / (name + '.nil')
                # ai_context selects the emitted declaration for public inspection.
                inspected_source = source.replace('@doc(', '@ai_context(')
                path.write_text(inspected_source)
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle', *flags, '-o', output, path])
                inspected.append(json.loads(run([compiler, '--inspect-context', output]).stdout))
                path.write_text(source)
            assert inspected[0] == inspected[1], inspected
            checks += 1

        base = 'book invalid\n{annotation}\nmain returns override integer begin paragraph\n give 0\nend paragraph\n'
        invalid = ['@flag(key=1, key=2)', '@flag(key=1, 2)', '@flag(make())', '@flag(key=)',
                   '@flag([1,2])', '@flag(1e999)', '@', '@nil_binding_cell', 'note flag with key being 1, key being 2',
                   'note flag with', 'note flag with count being negative thing', 'context 3', '@flag("unfinished)',
                   'context "a\0b"', 'note doc with text being "a\0b"', '@doc("a\0b")']
        for annotation in invalid:
            path = root / 'invalid.matrix'
            path.write_text(base.replace('{annotation}', annotation))
            output = root / 'previous.nil'
            output.write_text('sentinel')
            run([compiler, '--quiet', '--no-stdlib', '--no-bundle', '-o', output, path], status=1)
            assert output.read_text() == 'sentinel'
            if '\0' in annotation:
                report = json.loads(run([compiler, '--analyze', '--no-stdlib', path], status=1).stdout)
                assert any(d['code'] == 'MX0001' and 'NUL' in d['message'] for d in report['diagnostics']), report
            checks += 1
        for declaration in ('value target is 1', 'Alias means number',
                            'Protocol begin contract\nend contract',
                            'Parcel begin record\n@flag\nfield x as number\nend record',
                            'main begin paragraph\n @flag\n value local is 1\nend paragraph',
                            'Parcel begin extension\nend extension', ''):
            path = root / 'target.matrix'
            path.write_text('book invalid\n@flag\n' + declaration + '\n')
            run([compiler, '--analyze', '--no-stdlib', path], status=1)
            checks += 1

        # Named test declarations now retain declaration annotations too.
        path = root / 'annotated-test.matrix'
        path.write_text('book annotated\n@flag\nTask begin test\n expect yes\nend test\n')
        report = json.loads(run([compiler, '--analyze', '--no-stdlib', path]).stdout)
        assert report['success'], report
        manifest = json.loads(run([compiler, '--test-list', '--no-stdlib', path]).stdout)
        assert len(manifest['tests']) == 1 and manifest['tests'][0]['name'] == 'Task', manifest
        checks += 1

        path = root / 'preprocessed.matrix'
        path.write_text('''book preprocessing
@define ANSWER 42
@if 0
@doc("discarded")
wrong begin paragraph
end paragraph
@endif
@doc("kept")
main returns override integer begin paragraph
 value literal is begin text
@error not a directive
ANSWER
end text
 say ANSWER
 give 0
end paragraph
''')
        result = analyze(path, extra=['--semantic-index'])
        assert result['success'], result
        no_editable_symbols(result)
        assert [a['arguments'] for a in result['reflection']['annotations']] == ['"kept"'], result
        output = root / 'preprocessed.nil'
        run([compiler, '--no-stdlib', '--no-bundle', '--quiet', '-o', output, path])
        assert run([interpreter, output]).stdout == '42\n'
        checks += 1
        raw = root / 'raw.matrix'
        raw.write_text('book raw\n@path("C:\\folder\\next")\nmain returns override integer begin paragraph\n give 0\nend paragraph\n')
        assert analyze(raw)['reflection']['annotations'][0]['parsed_arguments'][0]['value'] == 'C:\\folder\\next'
        checks += 1
        raw.write_text(base.replace('book invalid', 'book casing').replace(
            '{annotation}', '@INLINE\n@policy(enabled=TRUE, disabled=FALSE)'))
        annotations = analyze(raw)['reflection']['annotations']
        assert annotations[0]['name'] == 'inline', annotations
        assert [(a['kind'], a['value']) for a in annotations[1]['parsed_arguments']] == [
            ('boolean', 'true'), ('boolean', 'false')], annotations
        checks += 1
        for sign in ('negative ', 'minus ', '-'):
            raw.write_text(base.replace('{annotation}',
                f'note bounds with minimum being {sign}9223372036854775808'))
            argument = analyze(raw)['reflection']['annotations'][0]['parsed_arguments'][0]
            assert argument == {'name': 'minimum', 'kind': 'integer', 'value': '-9223372036854775808'}, argument
            checks += 1
        for value in ('9223372036854775808', 'negative 9223372036854775809'):
            raw.write_text(base.replace('{annotation}', f'note bounds with minimum being {value}'))
            run([compiler, '--analyze', '--no-stdlib', raw], status=1)
            checks += 1
        # A namespace separator on a prose annotation must not consume the
        # declaration on the following line as another name component.
        raw.write_text(base.replace('{annotation}', 'note policy::'))
        run([compiler, '--analyze', '--no-stdlib', raw], status=1)
        checks += 1
        for body, code, has_location in (
            ('@error original marker\nmain returns override integer begin paragraph\n give 0\nend paragraph\n', 'MX0002', True),
            ('@broken(key=)\nmain returns override integer begin paragraph\n give 0\nend paragraph\n', 'MX0001', False),
            ('main returns override integer begin paragraph\n give unknown-name\nend paragraph\n', 'NIL0002', False),
        ):
            source = 'book provenance\n@define VALUE 2\n' + body
            raw.write_text(source)
            result = json.loads(run([compiler, '--analyze', '--no-stdlib', raw], status=1).stdout)
            diagnostic = next(d for d in result['diagnostics'] if d['code'] == code)
            assert (diagnostic['start'] is not None) == has_location, diagnostic
            if has_location:
                assert source[diagnostic['start']:].startswith('@error'), diagnostic
            else:
                assert diagnostic['length'] is None and diagnostic['fix'] is None, diagnostic
                compiled = run([compiler, '--no-stdlib', '--no-bundle', '--quiet', '-o', output, raw], status=1)
                assert f'{raw}:0' in compiled.stderr and ' | ' not in compiled.stderr, compiled.stderr
            checks += 1
        sources = {'included.matrix': '@doc("unsaved")\nanswer returns number begin paragraph\n give 8\nend paragraph\n',
                   'entry.matrix': 'book includes\n@include "included.matrix"\nmain returns override integer begin paragraph\n give 0\nend paragraph\n'}
        payload = json.dumps({'schema': 'nil.analysis.workspace.v1',
                             'sources': [{'path': str(root / name), 'text': text} for name, text in sources.items()]})
        result = analyze(root / 'entry.matrix', extra=['--workspace-stdin', '--semantic-index'], payload=payload)
        assert result['success'], result
        no_editable_symbols(result)
        assert result['reflection']['annotations'][0]['arguments'] == '"unsaved"', result
        checks += 1
        (root / 'included.matrix').write_text('@error included marker\n')
        entry = root / 'entry.matrix'
        entry.write_text(sources['entry.matrix'])
        result = json.loads(run([compiler, '--analyze', '--no-stdlib', entry], status=1).stdout)
        diagnostic = next(d for d in result['diagnostics'] if d['code'] == 'MX0002')
        assert 'included.matrix:1: included marker' in diagnostic['message'], diagnostic
        assert diagnostic['start'] is None and diagnostic['length'] is None, diagnostic
        compiled = run([compiler, '--no-stdlib', '--no-bundle', '--quiet', '-o', output, entry], status=1)
        assert f'{entry}:0:1' not in compiled.stderr and ' | ' not in compiled.stderr, compiled.stderr
        checks += 1
    print(f'annotation contract: {checks} checks passed')


if __name__ == '__main__':
    main()
