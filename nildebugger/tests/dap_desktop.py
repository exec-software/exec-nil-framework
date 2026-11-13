#!/usr/bin/env python3
"""Real UI handles and queued events survive DAP source stops on process main.

This requires a usable desktop provider. Linux owns a short-lived private Xvfb;
Windows/macOS use an explicitly selected interactive test session. Programmatic
activation targets only the fixture's own button; no external input is sent.
Paused-window responsiveness or physical keyboard injection is not asserted.
"""
from __future__ import annotations

import argparse
from contextlib import nullcontext
import importlib.util
import json
from pathlib import Path
import sys

import dap_contract_support as support

SOURCE = '''book dap-desktop
read nil.std.ui
read nil.std.testing
read nil.std.threading
read nil.std.core.result
read nil.std.core.option

next-event receives surface as Window returns Event begin paragraph
    variable attempts as number is 0
    repeat while attempts is less than 200
        value pending is require-ok of PendingEvent, text with (poll-event with surface), "poll owned window"
        consider pending
            when Option.Some holding notification then give notification
            when Option.None then evaluate sleep-ms with 5
        end consider
        set attempts to attempts plus 1
    end repeat
    evaluate panic with "owned button activation did not produce an event"
end paragraph

main returns override integer begin paragraph
    value surface is require-ok of Window, text with (create-window with "NIL DAP owned desktop", 360, 180), "create window on process main"
    value button is require-ok of Widget, text with (create-button with (window-root with surface), "before"), "create button"
    evaluate require-ok of boolean, text with (show-window with surface), "show window"
    value created is surface.handle
    evaluate require-ok of boolean, text with (set-widget-text with button, "after"), "mutate retained button"
    value after is require-ok of text, text with (widget-text with button), "read retained button"
    when after does not equal "after" then give 71 end when
    evaluate require-ok of boolean, text with (activate-widget with button), "activate retained button"
    value queued is button.handle
    value click is next-event with surface
    value matched is event-for with click, button
    value clicked is is-click with click
    when not matched then give 72 end when
    when not clicked then give 73 end when
    value snapshot is click.window
    evaluate require-ok of boolean, text with (set-widget-text with button, "final"), "mutate after snapshot"
    value finaltext is require-ok of text, text with (widget-text with button), "read after snapshot"
    when finaltext does not equal "final" then give 74 end when
    evaluate require-ok of boolean, text with (close-window with surface), "close owned window"
    value retained is snapshot.handle
    when retained does not equal created then give 75 end when
    say "desktop-owner-pass"
    give 0
end paragraph
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--debugger', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--nilstd', type=Path, required=True)
    parser.add_argument('--xvfb', type=Path)
    parser.add_argument('--output-parent', type=Path)
    args = parser.parse_args()
    debugger, compiler, nilstd = (path.resolve(strict=True) for path in (args.debugger, args.compiler, args.nilstd))
    root = support.output_directory(args.output_parent, 'nil-dap-desktop-')
    report = {'schema': 'nil.dap-desktop.v1', 'status': 'FAIL', 'commands': [], 'sessions': [],
              'tools': {str(path): support.digest(path) for path in (debugger, compiler)},
              'scope': 'VM UI ownership and event retention across stops; no paused host pumping or external input'}
    try:
        display = nullcontext(support.environment())
        if sys.platform.startswith('linux'):
            assert args.xvfb, '--xvfb is required for the owned Linux desktop'
            spec = importlib.util.spec_from_file_location('nil_dap_private_display', support.ROOT / 'tests/tools/ui_display_contract.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            display = module.isolated_display(args.xvfb.resolve(strict=True))
        source = root / 'desktop.matrix'
        source.write_text(SOURCE, encoding='utf-8', newline='\n')
        lines = {marker: next(index for index, line in enumerate(SOURCE.splitlines(), 1) if marker in line)
                 for marker in ('value created', 'value queued', 'value snapshot', 'value retained')}
        with display as env:
            # Keep the selected private display, while avoiding incidental SDK
            # runtime overrides from the invoking developer shell.
            env = {key: value for key, value in env.items() if not key.startswith(('NY_', 'NIL_'))}
            for binary in (False, True):
                encoding = 'binary' if binary else 'text'
                program = root / (encoding + '.nil')
                support.compile_program(compiler, source, program, report['commands'], env,
                                        binary=binary, nilstd=nilstd)

                def initialize(client):
                    client.initialize(program)
                    points = client.request('setBreakpoints', {'source': {'path': str(source)},
                        'breakpoints': [{'line': line} for line in lines.values()]})['breakpoints']
                    assert len(points) == 4 and all(point['verified'] for point in points), points
                    client.request('configurationDone')
                    frame = client.stopped('breakpoint')
                    assert frame['line'] == lines['value created'], frame
                    surface, button = int(client.evaluate('surface.handle')), int(client.evaluate('button.handle'))
                    assert surface > 0 and button > 0 and surface != button
                    client.record['window_handle'], client.record['button_handle'] = surface, button
                    return surface, button

                def retained_handles(client):
                    surface, button = initialize(client)
                    # Each next executes on the same owner, including calls into
                    # the real provider using handles created before the stop.
                    for unused in range(3):
                        client.request('next', {'threadId': 1})
                        client.stopped('step')
                        assert int(client.evaluate('surface.handle')) == surface
                        assert int(client.evaluate('button.handle')) == button
                    assert client.evaluate('after') == '"after"'
                    client.request('continue', {'threadId': 1})
                    frame = client.stopped('breakpoint')
                    assert frame['line'] == lines['value queued'], frame
                    assert int(client.evaluate('button.handle')) == button
                    client.request('continue', {'threadId': 1})
                    frame = client.stopped('breakpoint')
                    assert frame['line'] == lines['value snapshot'], frame
                    flags = {name: client.evaluate(name) for name in ('matched', 'clicked')}
                    assert flags == {'matched': '1', 'clicked': '1'}, flags
                    client.request('continue', {'threadId': 1})
                    frame = client.stopped('breakpoint')
                    assert frame['line'] == lines['value retained'], frame
                    assert int(client.evaluate('snapshot.handle')) == surface
                    assert client.evaluate('finaltext') == '"final"'
                    client.request('continue', {'threadId': 1})
                    assert 'desktop-owner-pass' in client.event('output')['output']
                    assert client.event('exited')['exitCode'] == 0
                    client.event('terminated')
                    client.request('disconnect')
                    client.natural_exit()
                    assert client.record['stdin_open_at_exit']
                    client.record['queued_event_retained'] = True

                def stopped_disconnect(client):
                    initialize(client)
                    # VM destruction must dispose the still-open window on main.
                    client.request('disconnect')
                    client.natural_exit()
                    assert client.record['stdin_open_at_exit']

                support.session(debugger, root, report, encoding + '-retention', env, retained_handles)
                support.session(debugger, root, report, encoding + '-disconnect', env, stopped_disconnect)
        report['status'] = 'PASS'
    except BaseException as error:
        report['error'] = str(error)
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'] + ': ' + str(root / 'report.json'))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
