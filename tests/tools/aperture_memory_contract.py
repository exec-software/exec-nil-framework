#!/usr/bin/env python3
"""Aperture records real VM/native memory activity without changing program output."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aperture', required=True, type=Path)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--interpreter', required=True, type=Path)
    parser.add_argument('--stdlib', required=True, type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    aperture, compiler, interpreter, stdlib = (p.resolve() for p in
        (args.aperture, args.compiler, args.interpreter, args.stdlib))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='aperture memory contract ') as directory:
        root = Path(directory)

        def run(command, status=0, env=None, input_text=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root,
                                    env=environment if env is None else env,
                                    stdin=subprocess.DEVNULL if input_text is None else None,
                                    input=input_text, capture_output=True,
                                    text=True, timeout=120)
            assert result.returncode == status, (command, result.returncode,
                                                result.stdout, result.stderr)
            checks += 1
            return result

        def validate(path, route, source=None):
            nonlocal checks
            profile = json.loads(path.read_text())
            assert profile['schema'] == 'aperture.memory.v1', profile
            assert profile['route'] == route, profile
            assert profile['completed'] and not profile['errors'], profile
            assert profile['measurement'] == 'runtime_logical_bytes', profile
            totals = profile['totals']
            assert totals['allocated_bytes'] > 0 and totals['allocated_objects'] > 0, profile
            for values in [totals, *profile['sites']]:
                for units in ('bytes', 'objects'):
                    assert values['allocated_' + units] - values['freed_' + units] == values['live_' + units], values
                    assert values['allocated_' + units] >= values['peak_' + units] >= values['live_' + units], values
            if source:
                assert any(site['file'] == str(source) and site['line'] > 0 and 'main' in site['function']
                           for site in profile['sites']), profile['sites']
            checks += 1
            return profile

        # Metacharacters are literal path components. A shell command assembled
        # from this path would attempt to create an unexpected marker file.
        source = root / 'memory $(touch INJECTED) source.ny'
        source.write_text('''fn main() -> i32
    let pointer = allocate<i32>(1)
    pointer[0] = 42
    println(pointer[0])
    deallocate(pointer)
    let values: array<i64> = [1 as i64]
    let mut index = 0
    while index < 64
        values.push(index as i64)
        index = index + 1
    end while
    let pair = (values, $"items-{values.len()}")
    println(pair.1)
    println(pair.0.len())
    return 0
end fn
''')
        expected = '42\nitems-65\n65\n'
        nil = root / 'compiled program.nil'
        run([compiler, '--quiet', '--debug-info', '--no-bundle', '--no-stdlib', '-o', nil, source])
        plain = run([interpreter, '--no-ai', nil])
        assert plain.stdout == expected and not plain.stderr, plain
        assert not list(root.glob('*.json')), list(root.iterdir())
        checks += 1
        reports = []
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            output = root / (route + ' memory report.json')
            command = [aperture, 'memory', '--output', output, '--sdk-path', stdlib]
            if route == 'native':
                command += ['--native']
            result = run([*command, source])
            assert result.stdout == expected, result
            assert 'Live at session end:' in result.stderr and 'not classified as leaks' in result.stderr, result
            profile = validate(output, route, source)
            assert profile['totals']['freed_objects'] >= 1, profile
            assert profile['totals']['resizes'] >= 1, profile
            assert profile['totals']['live_objects'] > 0, profile
            reports.append(output)
            inspection = run([aperture, 'memory', '--report', output, '--json'])
            assert json.loads(inspection.stdout) == profile and not inspection.stderr, inspection
            human = run([aperture, 'memory', '--report', output])
            assert str(source) in human.stdout and not human.stderr, human
            checks += 2
        if args.llvm == 'ON':
            executable = root / 'native $(touch INJECTED) executable'
            run([compiler, '--quiet', '--no-stdlib', '--native', '-o', executable, source])
            direct = run([executable])
            assert direct.stdout == expected and not direct.stderr, direct
            output = root / 'precompiled native.json'
            recorded = run([aperture, 'memory', '--output', output, executable])
            assert recorded.stdout == direct.stdout, recorded
            validate(output, 'native')
            checks += 1
        assert not (root / 'INJECTED').exists(), 'target path was interpreted by a shell'
        checks += 1

        # Text and binary NIL retain their debug-source identity, and JSON mode
        # leaves target stdout untouched while the saved file stays machine-readable.
        for encoding in ('text', 'binary'):
            artifact = root / (encoding + '.nil')
            flags = ['--emit-binary-nil'] if encoding == 'binary' else []
            run([compiler, '--quiet', '--debug-info', '--no-bundle', '--no-stdlib', *flags,
                 '-o', artifact, source])
            output = root / (encoding + '.json')
            result = run([aperture, 'memory', '--json', '--output', output, artifact])
            assert result.stdout == expected, result
            profile = validate(output, 'vm', source)
            assert json.loads(result.stderr) == profile, result.stderr
            checks += 1

        # Matrix uses the same report contract and source mapping.
        matrix = root / 'matrix source.MATRIX'
        matrix.write_text('''book memory_contract
main returns override integer begin paragraph
    value pair is (41, "matrix")
    value items is [pair, (42, "second")]
    say (items at 1).0
    say (items at 0).1
    give 0
end paragraph
''')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            output = root / ('matrix-' + route + '.json')
            flags = ['--native'] if route == 'native' else []
            result = run([aperture, 'memory', '--output', output, *flags, matrix])
            assert result.stdout == '42\nmatrix\n', result
            validate(output, route, matrix)

        # Scalar worker clones must remain isolated even when source-site
        # instrumentation is present in their ordinary callable originals.
        # Only the parent's input and output arrays belong to this capture.
        parallel = root / 'parallel workers.ny'
        parallel.write_text('fn square(value: i64) -> i64\nreturn value * value\nend fn\n'
            'fn transform(value: i64) -> i64\nreturn square(value) + 3\nend fn\n'
            'fn main() -> i32\n'
            'let inputs: array<i64> = [1 as i64, 2 as i64, 3 as i64, 4 as i64, '
            '5 as i64, 6 as i64, 7 as i64, 8 as i64]\n'
            'let outputs = parallel_map(transform, inputs)\n'
            'println(outputs[7])\nprintln(inputs[2])\nprintln(outputs.len())\n'
            'println(transform(5))\nreturn 0\nend fn\n')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            output = root / ('parallel-' + route + '.json')
            flags = ['--native'] if route == 'native' else []
            result = run([aperture, 'memory', '--output', output, *flags, parallel])
            assert result.stdout == '67\n3\n8\n28\n', result
            profile = validate(output, route, parallel)
            totals = profile['totals']
            assert totals['allocated_objects'] == totals['live_objects'] == 2, profile
            assert totals['freed_objects'] == totals['resizes'] == 0, profile
            assert all(site['function'] == 'main' and site['kind'].startswith('array')
                       and site['file'] == str(parallel) and site['line'] > 0
                       for site in profile['sites']), profile['sites']
            assert sum(site['allocated_objects'] for site in profile['sites']) == 2, profile
            assert any('parallel workers' in scope for scope in profile['scope']), profile
            if route == 'vm':
                # VM workers disable recording before opening a session.
                assert profile['excluded_sessions'] == 0, profile
            else:
                # Native workers initialize their own invocation contexts;
                # the first-session collector explicitly excludes those.
                assert profile['excluded_sessions'] > 0, profile
            checks += 2

        # Normal nonzero program exits are propagated with a usable report.
        failure = root / 'failure.ny'
        failure.write_text('fn main() -> i32\nlet values = [7]\nprintln(values[0])\nreturn 7\nend fn\n')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            output = root / ('failure-' + route + '.json')
            flags = ['--native'] if route == 'native' else []
            result = run([aperture, 'memory', '--output', output, *flags, failure], 7)
            assert result.stdout == '7\n', result
            validate(output, route, failure)

        # Collection accounting distinguishes reclaimed storage from rooted
        # live values. VM collection is requested explicitly in safe mode.
        gc_source = root / 'collection.ny'
        gc_source.write_text('import nil.std.runtime.*\n'
            'fn discarded() -> i32\nlet transient: array<i32> = [1, 2, 3]\nreturn transient[0]\nend fn\n'
            'fn main() -> i32\nlet retained: array<i32> = [42]\n'
            'println(discarded())\ncollect(2)\nprintln(retained[0])\nreturn 0\nend fn\n')
        gc_report = root / 'collection.json'
        gc_result = run([aperture, 'memory', '--memory-mode', 'safe', '--sdk-path', stdlib,
                         '--output', gc_report, gc_source])
        assert gc_result.stdout == '1\n42\n', gc_result
        gc_profile = validate(gc_report, 'vm', gc_source)
        assert gc_profile['collections']['count'] >= 1, gc_profile
        assert gc_profile['collections']['freed_objects'] >= 1, gc_profile
        assert gc_profile['collections']['freed_bytes'] >= 12, gc_profile
        assert gc_profile['totals']['live_objects'] >= 1, gc_profile
        checks += 1

        if args.llvm == 'ON':
            # Native collection runs at managed safepoints; the public
            # gc.collect intrinsic is not yet lowered on that route.
            fixture = Path(__file__).resolve().parents[1] / 'language/stdlib-native-primitives-lifetime.ny'
            native_gc = root / 'native collection.ny'
            native_gc.write_text(fixture.read_text())
            output = root / 'native-collection.json'
            result = run([aperture, 'memory', '--native', '--output', output, native_gc])
            assert result.stdout == fixture.with_suffix('.stdout').read_text(), result
            profile = validate(output, 'native', native_gc)
            assert profile['collections']['count'] >= 1, profile
            assert profile['collections']['freed_objects'] >= 1, profile
            assert profile['collections']['freed_bytes'] > 0, profile
            assert profile['totals']['live_objects'] >= 1, profile
            checks += 1

        owned = root / 'owned.ny'
        owned.write_text('fn main() -> i32\nif true then\nlet value = owned<i32>(4)\n'
                         'println(42)\nend if\nreturn 0\nend fn\n')
        fault = root / 'fault.ny'
        fault.write_text('struct Problem\ncode: i32\nend struct\nfn main() -> i32\n'
                         'let values = [42]\nprintln(values[0])\n'
                         'raise Problem { code: 7 }\nreturn 0\nend fn\n')
        explicit_exit = root / 'explicit exit.ny'
        explicit_exit.write_text('import nil.std.system.*\nfn main() -> i32\n'
                                 'let values = [42]\nprintln(values[0])\nexit(7)\nreturn 0\nend fn\n')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            flags = ['--native'] if route == 'native' else []
            output = root / ('owned-' + route + '.json')
            result = run([aperture, 'memory', '--output', output, *flags, owned])
            assert result.stdout == '42\n', result
            profile = validate(output, route, owned)
            assert profile['totals']['freed_objects'] >= 1 and profile['totals']['live_objects'] == 0, profile
            output = root / ('fault-' + route + '.json')
            result = run([aperture, 'memory', '--output', output, *flags, fault], 1)
            assert result.stdout == '42\n', result
            profile = json.loads(output.read_text())
            assert profile['status'] == 'failed' and not profile['completed'], profile
            assert profile['totals']['allocated_objects'] > 0, profile
            output = root / ('exit-' + route + '.json')
            result = run([aperture, 'memory', '--sdk-path', stdlib, '--output', output,
                          *flags, explicit_exit], 7)
            assert result.stdout == '42\n', result
            validate(output, route, explicit_exit)
            checks += 2

        forwarding = root / 'arguments and input.ny'
        forwarding.write_text('import nil.std.system.*\nimport nil.std.io.*\n'
            'fn main() -> i32\nlet values = [42]\nprintln(arg_count())\n'
            'println(arg(0))\nprintln(arg(1))\nprintln(arg(2))\n'
            'println(read_line())\nprintln(values[0])\nreturn 0\nend fn\n')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            flags = ['--native'] if route == 'native' else []
            output = root / ('arguments-' + route + '.json')
            result = run([aperture, 'memory', '--sdk-path', stdlib, '--output', output,
                          *flags, forwarding, '--', 'space argument', '', '$(touch INJECTED)'],
                         input_text='hello from stdin\n')
            expected_forwarding = '3\nspace argument\n\n$(touch INJECTED)\nhello from stdin\n42\n'
            assert result.stdout == expected_forwarding, result
            profile = validate(output, route, forwarding)
            if route == 'native':
                # The imported wrapper owns this allocation. Libraries without
                # source metadata still retain its function identity.
                assert any(site['kind'] == 'text' and 'read_line' in site['function']
                           and site['allocated_bytes'] == 17
                           for site in profile['sites']), profile
        assert not (root / 'INJECTED').exists(), 'program argument was interpreted by a shell'
        checks += 1

        # Console EOF is a target failure, with a usable incomplete profile.
        eof_source = root / 'console EOF.ny'
        eof_source.write_text('import nil.std.io.*\nfn main() -> i32\n'
            'let values = [42]\nprintln(values[0])\nprintln(read_line())\nreturn 0\nend fn\n')
        for route in ('vm', 'native') if args.llvm == 'ON' else ('vm',):
            output = root / ('console-eof-' + route + '.json')
            flags = ['--native'] if route == 'native' else []
            result = run([aperture, 'memory', '--sdk-path', stdlib, '--output', output,
                          *flags, eof_source], 1, input_text='')
            assert result.stdout == '42\n' and 'console.read_line' in result.stderr, result
            profile = json.loads(output.read_text())
            assert profile['status'] == 'failed' and not profile['completed'], profile
            assert profile['totals']['allocated_objects'] >= 1, profile
            checks += 1

        # The requested destination must be writable and fresh before target
        # execution. Existing files, target source, and symlink referents survive.
        sentinel = root / 'existing.json'
        sentinel.write_text('do not replace')
        alias = root / 'existing-link.json'
        alias.symlink_to(sentinel)
        for output in (sentinel, alias, root / 'absent-parent' / 'out.json', root):
            result = run([aperture, 'memory', '--output', output, source], 1)
            assert not result.stdout and 'cannot create output report' in result.stderr, result
        assert sentinel.read_text() == 'do not replace' and alias.is_symlink()
        checks += 1
        result = run([aperture, 'memory', root / 'missing.nil'], 1)
        assert 'not a regular file' in result.stderr, result
        invalid = root / 'invalid.nil'
        invalid.write_text('not valid NIL')
        failed_report = root / 'invalid.json'
        run([aperture, 'memory', '--output', failed_report, invalid], 1)
        # A loader failure can either carry an explicitly failed report or no
        # profile; it must never leave a successful fabricated measurement.
        if failed_report.exists():
            assert not json.loads(failed_report.read_text())['completed']
        checks += 1
        if os.name != 'nt':
            result = run([aperture, 'memory', '/bin/true'], 1)
            assert 'cannot read memory profile' in result.stderr, result
            checks += 1

        # Report inspection is strict and never executes a file's contents.
        original = json.loads(reports[0].read_text())
        malformed = root / 'malformed.json'
        mutations = [None, {'schema': 'aperture.memory.v999'}, {'schema': 'aperture.memory.v1'}]
        negative = json.loads(json.dumps(original))
        negative['totals']['allocated_bytes'] = -1
        mutations.append(negative)
        inconsistent = json.loads(json.dumps(original))
        inconsistent['totals']['live_bytes'] += 1
        mutations.append(inconsistent)
        for profile in mutations:
            malformed.write_text('not JSON' if profile is None else json.dumps(profile))
            result = run([aperture, 'memory', '--report', malformed], 1)
            assert not result.stdout, result
        malformed.write_text('{"schema":"aperture.memory.v1","schema":"duplicate"}')
        run([aperture, 'memory', '--report', malformed], 1)
        unfinished = json.loads(json.dumps(original))
        unfinished['completed'] = False
        unfinished['status'] = 'failed'
        malformed.write_text(json.dumps(unfinished))
        inspected = run([aperture, 'memory', '--json', '--report', malformed], 1)
        assert json.loads(inspected.stdout) == unfinished, inspected
        for command in ('explore', 'trace', 'agent'):
            result = run([aperture, command, source], 1)
            assert 'not implemented' in result.stderr and not result.stdout, result
        for flags in (['--unknown'], ['--native', nil], ['--report', reports[0], source],
                      ['--memory-mode', 'invalid', source], ['--output'], [source, 'extra']):
            run([aperture, 'memory', *flags], 1)
        print(f'Aperture memory contract: {checks} checks passed')


if __name__ == '__main__':
    main()
