#!/usr/bin/env python3
"""Analysis certifies exact consumed disk inputs for incremental project builds."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='NIL dependency graph ') as temporary:
        root = Path(temporary)
        source = root / 'main.ny'
        matrix = root / 'main.matrix'
        includes = root / 'shared include'
        includes.mkdir()
        header = includes / 'constant.fragment'
        nested = includes / 'nested.no-language-extension'
        header.write_text('@include "nested.no-language-extension"\n', encoding='utf8')
        nested.write_text('@define ANSWER 42\n', encoding='utf8')
        provider = root / 'provider.ny'
        provider.write_text('module provider\npub fn answer() -> i32\n return 42\nend fn\n', encoding='utf8')
        source.write_text('@include "shared include/constant.fragment"\nimport provider\n'
                          'fn main() -> i32\n return answer() - ANSWER\nend fn\n', encoding='utf8')
        matrix.write_text('@include "shared include/constant.fragment"\n'
                          'book dependencies\nmain returns number begin paragraph\n'
                          '    give ANSWER\nend paragraph\n', encoding='utf8')

        def run(arguments, *, input=None, status=0):
            nonlocal checks
            result = subprocess.run([str(compiler), *map(str, arguments)], cwd=root, env=environment,
                                    input=input, text=True, capture_output=True, timeout=30)
            checks += 1
            assert result.returncode == status, (arguments, result.returncode, result.stdout, result.stderr)
            return result

        def analyze(path=source, *, extra=(), input=None, status=0):
            result = run(['--analyze', '--no-stdlib', *extra, path], input=input, status=status)
            report = json.loads(result.stdout)
            assert isinstance(report['dependenciesComplete'], bool), report
            paths = [item['path'] for item in report['dependencies']]
            assert paths == sorted(set(paths)), paths
            assert all(Path(path).is_absolute() and not path.startswith('nillib:') for path in paths)
            for item in report['dependencies']:
                assert len(item['digest']) == 64 and set(item['digest']) <= set('0123456789abcdef'), item
            return report

        def consumed(report, expected):
            actual = {item['path']: item['digest'] for item in report['dependencies']}
            assert actual == {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in expected}, actual

        report = analyze()
        assert report['dependenciesComplete'] and report['success'], report
        consumed(report, [source, provider, header, nested])
        assert report['sources'] == [], 'build dependencies must not masquerade as diagnostic snapshots'
        # Reading via source aliases emits one canonical entry, not duplicate identities.
        alias = root / 'source-alias.ny'
        alias.symlink_to(source.name)
        consumed(analyze(alias), [source, provider, header, nested])
        consumed(analyze(matrix), [matrix, header, nested])
        for name in ('nativeRuntime', 'nativeLinker'):
            assert report[name] is None or Path(report[name]).is_absolute(), report
        compiler_files = report['compilerFiles']
        assert compiler_files == sorted(set(compiler_files)), compiler_files
        assert all(Path(path).is_absolute() and Path(path).is_file() for path in compiler_files)
        if sys.platform.startswith('linux'):
            assert str(compiler) in compiler_files, compiler_files

        # Preserving the timestamp cannot conceal an edit; the digest describes bytes.
        previous = nested.stat()
        nested.write_text('@define ANSWER 43\n', encoding='utf8')
        os.utime(nested, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        changed = analyze()
        consumed(changed, [source, provider, header, nested])
        assert changed['dependencies'] != report['dependencies']

        # Fresh dependency discovery observes a newly shadowing source candidate.
        libraries = root / 'libraries'
        libraries.mkdir()
        library = libraries / 'provider.nil'
        run(['--quiet', '--no-stdlib', '--no-bundle', '--emit-binary-nil', '--emit-library',
             '--output', library, provider])
        saved_provider = provider.read_bytes()
        provider.unlink()
        binary = analyze(extra=['-I', libraries])
        assert binary['dependenciesComplete'], binary
        consumed(binary, [source, library, header, nested])
        provider.write_bytes(saved_provider)
        shadowed = analyze(extra=['-I', libraries])
        consumed(shadowed, [source, provider, header, nested])

        # Editor/stream snapshots cannot certify that disk contains the checked program.
        stdin = analyze(extra=['--source-stdin'], input=source.read_text())
        assert not stdin['dependenciesComplete'], stdin
        workspace = analyze(extra=['--workspace-stdin'], input=json.dumps({
            'schema': 'nil.analysis.workspace.v1',
            'sources': [{'path': str(source), 'text': source.read_text()}]}))
        assert not workspace['dependenciesComplete'], workspace
        provider.write_text('module provider\npub fn answer() -> i32\n return "wrong"\nend fn\n')
        failed = analyze(status=1)
        assert not failed['dependenciesComplete'] and not failed['success'], failed
        provider.unlink()
        missing = analyze(status=1)
        assert not missing['dependenciesComplete'] and not missing['success'], missing
    print(f'analysis dependencies: {checks} consumed-file, digest, import, include, overlay and failure checks passed')


if __name__ == '__main__':
    main()
