#!/usr/bin/env python3
"""Exercise the public no-execution analysis protocol using real source buffers."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--stdlib', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nil-analysis-') as directory:
        root = pathlib.Path(directory)
        primary = root / 'unsaved.ny'
        def analyze(source, *, path=primary, extra=(), status=0):
            proc = subprocess.run([args.compiler, '--analyze', '--source-stdin',
                                   '--no-stdlib', '-I', args.stdlib, str(path), *extra],
                                  input=source, text=True, capture_output=True, timeout=20, cwd=root)
            assert proc.returncode == status, (proc.returncode, proc.stdout, proc.stderr)
            assert not proc.stderr, proc.stderr
            value = json.loads(proc.stdout)
            assert value['schema'] == 'nil.analysis.v1'
            assert value['source'] == str(path)
            assert value['success'] == (status == 0)
            for item in value['diagnostics']:
                assert {'code', 'severity', 'message', 'path', 'start', 'length', 'hint', 'fix'} <= item.keys()
            return value

        # A current buffer wins over disk; main is never executed and no output file exists.
        primary.write_text('this is deliberately invalid on disk')
        good = 'module sample\npub fn temp_sum(value: i32) -> i32\n return value + 1\nend fn\nfn main() -> i32\n panic("MUST NEVER EXECUTE")\n return 0\nend fn\n'
        snapshot = root / 'analysis_snapshot.ny'
        data = analyze(good, extra=('--emit-reflection', str(snapshot)))
        assert data['diagnostics'] == []
        assert snapshot.exists() and 'pub fn declaration(' in snapshot.read_text()
        assert not (root / 'analysis-unused.nil').exists()
        reflection = data['reflection']
        assert reflection['semantic_checked'] and not reflection['truncated']
        function = next(d for d in reflection['declarations'] if d['name'] == 'temp_sum')
        parameter = next(d for d in reflection['declarations'] if d['kind'] == 'parameter')
        assert function['qualified_name'] == 'sample::temp_sum'
        assert function['is_public'] and function['semantic_checked']
        assert parameter['parent_id'] == function['id'] and parameter['type_name'] == 'i32'

        unicode_source = 'fn main() -> i32\n println("λ🙂")  \n return 0\nend fn'
        data = analyze(unicode_source)
        edits = [d['fix'] for d in data['diagnostics'] if d['fix']]
        assert {d['code'] for d in data['diagnostics']} == {'NIL1001', 'NIL1002'}
        raw = unicode_source.encode()
        for edit in sorted(edits, key=lambda x: x['start'], reverse=True):
            raw = raw[:edit['start']] + edit['replacement'].encode() + raw[edit['start'] + edit['length']:]
        assert analyze(raw.decode())['diagnostics'] == []

        bad = analyze('fn main() -> i32\n return "bad"\nend fn\n', status=1)
        error = next(d for d in bad['diagnostics'] if d['severity'] == 'error')
        assert error['code'] == 'NIL0002' and error['path'] == str(primary)
        assert error['start'] == len('fn main() -> i32\n')
        assert not bad['reflection']['semantic_checked']
        syntax = analyze('fn main(\n', status=1)
        assert any(d['code'] == 'NIL0001' for d in syntax['diagnostics'])

        # Mixed frontend import aliases resolve against the same shared library.
        matrix = root / 'unsaved.matrix'
        matrix_good = 'book sample\nread everything from ny.lang.core\nmain returns override integer begin paragraph\n    say "hello"\n    give 0\nend paragraph\n'
        assert analyze(matrix_good, path=matrix)['language'] == 'matrix'
        dep = root / 'dep.ny'
        dep.write_text('module dep\npub fn broken() -> i32\n return "bad"\nend fn\n')
        imported = analyze('import dep.*\nfn main() -> i32\n return broken()\nend fn\n', status=1)
        assert any(d['path'] == str(dep) and d['start'] == len('module dep\npub fn broken() -> i32\n')
                   for d in imported['diagnostics'])

        # Logical URI identity survives linked workspaces and uppercase suffixes.
        linked = root / 'linked.ny'
        linked.symlink_to(primary)
        analyze(good, path=linked)
        analyze(matrix_good, path=root / 'UPPER.MATRIX')
        blob = root / 'blob'
        blob.write_text('invalid on disk')
        linked.unlink()
        linked.symlink_to(blob)
        analyze(good, path=linked)
        missing = analyze('import missing_package.*\nfn main() -> i32\n return 0\nend fn\n', status=1)
        assert missing['diagnostics'][0]['code'] == 'NIL0003'
        multiple = 'module first\npub fn one() -> i32\n return 1\nend fn\nmodule second\npub fn two() -> i32\n return 2\nend fn\nmodule consumer\nimport second.*\nfn main() -> i32\n return two()\nend fn\n'
        assert not analyze(multiple)['diagnostics']
        for invalid in (b'\xff', b'\xc0\x80', b'\xed\xa0\x80', b'\xf4\x90\x80\x80'):
            proc = subprocess.run([args.compiler, '--analyze', '--source-stdin', str(primary)],
                                  input=b'# ' + invalid + b'\n', capture_output=True, timeout=20)
            assert proc.returncode == 2 and proc.stdout == b''


        # Raw text contents are never modified by whitespace fixes.
        raw_text = 'book raw_text\nmain returns override integer begin paragraph\n    value body is begin text\n        literal spaces   \n    end text\n    say body\n    give 0\nend paragraph\n'
        assert not analyze(raw_text, path=matrix)['diagnostics']
        parenthesized = raw_text.replace('is begin text', 'is (begin text').replace('    end text', '    end text\n    )')
        assert not analyze(parenthesized, path=matrix)['diagnostics']
        backtick = raw_text.replace('value body', 'value `name#`').replace('say body', 'say `name#`')
        assert not analyze(backtick, path=matrix)['diagnostics']
        terminator = raw_text.replace('literal spaces   ', 'end\vtext\n        literal spaces   ')
        assert not analyze(terminator, path=matrix)['diagnostics']
        ny_raw = 'fn main() -> i32\n println("first\nsecond   \nthird")\n return 0\nend fn\n'
        assert not analyze(ny_raw)['diagnostics']

        for flags in [('--native',), ('--jit',), ('--no-typecheck',), ('-o', 'bad.nil'), ('--', 'arg')]:
            proc = subprocess.run([args.compiler, '--analyze', str(primary), *flags],
                                  text=True, capture_output=True, timeout=20, cwd=root)
            assert proc.returncode == 2 and not proc.stdout, (flags, proc)
        collision = subprocess.run([args.compiler, '--analyze', '--source-stdin', str(primary),
                                    '--emit-reflection', str(primary)], input=good,
                                   text=True, capture_output=True, timeout=20, cwd=root)
        assert collision.returncode == 2 and not collision.stdout
        assert primary.read_text() == 'this is deliberately invalid on disk'
    print('analysis protocol, UTF-8 fixes, imports, reflection and no-execution checks passed')


if __name__ == '__main__':
    main()
