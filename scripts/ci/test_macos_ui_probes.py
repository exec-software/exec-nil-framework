#!/usr/bin/env python3
"""Pure preflight binding controls; no native desktop execution claim."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('macos_ui_probes', Path(__file__).with_name('macos-ui-probes.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class ProbeControls(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.repo = self.base/'source'; self.repo.mkdir()

    def cocoa(self):
        return dict(schema=1, qualification=False, main_thread=True, session=True, on_console=True,
                    login_done=True, post_event_access=False, ax_trusted=False, window_created=True,
                    visible=True, event_ready=True, event_queued=True, own_event_delivered=True, own_close_delivered=True,
                    dispatch_count=1, view_delivery_count=1, wrong_target=False,
                    exception=False, cleanup_verified=True)

    def test_own_queue_success_does_not_claim_external_input_permission(self):
        result = gate.cocoa_result(json.dumps(self.cocoa()).encode())
        self.assertTrue(result['own_window_feasible'])
        self.assertFalse(result['qualification'])
        self.assertFalse(result['post_event_access'])
        self.assertFalse(result['ax_trusted'])

    def test_missing_event_or_close_is_not_feasible(self):
        for key in ('own_event_delivered', 'own_close_delivered', 'cleanup_verified', 'event_queued', 'event_ready'):
            with self.subTest(key=key):
                value = self.cocoa(); value[key] = False
                self.assertFalse(gate.cocoa_result(json.dumps(value).encode())['own_window_feasible'])

    def test_queued_event_without_app_dispatch_or_view_delivery_is_insufficient(self):
        for name, value in (('dispatch_count', 0), ('view_delivery_count', 0),
                            ('dispatch_count', True), ('view_delivery_count', 2), ('wrong_target', True)):
            with self.subTest(name=name, value=value):
                report = self.cocoa(); report[name] = value
                self.assertFalse(gate.cocoa_result(json.dumps(report).encode())['own_window_feasible'])

    def test_observation_flags_require_booleans(self):
        value = self.cocoa(); value['post_event_access'] = 1
        with self.assertRaises(ValueError): gate.cocoa_result(json.dumps(value).encode())
        value = self.cocoa(); del value['cleanup_verified']
        with self.assertRaises(ValueError): gate.cocoa_result(json.dumps(value).encode())

    def test_report_scope_and_bounds(self):
        value = self.cocoa(); value['qualification'] = True
        with self.assertRaises(ValueError): gate.cocoa_result(json.dumps(value).encode())
        with self.assertRaises(ValueError): gate.cocoa_result(b' ' * 8193)

    def test_lines_reject_duplicate_malformed_or_binary_observation(self):
        for raw in (b'a=b\na=c\n', b'no delimiter\n', b'a=\0bad\n', b'UPPER=value\n', b'a=\xff'):
            with self.subTest(raw=raw):
                with self.assertRaises((ValueError, UnicodeError)): gate.parse_lines(raw)

    def dependency_fixture(self):
        values = {'operations': 'passed'}
        packages = {}
        for name, module, version in (('glib', 'glib-2.0', '2.88.3'), ('cairo', 'cairo', '1.18.4'),
                                      ('harfbuzz', 'harfbuzz', '14.3.1')):
            values[name + '_header'] = version; values[name + '_runtime'] = version
            packages[module] = {'version': version}
        for name in ('glib', 'gobject', 'gio', 'cairo', 'harfbuzz', 'harfbuzz_subset'):
            values[name + '_path'] = '/declared/lib/' + name + '.dylib'
        return values, packages

    def test_dependency_header_runtime_and_pkg_version_must_agree(self):
        values, packages = self.dependency_fixture()
        encode = lambda v: ''.join(k + '=' + value + '\n' for k, value in v.items()).encode()
        self.assertTrue(gate.dependency_result(encode(values), packages)[1])
        values['glib_runtime'] = '2.90.1'
        self.assertFalse(gate.dependency_result(encode(values), packages)[1])
        values, packages = self.dependency_fixture(); packages['cairo']['version'] = '1.18.6'
        self.assertFalse(gate.dependency_result(encode(values), packages)[1])

    def test_dependency_output_requires_all_origins(self):
        values, packages = self.dependency_fixture(); del values['cairo_path']
        raw = ''.join(k + '=' + value + '\n' for k, value in values.items()).encode()
        with self.assertRaisesRegex(ValueError, 'Incomplete'): gate.dependency_result(raw, packages)

    def test_environment_drops_inherited_loader_and_compiler_overrides(self):
        with mock.patch.dict(os.environ, {'HOME': '/home/known', 'DYLD_LIBRARY_PATH': '/bad',
            'LD_PRELOAD': '/bad', 'PKG_CONFIG_PATH': '/bad', 'CXXFLAGS': '-bad', 'PATH': '/bad'}, clear=True):
            result = gate.environment(self.base, Path('/declared'))
            self.assertNotIn('DYLD_LIBRARY_PATH', result)
            self.assertNotIn('LD_PRELOAD', result)
            self.assertNotIn('CXXFLAGS', result)
            self.assertNotIn('PKG_CONFIG_PATH', result)
            self.assertEqual(os.environ['PATH'], '/bad')
            self.assertEqual(result['PKG_CONFIG_LIBDIR'], '/declared/lib/pkgconfig:/declared/share/pkgconfig')

    def test_sdk_metadata_is_explicit_without_rebasing_homebrew_paths(self):
        compatibility = Path('/declared/Library/Homebrew/os/mac/pkgconfig/15')
        with mock.patch.dict(os.environ, {'PKG_CONFIG_SYSROOT_DIR': '/bad', 'SDKROOT': '/bad'}, clear=True):
            actual = gate.environment(self.base, Path('/declared'), compatibility)
        self.assertEqual(actual['PKG_CONFIG_LIBDIR'],
                         '/declared/lib/pkgconfig:/declared/share/pkgconfig:' + str(compatibility))
        self.assertNotIn('PKG_CONFIG_SYSROOT_DIR', actual)
        self.assertNotIn('SDKROOT', actual)
        self.assertEqual(gate.pkg_command(Path('/declared/bin/pkg-config'), Path('/Apple SDK')),
                         [Path('/declared/bin/pkg-config'), '--define-variable=homebrew_sdkroot=/Apple SDK'])

    def test_compatibility_directory_is_observed_exactly_within_prefix(self):
        prefix = self.base/'brew'; profile = prefix/'Library/Homebrew/os/mac/pkgconfig/15'
        profile.mkdir(parents=True)
        self.assertEqual(gate.compatibility_directory('/other:' + str(profile), prefix, '15'), profile)
        for raw, root, major in ((str(profile), prefix, '14'), (str(profile), self.repo, '15'),
                                  ('/missing', prefix, '15'), (str(profile) + '\n', prefix, '15')):
            with self.subTest(raw=raw, root=root, major=major):
                with self.assertRaises(ValueError): gate.compatibility_directory(raw, root, major)
        duplicate = prefix/'Homebrew/Library/Homebrew/os/mac/pkgconfig/15'; duplicate.mkdir(parents=True)
        with self.assertRaises(ValueError): gate.compatibility_directory(str(profile) + ':' + str(duplicate), prefix, '15')

    def test_fresh_build_evidence_and_external_work_are_allowed(self):
        actual = gate.paths(self.repo/'build/evidence', self.base/'work', self.repo)
        self.assertEqual(actual, (self.repo/'build/evidence', self.base/'work'))

    def test_parent_alias_does_not_hide_overlap_or_checkout_work(self):
        alias = self.base/'alias'; alias.symlink_to(self.repo, target_is_directory=True)
        with self.assertRaises(ValueError): gate.paths(self.base/'evidence', alias/'work', self.repo)
        with self.assertRaises(ValueError): gate.paths(self.base/'work/evidence', self.base/'work', self.repo)
        with self.assertRaises(ValueError): gate.paths(alias/'docs/evidence', self.base/'work', self.repo)

    def test_existing_output_rejected(self):
        output = self.base/'evidence'; output.mkdir()
        with self.assertRaises(ValueError): gate.paths(output, self.base/'work', self.repo)

    def test_seal_records_selected_symlink_target_and_bytes(self):
        target = self.base/'file'; target.write_bytes(b'exact input')
        link = self.base/'alias'; link.symlink_to(target)
        row = gate.record(link, time.monotonic() + 2)
        self.assertEqual(row['canonical_path'], str(target))
        self.assertEqual(row['bytes'], 11)
        self.assertEqual(row, gate.record(link, time.monotonic() + 2))

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX FIFO control')
    def test_special_input_cannot_block_sealing(self):
        fifo = self.base/'fifo'; os.mkfifo(fifo)
        with self.assertRaisesRegex(ValueError, 'nonregular'): gate.record(fifo, time.monotonic() + 2)


if __name__ == '__main__':
    unittest.main()
