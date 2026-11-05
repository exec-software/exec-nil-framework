#!/usr/bin/env python3
"""Pure directed-probe controls; no desktop or native delivery claims."""
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import time
import unittest

SPEC = importlib.util.spec_from_file_location('directed_probe', Path(__file__).with_name('macos-ui-directed-probes.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class Controls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve(); self.root = self.base/'repo'; self.root.mkdir()

    def ready(self):
        return dict(pid=123, window_number=17, active=True, key_window=True, field_editor=True,
                    observer_subclass=True, main_thread=True)

    def final(self):
        return dict(ready=True, typed=True, native_close=True, cleanup_verified=True, success=True,
                    unexpected_input=False, expired=False, exception=False, marker_dispatches=8,
                    text_callbacks=4, window_should_close=1, window_did_close=1)

    def driver(self, phase):
        return dict(identity_verified=True, post_event_access=True, ax_trusted=True, own_window_selected=True,
                    focus_verified=True, marker_readback=phase == 'keys', events_posted=8 if phase == 'keys' else 0,
                    close_action=phase == 'close', success=True, focus_observation=self.focus())

    def focus(self):
        return dict(legacy_window_error=0, legacy_window_boolean=True, legacy_window_value=False,
                    app_window_error=0, app_window_element=True, app_window_pid_matches=True, app_window_matches=True,
                    field_error=0, field_element=True, field_pid_matches=True, field_role_error=0,
                    field_text_role=True, field_role_category='text_field', field_window_error=0, field_window_element=True,
                    field_window_pid_matches=True, field_window_matches=True)

    def test_private_fresh_disjoint_paths(self):
        self.assertEqual(gate.paths(self.root/'build/evidence', self.base/'work', self.root),
                         (self.root/'build/evidence', self.base/'work'))

    def test_overlap_existing_checkout_and_symlink_rejected(self):
        link = self.base/'link'; link.symlink_to(self.root, target_is_directory=True)
        for output, work in ((self.root, self.base/'work'), (self.base/'output', self.root/'work'),
                             (self.root/'evidence', self.base/'work'), (self.base/'output', self.base/'output/work'),
                             (self.base/'output', link)):
            with self.subTest(output=output, work=work), self.assertRaises(ValueError): gate.paths(output, work, self.root)

    def test_regular_input_seal_has_identity(self):
        path = self.base/'file'; path.write_bytes(b'private')
        row = gate.record(path, time.monotonic() + 1)
        self.assertEqual(row['bytes'], 7); self.assertEqual(row['inode'], path.stat().st_ino)
        self.assertEqual(row, gate.record(path, time.monotonic() + 1))

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX FIFO control')
    def test_nonregular_input_never_blocks(self):
        path = self.base/'fifo'; os.mkfifo(path)
        with self.assertRaises(ValueError): gate.record(path, time.monotonic() + 1)

    def test_input_deadline(self):
        path = self.base/'file'; path.write_bytes(b'x')
        with self.assertRaises(ValueError): gate.record(path, time.monotonic() - 1)

    def test_native_scope_and_duplicate_fields(self):
        raw = b'{"schema":1,"phase":"keys","qualification":false}'
        self.assertEqual(gate.decode(raw, 'keys')['phase'], 'keys')
        for value in (raw.replace(b'false', b'true'), raw.replace(b'1', b'true'), raw.replace(b'keys', b'close'),
                      b'{"schema":1,"schema":1,"phase":"keys","qualification":false}', b'x' * 8193):
            with self.subTest(raw=value[:100]), self.assertRaises((ValueError, json.JSONDecodeError)):
                gate.decode(value, 'keys')

    def test_ready_requires_exact_own_pid_and_window(self):
        gate.ready_frame(self.ready(), 123)
        for key, value in (('pid', 124), ('pid', True), ('window_number', 0), ('window_number', True)):
            row = self.ready(); row[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): gate.ready_frame(row, 123)

    def test_ready_requires_real_editor_main_thread_observer(self):
        for key in ('active', 'key_window', 'field_editor', 'observer_subclass', 'main_thread'):
            row = self.ready(); row[key] = False
            with self.subTest(key=key), self.assertRaises(ValueError): gate.ready_frame(row, 123)

    def test_text_requires_actual_marked_callbacks(self):
        original = dict(text='ny42', callbacks=4, marker_dispatches=8, during_marked_dispatch=True)
        gate.typed_frame(original)
        for key, value in (('text', 'ny4'), ('text', 'other'), ('callbacks', 1), ('callbacks', True),
                           ('marker_dispatches', 7), ('during_marked_dispatch', False)):
            row = dict(original); row[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): gate.typed_frame(row)

    def test_final_requires_all_eight_markers_and_one_native_close(self):
        gate.final_frame(self.final())
        for key, value in (('marker_dispatches', 7), ('text_callbacks', 3), ('window_should_close', 0),
                           ('window_did_close', 0), ('window_did_close', 2), ('window_did_close', True)):
            row = self.final(); row[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): gate.final_frame(row)

    def test_failure_cleanup_is_not_delivery(self):
        for key in ('ready', 'typed', 'native_close', 'success'):
            row = self.final(); row[key] = False
            with self.subTest(key=key), self.assertRaises(ValueError): gate.final_frame(row)
        for key in ('unexpected_input', 'expired', 'exception'):
            row = self.final(); row[key] = True
            with self.subTest(key=key), self.assertRaises(ValueError): gate.final_frame(row)

    def test_successful_driver_requires_capabilities_and_real_action(self):
        for phase in ('keys', 'close'):
            self.assertTrue(gate.driver_frame(self.driver(phase), phase, 0))
            for key in ('identity_verified', 'post_event_access', 'ax_trusted', 'own_window_selected', 'focus_verified'):
                row = self.driver(phase); row[key] = False; row['success'] = False
                if key == 'focus_verified': row['focus_observation']['app_window_matches'] = False
                self.assertFalse(gate.driver_frame(row, phase, 2))
                with self.assertRaises(ValueError): gate.driver_frame(row, phase, 0)

    def test_missing_marker_or_close_is_explicit_failure(self):
        for phase, key in (('keys', 'marker_readback'), ('close', 'close_action')):
            row = self.driver(phase); row[key] = False; row['success'] = False
            self.assertFalse(gate.driver_frame(row, phase, 2))
        row = self.driver('keys'); row['events_posted'] = True
        with self.assertRaises(ValueError): gate.driver_frame(row, 'keys', 0)

    def test_driver_arguments_keep_full_lifetime_and_executable(self):
        binary = self.base/'probe'; binary.write_bytes(b'binary')
        seal = gate.record(binary, time.monotonic() + 1)
        identity = dict(pid=123, created=(2**63 + 3) << 32 | 0xffffffff)
        argv = gate.driver_arguments(binary, 'keys', identity, 456, seal, 'a' * 32, 789)
        self.assertEqual(argv, list(map(str, [binary, 'keys', 123, 2**63 + 3, 0xffffffff, 456,
            seal['device'], seal['inode'], binary, 'a' * 32, 789])))

    def test_invalid_driver_identity_rejected_before_launch(self):
        binary = self.base/'probe'; binary.write_bytes(b'binary'); seal = gate.record(binary, time.monotonic() + 1)
        for identity in (dict(pid=True, created=1 << 32), dict(pid=0, created=1 << 32),
                         dict(pid=123, created=1), dict(pid=123, created=1 << 96), dict(pid=123, created=-1)):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                gate.driver_arguments(binary, 'keys', identity, 456, seal, 'a' * 32, 789)
        for marker in (0, True, 2**63):
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                gate.driver_arguments(binary, 'keys', dict(pid=123, created=1 << 32), 456, seal, 'a' * 32, marker)

    def test_wrong_executable_binding_rejected(self):
        binary = self.base/'probe'; binary.write_bytes(b'binary'); seal = gate.record(binary, time.monotonic() + 1)
        with self.assertRaises(ValueError):
            gate.driver_arguments(self.base/'different', 'keys', dict(pid=123, created=1 << 32),
                                  456, seal, 'a' * 32, 789)

    def test_split_frames_preserve_exact_raw_bytes(self):
        raw = b'{"schema":1,"phase":"ready","qualification":false}\n'
        class Reader:
            def __init__(self): self.parts = iter((raw[:10], raw[10:], b''))
            def read(self, _timeout): return next(self.parts)
        sink = io.BytesIO(); frames = gate.Frames(Reader(), sink)
        self.assertEqual(frames.next('ready', time.monotonic() + 1)['phase'], 'ready')
        frames.drain(time.monotonic() + 1)
        self.assertEqual(sink.getvalue(), raw)

    def test_eof_wrong_phase_and_oversize_fail(self):
        class Reader:
            def __init__(self, part): self.part = part
            def read(self, _timeout): return self.part
        for raw in (b'', b'x' * 16385, b'{"schema":1,"phase":"typed","qualification":false}\n'):
            with self.subTest(raw=raw[:100]), self.assertRaises(ValueError):
                gate.Frames(Reader(raw), io.BytesIO()).next('ready', time.monotonic() + 1)

    def test_late_stdout_is_retained_and_rejected(self):
        class Reader:
            def __init__(self): self.parts = iter((b'trailing\n', b'more\n', b''))
            def read(self, _timeout): return next(self.parts)
        sink = io.BytesIO(); frames = gate.Frames(Reader(), sink)
        with self.assertRaisesRegex(ValueError, 'trailing target stdout'):
            frames.drain(time.monotonic() + 1, reject_new=True)
        self.assertEqual(sink.getvalue(), b'trailing\nmore\n')
        self.assertTrue(frames.eof)

    def test_observed_eof_cleanup_does_not_wait_for_second_eof(self):
        class Reader:
            def read(self, _timeout): return b''
        frames = gate.Frames(Reader(), io.BytesIO())
        with self.assertRaises(ValueError): frames.next('ready', time.monotonic() + 1)
        frames.drain(time.monotonic() - 1)

    def test_application_window_and_field_identity_define_focus(self):
        self.assertTrue(gate.focus_proof(self.focus()))
        value = self.focus(); value['legacy_window_error'] = -25205; value['legacy_window_boolean'] = False
        self.assertTrue(gate.focus_proof(value))
        for key in ('app_window_element', 'app_window_pid_matches', 'app_window_matches', 'field_element',
                    'field_pid_matches', 'field_text_role', 'field_window_element', 'field_window_pid_matches',
                    'field_window_matches'):
            value = self.focus(); value[key] = False
            if key == 'field_text_role': value['field_role_category'] = 'text_area'
            with self.subTest(key=key): self.assertFalse(gate.focus_proof(value))

    def test_failed_focus_queries_cannot_pass(self):
        for key in ('app_window_error', 'field_error', 'field_role_error', 'field_window_error'):
            value = self.focus(); value[key] = -25205
            with self.subTest(key=key): self.assertFalse(gate.focus_proof(value))
        self.assertFalse(gate.focus_proof({}))

    def test_focus_diagnostics_are_bounded_and_cannot_add_private_text(self):
        for key, item in (('field_role_error', True), ('field_error', -99999), ('field_text_role', 'AXTextField'),
                          ('external_title', 'not retained')):
            value = self.focus(); value[key] = item
            with self.subTest(key=key), self.assertRaises(ValueError): gate.focus_proof(value)

    def test_focus_summary_cannot_override_wrong_window(self):
        value = self.driver('keys'); value['focus_observation']['field_window_matches'] = False
        with self.assertRaisesRegex(ValueError, 'Focus proof/summary mismatch'): gate.driver_frame(value, 'keys', 0)


if __name__ == '__main__':
    unittest.main()
