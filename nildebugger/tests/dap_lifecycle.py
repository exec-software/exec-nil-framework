#!/usr/bin/env python3
"""DAP pause/resume and transport shutdown must preserve one execution owner."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import dap_contract_support as support

SOURCE = '''fn main() -> i32
    let mut value: i64 = 0
    println("ready")
    while true
        value = value + 1
    end while
    return 0
end fn
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--debugger', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output-parent', type=Path)
    args = parser.parse_args()
    debugger, compiler = args.debugger.resolve(strict=True), args.compiler.resolve(strict=True)
    root = support.output_directory(args.output_parent, 'nil-dap-lifecycle-')
    report = {'schema': 'nil.dap-lifecycle.v1', 'status': 'FAIL', 'commands': [], 'sessions': [],
              'tools': {str(path): support.digest(path) for path in (debugger, compiler)}}
    env = support.environment()
    try:
        source = root / 'loop.ny'
        source.write_text(SOURCE, encoding='utf-8', newline='\n')
        for binary in (False, True):
            name = 'binary' if binary else 'text'
            program = root / (name + '.nil')
            support.compile_program(compiler, source, program, report['commands'], env, binary=binary)

            def running(client):
                client.initialize(program)
                client.request('configurationDone')
                assert 'ready' in client.event('output')['output']

            def pause_resume(client):
                running(client)
                client.request('stackTrace', {'threadId': 1}, success=False)
                previous = -1
                for unused in range(5):
                    client.request('pause', {'threadId': 1})
                    client.stopped('pause')
                    value = int(client.evaluate('value'))
                    assert value >= previous
                    previous = value
                    client.request('continue', {'threadId': 1})
                client.request('terminate')
                client.event('terminated')
                client.request('continue', {'threadId': 1}, success=False)
                client.request('disconnect')
                client.natural_exit()
                assert client.record['stdin_open_at_exit']

            def disconnect_running(client):
                running(client)
                client.request('disconnect')
                client.natural_exit()
                assert client.record['stdin_open_at_exit']

            def disconnect_stopped(client):
                client.initialize(program, stop_on_entry=True)
                client.request('configurationDone')
                client.stopped('entry')
                client.request('disconnect')
                client.natural_exit()
                assert client.record['stdin_open_at_exit']

            def eof_running(client):
                running(client)
                client.owner.process.stdin.close()
                client.natural_exit()
                assert not client.record['stdin_open_at_exit']

            def malformed_running(client):
                running(client)
                client.send(b'Content-Length: -1\r\n\r\n')
                client.natural_exit(1)
                assert client.record['stdin_open_at_exit']

            def partial_running(client, partial):
                running(client)
                client.send(partial)
                client.owner.process.stdin.close()
                client.natural_exit(1)

            for label, action in [('pause-resume', pause_resume), ('disconnect-running', disconnect_running),
                    ('disconnect-stopped', disconnect_stopped), ('eof-running', eof_running),
                    ('malformed-running', malformed_running),
                    ('partial-header', lambda client: partial_running(client, b'Content-Length: 10\r')),
                    ('partial-body', lambda client: partial_running(client, b'Content-Length: 10\r\n\r\n{}'))]:
                support.session(debugger, root, report, name + '-' + label, env, action)
        report['status'] = 'PASS'
    except BaseException as error:
        report['error'] = str(error)
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'] + ': ' + str(root / 'report.json'))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
