#!/usr/bin/env python3
"""Exercise the public no-execution analysis protocol using real source buffers."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--stdlib', required=True)
    args = parser.parse_args()
    listed = subprocess.run([args.compiler, '--list-analysis-rules'], text=True,
                            capture_output=True, timeout=20)
    assert listed.returncode == 0 and not listed.stderr, listed
    catalog = json.loads(listed.stdout)
    assert catalog['schema'] == 'nil.analyzers.v1'
    assert len(catalog['analyzers']) == 1 and catalog['analyzers'][0]['id'] == 'nil.default'
    rules = catalog['analyzers'][0]['rules']
    assert {r['code'] for r in rules} == {'NIL1001', 'NIL1002', 'NIL2001', 'NIL2002'}
    for rule in rules:
        assert rule['title'] and rule['description'] and rule['default_severity'] == 'warning'
        assert rule['phase'] == ('syntax' if rule['code'].startswith('NIL1') else 'semantic')
        assert rule['has_fix'] == rule['code'].startswith('NIL1')
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
            assert isinstance(value['sources'], list)
            assert len(json.dumps(value['sources'], ensure_ascii=False, separators=(',', ':')).encode()) <= 1024 * 1024
            assert all(s['path'] != str(path) for s in value['sources'])
            for item in value['diagnostics']:
                assert {'code', 'severity', 'message', 'path', 'start', 'length', 'hint', 'fix'} <= item.keys()
            return value

        # A repository root has a frontend ny/ directory, which is not the
        # historical SDK ny/lang/ namespace tree. Discover nilstd/src instead,
        # even when the caller is outside the repository and only adds its root.
        discovery_environment = os.environ.copy()
        for name in ('NIL_STDLIB_PATH', 'NY_STDLIB_PATH', 'NIL_LIBRARY_PATH', 'NY_LIBRARY_PATH'):
            discovery_environment.pop(name, None)
        repository = pathlib.Path(__file__).resolve().parents[2]
        def discover(source, path, include_root):
            proc = subprocess.run([args.compiler, '--analyze', '--source-stdin',
                                   '-I', str(include_root), str(path)], input=source, text=True,
                                  capture_output=True, timeout=20, cwd=root, env=discovery_environment)
            assert proc.returncode == 0 and not proc.stderr, (proc.returncode, proc.stdout, proc.stderr)
            result = json.loads(proc.stdout)
            assert result['success'] and not result['diagnostics'], result
        discover('import nil.std.text\nfn main() -> i32\n println(substring("abc", 0, 1))\n return 0\nend fn\n',
                 primary, repository)
        discover('book discovery\nread everything from ny.lang.text\nmain returns override integer begin paragraph\n'
                 ' say substring with s being "abc", start being 0, length being 1\n give 0\nend paragraph\n',
                 root / 'discovery.matrix', repository)
        # Preserve source SDKs installed with the old namespace directory and
        # conventional share/ny/stdlib prefix. No prelude marker is required.
        legacy_prefix = root / 'legacy sdk'
        legacy_module = legacy_prefix / 'share/ny/stdlib/ny/lang'
        legacy_module.mkdir(parents=True)
        (legacy_module / 'discovery.ny').write_text(
            'module ny.lang.discovery\npub fn answer() -> i32\n return 7\nend fn\n')
        discover('import ny.lang.discovery\nfn main() -> i32\n return answer()\nend fn\n',
                 primary, legacy_prefix)

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
        assert imported['sources'] == [{'path': str(dep), 'text': dep.read_text()}]
        # Snapshots are exact, including CRLF and Unicode. Primary input remains
        # the caller's buffer; imported source is the bytes actually checked.
        dep_source = 'module dep\r\n# λ🙂\r\npub fn broken() -> i32\r\n return "bad"\r\nend fn\r\n'
        dep.write_bytes(dep_source.encode())
        imported = analyze('import dep.*\nfn main() -> i32\n return broken()\nend fn\n', status=1)
        assert imported['sources'] == [{'path': str(dep), 'text': dep_source}]
        # Limit encoded JSON, not raw source bytes: a long backslash comment
        # doubles during JSON escaping and must not consume the source budget.
        dep.write_text('#' + '\\' * 600000 + '\nmodule dep\npub fn broken() -> i32\n return "bad"\nend fn\n')
        imported = analyze('import dep.*\nfn main() -> i32\n return broken()\nend fn\n', status=1)
        assert imported['sources'] == []
        assert any(d['path'] == str(dep) and d['start'] is not None for d in imported['diagnostics'])

        # The same typed AST rules apply to both surfaces. A retained call and
        # unwrapped call are handled; plain scalar/void calls are not outcomes.
        ny_rules = '''import nil.std.core
import nil.std.text
fn ordinary() -> i32
 return 1
end fn
fn partial(flag: bool) -> i32
 if flag then
  return 1
 end if
 return 2
end fn
fn total(flag: bool) -> i32
 if flag then
  return 1
 else
  return 2
 end if
 println("after both exits")
end fn
fn main() -> i32
 try_char_at("abc", 0)
 Convert.try_to_i32("1")
 let kept = try_char_at("abc", 0)
 try_char_at("abc", 0).unwrap()
 ordinary()
 println(kept.unwrap())
 return 0
 println("after return")
end fn
'''
        matrix_rules = '''book rules
read everything from nil.std.core
read everything from nil.std.text
ordinary returns override integer begin paragraph
 give 1
end paragraph
partial receives flag as boolean returns override integer begin paragraph
 when flag then
  give 1
 end when
 give 2
end paragraph
total receives flag as boolean returns override integer begin paragraph
 when flag then
  give 1
 otherwise
  give 2
 end when
 say "after both exits"
end paragraph
main returns override integer begin paragraph
 evaluate try-char-at with s being "abc", index being 0
 evaluate Convert.try-to-i32 with "1"
 value kept is try-char-at with s being "abc", index being 0
 evaluate (try-char-at with s being "abc", index being 0).unwrap with nothing
 evaluate ordinary with nothing
 say (kept.unwrap with nothing)
 give 0
 say "after return"
end paragraph
'''
        for source, path in ((ny_rules, primary), (matrix_rules, matrix)):
            data = analyze(source, path=path)
            codes = [d['code'] for d in data['diagnostics']]
            assert codes.count('NIL2001') == 2 and codes.count('NIL2002') == 2, data
            assert len(codes) == 4 and data['reflection']['semantic_checked']
            assert all(d['path'] == str(path) and d['start'] is not None and d['fix'] is None
                       for d in data['diagnostics'])
            promoted = analyze(source, path=path, extra=('--warnings-as-errors',), status=1)
            assert promoted['reflection']['semantic_checked']
            assert all(d['severity'] == 'error' for d in promoted['diagnostics'])
            suppressed = analyze(source, path=path, extra=('--disable-rule', 'NIL2001',
                                '--disable-rule', 'NIL2002', '--warnings-as-errors'))
            assert suppressed['diagnostics'] == []

        # Custom nominal enums with the same leaf name are not standard outcomes.
        custom = '''module custom
enum Option<T>
 Some(T)
 None
end enum
enum Result<T, E>
 Ok(T)
 Err(E)
end enum
fn optional() -> Option<i32>
 return Option.Some(1)
end fn
fn result() -> Result<i32, text>
 return Result.Ok(1)
end fn
fn main() -> i32
 optional()
 result()
 return 0
end fn
'''
        assert analyze(custom)['diagnostics'] == []
        loop_exits = '''fn main() -> i32
 while true do
  break
  println("after break")
 end while
 println("after loop is reachable")
 while false do
  continue
  println("after continue")
 end while
 return 0
end fn
'''
        data = analyze(loop_exits)
        assert [d['code'] for d in data['diagnostics']] == ['NIL2001', 'NIL2001'], data
        matrix_loops = '''book loops
main returns override integer begin paragraph
 repeat while true
  stop repeating
  say "after break"
 end repeat
 say "after loop is reachable"
 repeat while false
  continue repeating
  say "after continue"
 end repeat
 give 0
end paragraph
'''
        data = analyze(matrix_loops, path=matrix)
        assert [d['code'] for d in data['diagnostics']] == ['NIL2001', 'NIL2001'], data
        fault_exits = '''struct Problem
 code: i32
end struct
fn main() -> i32
 attempt
  raise Problem { code: 1 }
  println("after raise")
 fault Problem as problem
  println(problem.code)
 recover
  println("cleanup remains reachable")
 end attempt
 println("after handled fault remains reachable")
 return 0
end fn
'''
        matrix_faults = '''book faults
Problem begin record
 field code as override integer
end record
main returns override integer begin paragraph
 attempt
  raise Problem with code being 1
  say "after raise"
 caught Problem as problem
  say problem.code
 always
  say "cleanup remains reachable"
 end attempt
 say "after handled fault remains reachable"
 give 0
end paragraph
'''
        for source, path in ((fault_exits, primary), (matrix_faults, matrix)):
            data = analyze(source, path=path)
            assert [d['code'] for d in data['diagnostics']] == ['NIL2001'], data
        # Include expansion has no safe raw-source line mapping. The rule is
        # still reported, but no borrowed offsets or fix are exposed.
        (root / 'included.ny').write_text('fn included() -> i32\n return 1\n println("dead")\nend fn\n')
        data = analyze('@include "included.ny"\nfn main() -> i32\n return included()\nend fn\n')
        assert len(data['diagnostics']) == 1, data
        assert data['diagnostics'][0]['code'] == 'NIL2001'
        assert data['diagnostics'][0]['start'] is None and data['diagnostics'][0]['length'] is None
        assert data['diagnostics'][0]['fix'] is None
        # Dependencies are checked but default style/semantic rules target only
        # the entry document, so imported SDK implementation details stay quiet.
        dep.write_text('module dep\npub fn fine() -> i32\n return 1\n println("dead")  \nend fn')
        data = analyze('import dep.*\nfn main() -> i32\n return fine()\nend fn\n')
        assert data['diagnostics'] == [] and data['sources'] == []
        # Rules do not run on an unsuccessfully checked tree.
        data = analyze('fn main() -> i32\n return "bad"\n println("dead")\nend fn\n', status=1)
        assert not any(d['code'].startswith('NIL2') for d in data['diagnostics'])
        promoted = analyze(unicode_source, extra=('--warnings-as-errors',), status=1)
        assert promoted['reflection']['semantic_checked']
        assert all(d['severity'] == 'error' and d['fix'] for d in promoted['diagnostics'])
        assert analyze(unicode_source, extra=('--disable-rule', 'NIL1001', '--disable-rule', 'NIL1002',
                                             '--warnings-as-errors'))['diagnostics'] == []

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

        for flags in [('--native',), ('--jit',), ('--no-typecheck',), ('-o', 'bad.nil'), ('--', 'arg'),
                      ('--disable-rule',), ('--disable-rule', 'NIL0002'), ('--disable-rule', 'NIL9999'),
                      ('--list-analysis-rules',)]:
            proc = subprocess.run([args.compiler, '--analyze', str(primary), *flags],
                                  text=True, capture_output=True, timeout=20, cwd=root)
            assert proc.returncode == 2 and not proc.stdout, (flags, proc)
        collision = subprocess.run([args.compiler, '--analyze', '--source-stdin', str(primary),
                                    '--emit-reflection', str(primary)], input=good,
                                   text=True, capture_output=True, timeout=20, cwd=root)
        assert collision.returncode == 2 and not collision.stdout
        assert primary.read_text() == 'this is deliberately invalid on disk'

        # A workspace request is a single immutable source view. Only imports
        # selected by ordinary candidate/root ordering enter the graph.
        workspace_root = root / 'workspace with spaces'
        workspace_root.mkdir()
        workspace_primary = workspace_root / 'entry.ny'
        workspace_dep = workspace_root / 'dependency.ny'
        workspace_primary.write_text('invalid entry on disk')
        workspace_dep.write_text('invalid dependency on disk')
        entry_source = 'import dependency.*\nfn main() -> i32\n return answer()\nend fn\n'
        dependency_source = 'module dependency\npub fn answer() -> i32\n return 42\nend fn\n'

        def workspace(sources, *, path=workspace_primary, extra=(), status=0, raw=None, error=None):
            request = raw if raw is not None else json.dumps({
                'schema': 'nil.analysis.workspace.v1',
                'sources': [{'path': str(p), 'text': text} for p, text in sources],
            }, ensure_ascii=False).encode()
            proc = subprocess.run([args.compiler, '--analyze', '--workspace-stdin', '--no-stdlib',
                                   '-I', args.stdlib, str(path), *extra], input=request,
                                  capture_output=True, timeout=20, cwd=root)
            assert proc.returncode == status, (proc.returncode, proc.stdout, proc.stderr)
            if status == 2:
                assert not proc.stdout and proc.stderr.startswith(b'Analysis error:'), proc
                if error:
                    assert error in proc.stderr.decode(), proc.stderr
                return None
            assert not proc.stderr, proc.stderr
            value = json.loads(proc.stdout)
            assert value['schema'] == 'nil.analysis.v1' and value['source'] == str(path), value
            assert value['success'] == (status == 0), value
            assert len(json.dumps(value['sources'], ensure_ascii=False, separators=(',', ':')).encode()) <= 1024 * 1024
            return value

        buffers = [(workspace_primary, entry_source), (workspace_dep, dependency_source)]
        data = workspace(buffers)
        assert data['reflection']['semantic_checked'] and not data['diagnostics'], data
        assert workspace_primary.read_text() == 'invalid entry on disk'
        assert workspace_dep.read_text() == 'invalid dependency on disk'
        assert not (root / 'analysis-unused.nil').exists()
        # The imported snapshot is exactly the dirty text, including UTF-8 and
        # CRLF. Fixing it without saving removes the dependent diagnostic.
        dirty_dependency = dependency_source.replace('\n', '\r\n').replace('return 42', 'return "λ🙂"')
        data = workspace([(workspace_primary, entry_source), (workspace_dep, dirty_dependency)], status=1)
        assert data['sources'] == [{'path': str(workspace_dep), 'text': dirty_dependency}], data
        assert any(d['path'] == str(workspace_dep) and d['start'] is not None for d in data['diagnostics'])
        assert not workspace(buffers)['diagnostics']
        # An unrelated broken buffer is neither parsed nor reflected.
        unused = workspace_root / 'unrelated.ny'
        data = workspace(buffers + [(unused, 'deliberately malformed source')])
        assert not data['diagnostics'] and all(d['source_path'] != str(unused) for d in data['reflection']['declarations'])
        # A new Matrix module in a directory that does not exist participates
        # in Ny imports; the reciprocal frontend direction uses the same view.
        fresh = workspace_root / 'new' / 'package.matrix'
        matrix_dependency = 'book new\npublic answer returns override integer begin paragraph\n give 42\nend paragraph\n'
        data = workspace([(workspace_primary, entry_source.replace('dependency.*', 'new.*')),
                          (fresh, matrix_dependency)])
        assert data['reflection']['semantic_checked'] and not fresh.exists(), data
        matrix_entry = workspace_root / 'entry.matrix'
        matrix_source = 'book consumer\nread everything from dependency\nmain returns override integer begin paragraph\n give answer with nothing\nend paragraph\n'
        assert not workspace([(matrix_entry, matrix_source), (workspace_dep, dependency_source)],
                             path=matrix_entry)['diagnostics']
        # Existing .ny candidates still win before a new .matrix alternative.
        workspace_dep.write_text(dependency_source)
        data = workspace([(workspace_primary, entry_source),
                          (workspace_dep.with_suffix('.matrix'), 'invalid but unselected')])
        assert not data['diagnostics'], data
        earlier = workspace_root / 'earlier'
        later = workspace_root / 'later'
        earlier.mkdir()
        (earlier / 'ordered.ny').write_text(dependency_source.replace('dependency', 'ordered'))
        data = workspace([(workspace_primary, entry_source.replace('dependency', 'ordered')),
                          (later / 'ordered.ny', 'invalid but later in search order')],
                         extra=('-I', str(earlier), '-I', str(later)))
        assert not data['diagnostics'], data
        (earlier / 'ordered.ny').unlink()
        (earlier / 'ordered.ny').symlink_to('ordered.ny')
        data = workspace([(workspace_primary, entry_source.replace('dependency', 'ordered')),
                          (later / 'ordered.ny', dependency_source.replace('dependency', 'ordered'))],
                         extra=('-I', str(earlier), '-I', str(later)))
        assert not data['diagnostics'], data
        # Source candidates remain ahead of binary candidates. Binary-only
        # modules remain available and synthetic identities cannot be overlaid.
        library_dir = workspace_root / 'libraries'
        library_dir.mkdir()
        library_source = workspace_root / 'library_source.ny'
        library_source.write_text(dependency_source.replace('dependency', 'binary_dependency'))
        library = library_dir / 'binary_dependency.nil'
        compiled = subprocess.run([args.compiler, '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                                   '--emit-library', '-q', '-o', str(library), str(library_source)],
                                  text=True, capture_output=True, timeout=20, cwd=root)
        assert compiled.returncode == 0, compiled.stderr
        library_entry = entry_source.replace('dependency', 'binary_dependency')
        assert not workspace([(workspace_primary, library_entry)], extra=('-I', str(library_dir)))['diagnostics']
        overlay_library_source = dependency_source.replace('dependency', 'binary_dependency').replace('return 42', 'return "bad"')
        data = workspace([(workspace_primary, library_entry),
                          (library_dir / 'binary_dependency.ny', overlay_library_source)],
                         extra=('-I', str(library_dir)), status=1)
        assert any(d['path'] == str(library_dir / 'binary_dependency.ny') for d in data['diagnostics']), data
        large_library_source = workspace_root / 'large_library_source.ny'
        large_library_source.write_text('#' + 'x' * (2 * 1024 * 1024) + '\n' +
                                        dependency_source.replace('dependency', 'large_binary'))
        compiled = subprocess.run([args.compiler, '--no-stdlib', '--no-bundle', '--emit-binary-nil',
                                   '--emit-library', '-q', '-o', str(library_dir / 'large_binary.nil'),
                                   str(large_library_source)], text=True, capture_output=True, timeout=20, cwd=root)
        assert compiled.returncode == 0, compiled.stderr
        workspace([(workspace_primary, entry_source.replace('dependency', 'large_binary'))],
                  extra=('-I', str(library_dir)), status=2, error='2 MiB')
        # Linked parent directories and primary-file aliases retain the
        # invocation identity while imports use one canonical snapshot.
        alias_dir = root / 'workspace alias'
        alias_dir.symlink_to(workspace_root, target_is_directory=True)
        alias_primary = alias_dir / 'entry.ny'
        assert not workspace([(alias_primary, entry_source), (alias_dir / 'dependency.ny', dependency_source)],
                             path=alias_primary)['diagnostics']
        fresh_alias = alias_dir / 'another' / 'package.ny'
        data = workspace([(alias_primary, entry_source.replace('dependency', 'another')),
                          (fresh_alias, dependency_source.replace('dependency', 'another'))], path=alias_primary)
        assert not data['diagnostics'] and not fresh_alias.exists()
        workspace(buffers + [(alias_dir / 'dependency.ny', dependency_source)], status=2,
                  error='duplicate canonical')
        # Active textual includes use the same authoritative source provider,
        # including new/nested files. Their flattened positions remain unmapped.
        include_file = workspace_root / 'included.ny'
        nested_file = workspace_root / 'nested.ny'
        include_file.write_text('invalid include on disk')
        include_entry = '@include "included.ny"\nfn main() -> i32\n return included()\nend fn\n'
        included = 'fn included() -> i32\n return 1\n println("dead")\nend fn\n'
        data = workspace([(workspace_primary, include_entry), (include_file, '@include "nested.ny"\n'),
                          (nested_file, included)])
        assert [d['code'] for d in data['diagnostics']] == ['NIL2001'], data
        assert all(d['start'] is None and d['length'] is None and d['fix'] is None for d in data['diagnostics'])
        assert not nested_file.exists() and include_file.read_text() == 'invalid include on disk'
        # Oversized/nonregular include targets cannot hang or allocate through
        # the ordinary preprocessor disk reader.
        include_file.write_bytes(b'#' + b'x' * (2 * 1024 * 1024))
        workspace([(workspace_primary, include_entry)], status=2, error='cannot read analysis source')
        if hasattr(os, 'mkfifo'):
            fifo = workspace_root / 'pipe.ny'
            os.mkfifo(fifo)
            data = workspace([(workspace_primary, include_entry.replace('included.ny', 'pipe.ny'))], status=1)
            assert any('Included file not found' in d['message'] for d in data['diagnostics'])
        depth_buffers = [(workspace_primary, '@include "depth0.ny"\n')]
        depth_buffers.extend((workspace_root / f'depth{i}.ny', f'@include "depth{i + 1}.ny"\n') for i in range(33))
        depth_buffers.append((workspace_root / 'depth33.ny', '\n'))
        workspace(depth_buffers, status=2, error='include levels')
        empty_include = workspace_root / 'empty.ny'
        workspace([(workspace_primary, '@include "empty.ny"\n' * 1025), (empty_include, '')],
                  status=2, error='include visits')
        large_comment = '#' + 'x' * (2 * 1024 * 1024 - 2) + '\n'
        workspace([(workspace_primary, '@include "included.ny"\n' * 5), (include_file, large_comment)],
                  status=2, error='preprocessor expansion')
        workspace([(workspace_primary, '@define BIG ' + 'x' * (1024 * 1024) + '\n' + 'BIG ' * 9 + '\n')],
                  status=2, error='macro expansion')
        workspace([(workspace_primary, '@define DUP(x) ' + 'x ' * 10 + '\nDUP(' + 'y' * (1024 * 1024) + ')\n')],
                  status=2, error='macro expansion')
        aggregate_entry = ''
        for index in range(9):
            name = f'budget{index}.ny'
            (workspace_root / name).write_text('#' + 'x' * (1024 * 1024 - 2) + '\n')
            aggregate_entry += f'@include "{name}"\n'
        workspace([(workspace_primary, aggregate_entry)], status=2, error='source reads exceed 8 MiB')

        # Request validation is fail-closed and never writes response JSON or
        # files for malformed input, contradictory options, or aliases.
        minimal = {'schema': 'nil.analysis.workspace.v1', 'sources': [{'path': str(workspace_primary), 'text': good}]}
        malformed = [b'{', b'[]', b'{"schema":"nil.analysis.workspace.v1","schema":"nil.analysis.workspace.v1","sources":[]}',
                     b'{"schema":"nil.analysis.workspace.v1","sources":[],"extra":true}',
                     json.dumps({**minimal, 'schema': 'nil.analysis.workspace.v2'}).encode(),
                     json.dumps({**minimal, 'sources': []}).encode(),
                     json.dumps({**minimal, 'sources': [{'path': str(workspace_primary), 'text': good, 'extra': 1}]}).encode(),
                     json.dumps({**minimal, 'sources': [{'path': str(workspace_primary), 'text': 1}]}).encode(),
                     json.dumps({**minimal, 'sources': [{'path': str(workspace_primary), 'text': '\ud800'}]}).encode(),
                     json.dumps(minimal).encode() + b'\xff',
                     b' ' * (8 * 1024 * 1024 + 1)]
        for raw in malformed:
            workspace([], raw=raw, status=2)
        for invalid_path in ('relative.ny', 'nillib://module/source.ny', str(workspace_root / 'module.nil'),
                             str(workspace_root / ('x' * 4096 + '.ny')), str(workspace_root / 'nul\0.ny')):
            workspace([(invalid_path, good)], status=2)
        workspace([(workspace_dep, dependency_source)], status=2, error='primary source')
        workspace(buffers + [buffers[0]], status=2, error='duplicate canonical')
        workspace([(workspace_primary, '\0')], status=2, error='NUL byte')
        workspace([(workspace_primary, 'x' * (2 * 1024 * 1024 + 1))], status=2, error='2 MiB')
        workspace([(workspace_root / f'file{i}.ny', '') for i in range(257)], status=2, error='256 sources')
        directory_source = workspace_root / 'directory.ny'
        directory_source.mkdir()
        workspace([(directory_source, good)], path=directory_source, status=2, error='regular file')
        workspace(buffers, extra=('--source-stdin',), status=2, error='mutually exclusive')
        workspace(buffers + [(unused, 'ignored')], extra=('--emit-reflection', str(unused)), status=2,
                  error='overwrite an analyzed source')
        assert not unused.exists()
    print('analysis protocol, workspace snapshots, default rules, policies, reflection and no-execution checks passed')


if __name__ == '__main__':
    main()
