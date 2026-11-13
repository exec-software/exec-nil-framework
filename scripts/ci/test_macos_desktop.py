#!/usr/bin/env python3
"""Rejection controls for installed desktop evidence; these do not qualify macOS."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('macos_desktop', Path(__file__).with_name('macos-desktop.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class DesktopTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.sdk = self.root / 'SDK λ'; self.sdk.mkdir()
        self.helpers = self.root / 'helpers'
        self.deadline = time.monotonic() + 30
        self.common = gate.load('scripts/ci/macos-debugger.py', 'desktop_control_common')
        for tool in ('nilcompiler', 'nildebugger'):
            (self.sdk / tool).write_bytes(tool.encode())

    def editor(self):
        names = ['check-' + str(n) for n in range(88)]
        digest = hashlib.sha256(json.dumps(sorted(names), separators=(',', ':')).encode()).hexdigest()
        patch = mock.patch.object(gate, 'EDITOR_NAMES', digest)
        patch.start(); self.addCleanup(patch.stop)
        report = dict(status='PASS', platform='darwin', vscode='1.146.0', skippedChecks=[], fullSuiteCompleted=True,
                      completedModules=gate.MODULES.copy(), checks=[dict(name=n, status='PASS') for n in names],
                      runId='run', workspace='/private/workspace')
        launch = dict(runId='run', workspace='/private/workspace')
        settings = {'ny.sdkPath': str(self.sdk), **{'ny.' + key + '.path': str(self.sdk / value)
                                                 for key, value in gate.TOOL_SETTINGS.items()}}
        return report, launch, settings

    def test_complete_editor_and_exact_names(self):
        report, launch, settings = self.editor()
        self.assertEqual(gate.audit_editor(report, launch, settings, self.sdk)['groups'], 88)
        for change in ('missing', 'duplicate', 'replacement', 'failure'):
            altered = copy.deepcopy(report)
            if change == 'missing': altered['checks'].pop()
            if change == 'duplicate': altered['checks'][0] = altered['checks'][1]
            if change == 'replacement': altered['checks'][0]['name'] = 'unknown'
            if change == 'failure': altered['checks'][0]['status'] = 'FAIL'
            with self.subTest(change=change), self.assertRaises(ValueError):
                gate.audit_editor(altered, launch, settings, self.sdk)

    def test_editor_rejects_partial_or_stale_reports(self):
        report, launch, settings = self.editor()
        for key, value in [('platform', 'linux'), ('vscode', '1.145.0'), ('skippedChecks', ['omitted']),
                           ('fullSuiteCompleted', False), ('completedModules', gate.MODULES[:-1]),
                           ('runId', 'old'), ('workspace', '/other')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.audit_editor({**report, key: value}, launch, settings, self.sdk)

    def test_editor_rejects_source_tool_and_library_fallback(self):
        report, launch, settings = self.editor()
        for key in settings:
            with self.subTest(setting=key), self.assertRaises(ValueError):
                gate.audit_editor(report, launch, {**settings, key: '/source/build/bin'}, self.sdk)

    def test_editor_owned_profile_paths(self):
        root = Path('/tmp/nil vscode host café 中文-123')
        launch = dict(temporary=str(root), platform='darwin', workspace=str(root / 'workspace'),
                      profile=str(root / 'profile'), runId='12345678-1234-1234-1234-123456789abc')
        self.assertEqual(gate.editor_launch(launch), root)
        for key, value in [('temporary', '/tmp/unrelated'), ('profile', '/tmp/user-profile'),
                           ('workspace', '/tmp/user-work'), ('runId', '../outside')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.editor_launch({**launch, key: value})

    def test_ui_commands_use_both_installed_roots_without_product_fixtures(self):
        for library in ('nilstd', 'nillib'):
            argv = gate.ui_command(self.sdk, library, self.helpers, self.root / library)
            for option, expected in [('--sdk', self.sdk / library), ('--compiler', self.sdk / 'nilcompiler'),
                                     ('--tester', self.sdk / 'niltest'), ('--interpreter', self.sdk / 'nilinterpreter')]:
                self.assertEqual(argv[argv.index(option) + 1], str(expected))
            self.assertNotIn('--provider-test', argv)
            self.assertNotIn('--lifecycle-test', argv)
            self.assertEqual(argv[argv.index('--llvm') + 1], 'ON')
        with self.assertRaises(ValueError):
            gate.ui_command(self.sdk, 'nilstd/src', self.helpers, self.root)

    def test_json_stream_preserves_top_level_reports(self):
        values = [dict(schema='one', tests=[dict(name='nested')]), dict(schema='two')]
        text = 'human line\n' + json.dumps(values[0], indent=2) + '\nmore text\n' + json.dumps(values[1])
        self.assertEqual(list(gate.objects(text)), values)
        with self.assertRaises(json.JSONDecodeError):
            list(gate.objects('{invalid}\n'))

    def test_ui_audit_requires_all_ten_cases_and_bound_retained_inputs(self):
        controls = gate.load('tests/tools/test_ui_appkit_display_contract.py', 'installed_ui_fixture_controls')
        fixture = controls.AppKitHarnessTests(); fixture.setUp(); self.addCleanup(fixture.doCleanups)
        parent = self.root / 'ui-nilstd'; root = parent / 'nil-appkit-input-fixture'
        root.mkdir(parents=True)
        source = gate.ROOT / 'tests/ui/gtk_interaction.matrix'
        summary = dict(schema='nil.appkit.interaction.v1', status='passed', compiler=str(self.sdk / 'nilcompiler'),
                       evidenceDirectory=str(root), source=str(source),
                       sourceSha256=self.common.record(source, self.deadline)['sha256'], routes=[])
        for key, path in [('inputDriver', self.helpers / 'lib/libnil_ui_appkit_input_driver.dylib'),
                          ('compileDriver', self.helpers / 'bin/nil_ui_appkit_compile_driver')]:
            path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(key.encode())
            summary[key], summary[key + 'Sha256'] = str(path), self.common.record(path, self.deadline)['sha256']
        desktop = dict(schema='nil.test.results.v1', errors=[], tests=[dict(name=name,
            suite='nil.std.ui.gtk.desktop', route=route, status='passed', exitCode=77, signal=0, timedOut=False, cancelled=False)
            for name in ('widgets_layout_and_model_binding', 'window_ownership_and_size_errors',
                         'dynamic_subtrees_and_effective_state') for route in ('vm', 'native')])
        reports = [desktop]
        for route in ('vm', 'native'):
            directory = root / route; directory.mkdir()
            report = fixture.evidence(directory, route=route); reports.append(report)
            (root / (route + '.stdout')).write_text(json.dumps(report))
            records = [json.loads(row['stderr'].removeprefix('NIL_APPKIT_INPUT ')) for row in report['tests']]
            summary['routes'].append(dict(route=route, naturalCompletion=True, languageCases=2,
                                          parentPid=fixture.parent, inputs=records))
        path = root / 'report.json'; path.write_text(json.dumps(summary))
        stdout = '\n'.join(json.dumps(row, indent=2) for row in reports)
        audit = lambda: gate.audit_ui(stdout, parent, self.sdk, self.helpers, self.common, self.deadline)
        self.assertEqual(audit()['total_cases'], 10)
        for mutate in (lambda value: value['routes'].pop(),
                       lambda value: value['routes'][0].update(naturalCompletion=False),
                       lambda value: value.update(compiler='/source/compiler')):
            altered = copy.deepcopy(summary); mutate(altered); path.write_text(json.dumps(altered))
            with self.assertRaises(ValueError): audit()
        path.write_text(json.dumps(summary))
        (root / 'vm.stdout').write_text('{}')
        with self.assertRaises(ValueError): audit()

    def dap(self):
        sessions = [dict(name=encoding + '-' + action, status='PASS', natural_exit=True, stdin_open_at_exit=True,
                         cleanup_verified=True, exit_code=0, queued_event_retained=True)
                    for encoding in ('text', 'binary') for action in ('retention', 'disconnect')]
        commands = [dict(argv=[str(self.sdk / 'nilcompiler'), '--sdk-path', str(self.sdk / 'nilstd')]
                              + (['--emit-binary-nil'] if binary else []), exit_code=0, cleanup_verified=True)
                    for binary in (False, True)]
        return dict(schema='nil.dap-desktop.v1', status='PASS', sessions=sessions, commands=commands,
                    tools={str(self.sdk / name): self.common.record(self.sdk / name, self.deadline)['sha256']
                           for name in ('nildebugger', 'nilcompiler')})

    def test_four_desktop_dap_sessions_require_natural_owned_completion(self):
        report = self.dap()
        self.assertEqual(gate.audit_dap(report, self.sdk, self.common, self.deadline)['sessions'], 4)
        for key, value in [('natural_exit', False), ('stdin_open_at_exit', False), ('cleanup_verified', False),
                           ('exit_code', 1), ('queued_event_retained', False)]:
            altered = copy.deepcopy(report); altered['sessions'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.audit_dap(altered, self.sdk, self.common, self.deadline)
        report['sessions'].pop()
        with self.assertRaises(ValueError): gate.audit_dap(report, self.sdk, self.common, self.deadline)

    def test_dap_rejects_wrong_sdk_bytes_or_source_layout(self):
        report = self.dap()
        changed = copy.deepcopy(report); changed['tools'][str(self.sdk / 'nilcompiler')] = '0' * 64
        with self.assertRaises(ValueError): gate.audit_dap(changed, self.sdk, self.common, self.deadline)
        changed = copy.deepcopy(report); changed['commands'][0]['argv'][2] += '/src'
        with self.assertRaises(ValueError): gate.audit_dap(changed, self.sdk, self.common, self.deadline)
        changed = copy.deepcopy(report); changed['commands'][1]['argv'].remove('--emit-binary-nil')
        with self.assertRaises(ValueError): gate.audit_dap(changed, self.sdk, self.common, self.deadline)


if __name__ == '__main__':
    unittest.main()
