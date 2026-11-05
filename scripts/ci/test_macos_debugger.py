#!/usr/bin/env python3
"""Pure acceptance-binding and platform-environment controls; no native SDK claim."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('macos_debugger', Path(__file__).with_name('macos-debugger.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
sys.path.insert(0, str(gate.PROJECT / 'scripts'))
try:
    consumer = gate.load(gate.PROJECT / 'scripts/validate_debugger.py', 'macos_environment_control')
finally:
    sys.path.pop(0)


class EnvironmentTests(unittest.TestCase):
    def test_darwin_clears_apple_and_framework_overrides(self):
        variables = {key: 'untrusted' for key in ('CC', 'CXX', 'SDKROOT', 'LDFLAGS', 'CPPFLAGS',
            'CFLAGS', 'CXXFLAGS', 'MACOSX_DEPLOYMENT_TARGET', 'DEVELOPER_DIR', 'TOOLCHAINS',
            'COMPILER_PATH', 'GCC_EXEC_PREFIX', 'CCC_OVERRIDE_OPTIONS', 'DYLD_FRAMEWORK_PATH',
            'DYLD_FALLBACK_LIBRARY_PATH', 'LD_PRELOAD', 'NIL_SDK', 'NY_IMPORT_PATH', 'CPATH')}
        variables.update(HOME='/home/control', TMPDIR='/tmp/control', PATH='/bad', ORDINARY='keep')
        with mock.patch.dict(os.environ, variables, clear=True), mock.patch.object(consumer.sys, 'platform', 'darwin'):
            actual = consumer.clean_env()
            self.assertEqual(dict(os.environ), variables)
        self.assertEqual(actual, {'HOME': '/home/control', 'TMPDIR': '/tmp/control', 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin',
                                  'ORDINARY': 'keep', 'LC_ALL': 'C'})

    def test_linux_environment_policy_unchanged(self):
        with mock.patch.dict(os.environ, {'CC': 'chosen', 'PATH': '/bad', 'NIL_SDK': '/bad'}, clear=True), \
                mock.patch.object(consumer.sys, 'platform', 'linux'), mock.patch.object(consumer.os, 'name', 'posix'):
            self.assertEqual(consumer.clean_env(), {'CC': 'chosen', 'PATH': '/usr/local/bin:/usr/bin:/bin', 'LC_ALL': 'C'})

    def test_windows_keeps_vcvars(self):
        environment = dict(PATH='selected', INCLUDE='includes', LIB='libs', LIBPATH='framework', SystemRoot='windows', NIL_SDK='bad')
        with mock.patch.dict(os.environ, environment, clear=True), mock.patch.object(consumer.sys, 'platform', 'win32'), \
                mock.patch.object(consumer.os, 'name', 'nt'):
            actual = consumer.clean_env()
        self.assertEqual(actual, {**{k: v for k, v in environment.items() if k != 'NIL_SDK'}, 'LC_ALL': 'C'})


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.sdk = self.root / 'SDK space λ'; self.sdk.mkdir()
        self.private = self.root / 'consumer'; self.private.mkdir()
        self.work = self.private / 'work/debugger/unique'; self.work.mkdir(parents=True)
        self.origin = self.root / 'origin.json'; self.origin.write_text('{}')
        self.deadline = time.monotonic() + 30
        self.names = ['check-' + str(i) for i in range(146)]
        digest = hashlib.sha256(json.dumps(sorted(self.names), separators=(',', ':')).encode()).hexdigest()
        patch = mock.patch.object(gate, 'DEBUGGER_NAMES', digest); patch.start(); self.addCleanup(patch.stop)
        self.sources = {'scripts/exact.py': 'source-digest'}
        self.state = {'status': 'PASS', 'errors': [], 'anchor': 'origin file', 'sdk': str(self.sdk),
                      'manifest_sha256': 'anchor', 'origin_manifest_sha256': 'anchor',
                      'baseline': {'source': {'revision': 'a'*40, 'worktree_dirty': False},
                                   'host': {'system': 'Darwin', 'machine': 'arm64'}}}
        self.report = {'schema': 1, 'status': 'PASS', 'completed': True, 'counts': {'PASS': 146, 'FAIL': 0},
            'checks': [{'name': n, 'status': 'PASS'} for n in self.names], 'source_hashes_before': self.sources,
            'source_hashes': self.sources, 'host': self.state['baseline']['host'], 'origin_path': str(self.origin),
            'origin_file_sha256_before': gate.record(self.origin, self.deadline)['sha256'],
            'origin_file_sha256_after': gate.record(self.origin, self.deadline)['sha256'],
            'sdk_before': self.state, 'sdk_after': self.state, 'work': str(self.work), 'commands': [], 'artifacts': {}}
        for label, argv in gate.debugger_graph(self.work, self.private, self.sdk).items():
            cwd = self.work / label; cwd.mkdir()
            row = dict(label=label, command=argv, cwd=str(cwd), exit_code=0, timeout=False)
            for channel in ('stdout', 'stderr'):
                path = cwd / (channel + '.txt'); path.write_bytes(b'raw ' + channel.encode())
                row[channel] = str(path); row[channel + '_sha256'] = gate.record(path, self.deadline)['sha256']
            self.report['commands'].append(row)
            if label.startswith('compile-'):
                path = Path(argv[argv.index('-o')+1]); path.write_bytes(b'artifact ' + label.encode())
                self.report['artifacts'][str(path)] = gate.record(path, self.deadline)['sha256']
        for label, argv in gate.session_graph(self.work, self.sdk).items():
            directory = self.work / label; directory.mkdir()
            events = [{'direction': 'start', 'message': {'command': argv, 'cwd': str(directory)}},
                      {'direction': 'exit', 'message': {'code': 0}}]
            (directory / 'transcript.jsonl').write_text(''.join(json.dumps(e)+'\n' for e in events))
            (directory / 'stderr.txt').write_bytes(b'')

    def audit(self):
        return gate.audit_debugger(self.report, self.private, self.sdk, self.origin, self.state, self.sources, self.deadline)

    def test_complete_bound_evidence(self):
        actual = self.audit()
        self.assertEqual((actual['checks'], actual['commands'], actual['protocol_sessions']), (146, 20, 15))

    def test_changed_command_tool_rejected(self):
        self.report['commands'][0]['command'][0] = '/alternate/nilcompiler'
        with self.assertRaisesRegex(ValueError, 'command differs'): self.audit()

    def test_changed_raw_stream_rejected(self):
        Path(self.report['commands'][0]['stdout']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'raw stream'): self.audit()

    def test_changed_session_tool_rejected(self):
        path = self.work / 'dap-text/transcript.jsonl'
        events = [json.loads(line) for line in path.read_text().splitlines()]
        events[0]['message']['command'][0] = '/alternate/nildebugger'
        path.write_text(''.join(json.dumps(e)+'\n' for e in events))
        with self.assertRaisesRegex(ValueError, 'ownership/exit'): self.audit()

    def test_missing_session_rejected(self):
        (self.work / 'dap-text/transcript.jsonl').unlink()
        with self.assertRaisesRegex(ValueError, 'Protocol inventory'): self.audit()

    def test_incomplete_or_changed_named_checks_rejected(self):
        self.report['checks'][-1]['name'] = 'unexpected-check'
        with self.assertRaisesRegex(ValueError, 'named check inventory'): self.audit()
        self.report['completed'] = False
        with self.assertRaisesRegex(ValueError, 'Incomplete'): self.audit()

    def test_extra_artifact_rejected(self):
        self.report['artifacts'][str(self.work / 'unplanned')] = 'digest'
        with self.assertRaisesRegex(ValueError, 'artifact inventory'): self.audit()

    def test_external_origin_revision_and_host_required(self):
        gate.verify_origin(self.state, self.sdk, 'a'*40, 'arm64')
        for mutate in (lambda s: s.update(anchor='self (no origin anchor)'),
                       lambda s: s['baseline']['source'].update(revision='b'*40),
                       lambda s: s['baseline']['source'].update(worktree_dirty=True),
                       lambda s: s['baseline']['host'].update(machine='x86_64')):
            with self.subTest(mutate=mutate):
                state = copy.deepcopy(self.state); mutate(state)
                with self.assertRaises(ValueError): gate.verify_origin(state, self.sdk, 'a'*40, 'arm64')

    def test_origin_consumes_actual_integrity_manifest_source_schema(self):
        # SDK manifests use worktree_dirty; pre-test snapshots use dirty.
        manifest = {'source': {'revision': 'a'*40, 'worktree_dirty': False},
                    'host': {'system': 'Darwin', 'machine': 'arm64'},
                    'payload': {'file_count': 0, 'files': []}}
        path = self.sdk / 'manifest.json'; path.write_text(json.dumps(manifest))
        self.origin.write_text(json.dumps({'source_manifest_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}))
        integrity = gate.load(gate.PROJECT / 'scripts/sdk_integrity.py', 'manifest_schema_control')
        observed = integrity.verify(self.sdk, self.origin)
        self.assertEqual(observed['status'], 'PASS')
        gate.verify_origin(observed, self.sdk, 'a'*40, 'arm64')
        observed['baseline']['source'] = {'revision': 'a'*40, 'dirty': False}
        with self.assertRaisesRegex(ValueError, 'exact clean requested revision'):
            gate.verify_origin(observed, self.sdk, 'a'*40, 'arm64')

    def test_sdk_and_origin_symlinks_rejected_before_resolve(self):
        link = self.root / 'link'; link.symlink_to(self.sdk, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'): gate.paths(link, self.origin, self.root/'output')
        link.unlink(); link.symlink_to(self.origin)
        with self.assertRaisesRegex(ValueError, 'symlink'): gate.paths(self.sdk, link, self.root/'output')

    def test_output_cannot_overlap_sdk_origin_or_sources(self):
        for output in (self.sdk/'output', self.origin.parent, gate.PROJECT/'output'):
            with self.subTest(output=output), self.assertRaises(ValueError): gate.paths(self.sdk, self.origin, output)
        self.assertEqual(gate.paths(self.sdk, self.origin, self.root/'fresh'),
                         (self.sdk.resolve(), self.origin.resolve(), (self.root/'fresh').resolve()))

    def test_parent_alias_is_canonicalized_without_accepting_sdk_overlap(self):
        actual = self.root / 'actual-parent'; actual.mkdir()
        alias = self.root / 'parent-alias'; alias.symlink_to(actual, target_is_directory=True)
        sdk = alias / 'sdk'; sdk.mkdir()
        origin = alias / 'origin.json'; origin.write_text('{}')
        output = alias / 'fresh'
        self.assertNotEqual(sdk, sdk.resolve())
        self.assertEqual(gate.paths(sdk, origin, output),
                         (sdk.resolve(), origin.resolve(), output.resolve()))
        with self.assertRaisesRegex(ValueError, 'outside SDK/origin'):
            gate.paths(sdk, origin, sdk / 'output')

    def test_inventory_detects_bytes_modes_and_links(self):
        file = self.sdk/'payload'; file.write_bytes(b'before'); file.chmod(0o755)
        original = gate.inventory(self.sdk, self.deadline)
        file.chmod(0o644); self.assertNotEqual(gate.inventory(self.sdk, self.deadline), original)
        file.chmod(0o755); file.write_bytes(b'after'); self.assertNotEqual(gate.inventory(self.sdk, self.deadline), original)
        (self.sdk/'link').symlink_to(file)
        with self.assertRaisesRegex(ValueError, 'symlink'): gate.inventory(self.sdk, self.deadline)

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX special-file control')
    def test_record_rejects_special_file_without_waiting_for_writer(self):
        fifo = self.root / 'untrusted-fifo'; os.mkfifo(fifo)
        with self.assertRaisesRegex(ValueError, 'regular file'): gate.record(fifo, self.deadline)


if __name__ == '__main__':
    unittest.main()
