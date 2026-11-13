#!/usr/bin/env python3
"""Accept installed macOS UI, desktop DAP and the complete editor; no SDK build."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import re
import shutil
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOTAL_SECONDS = 2400
MODULES = ['analyzer-checks', 'project-checks', 'workspace-checks', 'semantic-checks',
           'completion-checks', 'member-signature-checks', 'language-values-checks',
           'memory-profile-checks', 'niltest-checks']
EDITOR_NAMES = '9374544bc95fde9f58f9400444aa734ecdd0f4f16575a6042e39eef46ab2583d'
TOOL_SETTINGS = {'server': 'nillsp', 'compiler': 'nilcompiler', 'interpreter': 'nilinterpreter',
                 'debugger': 'nildebugger', 'projectTool': 'nilbuild', 'testTool': 'niltest', 'aperture': 'aperture'}


def require(value, message):
    if not value:
        raise ValueError(message)


def load(relative, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ui_command(sdk, library, helpers, evidence):
    require(library in ('nilstd', 'nillib'), 'Unknown installed library root')
    return [sys.executable, '-B', str(ROOT / 'tests/tools/ui_display_contract.py'),
            '--tester', str(sdk / 'niltest'), '--compiler', str(sdk / 'nilcompiler'),
            '--interpreter', str(sdk / 'nilinterpreter'), '--sdk', str(sdk / library),
            '--source', str(ROOT / 'tests/ui/gtk_desktop.matrix'),
            '--interaction-source', str(ROOT / 'tests/ui/gtk_interaction.matrix'), '--llvm', 'ON',
            '--appkit-input-driver', str(helpers / 'lib/libnil_ui_appkit_input_driver.dylib'),
            '--appkit-compile-driver', str(helpers / 'bin/nil_ui_appkit_compile_driver'),
            '--appkit-evidence-directory', str(evidence)]


def objects(text):
    """Decode whole top-level JSON documents among the harness's human lines."""
    decoder, position = json.JSONDecoder(), 0
    while position < len(text):
        if text[position] == '{' and (position == 0 or text[position - 1] == '\n'):
            value, end = decoder.raw_decode(text, position)
            yield value
            position = end
        else:
            position += 1


def audit_ui(stdout, directory, sdk, helpers, common, deadline):
    ui = load('tests/tools/ui_display_contract.py', 'installed_ui_contract')
    reports = [r for r in objects(stdout) if r.get('schema') == 'nil.test.results.v1']
    require(len(reports) == 3, 'Expected one desktop and two interaction reports')
    ui.validate_desktop_report(reports[0], 'ON')
    paths = list(directory.glob('nil-appkit-input-*/report.json'))
    require(len(paths) == 1, 'Expected one owned AppKit interaction directory')
    root = paths[0].parent
    summary = common.read_json(paths[0])
    require(summary['schema'] == 'nil.appkit.interaction.v1' and summary['status'] == 'passed' and
            summary['compiler'] == str(sdk / 'nilcompiler') and summary['evidenceDirectory'] == str(root) and
            summary['source'] == str(ROOT / 'tests/ui/gtk_interaction.matrix') and
            summary['sourceSha256'] == common.record(ROOT / 'tests/ui/gtk_interaction.matrix', deadline)['sha256'],
            'Installed UI compiler/source binding differs')
    for key, path in (('inputDriver', helpers / 'lib/libnil_ui_appkit_input_driver.dylib'),
                      ('compileDriver', helpers / 'bin/nil_ui_appkit_compile_driver')):
        require(summary[key] == str(path) and summary[key + 'Sha256'] == common.record(path, deadline)['sha256'],
                'AppKit test-only helper binding differs')
    require([r['route'] for r in summary['routes']] == ['vm', 'native'], 'Incomplete UI route inventory')
    for index, row in enumerate(summary['routes'], 1):
        route = row['route']
        require(row['naturalCompletion'] is True and row['languageCases'] == 2 and len(row['inputs']) == 2,
                'UI route did not complete naturally')
        raw = common.read_json(root / (route + '.stdout'))
        require(raw == reports[index], 'Interaction stdout differs from retained report')
        records = ui.validate_appkit_interaction(raw, route, root / route, row['inputs'][0]['token'], row['parentPid'])
        require(records == row['inputs'], 'Interaction responder records differ')
    return {'desktop_cases': 6, 'interaction_cases': 4, 'total_cases': 10,
            'report': common.record(paths[0], deadline)}


def audit_dap(report, sdk, common, deadline):
    expected = [encoding + '-' + action for encoding in ('text', 'binary') for action in ('retention', 'disconnect')]
    require(report['schema'] == 'nil.dap-desktop.v1' and report['status'] == 'PASS' and
            [row['name'] for row in report['sessions']] == expected, 'Incomplete desktop DAP inventory')
    require(report['tools'] == {str(sdk / name): common.record(sdk / name, deadline)['sha256']
                               for name in ('nildebugger', 'nilcompiler')}, 'DAP used different installed tools')
    for row in report['sessions']:
        require(row['status'] == 'PASS' and row['natural_exit'] is True and row['stdin_open_at_exit'] is True and
                row['cleanup_verified'] is True and row['exit_code'] == 0, 'Desktop DAP ownership/shutdown failed')
        if row['name'].endswith('-retention'):
            require(row['queued_event_retained'] is True, 'Desktop DAP lost its queued event')
    require(len(report['commands']) == 2, 'Expected both desktop DAP encodings')
    for index, row in enumerate(report['commands']):
        argv = row['argv']
        require(row['exit_code'] == 0 and row['cleanup_verified'] is True and argv[0] == str(sdk / 'nilcompiler') and
                argv[argv.index('--sdk-path') + 1] == str(sdk / 'nilstd') and
                ('--emit-binary-nil' in argv) == bool(index), 'DAP compile SDK/encoding differs')
    return {'sessions': 4, 'names': expected}


def audit_editor(report, launch, settings, sdk):
    require(report['status'] == 'PASS' and report['platform'] == 'darwin' and report['vscode'] == '1.146.0' and
            report['skippedChecks'] == [] and report['fullSuiteCompleted'] is True and
            report['completedModules'] == MODULES, 'Incomplete pinned editor suite')
    names = [row['name'] for row in report['checks']]
    require(len(names) == len(set(names)) == 88 and all(row['status'] == 'PASS' for row in report['checks']) and
            hashlib.sha256(json.dumps(sorted(names), separators=(',', ':'), ensure_ascii=False).encode()).hexdigest() == EDITOR_NAMES,
            'Editor named check inventory differs')
    require(report['runId'] == launch['runId'] and report['workspace'] == launch['workspace'], 'Stale editor result')
    require(settings['ny.sdkPath'] == str(sdk) and
            all(settings['ny.' + key + '.path'] == str(sdk / tool) for key, tool in TOOL_SETTINGS.items()),
            'Editor configuration fell back to source-built tools/stdlib')
    return {'groups': 88, 'modules': MODULES, 'version': '1.146.0', 'run_id': report['runId']}


def editor_launch(launch):
    temporary = Path(launch['temporary'])
    require(launch['platform'] == 'darwin' and temporary.parent == Path('/tmp') and
            temporary.name.startswith('nil vscode host café 中文-') and not temporary.is_symlink() and
            launch['profile'] == str(temporary / 'profile') and launch['workspace'] == str(temporary / 'workspace'),
            'Editor escaped its isolated profile/workspace')
    require(re.fullmatch('[0-9a-f-]{36}', launch['runId']), 'Invalid editor run identifier')
    return temporary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'origin', 'output', 'helpers', 'editor', 'node'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--expected-revision', required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() in ('arm64', 'x86_64'), 'Native macOS required')
    common = load('scripts/ci/macos-debugger.py', 'installed_desktop_common')
    sdk, origin, output = common.paths(args.sdk, args.origin, args.output)
    helpers, editor, node = (path.resolve(strict=True) for path in (args.helpers, args.editor, args.node))
    output.mkdir(parents=True)
    started = time.monotonic(); deadline = started + TOTAL_SECONDS
    report = {'schema': 'nil.macos-installed-desktop.v1', 'status': 'FAIL', 'problems': [], 'phases': [],
              'sdk': str(sdk), 'expected_revision': args.expected_revision,
              'host': {'system': platform.system(), 'machine': platform.machine()},
              'scope': 'Installed nilstd/nillib VM/native UI, four desktop DAP sessions and full editor; '
                       'source provider/lifecycle-depth tests remain separate; no Accessibility, SDK activation or release qualification.'}
    before = seal = anchor = None
    editor_reports = ROOT / 'build/vscode-host-smoke'
    try:
        require(not editor_reports.exists(), 'Editor reports must be fresh, never reused')
        require((sdk / 'nilstd/ui/package.matrix').is_file(), 'Installed flat nilstd UI package is missing')
        require((sdk / 'nillib/ui/package.nil').is_file(), 'Installed binary UI package is missing')
        inputs = [node, editor, helpers / 'bin/nil_vscode_host_runner',
                  helpers / 'bin/nil_ui_appkit_compile_driver', helpers / 'lib/libnil_ui_appkit_input_driver.dylib',
                  ROOT / 'vscode/package.json', ROOT / 'vscode/dist/extension.js']
        before = {str(path): common.record(path, deadline) for path in inputs}
        seal, anchor = common.inventory(sdk, deadline), common.record(origin, deadline)
        verify = common.load(common.PROJECT / 'scripts/sdk_integrity.py', 'installed_desktop_integrity').verify
        state = verify(sdk, origin)
        common.verify_origin(state, sdk, args.expected_revision, platform.machine())
        report.update(inputs_before=before, sdk_before=seal, origin_before=anchor, sdk_integrity=state)
        capture = load('tests/tools/ordinary_io_parity_contract.py', 'installed_desktop_capture')
        owner = capture.load_ownership(ROOT)
        environment = load('scripts/check-sdk-routes.py', 'installed_desktop_environment').consumer_environment()
        for name in ('GH_TOKEN', 'GITHUB_TOKEN', 'ACTIONS_RUNTIME_TOKEN'):
            environment.pop(name, None)

        def phase(name, argv, seconds, env=environment):
            require(deadline - time.monotonic() > seconds + 20, 'Insufficient remaining desktop deadline')
            row = capture.run_process(owner, list(map(str, argv)), output, env, output, name, seconds)
            report['phases'].append(row)
            require(row['status'] == 'PASS' and row['cleanup_verified'], 'Installed phase failed: ' + name)
            return (output / row['stdout']).read_text(encoding='utf-8')

        report['ui'] = {}
        for library in ('nilstd', 'nillib'):
            directory = output / ('ui-' + library)
            stdout = phase('ui-' + library, ui_command(sdk, library, helpers, directory), 450)
            report['ui'][library] = audit_ui(stdout, directory, sdk, helpers, common, deadline)
        directory = output / 'desktop-dap'; directory.mkdir()
        phase('desktop-dap', [sys.executable, '-B', ROOT / 'nildebugger/tests/dap_desktop.py',
              '--debugger', sdk / 'nildebugger', '--compiler', sdk / 'nilcompiler',
              '--nilstd', sdk / 'nilstd', '--output-parent', directory], 300)
        paths = list(directory.glob('nil-dap-desktop-*/report.json'))
        require(len(paths) == 1, 'Expected one fresh desktop DAP result')
        report['desktop_dap'] = audit_dap(common.read_json(paths[0]), sdk, common, deadline)
        editor_environment = dict(environment, VSCODE_EXECUTABLE=str(editor), NIL_VSCODE_TOOL_DIRECTORY=str(sdk),
            NIL_VSCODE_SDK_PATH=str(sdk), NIL_VSCODE_HOST_RUNNER=str(helpers / 'bin/nil_vscode_host_runner'),
            NIL_VSCODE_HOST_TIMEOUT_MS='900000')
        report['editor_environment'] = {key: value for key, value in editor_environment.items()
                                        if key.startswith('NIL_VSCODE_') or key == 'VSCODE_EXECUTABLE'}
        phase('editor', [node, ROOT / 'vscode/test/run-host.cjs'], 960, editor_environment)
        launches = list(editor_reports.glob('*-launch.json'))
        require(len(launches) == 1, 'Expected one fresh editor launch')
        launch = common.read_json(launches[0]); temporary = editor_launch(launch)
        settings = common.read_json(temporary / 'workspace/.vscode/settings.json')
        result = common.read_json(editor_reports / (launch['runId'] + '.json'))
        require(result == common.read_json(editor_reports / 'latest.json'), 'Editor latest report differs')
        report['editor'] = audit_editor(result, launch, settings, sdk)
        common.write_json(output / 'editor-settings.json', settings)
        report['status'] = 'PASS'
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        try:
            if editor_reports.is_dir():
                common.inventory(editor_reports, deadline)
                shutil.copytree(editor_reports, output / 'editor-reports')
                for path in editor_reports.glob('*-launch.json'):
                    launch = common.read_json(path); temporary = editor_launch(launch)
                    logs = temporary / 'profile/logs'
                    if logs.is_dir():
                        common.inventory(logs, deadline)
                        shutil.copytree(logs, output / 'editor-logs' / launch['runId'])
            require(before is not None and before == {path: common.record(Path(path), deadline) for path in before},
                    'Test helper/editor/node input changed')
            require(seal is not None and common.inventory(sdk, deadline) == seal, 'Installed SDK changed')
            require(anchor is not None and common.record(origin, deadline) == anchor, 'SDK origin changed')
            report['inputs_unchanged'] = True
            report['evidence_files'] = common.inventory(output, deadline)
        except Exception as error:
            report['problems'].append(type(error).__name__ + ': ' + str(error))
        report['cleanup_verified'] = len(report['phases']) == 4 and all(row['cleanup_verified'] for row in report['phases'])
        report['elapsed_seconds'] = time.monotonic() - started
        if report['problems'] or not report['cleanup_verified'] or report['elapsed_seconds'] > TOTAL_SECONDS:
            report['status'] = 'FAIL'
        common.write_json(output / 'report.json', report)
    print(json.dumps({key: report[key] for key in ('status', 'expected_revision', 'problems')}))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
