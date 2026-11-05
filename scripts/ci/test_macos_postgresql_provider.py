#!/usr/bin/env python3
"""Pure provider archive, identity and execution-bound controls; no native build."""
import copy
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('provider', Path(__file__).with_name('macos-postgresql-provider.py'))
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)


def member(name, *, kind=tarfile.REGTYPE, data=b'fixture', mode=0o644):
    item = tarfile.TarInfo(name)
    item.type, item.mode = kind, mode
    item.size = len(data) if kind == tarfile.REGTYPE else 0
    return item, data


def archive(root, extra=None):
    name = f'postgresql-{provider.VERSION}'
    entries = [member(name, kind=tarfile.DIRTYPE), member(name + '/configure', data=b'#!/bin/sh\n', mode=0o755),
               member(name + '/src/fixture.c', data=b'int fixture;\n')]
    if extra:
        entries.append(extra)
    path = root / 'source.tar.bz2'
    with tarfile.open(path, 'w:bz2') as output:
        for item, data in entries:
            output.addfile(item, io.BytesIO(data) if item.isfile() else None)
    return path, hashlib.sha256(path.read_bytes()).hexdigest()


class ProviderControls(unittest.TestCase):
    def test_standalone_support_imports_without_repository_pythonpath(self):
        helper = Path(provider.__file__).resolve(strict=True)
        code = ('import importlib.util\n'
                f'spec=importlib.util.spec_from_file_location("private_provider", {str(helper)!r})\n'
                'module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)\n'
                'support=module.load("private_qualification", "scripts/sdk_qualification.py")\n'
                'assert callable(support.source_identity)\n'
                'print("standalone SDK dependency import passed")\n')
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([sys.executable, '-I', '-B', '-c', code], cwd=temporary,
                                    capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(result.stdout, b'standalone SDK dependency import passed\n')
        self.assertEqual(result.stderr, b'')

    def test_startup_dependency_failure_retains_failed_report_without_processes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            output, work = root / 'evidence', root / 'work'
            argv = ['provider', '--output', str(output), '--work-dir', str(work)]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(sys, 'platform', 'darwin'), \
                 mock.patch.object(provider.platform, 'machine', return_value='arm64'), \
                 mock.patch.object(provider, 'load', side_effect=ModuleNotFoundError('synthetic startup dependency')), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(provider.main(), 1)
            report = json.loads((output / 'report.json').read_bytes())
            self.assertEqual(report['status'], 'FAIL')
            self.assertEqual(report['phase'], 'initialize')
            self.assertEqual(report['phases'], [])
            self.assertFalse(report['cleanup_verified'])
            self.assertTrue(any('synthetic startup dependency' in error for error in report['problems']))
            self.assertTrue(work.is_dir())

    def test_exact_archive_extracts_regular_files_and_executable_mode(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            path, digest = archive(root)
            destination = root / 'source'
            destination.mkdir()
            result = provider.extract_source(path, destination, time.monotonic() + 10, expected=digest)
            configure = destination / f'postgresql-{provider.VERSION}/configure'
            self.assertEqual(result['entries'], 3)
            self.assertEqual(configure.read_bytes(), b'#!/bin/sh\n')
            self.assertEqual(stat.S_IMODE(configure.stat().st_mode), 0o755)

    def test_digest_mismatch_never_extracts(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True); path, _ = archive(root)
            destination = root / 'source'; destination.mkdir()
            with self.assertRaisesRegex(ValueError, 'digest differs'):
                provider.extract_source(path, destination, time.monotonic() + 10, expected='0' * 64)
            self.assertEqual(list(destination.iterdir()), [])

    def test_release_generated_parser_remains_newer_than_grammar(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            prefix = f'postgresql-{provider.VERSION}'
            path = root / 'release.tar.bz2'
            generated, data = member(prefix + '/gram.c', data=b'generated')
            generated.mtime = 1786396269
            grammar, grammar_data = member(prefix + '/gram.y', data=b'grammar')
            grammar.mtime = 1786395360
            with tarfile.open(path, 'w:bz2') as output:
                output.addfile(member(prefix, kind=tarfile.DIRTYPE)[0])
                output.addfile(generated, io.BytesIO(data))
                output.addfile(grammar, io.BytesIO(grammar_data))
            destination = root / 'source'; destination.mkdir()
            provider.extract_source(path, destination, time.monotonic() + 10,
                                    expected=hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual((destination / generated.name).stat().st_mtime, generated.mtime)
            self.assertGreater((destination / generated.name).stat().st_mtime,
                               (destination / grammar.name).stat().st_mtime)

    def test_paths_duplicates_links_and_special_files_rejected_before_extraction(self):
        prefix = f'postgresql-{provider.VERSION}'
        variants = [member('../escape'), member('/absolute'), member(prefix + '/../escape'),
                    member('other/version'), member(prefix + '/bad\\name'), member(prefix + '/bad\nname'),
                    member(prefix + '/configure'), member(prefix + '/a', kind=tarfile.SYMTYPE),
                    member(prefix + '/a', kind=tarfile.LNKTYPE), member(prefix + '/a', kind=tarfile.FIFOTYPE),
                    member(prefix + '/a', mode=0o4755)]
        for extra in variants:
            with self.subTest(name=extra[0].name, kind=extra[0].type), tempfile.TemporaryDirectory() as name:
                root = Path(name).resolve(strict=True); path, digest = archive(root, extra)
                destination = root / 'source'; destination.mkdir()
                with self.assertRaises(ValueError):
                    provider.extract_source(path, destination, time.monotonic() + 10, expected=digest)
                self.assertEqual(list(destination.iterdir()), [])
                self.assertFalse((root / 'escape').exists())

    def test_archive_member_count_expanded_and_per_member_limits(self):
        root = member(f'postgresql-{provider.VERSION}', kind=tarfile.DIRTYPE)[0]
        file = member(f'postgresql-{provider.VERSION}/file')[0]
        for changed in (-1, provider.MAX_MEMBER + 1):
            item = copy.copy(file); item.size = changed
            with self.subTest(size=changed), self.assertRaises(ValueError):
                provider.inspect_members([root, item])
        with mock.patch.object(provider, 'MAX_ENTRIES', 1), self.assertRaises(ValueError):
            provider.inspect_members([root, file])
        with mock.patch.object(provider, 'MAX_EXPANDED', 1), self.assertRaises(ValueError):
            provider.inspect_members([root, file])

    def test_extract_refuses_nonempty_destination_and_expired_deadline(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True); path, digest = archive(root)
            destination = root / 'source'; destination.mkdir()
            with self.assertRaisesRegex(ValueError, 'deadline'):
                provider.extract_source(path, destination, time.monotonic() - 1, expected=digest)
            self.assertEqual(list(destination.iterdir()), [])
            sentinel = destination / 'sentinel'; sentinel.write_bytes(b'preserve')
            with self.assertRaisesRegex(ValueError, 'fresh empty'):
                provider.extract_source(path, destination, time.monotonic() + 10, expected=digest)
            self.assertEqual(sentinel.read_bytes(), b'preserve')

    def test_record_rejects_links_and_limits(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True); real = root / 'real'; real.write_bytes(b'actual')
            alias = root / 'alias'; alias.symlink_to(real)
            with self.assertRaises(OSError):
                provider.record(alias)
            with self.assertRaises(ValueError):
                provider.record(real, maximum=2)
            if hasattr(os, 'mkfifo'):
                fifo = root / 'fifo'; os.mkfifo(fifo)
                with self.assertRaises(ValueError):
                    provider.record(fifo)

    def test_provider_alias_seal_is_confined_and_observes_target_bytes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True); prefix = root / 'prefix'; prefix.mkdir()
            target = prefix / 'libpq.5.dylib'; target.write_bytes(b'one')
            alias = prefix / 'libpq.dylib'; alias.symlink_to(target.name)
            before = provider.tree_seal(prefix, time.monotonic() + 10, aliases=True)
            self.assertEqual(before['libpq.dylib']['target'], target.name)
            target.write_bytes(b'two')
            self.assertNotEqual(before, provider.tree_seal(prefix, time.monotonic() + 10, aliases=True))
            with self.assertRaises(ValueError):
                provider.tree_seal(prefix, time.monotonic() + 10)
            alias.unlink(); outside = root / 'outside'; outside.write_bytes(b'outside')
            alias.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, 'leaves private prefix'):
                provider.tree_seal(prefix, time.monotonic() + 10, aliases=True)

    def layout(self, root):
        prefix = root / 'prefix'; prefix.mkdir()
        paths = {key: prefix / value for key, value in
                 [('include_dir', 'include'), ('library_dir', 'lib'), ('bin_dir', 'bin'), ('share_dir', 'share')]}
        for path in paths.values(): path.mkdir()
        for key, filename in [('include_dir', 'libpq-fe.h'), ('include_dir', 'pg_config.h'),
                              ('library_dir', 'libpq.dylib'), ('share_dir', 'postgres.bki')]:
            (paths[key] / filename).write_bytes(b'fixture')
        for binary in ('pg_config', 'postgres', 'initdb', 'pg_ctl', 'psql'):
            path = paths['bin_dir'] / binary; path.write_bytes(b'executable'); path.chmod(0o755)
        raw = ('PostgreSQL ' + provider.VERSION + '\n' + '\n'.join(str(p) for p in paths.values()) + '\n').encode()
        return prefix, paths, raw

    def test_coherent_layout_requires_all_server_and_client_inputs(self):
        with tempfile.TemporaryDirectory() as name:
            prefix, paths, raw = self.layout(Path(name).resolve(strict=True))
            self.assertEqual(provider.parse_layout(raw, prefix), {key: str(path) for key, path in paths.items()})
            (paths['bin_dir'] / 'postgres').unlink()
            with self.assertRaises(FileNotFoundError):
                provider.parse_layout(raw, prefix)

    def test_layout_rejects_wrong_version_extra_output_and_external_alias(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True); prefix, paths, raw = self.layout(root)
            for changed in (raw.replace(b'16.15', b'18.6'), raw + b'extra\n'):
                with self.assertRaises(ValueError): provider.parse_layout(changed, prefix)
            target = root / 'external.dylib'; target.write_bytes(b'external')
            library = paths['library_dir'] / 'libpq.dylib'; library.unlink(); library.symlink_to(target)
            with self.assertRaisesRegex(ValueError, 'leaves private prefix'):
                provider.parse_layout(raw, prefix)

    def test_header_runtime_and_canonical_loader_identity_all_must_match(self):
        expected = Path('/private/provider/lib/libpq.5.dylib')
        raw = f'header_version=160015\nruntime_version=160015\nruntime_path={expected}\n'.encode()
        self.assertEqual(provider.parse_preflight(raw, expected)['path'], str(expected))
        for changed in (raw.replace(b'header_version=160015', b'header_version=180006'),
                        raw.replace(b'runtime_version=160015', b'runtime_version=160014'),
                        raw.replace(b'/private/provider/', b'/unrelated/'), raw + b'header_version=160015\n', b''):
            with self.subTest(raw=changed), self.assertRaises(ValueError):
                provider.parse_preflight(changed, expected)

    def test_environment_only_admits_declared_loader_and_apple_profile(self):
        incoming = {'HOME': '/user', 'USER': 'runner', 'PATH': '/bad', 'DYLD_LIBRARY_PATH': '/wrong',
                    'DYLD_FALLBACK_LIBRARY_PATH': '/wrong', 'PGSERVICE': 'ambient', 'CC': 'wrong',
                    'DEVELOPER_DIR': '/wrong', 'TOOLCHAINS': 'wrong', 'MAKEFLAGS': '--jobs=100',
                    'CONFIG_SITE': '/wrong', 'CURL_CA_BUNDLE': '/wrong', 'http_proxy': 'wrong', 'AR': 'wrong'}
        env = provider.provider_environment(Path('/temporary'), Path('/provider/lib'), incoming)
        self.assertEqual(env['HOME'], incoming['HOME'])
        self.assertEqual(env['PATH'], '/usr/bin:/bin:/usr/sbin:/sbin')
        self.assertEqual(env['DYLD_FALLBACK_LIBRARY_PATH'], '/provider/lib:/usr/lib')
        self.assertEqual(env['CONFIG_SITE'], '/dev/null')
        for key in incoming.keys() - {'HOME', 'USER', 'PATH', 'DYLD_FALLBACK_LIBRARY_PATH', 'CONFIG_SITE'}:
            self.assertNotIn(key, env)
        self.assertFalse(any(k.startswith('DYLD_') for k in provider.provider_environment(Path('/temporary'), inherited=incoming)))

    def test_one_work_deadline_bounds_every_phase(self):
        self.assertEqual(provider.remaining_timeout(100, 30, now=0), 30)
        self.assertEqual(provider.remaining_timeout(100, 30, now=95), 5)
        for now in (100, 101):
            with self.assertRaisesRegex(ValueError, 'deadline'):
                provider.remaining_timeout(100, 30, now=now)

    def test_fresh_evidence_and_work_are_exclusive_disjoint_and_private(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            output, work = provider.fresh_roots(root / 'evidence', root / 'work')
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(work.stat().st_mode), 0o700)
            for evidence, build in ((output, root / 'other'), (root / 'same', root / 'same')):
                with self.assertRaises(ValueError): provider.fresh_roots(evidence, build)
            link = root / 'link'; link.symlink_to(root / 'absent')
            with self.assertRaises(ValueError): provider.fresh_roots(root / 'new', link)

    def valid_report(self, root):
        prefix, paths, _ = self.layout(root)
        manifest = root / 'provider-after.json'
        provider.save(manifest, provider.tree_seal(prefix, time.monotonic() + 10, aliases=True))
        library = paths['library_dir'] / 'libpq.dylib'
        result = {'schema': 'nil.macos.postgresql-provider.v1', 'status': 'PASS', 'qualification': False,
                  'version': provider.VERSION, 'architecture': 'arm64', 'work_dir': str(root), 'prefix': str(prefix),
                  **{key: str(path) for key, path in paths.items()},
                  'provider_manifest': {'path': str(manifest), **provider.record(manifest)},
                  'runtime': {'path': str(library), 'declared_path': str(library),
                              'version': provider.VERSION_NUMBER, 'header_version': provider.VERSION_NUMBER,
                              **provider.record(library)}}
        result.update({key: True for key in ('source_unchanged', 'release_source_unchanged', 'provider_unchanged',
                                            'archive_unchanged', 'cleanup_verified')})
        return result

    def test_read_only_validation_accepts_seal_then_rejects_payload_mutation(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            report = self.valid_report(root)
            self.assertIs(provider.validate_provider(report), report)
            binary = Path(report['bin_dir']) / 'postgres'
            binary.write_bytes(b'changed executable')
            with self.assertRaisesRegex(ValueError, 'sealed manifest'):
                provider.validate_provider(report)

    def test_validation_rejects_missing_proof_manifest_and_runtime_mismatch(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(strict=True)
            original = self.valid_report(root)
            changes = [('version', '18.6'), ('status', 'FAIL'), ('source_unchanged', False),
                       ('cleanup_verified', False), ('qualification', True)]
            for key, value in changes:
                report = copy.deepcopy(original); report[key] = value
                with self.subTest(key=key), self.assertRaises(ValueError): provider.validate_provider(report)
            for key, value in [('version', 160014), ('version', 160015.0), ('path', '/wrong/libpq.dylib'), ('sha256', '0' * 64)]:
                report = copy.deepcopy(original); report['runtime'][key] = value
                with self.subTest(runtime=key), self.assertRaises(ValueError): provider.validate_provider(report)
            manifest = Path(original['provider_manifest']['path'])
            manifest.write_text(json.dumps({'changed': True}))
            with self.assertRaisesRegex(ValueError, 'manifest digest'):
                provider.validate_provider(original)


if __name__ == '__main__':
    unittest.main()
