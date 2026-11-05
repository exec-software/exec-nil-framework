#!/usr/bin/env python3
"""Pure sealed-artifact replay controls; no native SDK execution or qualification."""
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import tarfile
import tempfile
import time
import unittest
from unittest import mock
import xml.etree.ElementTree as ET
import zipfile

SPEC = importlib.util.spec_from_file_location('macos_sdk_replay', Path(__file__).with_name('macos-sdk-replay.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
REVISION = 'a' * 40


def digest(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve(strict=True)
        self.deadline = time.monotonic() + 30
        self.artifact = self.root / 'artifact'; self.artifact.mkdir()
        self.sdk = self.root / 'SDK space λ'; self.sdk.mkdir()
        self.metadata = {
            'run': {'id': 42, 'repository': {'full_name': gate.REPOSITORY}, 'head_sha': REVISION,
                    'status': 'completed', 'conclusion': 'failure', 'run_attempt': 1,
                    'path': '.github/workflows/macos-sdk.yml', 'event': 'workflow_dispatch'},
            'artifact': {'id': 9, 'workflow_run': {'id': 42, 'head_sha': REVISION}, 'expired': False,
                         'size_in_bytes': 3, 'digest': 'sha256:' + digest(b'zip')['sha256'],
                         'name': 'macos-development-sdk-arm64-' + REVISION + '-1'}}
        self.binding = gate.bind_transport(self.metadata, digest(b'zip'), REVISION, 'arm64')
        self.collector = gate.load('sdk_replay_control_collector', 'scripts/ci/macos-evidence.py')
        self.git = mock.patch.object(gate.subprocess, 'check_output', side_effect=self.git_blob)
        self.git.start(); self.addCleanup(self.git.stop)

    def git_blob(self, argv, **kwargs):
        self.assertEqual(argv[:2], ['git', 'show'])
        self.assertTrue(argv[2].startswith(REVISION + ':'))
        return (gate.ROOT / argv[2].split(':', 1)[1]).read_bytes()

    def write_json(self, name, value):
        (self.artifact / name).write_text(json.dumps(value), encoding='utf-8')

    def ctest(self, xml=None, names=None):
        names = ['pass', 'fail', 'skip'] if names is None else names
        registration = {'kind': 'ctestInfo', 'tests': [{'name': name} for name in names]}
        xml = xml if xml is not None else ('<testsuite><testcase name="pass" status="run"/>'
            '<testcase name="fail" status="fail"><failure>known failure</failure></testcase>'
            '<testcase name="skip" status="notrun"><skipped message="77"/></testcase></testsuite>')
        self.write_json('registration.json', registration)
        (self.artifact / 'ctest-results.xml').write_text(xml)
        summary = self.collector.summarize(registration, ET.fromstring(xml))
        self.write_json('ctest-inventory.json', {**summary, 'errors': self.collector.inventory_errors(summary)})
        return {'registered_tests': sorted(names)}

    def make_tar(self, entries):
        path = self.artifact / 'development-candidate.tar.gz'
        with tarfile.open(path, 'w:gz') as archive:
            for name, raw, mode, kind in entries:
                item = tarfile.TarInfo(name); item.mode = mode; item.type = kind
                item.size = len(raw) if kind == tarfile.REGTYPE else 0
                if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE): item.linkname = 'outside'
                archive.addfile(item, io.BytesIO(raw) if kind == tarfile.REGTYPE else None)
        return path

    def candidate(self):
        blobs = {name: (b'tool ' + name.encode(), 0o755) for name in gate.TOOLS}
        blobs.update({'nillib/native/libnil.a': (b'archive', 0o644),
                      'nillib/native/libnil.dylib': (b'dylib', 0o755),
                      'nilstd/core.ny': (b'// source', 0o644)})
        manifest = {'source': {'revision': REVISION, 'worktree_dirty': False},
                    'host': {'system': 'Darwin', 'machine': 'arm64'},
                    'build': {'llvm': 'ON', 'type': 'Release'}, 'qualification': {'mode': 'development'},
                    'payload': {'file_count': len(blobs), 'files': [{'path': name, **digest(raw)}
                        for name, (raw, _) in sorted(blobs.items())]},
                    'tools': [{'name': name, 'path': name, **digest(blobs[name][0])} for name in gate.TOOLS],
                    'native_runtime': {'path': 'nillib/native/libnil.a', **digest(b'archive')},
                    'native_shared_runtime': {'path': 'nillib/native/libnil.dylib', **digest(b'dylib')}}
        raw_manifest = json.dumps(manifest).encode()
        blobs['manifest.json'] = raw_manifest, 0o644
        payload = {name: {**digest(raw), 'mode': mode} for name, (raw, mode) in blobs.items()}
        self.make_tar([('sdk/' + name, raw, mode, tarfile.REGTYPE) for name, (raw, mode) in blobs.items()])
        self.write_json('candidate-manifest.json', manifest)
        self.write_json('relocation.json', {'new_path': '/original/relocated', 'old_path': '/original/stage',
                                          'old_path_absent': True, 'payload': payload})
        self.write_json('candidate-capture.json', {'archive': 'development-candidate.tar.gz', 'error': None,
                                                'path': '/original/relocated', 'seal_unchanged': True})
        self.write_json('relocation-origin.json', {'source_revision': REVISION,
            'source_manifest_sha256': digest(raw_manifest)['sha256']})
        self.write_json('sdk-build-evidence.json', {**self.ctest(), 'source': {'revision': REVISION, 'dirty': False,
            'fingerprint': hashlib.sha256(REVISION.encode() + b'\0').hexdigest()},
            'tools': {name: digest(blobs[name][0])['sha256'] for name in gate.TOOLS},
            'runtime_sha256': digest(b'archive')['sha256'], 'shared_runtime_sha256': digest(b'dylib')['sha256']})
        return payload

    def seal(self):
        identity = {'source': {'revision': REVISION, 'dirty': False,
            'fingerprint': hashlib.sha256(REVISION.encode() + b'\0').hexdigest()},
            'architecture': 'arm64', 'run_id': '42', 'attempt': '1',
            'helper': digest((gate.ROOT / 'scripts/ci/macos-sdk.py').read_bytes()),
            'workflow': digest((gate.ROOT / '.github/workflows/macos-sdk.yml').read_bytes())}
        self.write_json('identity.json', identity)
        self.write_json('evidence.json', {'source': identity['source'], 'source_unchanged': True,
            'nonregular_evidence_entries': [], 'relocated_candidate_unchanged': True,
            'release_declaration': False, 'platform_parity': False,
            'files': gate.namespace(self.artifact, self.deadline)})

    def test_transport_preserves_failed_run_without_claiming_qualification(self):
        self.assertEqual(self.binding['original_run_conclusion'], 'failure')

    def test_transport_rejects_wrong_source_workflow_attempt_digest_and_size(self):
        for mutate in (lambda x: x['run'].update(head_sha='b'*40),
                       lambda x: x['run'].update(path='.github/workflows/other.yml'),
                       lambda x: x['run'].update(status='in_progress'),
                       lambda x: x['run'].update(run_attempt=2),
                       lambda x: x['artifact'].update(expired=True),
                       lambda x: x['artifact'].update(digest='sha256:'+'0'*64),
                       lambda x: x['artifact'].update(size_in_bytes=4)):
            value = copy.deepcopy(self.metadata); mutate(value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                gate.bind_transport(value, digest(b'zip'), REVISION, 'arm64')

    def test_zip_safe_data_extraction_and_no_execute_bits(self):
        path = self.root / 'source.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            entry = zipfile.ZipInfo('folder/data'); entry.external_attr = (stat.S_IFREG | 0o755) << 16
            archive.writestr(entry, b'data')
        gate.extract_zip(path, self.artifact, self.deadline)
        self.assertEqual((self.artifact/'folder/data').read_bytes(), b'data')
        self.assertEqual(stat.S_IMODE((self.artifact/'folder/data').stat().st_mode), 0o600)

    def test_zip_rejects_traversal_symlink_and_normalized_duplicates(self):
        for i, entries in enumerate(([('../escape', stat.S_IFREG)], [('link', stat.S_IFLNK)],
                                    [('same', stat.S_IFREG), ('same/', stat.S_IFDIR)])):
            path = self.root / f'bad{i}.zip'; destination = self.root/f'out{i}'; destination.mkdir()
            with zipfile.ZipFile(path, 'w') as archive:
                for name, kind in entries:
                    entry = zipfile.ZipInfo(name); entry.external_attr = (kind | 0o755) << 16
                    archive.writestr(entry, b'x' if kind != stat.S_IFDIR else b'')
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                gate.extract_zip(path, destination, self.deadline)
            self.assertEqual(list(destination.iterdir()), [])

    def test_sealed_candidate_preserves_exact_bytes_modes_and_known_inventory(self):
        payload = self.candidate(); self.seal()
        gate.verify_evidence(self.artifact, self.binding, self.deadline)
        actual, origin, inventory = gate.verify_candidate(self.artifact, self.sdk, self.binding, self.deadline)
        self.assertEqual(actual, payload)
        self.assertEqual(gate.namespace(self.sdk, self.deadline, modes=True), payload)
        self.assertEqual(inventory['counts'], {'passed': 1, 'failed': 1, 'skipped': 1})
        self.assertEqual(origin['source_revision'], REVISION)

    def test_collector_seal_rejects_changed_and_extra_bytes(self):
        self.candidate(); self.seal()
        (self.artifact/'ctest-results.xml').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'evidence file differs'):
            gate.verify_evidence(self.artifact, self.binding, self.deadline)
        (self.artifact/'extra').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'namespace differs'):
            gate.verify_evidence(self.artifact, self.binding, self.deadline)

    def test_original_producer_source_hash_required(self):
        self.candidate(); self.seal()
        with mock.patch.object(gate.subprocess, 'check_output', return_value=b'other source'):
            with self.assertRaisesRegex(ValueError, 'workflow/helper blob'):
                gate.verify_evidence(self.artifact, self.binding, self.deadline)

    def test_candidate_missing_origin_or_wrong_snapshot_is_rejected(self):
        self.candidate()
        snapshot = json.loads((self.artifact/'sdk-build-evidence.json').read_text())
        snapshot['source']['revision'] = 'b'*40
        self.write_json('sdk-build-evidence.json', snapshot)
        with self.assertRaisesRegex(ValueError, 'Pre-test source'):
            gate.verify_candidate(self.artifact, self.sdk, self.binding, self.deadline)
        self.candidate(); (self.artifact/'relocation-origin.json').unlink()
        with self.assertRaises(FileNotFoundError):
            gate.verify_candidate(self.artifact, self.sdk, self.binding, self.deadline)

    def test_candidate_cannot_substitute_tested_tool_or_origin_manifest(self):
        for name in ('tool', 'origin'):
            self.candidate()
            target = self.root / ('candidate-' + name); target.mkdir()
            if name == 'tool':
                snapshot = json.loads((self.artifact/'sdk-build-evidence.json').read_text())
                snapshot['tools']['nilcompiler'] = '0'*64
                self.write_json('sdk-build-evidence.json', snapshot)
            else:
                self.write_json('relocation-origin.json', {'source_revision': REVISION,
                    'source_manifest_sha256': '0'*64})
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Tool snapshot|manifest anchor'):
                gate.verify_candidate(self.artifact, target, self.binding, self.deadline)

    def test_tar_rejects_links_traversal_extra_members_modes_and_changed_bytes(self):
        expected = {'file': {**digest(b'good'), 'mode': 0o755}}
        cases = [('sdk/file', b'', 0o755, tarfile.SYMTYPE),
                 ('sdk/file', b'', 0o755, tarfile.LNKTYPE),
                 ('sdk/../escape', b'good', 0o755, tarfile.REGTYPE),
                 ('sdk/extra', b'good', 0o755, tarfile.REGTYPE),
                 ('sdk/file', b'good', 0o644, tarfile.REGTYPE),
                 ('sdk/file', b'evil', 0o755, tarfile.REGTYPE)]
        for i, entry in enumerate(cases):
            target = self.root/f'sdk{i}'; target.mkdir()
            path = self.make_tar([entry])
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                gate.extract_sdk(path, target, expected, self.deadline)

    def test_tar_rejects_normalized_duplicate_directory(self):
        path = self.make_tar([('sdk', b'', 0o755, tarfile.DIRTYPE),
                              ('sdk/', b'', 0o755, tarfile.DIRTYPE)])
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            gate.extract_sdk(path, self.sdk, {'file': {**digest(b''), 'mode': 0o644}}, self.deadline)

    def test_ctest_missing_unknown_unexplained_notrun_and_duplicate_rejected(self):
        for xml, names in (('<testsuite/>', ['pass']),
            ('<testsuite><testcase name="pass" status="weird"/></testsuite>', ['pass']),
            ('<testsuite><testcase name="pass" status="notrun"/></testsuite>', ['pass']),
            ('<testsuite><testcase name="pass" status="run"/></testsuite>', ['pass', 'pass']),
            ('<testsuite/>', [])):
            snapshot = self.ctest(xml, names)
            with self.subTest(xml=xml, names=names), self.assertRaisesRegex(ValueError, 'Incomplete CTest'):
                gate.verify_ctest(self.artifact, snapshot, REVISION, self.deadline)

    def test_ctest_summary_cannot_hide_raw_failure(self):
        snapshot = self.ctest()
        summary = json.loads((self.artifact/'ctest-inventory.json').read_text())
        summary['counts'] = {'passed': 3}; self.write_json('ctest-inventory.json', summary)
        with self.assertRaisesRegex(ValueError, 'Recorded CTest summary'):
            gate.verify_ctest(self.artifact, snapshot, REVISION, self.deadline)

    def test_direct_prerequisite_alias_binds_target_and_bytes(self):
        target = self.root/'libactual.dylib'; target.write_bytes(b'runtime')
        alias = self.root/'libalias.dylib'; alias.symlink_to(target)
        self.write_json('loader-dependencies.json', {'external_prerequisites': {str(alias): {
            'declared_name': str(alias), 'canonical_target': str(target),
            'kind': 'external development prerequisite', 'file': digest(b'runtime')}}})
        gate.external_prerequisites(self.artifact, self.deadline)
        target.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'prerequisite differs'):
            gate.external_prerequisites(self.artifact, self.deadline)
        target.unlink()
        with self.assertRaises(FileNotFoundError): gate.external_prerequisites(self.artifact, self.deadline)

    def test_namespace_detects_links_and_mode_changes(self):
        path = self.sdk/'tool'; path.write_bytes(b'tool'); path.chmod(0o755)
        before = gate.namespace(self.sdk, self.deadline, modes=True)
        path.chmod(0o644); self.assertNotEqual(before, gate.namespace(self.sdk, self.deadline, modes=True))
        (self.sdk/'alias').symlink_to(path)
        with self.assertRaisesRegex(ValueError, 'links or special'): gate.namespace(self.sdk, self.deadline)

    def test_fresh_roots_reject_final_symlink_overlap_and_existing_paths(self):
        alias = self.root/'alias'; alias.symlink_to(self.root/'absent')
        with self.assertRaisesRegex(ValueError, 'symlinks'):
            gate.fresh_directories(alias, self.root/'work', ())
        with self.assertRaisesRegex(ValueError, 'disjoint'):
            gate.fresh_directories(self.root/'new', self.root/'new/work', ())
        with self.assertRaisesRegex(ValueError, 'overlaps artifact'):
            gate.fresh_directories(self.root/'new', self.root/'work', (self.root/'new/input.zip',))
        with self.assertRaisesRegex(ValueError, 'Fresh replay'):
            gate.fresh_directories(self.artifact, self.root/'work', ())

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX special-file control')
    def test_regular_input_rejects_fifo_without_waiting(self):
        path = self.root/'fifo'; os.mkfifo(path)
        with self.assertRaisesRegex(ValueError, 'Nonregular'): gate.record(path, self.deadline)

    def test_expired_deadline_rejects_before_extraction(self):
        path = self.make_tar([('sdk/file', b'good', 0o755, tarfile.REGTYPE)])
        with self.assertRaisesRegex(ValueError, 'deadline expired'):
            gate.extract_sdk(path, self.sdk, {'file': {**digest(b'good'), 'mode': 0o755}}, time.monotonic()-1)


if __name__ == '__main__':
    unittest.main()
