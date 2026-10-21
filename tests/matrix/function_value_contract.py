#!/usr/bin/env python3
"""Matrix paragraph values share ordinary declaration lookup and lexical binding."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    args = parser.parse_args()
    compiler = str(args.compiler.resolve(strict=True))
    interpreter = str(args.interpreter.resolve(strict=True))
    checks = 0
    with tempfile.TemporaryDirectory(prefix='matrix-function-values-') as directory:
        root = Path(directory)

        def run(command, *, status=0):
            result = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, timeout=30)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def check(name, source, expected, bindings):
            nonlocal checks
            path = root / (name + '.matrix')
            path.write_bytes(source.encode('utf-8'))
            analysis = run([compiler, '--analyze', '--semantic-index', '--no-stdlib',
                '-I', str(root), str(path)])
            assert not analysis.stderr, analysis.stderr
            result = json.loads(analysis.stdout)
            assert result['success'] and result['semantic'], result
            index = result['semantic']
            snapshots = {item['path']: item['text'] for item in index['sources']}
            assert snapshots[str(path)] == source
            for context, spelling, target_file, target_context, target_spelling, kind in bindings:
                start = len(source[:source.index(context) + context.index(spelling)].encode('utf-8'))
                references = [item for item in index['references']
                    if item['path'] == str(path) and item['start'] == start]
                assert len(references) == 1, (context, references)
                reference = references[0]
                assert reference['length'] == len(spelling.encode('utf-8')), reference
                target = next(item for item in index['symbols'] if item['id'] == reference['symbol_id'])
                target_path = str(root / target_file) if target_file else str(path)
                target_source = snapshots[target_path]
                target_start = len(target_source[:target_source.index(target_context)
                    + target_context.index(target_spelling)].encode('utf-8'))
                assert (target['path'], target['start'], target['length'], target['kind']) == (
                    target_path, target_start, len(target_spelling.encode('utf-8')), kind), (context, target)
                checks += 1
            # Indirect calls are currently supported by the VM. Validate both
            # encodings; native indirect calls are outside this parser fix.
            for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                artifact = root / (name + '-' + encoding + '.nil')
                run([compiler, '--no-stdlib', '--no-bundle', '-I', str(root), *flags,
                    '--output', str(artifact), str(path)])
                executed = run([interpreter, str(artifact)])
                assert executed.stdout == expected and not executed.stderr, (name, encoding, executed)
                checks += 1

        source = '''book folded-values
!!! é😀: names retain UTF-8 source offsets and CRLF line endings.
main returns number begin paragraph
 value first is the paragraph READ_COUNT
 value second is the paragraph readcount
 value third is the paragraph Read-Count
 value fourth is the paragraph `READ_COUNT`
 value escaped is the paragraph `VALUE`
 say first with 1
 say second with 2
 say third with 3
 say fourth with 4
 say escaped with 5
 give 0
end paragraph
read-count receives sample-value as number returns number begin paragraph
 give sample-value plus 10
end paragraph
`value` receives sample-value as number returns number begin paragraph
 give sample-value plus 20
end paragraph
'''.replace('\n', '\r\n')
        bindings = [(f'the paragraph {written}', spelling, None,
            'read-count receives', 'read-count', 'function') for written, spelling in
            [('READ_COUNT', 'READ_COUNT'), ('readcount', 'readcount'),
             ('Read-Count', 'Read-Count'), ('`READ_COUNT`', 'READ_COUNT')]]
        bindings.append(('the paragraph `VALUE`', 'VALUE', None, '`value` receives', 'value', 'function'))
        check('folded', source, '11\n12\n13\n14\n25\n', bindings)

        (root / 'callbacks.ny').write_text('module callbacks\n'
            'pub type Callback = fn(i64) -> i64\n', encoding='utf-8')
        source = '''book lexical-values
read everything from callbacks
read-count receives sample-value as number returns number begin paragraph
 give sample-value plus 1
end paragraph
other-count receives sample-value as number returns number begin paragraph
 give sample-value plus 10
end paragraph
invoke receives worker-value as Callback returns number begin paragraph
 value parameter-copy is the paragraph WORKER_VALUE
 say parameter-copy with 2
 when true then
  value worker-value is the paragraph other-count
  value inner-copy is the paragraph WORKERVALUE
  say inner-copy with 2
 end when
 value outer-copy is the paragraph worker_value
 give outer-copy with 3
end paragraph
main returns number begin paragraph
 value callback-value is the paragraph read-count
 when true then
  value read-count is the paragraph other-count
  value shadow-copy is the paragraph READ_COUNT
  say shadow-copy with 1
 end when
 value function-copy is the paragraph READCOUNT
 say function-copy with 1
 when true then
  value callback-value is the paragraph other-count
  value variable-copy is the paragraph CALLBACK_VALUE
  say variable-copy with 1
 end when
 say invoke with callback-value
 give 0
end paragraph
'''
        check('lexical', source, '11\n2\n11\n3\n12\n4\n', [
            ('parameter-copy is the paragraph WORKER_VALUE', 'WORKER_VALUE', None,
             'receives worker-value', 'worker-value', 'parameter'),
            ('inner-copy is the paragraph WORKERVALUE', 'WORKERVALUE', None,
             'value worker-value is', 'worker-value', 'variable'),
            ('outer-copy is the paragraph worker_value', 'worker_value', None,
             'receives worker-value', 'worker-value', 'parameter'),
            ('shadow-copy is the paragraph READ_COUNT', 'READ_COUNT', None,
             'value read-count is', 'read-count', 'variable'),
            ('function-copy is the paragraph READCOUNT', 'READCOUNT', None,
             'read-count receives', 'read-count', 'function'),
            ('variable-copy is the paragraph CALLBACK_VALUE', 'CALLBACK_VALUE', None,
             'value callback-value is the paragraph other-count', 'callback-value', 'variable'),
        ])

        (root / 'ny_provider.ny').write_text('module ny_provider\n'
            'pub fn ny_value(input: i64) -> i64\n return input + 100\nend fn\n', encoding='utf-8')
        (root / 'matrix_provider.matrix').write_text('book matrix-provider\n'
            'public matrix-value receives input-value as number returns number begin paragraph\n'
            ' give input-value plus 200\nend paragraph\n', encoding='utf-8')
        source = '''book imported-values
read ny-value from ny-provider
read everything from matrix-provider
main returns number begin paragraph
 value ny-copy is the paragraph ny-value
 value matrix-copy is the paragraph matrix-value
 say ny-copy with 1
 say matrix-copy with 2
 give 0
end paragraph
'''
        check('imports', source, '101\n202\n', [
            ('the paragraph ny-value', 'ny-value', 'ny_provider.ny', 'fn ny_value', 'ny_value', 'function'),
            ('the paragraph matrix-value', 'matrix-value', 'matrix_provider.matrix',
             'public matrix-value', 'matrix-value', 'function'),
        ])

        # An external declaration is not in this parser's declaration map.
        # The fix must preserve ordinary imports' case-sensitive NIL spelling.
        path = root / 'external-case.matrix'
        path.write_text(source.replace('the paragraph ny-value', 'the paragraph NY_VALUE'), encoding='utf-8')
        result = json.loads(run([compiler, '--analyze', '--semantic-index', '--no-stdlib',
            '-I', str(root), str(path)], status=1).stdout)
        assert result['semantic'] is None and any("Undefined identifier 'NY_VALUE'" in item['message']
            for item in result['diagnostics']), result
        checks += 1
    print(f'PASS: {checks} Matrix function-value checks')


if __name__ == '__main__':
    main()
