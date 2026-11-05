#!/usr/bin/env python3
"""Pure identity/range/budget controls; these do not execute macOS APIs."""
import copy
import importlib.util
import os
from pathlib import Path
import stat
import struct
import tempfile
import time
import unittest
from unittest.mock import patch

PATH = Path(__file__).with_name('macos-cache-backing-probes.py')
SPEC = importlib.util.spec_from_file_location('cache_backing', PATH)
probe = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(probe)
detail = probe.load(PATH.with_name('macos-cache-detail-probes.py'), 'cache_backing_control_detail')
BASE, SLIDE = 0x100000000, 0x100000
ROOT_UUID, CHILD_UUID = '11' * 16, '22' * 16
DYLD_UUID, LIB_UUID, MAIN_UUID = '33' * 16, '44' * 16, '55' * 16


def cache_bytes(child=False):
    raw = bytearray(4096)
    raw[:16] = b'dyld_v1  arm64e\0'.ljust(16, b'\0')
    struct.pack_into('<II', raw, 16, 552, 1)
    raw[88:104] = bytes.fromhex(CHILD_UUID if child else ROOT_UUID)
    struct.pack_into('<QQ', raw, 224, BASE, 8192)
    struct.pack_into('<II', raw, 312, 640, 1)
    struct.pack_into('<II', raw, 392, 1024, 0 if child else 1)
    address = BASE + (4096 if child else 0)
    struct.pack_into('<QQQII', raw, 552, address, 4096, 0, 5, 5)
    struct.pack_into('<QQQQQQII', raw, 640, address, 4096, 0, 0, 0, 0, 5, 5)
    if not child:
        struct.pack_into('<16sQ32s', raw, 1024, bytes.fromhex(CHILD_UUID), 4096, b'.01')
        struct.pack_into('<QQ', raw, 136, 1536, 2)
        for index, (uuid, offset, path) in enumerate(((DYLD_UUID, 3072, '/usr/lib/dyld'), (LIB_UUID, 3328, '/usr/lib/libfixture.dylib'))):
            position = 1900 + index * 128
            struct.pack_into('<16sQII', raw, 1536 + index * 32, bytes.fromhex(uuid), BASE + offset, 128, position)
            data = path.encode() + b'\0'; raw[position:position + len(data)] = data
    return raw


def native_events():
    rows = [{'event': 'hash-controls', 'passed': 12, 'algorithm': 'SHA256'}]
    for number in (0, 1):
        rows.append({'event': 'cache', 'round': number, 'uuid': ROOT_UUID, 'base': BASE + SLIDE, 'size': 8192,
                     'private': False, 'loader_header': BASE + SLIDE + 3072, 'loader_cache_base': BASE + SLIDE,
                     'loader_cache_slide': SLIDE, 'loader_cache_uuid': ROOT_UUID, 'loader_timestamp': 5})
        for path in ('/cache', '/cache.01'):
            rows.append({'event': 'cache-file', 'round': number, 'path': path})
        for uuid, address, slide, origin, path, flags, kind in (
                (DYLD_UUID, BASE + SLIDE + 3072, SLIDE, 'own-task-loader', '/usr/lib/dyld', 0x80000000, 7),
                (LIB_UUID, BASE + SLIDE + 3328, SLIDE, 'public-callback', '/usr/lib/libfixture.dylib', 0x80000000, 6),
                (MAIN_UUID, 0x500000000, 0x400000000, 'public-callback', '/work/probe', 0, 2)):
            rows.append({'event': 'header', 'round': number, 'origin': origin, 'address': address,
                         'slide': slide, 'text_address': address - slide, 'text_size': 128, 'cpu': 0x100000c,
                         'flags': flags, 'kind': kind, 'uuid': uuid, 'valid': True, 'path': path, 'dladdr_base': address})
            rows.append({'event': 'spi-image', 'round': number, 'path': path if not flags else None,
                         'install_name': path if flags else None, 'uuid': uuid, 'text_address': address, 'text_size': 128})
        rows.append({'event': 'generation', 'round': number, 'before': 3, 'after': 3, 'overflow': False, 'complete': True})
    rows.append({'event': 'images-ended', 'complete': True})
    return rows


class ImageTables(unittest.TestCase):
    def parse(self, raw):
        return probe.image_table(lambda offset, size: bytes(raw[offset:offset + size]), len(raw))

    def test_exact_image_uuid_address_size_and_name(self):
        rows = self.parse(cache_bytes())
        self.assertEqual(rows, [{'uuid': DYLD_UUID, 'address': BASE + 3072, 'size': 128, 'path': '/usr/lib/dyld'},
                                {'uuid': LIB_UUID, 'address': BASE + 3328, 'size': 128, 'path': '/usr/lib/libfixture.dylib'}])

    def test_missing_oversized_truncated_and_overflow_table(self):
        for offset, count in ((1536, 0), (1536, probe.MAX_CACHE_IMAGES + 1), (4090, 2), (probe.U64, 1)):
            raw = cache_bytes(); struct.pack_into('<QQ', raw, 136, offset, count)
            with self.subTest(offset=offset, count=count), self.assertRaises(ValueError): self.parse(raw)

    def test_invalid_uuid_size_duplicate_address_and_duplicate_path(self):
        for action in ('uuid', 'size', 'address', 'path'):
            raw = cache_bytes()
            if action == 'uuid': raw[1536:1552] = bytes(16)
            if action == 'size': struct.pack_into('<I', raw, 1560, 0)
            if action == 'address': struct.pack_into('<Q', raw, 1584, BASE + 3072)
            if action == 'path': struct.pack_into('<I', raw, 1596, 1900)
            with self.subTest(action=action), self.assertRaises(ValueError): self.parse(raw)

    def test_name_outside_unterminated_and_invalid_utf8(self):
        for action in ('outside', 'unterminated', 'utf8'):
            raw = cache_bytes()
            if action == 'outside': struct.pack_into('<I', raw, 1564, 4096)
            if action == 'unterminated': raw[1900:] = b'x' * (4096 - 1900)
            if action == 'utf8': raw[1901] = 255
            with self.subTest(action=action), self.assertRaises((ValueError, UnicodeError)): self.parse(raw)


class Observations(unittest.TestCase):
    def test_public_headers_plus_loader_account_for_all_spi_images(self):
        result = probe.image_observation(native_events())
        self.assertEqual(len(result['headers']), 3)
        self.assertEqual(result['cache']['loader_header'], result['headers'][0]['address'])

    def mutate(self, event, key, value):
        rows = native_events()
        next(row for row in rows if row['event'] == event)[key] = value
        return rows

    def test_generation_change_overflow_false_and_private_profile(self):
        for event, key, value in (('generation', 'after', 4), ('generation', 'overflow', True),
                                  ('generation', 'complete', False), ('cache', 'private', True)):
            with self.subTest(key=key), self.assertRaises(ValueError): probe.image_observation(self.mutate(event, key, value))

    def test_loader_missing_and_wrong_cache_identity(self):
        for key, value in (('loader_header', 1), ('loader_cache_uuid', '66' * 16), ('loader_cache_base', BASE)):
            with self.subTest(key=key), self.assertRaises(ValueError): probe.image_observation(self.mutate('cache', key, value))

    def test_wrong_slide_uuid_and_spi_header_not_silently_accepted(self):
        for event, key, value in (('header', 'slide', SLIDE + 1), ('spi-image', 'uuid', '66' * 16),
                                  ('spi-image', 'text_address', BASE + SLIDE + 3073), ('header', 'valid', False)):
            with self.subTest(key=key), self.assertRaises(ValueError): probe.image_observation(self.mutate(event, key, value))

    def test_extra_or_missing_image_and_unknown_round_rejected(self):
        for kind in ('extra', 'missing', 'round'):
            rows = native_events()
            selected = next(row for row in rows if row['event'] == 'header')
            if kind == 'extra': rows.append(copy.deepcopy(selected))
            if kind == 'missing': rows.remove(selected)
            if kind == 'round': selected['round'] = 2
            with self.subTest(kind=kind), self.assertRaises(ValueError): probe.image_observation(rows)

    def test_false_and_malformed_types_do_not_count_as_success(self):
        for event, key, value in (('images-ended', 'complete', 1), ('header', 'address', True),
                                  ('generation', 'before', True), ('header', 'cpu', 7), ('cache', 'loader_cache_slide', True)):
            with self.subTest(key=key), self.assertRaises(ValueError): probe.image_observation(self.mutate(event, key, value))


class Classification(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()/'cache'; self.child = Path(str(self.root) + '.01')
        self.root.write_bytes(cache_bytes()); self.child.write_bytes(cache_bytes(True))
        budget = {'remaining': probe.MAX_METADATA, 'deadline': time.monotonic() + 10}
        self.files = [probe.observe_cache(self.root, budget, detail, with_images=True), probe.observe_cache(self.child, budget, detail)]
        self.observation = probe.image_observation(native_events())
        self.observation['files'] = list(map(str, (self.root, self.child)))
        self.correspondence = detail.cache_correspondence(self.files, ROOT_UUID, self.observation['files'])

    def classify(self):
        return probe.classify_headers(self.observation, self.files, self.correspondence, 'arm64')

    def test_all_headers_cache_and_ordinary_keep_distinct_identity(self):
        rows = self.classify()
        self.assertEqual([row['kind'] for row in rows], ['cache', 'cache', 'ordinary'])
        self.assertEqual(rows[0]['file_offset'], 3072)
        self.assertTrue(rows[0]['loader'])
        self.assertEqual(rows[2]['path'], '/work/probe')

    def test_copied_same_name_uuid_outside_cache_stays_ordinary(self):
        header = self.observation['headers'][1]; previous = header['address']
        header.update(address=0x600000000, text_address=0x200000000, slide=0x400000000, dladdr_base=0x600000000,
                      path='/owned/copy/libfixture.dylib')
        spi = next(row for row in self.observation['spi'] if row['text_address'] == previous)
        spi['text_address'] = header['address']
        rows = self.classify()
        self.assertEqual(rows[1]['kind'], 'ordinary')
        self.assertEqual(rows[1]['uuid'], LIB_UUID)
        self.assertEqual(rows[1]['path'], '/owned/copy/libfixture.dylib')

    def test_actual_outside_loader_uses_ordinary_path_not_cache_slide(self):
        header = self.observation['headers'][0]; previous = header['address']
        header.update(address=0x600001000, text_address=0x200000000, slide=0x400001000,
                      dladdr_base=0x600001000, path='/owned/alternate-dyld', flags=0)
        self.observation['cache']['loader_header'] = header['address']
        next(row for row in self.observation['spi'] if row['text_address'] == previous)['text_address'] = header['address']
        row = self.classify()[0]
        self.assertTrue(row['loader']); self.assertEqual(row['kind'], 'ordinary')
        self.assertEqual(row['path'], '/owned/alternate-dyld')

    def test_envelope_gap_does_not_count_as_cache_or_ordinary(self):
        header = self.observation['headers'][1]
        header['address'] = BASE + SLIDE + 4090
        self.files[0]['mappings'][0]['size'] = 4000
        with self.assertRaisesRegex(ValueError, 'envelope gap'): self.classify()

    def test_wrong_slide_and_image_uuid_and_exact_address(self):
        for change in ('slide', 'uuid', 'address'):
            observation, files = copy.deepcopy(self.observation), copy.deepcopy(self.files)
            if change == 'slide': self.observation['cache']['loader_cache_slide'] += 1
            if change == 'uuid': self.files[0]['images'][1]['uuid'] = '77' * 16
            if change == 'address': self.files[0]['images'][1]['address'] += 1
            with self.subTest(change=change), self.assertRaises(ValueError): self.classify()
            self.observation, self.files = observation, files

    def test_text_crossing_mapping_or_nonexecutable_mapping_rejected(self):
        for action in ('cross', 'permission'):
            files = copy.deepcopy(self.files)
            if action == 'cross': self.files[0]['mappings'][0]['size'] = 3400
            if action == 'permission': self.files[0]['mappings'][0]['init_prot'] = 1
            with self.subTest(action=action), self.assertRaises(ValueError): self.classify()
            self.files = files

    def test_cache_cpu_duplicate_identity_and_total_bound(self):
        for action in ('cpu', 'inode', 'size'):
            files = copy.deepcopy(self.files)
            if action == 'cpu': self.files[1]['magic'] = 'dyld_v1 x86_64h'
            if action == 'inode': self.files[1]['file_identity'] = self.files[0]['file_identity']
            if action == 'size': self.files[1]['file_bytes'] = probe.MAX_BYTES
            with self.subTest(action=action), self.assertRaises(ValueError): self.classify()
            self.files = files

    def test_outside_image_missing_actual_path_fails(self):
        for key, value in (('path', None), ('path', 'relative'), ('dladdr_base', 0)):
            original = self.observation['headers'][2][key]
            self.observation['headers'][2][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.classify()
            self.observation['headers'][2][key] = original

    def test_retained_metadata_is_exact_and_budget_failures_explicit(self):
        for row in self.files:
            self.assertTrue(row['complete'])
            raw = Path(row['path']).read_bytes()
            for read in row['retained_metadata']:
                self.assertEqual(bytes.fromhex(read['hex']), raw[read['offset']:read['offset'] + read['bytes']])
        for budget in ({'remaining': 1, 'deadline': time.monotonic() + 10}, {'remaining': probe.MAX_METADATA, 'deadline': 0}):
            self.assertFalse(probe.observe_cache(self.root, budget, detail)['complete'])

    def hashes(self):
        rows = [{'event': 'hash-controls', 'passed': 12, 'algorithm': 'SHA256'}]
        total = sum(row['file_bytes'] for row in self.files)
        for number in (0, 1):
            for file in self.files:
                rows.append({'event': 'whole-hash', 'pass': number, 'path': file['canonical_path'], 'bytes': file['file_bytes'],
                             'sha256': 'ab' * 32, 'identity': file['file_identity'], 'identity_unchanged': True,
                             'seconds_including_pread': .1})
            rows.append({'event': 'hash-pass', 'pass': number, 'files': len(self.files), 'bytes': total, 'seconds_including_pread': .2})
        rows.append({'event': 'hash-ended', 'complete': True, 'files': len(self.files), 'bytes_per_pass': total,
                     'total_hashed_bytes': total * 2, 'seconds': .4})
        return rows

    def test_complete_two_pass_hash_observation_never_qualifies(self):
        result = probe.hash_result(self.hashes(), self.files)
        self.assertTrue(result['whole_file_hashes_observed'])
        self.assertFalse(result['qualification'])
        self.assertEqual(result['final']['total_hashed_bytes'], 16384)

    def test_short_changed_duplicate_and_unknown_hash_pass(self):
        for key, value in (('bytes', 1), ('sha256', 'cc' * 32), ('identity_unchanged', False),
                           ('identity', [1, 2, 3, 4, 5]), ('pass', 2), ('path', '/other')):
            rows = self.hashes()
            next(row for row in rows if row['event'] == 'whole-hash' and row['pass'] == 1)[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): probe.hash_result(rows, self.files)
        rows = self.hashes(); rows.append(copy.deepcopy(rows[1]))
        with self.assertRaises(ValueError): probe.hash_result(rows, self.files)

    def test_hash_failure_partial_and_nonfinite_times_rejected(self):
        for action in ('partial', 'failed', 'infinite', 'expired'):
            rows = self.hashes()
            if action == 'partial': rows.pop()
            if action == 'failed': rows.append({'event': 'incomplete', 'error': 'deadline'})
            if action == 'infinite': rows[1]['seconds_including_pread'] = float('nan')
            if action == 'expired': rows[1]['seconds_including_pread'] = probe.HASH_SECONDS + 1
            with self.subTest(action=action), self.assertRaises(ValueError): probe.hash_result(rows, self.files)

    def test_missing_hash_timings_and_boolean_pass_rejected(self):
        for event, key in (('whole-hash', 'seconds_including_pread'), ('hash-pass', 'seconds_including_pread'),
                           ('hash-ended', 'seconds')):
            rows = self.hashes(); next(row for row in rows if row['event'] == event).pop(key)
            with self.subTest(event=event), self.assertRaisesRegex(ValueError, 'timing'): probe.hash_result(rows, self.files)
        rows = self.hashes(); rows[1]['pass'] = False
        with self.assertRaisesRegex(ValueError, 'pass'): probe.hash_result(rows, self.files)

    def test_unreadable_cache_retains_reason_and_partial_metadata(self):
        self.child.unlink()
        row = probe.observe_cache(self.child, {'remaining': probe.MAX_METADATA, 'deadline': time.monotonic() + 5}, detail)
        self.assertFalse(row['complete']); self.assertIn('FileNotFoundError', row['error'])
        self.assertEqual(row['retained_metadata'], [])
        self.root.write_bytes(b'bad header')
        row = probe.observe_cache(self.root, {'remaining': probe.MAX_METADATA, 'deadline': time.monotonic() + 5}, detail)
        self.assertFalse(row['complete']); self.assertIn('error', row)


class Boundaries(unittest.TestCase):
    def test_final_whole_deadline_is_required_for_observed_status(self):
        base = {'observations_complete': True, 'cache_metadata_unchanged': True, 'inputs_unchanged': True,
                'cleanup_verified': True, 'problems': [], 'elapsed_seconds': probe.TOTAL_SECONDS}
        self.assertEqual(probe.final_status(copy.deepcopy(base)), 'OBSERVED')
        for elapsed in (probe.TOTAL_SECONDS + .001, float('inf'), float('nan'), None, True):
            row = copy.deepcopy(base); row['elapsed_seconds'] = elapsed
            with self.subTest(elapsed=elapsed):
                self.assertEqual(probe.final_status(row), 'INCOMPLETE'); self.assertTrue(row['problems'])
        row = copy.deepcopy(base); row['inputs_unchanged'] = False
        self.assertEqual(probe.final_status(row), 'INCOMPLETE')

    def test_integer_overflow_and_boolean_ranges(self):
        for offset, size in ((-1, 1), (True, 1), (0, -1), (probe.U64, 1), (0, probe.U64 + 1)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError): probe.span(offset, size)

    def test_safe_work_and_canonical_alias_overlap(self):
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw).resolve(); root = base/'repo'; root.mkdir(); alias = base/'alias'; alias.symlink_to(root)
            self.assertEqual(probe.paths(root/'build/evidence', base/'work', root), (root/'build/evidence', base/'work'))
            for out, work in ((base/'out', root/'work'), (base/'out', base/'out/sub'), (alias/'evidence', base/'work')):
                with self.subTest(out=out), self.assertRaises(ValueError): probe.paths(out, work, root)

    def test_environment_excludes_loader_and_driver_overrides(self):
        with patch.dict(os.environ, {'DYLD_INSERT_LIBRARIES': '/wrong', 'SDKROOT': '/wrong', 'DEVELOPER_DIR': '/wrong',
                                     'TOOLCHAINS': 'wrong', 'COMPILER_PATH': '/wrong', 'GCC_EXEC_PREFIX': '/wrong',
                                     'CCC_OVERRIDE_OPTIONS': 'wrong', 'GH_TOKEN': 'not retained'}, clear=True):
            values = probe.environment(Path('/work'))
            self.assertEqual(set(values), {'PATH', 'LC_ALL', 'TMPDIR', 'PYTHONDONTWRITEBYTECODE', 'GIT_OPTIONAL_LOCKS'})

    def test_regular_file_alias_seal_and_fifo_rejection(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw).resolve(); file = root/'file'; file.write_bytes(b'content'); alias = root/'alias'; alias.symlink_to(file)
            row = probe.record(alias, time.monotonic() + 10)
            self.assertEqual(row['canonical_path'], str(file)); self.assertEqual(row['bytes'], 7)
            fifo = root/'fifo'; os.mkfifo(fifo)
            with self.assertRaises(ValueError): probe.record(fifo, time.monotonic() + 10)
            with self.assertRaises(ValueError): probe.record(file, time.monotonic() + 10, maximum=3)

    def test_output_event_count_type_and_bytes(self):
        for raw in (b'', b'[]\n', b'{"event":1}\n', b'x' * (probe.MAX_OUTPUT + 1)):
            with self.subTest(size=len(raw)), self.assertRaises(ValueError): probe.events(raw)


if __name__ == '__main__':
    unittest.main()
