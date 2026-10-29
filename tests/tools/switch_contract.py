#!/usr/bin/env python3
"""Fallthrough, result values, diagnostics, ownership and source-debugging parity."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    for tool in ('compiler', 'interpreter', 'debugger'):
        parser.add_argument('--' + tool, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    opts = parser.parse_args()
    compiler, interpreter, debugger = (getattr(opts, tool).resolve() for tool in ('compiler', 'interpreter', 'debugger'))
    environment = {key: value for key, value in os.environ.items() if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil-switch-') as directory:
        work = Path(directory)

        def run(command, status=0):
            result = subprocess.run(list(map(str, command)), cwd=work, env=environment,
                                    capture_output=True, text=True, timeout=120)
            if result.returncode != status:
                for line in result.stdout.splitlines():
                    if line.startswith('Report: '):
                        report = json.loads(Path(line[8:]).read_text())
                        for route in report['results']:
                            if route['status'] == 'FAIL':
                                print(json.dumps(route['commands'], indent=2), file=sys.stderr)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def routes(source, expected):
            modes = 'all' if opts.llvm == 'ON' else 'text-interpreter,binary-interpreter,bundle-text,bundle-binary'
            run([sys.executable, ROOT / 'tests/routes/ny_route_matrix.py', '--compiler', compiler,
                 '--interpreter', interpreter, '--source', source, '--expected-stdout', expected,
                 '--expected-exit', '0', '--compiler-arg=--no-stdlib', '--routes', modes,
                 '--work-dir', work / (source.name + '-routes'), '--strict'])

        for language in ('ny', 'matrix'):
            source = ROOT / ('tests/language/fallthrough-switch.' + language)
            routes(source, ROOT / 'tests/language/fallthrough-switch.stdout')
            checks += 6 if opts.llvm == 'ON' else 4

        character_source = work / 'characters.ny'
        character_source.write_text("fn main() -> i32\nprintln(switch 'A' { case 'A': break 65; default: break 0; })\nprintln(switch 10 { case '\\n': break 10; default: break 0; })\nreturn 0\nend fn\n")
        character_expected = work / 'characters.stdout'
        character_expected.write_text('65\n10\n')
        routes(character_source, character_expected)
        checks += 6 if opts.llvm == 'ON' else 4

        mask_source = work / 'mask-labels.ny'
        mask_source.write_text("fn main() -> i32\nprintln(switch 1 { case -0xFFFFFFFF: break 1; case -4294967295: break 2; default: break 0; })\nreturn 0\nend fn\n")
        mask_expected = work / 'mask-labels.stdout'
        mask_expected.write_text('1\n')
        routes(mask_source, mask_expected)
        checks += 6 if opts.llvm == 'ON' else 4

        forward_source = work / 'forward-enum.ny'
        forward_source.write_text("fn choose(input: Choice) -> i32\nreturn switch input { case Choice::A: break 1; default: break 0; }\nend fn\nenum Choice\nA\nB\nend enum\nfn main() -> i32\nprintln(choose(Choice::A))\nreturn 0\nend fn\n")
        forward_expected = work / 'forward-enum.stdout'
        forward_expected.write_text('1\n')
        routes(forward_source, forward_expected)
        checks += 6 if opts.llvm == 'ON' else 4

        # Contextual control words must preserve ordinary declarations/calls.
        names = {
            'ny': 'fn switch(value: i32) -> i32\nreturn value + 1\nend fn\nfn main() -> i32\nlet case = 3\nlet default = 4\nprintln(switch(case) + default)\nreturn 0\nend fn\n',
            'matrix': 'book contextual-names\nswitch receives input as number returns number begin paragraph\ngive input plus 1\nend paragraph\nmain returns override integer begin paragraph\nvalue case is 3\nvalue default is 4\nsay (switch with input being case) plus default\ngive 0\nend paragraph\n',
        }
        expected_names = work / 'names.stdout'
        expected_names.write_text('8\n')
        for language, contents in names.items():
            source = work / ('names.' + language)
            source.write_text(contents)
            routes(source, expected_names)
            checks += 6 if opts.llvm == 'ON' else 4

        ny = lambda body: 'fn main() -> i32\n' + body + '\nreturn 0\nend fn\n'
        matrix = lambda body: 'book invalid-switch\nmain returns override integer begin paragraph\n' + body + '\ngive 0\nend paragraph\n'
        invalid = {
            'duplicate-character.ny': ny("switch 65 { case 'A': break; case 65: break; }"),
            'duplicate-enum.ny': 'enum State\nIdle\nBusy\nend enum\n' + ny('let state: State = State::Idle\nswitch state { case State::Idle: break; case State.Idle: break; }'),
            'foreign-enum.ny': 'enum State\nIdle\nend enum\nenum Other\nIdle\nend enum\n' + ny('let state: State = State::Idle\nswitch state { case Other::Idle: break; }'),
            'payload-enum.ny': 'enum State\nIdle\nBusy(i32)\nend enum\n' + ny('let state: State = State::Idle\nswitch state { case State::Idle: break; }'),
            'duplicate-mask.ny': ny('switch -1 { case 0xFFFFFFFF: break; case -1: break; }'),
            'cleanup-outer-break.ny': ny('while true\nattempt\nprintln(100 + switch 1 { case 1: return 77; default: break 2; })\nrecover\nbreak\nend attempt\nend while'),
            'cleanup-outer-switch.ny': ny('switch 1 { default: attempt\nprintln(100 + switch 1 { case 1: return 77; default: break 2; })\nrecover\nbreak\nend attempt\n}'),
            'cleanup-outer-break.matrix': matrix('repeat forever\nattempt\nsay 100 plus switch 1\ncase 1 then\ngive 77\notherwise\nstop switching with 2\nend switch\nalways\nstop repeating\nend attempt\nend repeat'),
            'outer-continue.ny': ny('let mut i = 0\nwhile i < 2\ni = i + 1\nprintln(100 + switch i { case 1: continue; default: break 2; })\nend while'),
            'outer-loop-break.matrix': matrix('repeat forever\nsay 100 plus switch 1\ncase 1 then\nstop repeating\notherwise\nstop switching with 2\nend switch\nend repeat'),
            'outer-continue.matrix': matrix('repeat forever\nsay 100 plus switch 1\ncase 1 then\ncontinue repeating\notherwise\nstop switching with 2\nend switch\nend repeat'),
            'duplicate.ny': ny('switch 1 { case 1: break; case 0x1: break; }'),
            'defaults.ny': ny('switch 1 { default: break; default: break; }'),
            'label-type.ny': ny('switch 1 { case "1": break; }'),
            'dynamic-label.ny': ny('let key = 1\nswitch 1 { case key: break; }'),
            'floating-selector.ny': ny('switch 1.5 { case 1: break; }'),
            'floating-label.ny': ny('switch 1 { case 1.0: break; }'),
            'aggregate-selector.ny': ny('switch [1] { case 1: break; }'),
            'missing-default.ny': ny('let result = switch 1 { case 1: break 2; }'),
            'missing-result.ny': ny('let result = switch 1 { case 1: break 2; default: println(3); }'),
            'bare-result-break.ny': ny('let result = switch 1 { default: break; }'),
            'statement-result.ny': ny('switch 1 { default: break 2; }'),
            'loop-result.ny': ny('let result = switch 1 { default: while true\nbreak 2\nend while\nbreak 3; }'),
            'result-type.ny': ny('let result = switch 1 { case 1: break "text"; default: break 2; }'),
            'partial-result.ny': ny('let result = switch 1 { default: if false then\nbreak 2\nend if\n}'),
            'scope.ny': ny('switch 1 { case 1: let hidden = 7\ncase 2: println(hidden)\n}'),
            'continue.ny': ny('switch 1 { case 1: continue; }'),
            'lambda-exit.ny': ny('while true\nlet callback = fn() -> i32\nbreak\nreturn 1\nend fn\nbreak\nend while'),
            'switch-lambda-exit.ny': ny('switch 1 { default: let callback = fn() -> i32\nbreak\nreturn 1\nend fn\n}'),
            'owner-move.ny': ny('let storage = owned<i32>(1)\nswitch 1 { case 1: let moved = owned_move(storage)\ncase 2: println(storage[0])\n}'),
            'owner-result.ny': ny('let storage = owned<i32>(1)\nlet result = switch 1 { default: break storage; }'),
            'before-case.ny': ny('switch 1 { println(1)\ncase 1: break; }'),
            'duplicate.matrix': matrix('switch 1\ncase 1 then\nstop switching\ncase 1 then\nstop switching\nend switch'),
            'defaults.matrix': matrix('switch 1\notherwise\nstop switching\notherwise\nstop switching\nend switch'),
            'missing-default.matrix': matrix('value result is switch 1\ncase 1 then\nstop switching with 2\nend switch'),
            'missing-result.matrix': matrix('value result is switch 1\notherwise\nsay 1\nend switch'),
            'bare-result-break.matrix': matrix('value result is switch 1\notherwise\nstop switching\nend switch'),
            'statement-result.matrix': matrix('switch 1\notherwise\nstop switching with 2\nend switch'),
            'loop-only.matrix': matrix('switch 1\notherwise\nstop repeating\nend switch'),
            'outside-switch.matrix': matrix('stop switching'),
            'scope.matrix': matrix('switch 1\ncase 1 then\nvalue hidden is 7\ncase 2 then\nsay hidden\nend switch'),
            'label-type.matrix': matrix('switch 1\ncase "1" then\nstop switching\nend switch'),
            'before-case.matrix': matrix('switch 1\nsay 1\ncase 1 then\nstop switching\nend switch'),
        }
        for name, source in invalid.items():
            path = work / name
            path.write_text(source)
            for flags in ([], ['--emit-binary-nil'], ['--no-typecheck']):
                output = work / 'prior.nil'
                output.write_bytes(b'prior artifact')
                run([compiler, '--quiet', '--no-stdlib', '--no-bundle', *flags, '-o', output, path], status=1)
                assert output.read_bytes() == b'prior artifact', (name, flags)
                checks += 1

        # Matrix's explicit target must distinguish a surrounding loop from
        # the surrounding switch, including an exit across an inner loop.
        explicit = work / 'explicit-targets.matrix'
        explicit.write_text('''book explicit-targets
main returns override integer begin paragraph
    variable total is 0
    repeat forever
        switch 1
            case 1 then
                set total to 7
                stop repeating
        end switch
        set total to 99
    end repeat
    say total
    value result is switch 1
        otherwise
            repeat forever
                stop switching with 8
            end repeat
            stop switching with 99
    end switch
    say result
    value inner is 100 plus switch 1
        otherwise
            variable index is 0
            repeat while index is less than 2
                set index to index plus 1
                continue repeating
            end repeat
            stop switching with index
    end switch
    say inner
    give 0
end paragraph
''')
        expected = work / 'explicit.stdout'
        expected.write_text('7\n8\n102\n')
        routes(explicit, expected)
        checks += 6 if opts.llvm == 'ON' else 4

        # Case breaks end their case bodies, not the enclosing function; the
        # same analyzer must also find unreachable code in switch expressions.
        source = work / 'analyzer.ny'
        source.write_text('''fn unused(value: i32 = switch 1 {
 default:
  break 4;
  println("unreachable default");
 }) -> i32
 return value
end fn
fn main() -> i32
 switch 1 {
 case 1:
  break;
  println("unreachable statement");
 }
 println("reachable after switch")
 let result = switch 1 {
 default:
  break 2;
  println("unreachable expression");
 }
 println(result)
 return 0
end fn
''')
        report = json.loads(run([compiler, '--analyze', '--no-stdlib', source]).stdout)
        warnings = [d for d in report['diagnostics'] if d['code'] == 'NIL2001']
        assert len(warnings) == 3, report
        contents = source.read_text()
        assert all(contents[d['start']:d['start'] + d['length']].lstrip().startswith('println("unreachable') for d in warnings), warnings
        checks += 1

        spec = importlib.util.spec_from_file_location('switch_dap', ROOT / 'nildebugger/tests/dap_protocol.py')
        dap = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(dap)
        debug_sources = {
            'ny': 'module switch_debug\nfn main() -> i32\n let mut total: i32 = 0\n switch 2\n case 1:\n total = 99\n case 2:\n let scoped = 10\n total = scoped\n case 3:\n total = total + 20\n break\n default:\n total = 99\n end switch\n println(total)\n return 0\nend fn\n',
            'matrix': 'book switch-debug\nmain returns override integer begin paragraph\n variable total as override integer is 0\n switch 2\n case 1 then\n set total to 99\n case 2 then\n value scoped is 10\n set total to scoped\n case 3 then\n set total to total plus 20\n stop switching\n otherwise\n set total to 99\n end switch\n say total\n give 0\nend paragraph\n',
        }
        for language, contents in debug_sources.items():
            source = work / ('debug.' + language)
            source.write_text(contents)
            for binary in (False, True):
                output = work / ('debug-' + language + ('-binary.nil' if binary else '-text.nil'))
                dap.compile_program(compiler, output, source, binary=binary)
                client = dap.Client(debugger)
                try:
                    client.initialize(output, stop_on_entry=False)
                    points = client.breakpoints(source, 6, 9, 11, 16)
                    assert all(point['verified'] for point in points), points
                    client.request('configurationDone')
                    for line, total in ((9, '0'), (11, '10'), (16, '30')):
                        assert client.event('stopped')['reason'] == 'breakpoint'
                        frame = client.stack()[0]
                        assert frame['line'] == line and Path(frame['source']['path']) == source, frame
                        assert client.evaluate('total') == total
                        if line == 9:
                            assert client.evaluate('scoped') == '10'
                        else:
                            client.request('evaluate', {'expression': 'scoped'}, success=False)
                        client.request('continue', {'threadId': 1})
                    assert client.event('exited')['exitCode'] == 0
                    client.event('terminated')
                finally:
                    client.close()
                checks += 1
    print(f'switch contract: {checks} checks passed')


if __name__ == '__main__':
    main()
