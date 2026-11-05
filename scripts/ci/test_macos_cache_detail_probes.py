#!/usr/bin/env python3
"""Pure malformed-metadata/range controls; no claim of native SPI availability."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import time
import unittest

PATH = Path(__file__).with_name('macos-cache-detail-probes.py')
spec = importlib.util.spec_from_file_location('cache_detail', PATH)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
BASE = 0x100000000
ROOT_UUID = bytes(range(1, 17))
CHILD_UUID = bytes(range(17, 33))


def fixture(*, child=False, modern=True, subcache=True):
    raw = bytearray(4096)
    raw[:16] = b'dyld_v1  arm64e\0'.ljust(16, b'\0')
    header_size = 552 if modern else 400
    struct.pack_into('<II', raw, 16, header_size, 1)
    raw[88:104] = CHILD_UUID if child else ROOT_UUID
    struct.pack_into('<QQ', raw, 40, 4096, 0)
    struct.pack_into('<QQ', raw, 224, BASE, 8192)
    struct.pack_into('<II', raw, 312, 640, 1)
    struct.pack_into('<II', raw, 392, 1024, int(not child and subcache))
    address = BASE + (4096 if child else 0)
    struct.pack_into('<QQQII', raw, header_size, address, 4096, 0, 5, 5)
    struct.pack_into('<QQQQQQII', raw, 640, address, 4096, 0, 2048, 16, 0, 5, 5)
    if not child and subcache:
        if modern:
            struct.pack_into('<16sQ32s', raw, 1024, CHILD_UUID, 4096, b'.01')
        else:
            struct.pack_into('<16sQ', raw, 1024, CHILD_UUID, 4096)
    return raw


def parsed(raw):
    return probe.parse_cache(lambda offset, size: bytes(raw[offset:offset + size]), len(raw))


class Tables(unittest.TestCase):
    def test_current_subcache_offset_and_slide_location(self):
        row = parsed(fixture())
        self.assertEqual(row['subcache_entry_bytes'], 56)
        self.assertEqual(row['subcaches'], [{'uuid': CHILD_UUID.hex(), 'vm_offset': 4096, 'suffix': '.01'}])
        self.assertEqual(row['slide_mappings'][0]['slide_offset'], 2048)
        self.assertFalse(row['range_identity_complete'])

    def test_v1_uses_header_extent_not_cache_subtype_value(self):
        row = parsed(fixture(modern=False))
        self.assertEqual(row['subcache_entry_bytes'], 24)
        self.assertEqual(row['subcaches'][0]['suffix'], '.1')
        raw = fixture(); struct.pack_into('<I', raw, 456, 0)
        self.assertEqual(parsed(raw)['subcache_entry_bytes'], 56)

    def test_unknown_magic_and_truncated_header_rejected(self):
        for raw in (b'', fixture()[:200], b'dyld_v9' + bytes(fixture()[7:]), b'other' + bytes(fixture()[5:])):
            with self.subTest(length=len(raw)), self.assertRaises(ValueError):
                parsed(raw)

    def test_oversized_header_and_mapping_count_rejected(self):
        for offset, value in ((16, 8192), (20, 0), (20, 129), (316, 129), (396, 129)):
            raw = fixture(); struct.pack_into('<I', raw, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                parsed(raw)

    def test_subcache_truncation_does_not_synthesize_entry(self):
        raw = fixture(); struct.pack_into('<I', raw, 392, len(raw) - 20)
        with self.assertRaises(ValueError):
            parsed(raw)

    def test_subcache_suffix_path_escape_and_unterminated_rejected(self):
        for suffix in (b'../bad', b'.a/b', b'.', b'.a..b', b'A' * 32):
            raw = fixture(); struct.pack_into('<32s', raw, 1048, suffix)
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                parsed(raw)

    def test_duplicate_subcache_uuid_suffix_and_vm_offset_rejected(self):
        for duplicate in ('uuid', 'suffix', 'offset'):
            raw = fixture(); struct.pack_into('<I', raw, 396, 2)
            struct.pack_into('<16sQ32s', raw, 1080,
                             CHILD_UUID if duplicate == 'uuid' else bytes(range(33, 49)),
                             4096 if duplicate == 'offset' else 8192,
                             b'.01' if duplicate == 'suffix' else b'.02')
            with self.subTest(duplicate=duplicate), self.assertRaises(ValueError):
                parsed(raw)

    def test_slide_mapping_disagreement_rejected(self):
        raw = fixture(); struct.pack_into('<Q', raw, 640, BASE + 4096)
        with self.assertRaisesRegex(ValueError, 'disagree'):
            parsed(raw)

    def test_slide_info_outside_file_rejected(self):
        raw = fixture(); struct.pack_into('<QQ', raw, 664, 4090, 16)
        with self.assertRaises(ValueError):
            parsed(raw)

    def test_mapping_file_range_overflow_rejected(self):
        raw = fixture(); struct.pack_into('<Q', raw, 568, probe.MAX_U64)
        with self.assertRaises(ValueError):
            parsed(raw)

    def test_mapping_vm_overflow_and_overlap_rejected(self):
        raw = fixture(); struct.pack_into('<Q', raw, 552, probe.MAX_U64 - 2)
        with self.assertRaises(ValueError):
            parsed(raw)
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            probe.disjoint([{'a': 10, 's': 4}, {'a': 13, 's': 4}], 'a', 's')

    def test_range_integer_and_unsigned_constraints(self):
        for offset, size in ((-1, 1), (True, 1), (0, -1), (probe.MAX_U64, 1), (0, probe.MAX_U64 + 1)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                probe.span(offset, size, probe.MAX_U64)


class Correspondence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve() / 'cache'
        self.child = Path(str(self.root) + '.01')
        self.root.write_bytes(fixture()); self.child.write_bytes(fixture(child=True))

    def rows(self):
        budget = {'remaining_bytes': probe.MAX_METADATA, 'deadline': time.monotonic() + 10}
        return [probe.observe_cache(path, budget) for path in (self.root, self.child)]

    def reconcile(self, rows=None, paths=None, uuid=None):
        return probe.cache_correspondence(self.rows() if rows is None else rows,
                                          ROOT_UUID.hex() if uuid is None else uuid,
                                          [str(self.root), str(self.child)] if paths is None else paths)

    def test_root_api_subcache_and_vm_correspondence(self):
        result = self.reconcile()
        self.assertEqual(result['subcache_matches'][0]['first_mapping_address'], BASE + 4096)
        self.assertTrue(result['api_table_files_equal'])
        self.assertEqual(result['root_path'], str(self.root))

    def test_missing_file_and_wrong_root_remain_explicit(self):
        with self.assertRaisesRegex(ValueError, 'root UUID'):
            self.reconcile(uuid='ff' * 16)
        self.child.unlink()
        rows = self.rows()
        self.assertFalse(rows[1]['complete'])
        self.assertIn('FileNotFoundError', rows[1]['error'])
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.reconcile(rows)

    def test_extra_api_file_and_missing_subcache_rejected(self):
        rows = self.rows()
        with self.assertRaises(ValueError):
            self.reconcile(rows, [str(self.root), str(self.child), '/unknown'])
        with self.assertRaises(ValueError):
            self.reconcile(rows[:1], [str(self.root)])

    def test_subcache_header_uuid_mismatch_rejected(self):
        rows = self.rows(); rows[1]['uuid'] = 'fe' * 16
        with self.assertRaisesRegex(ValueError, 'UUID, mapping'):
            self.reconcile(rows)

    def test_subcache_mapping_offset_is_not_file_offset(self):
        rows = self.rows(); rows[0]['subcaches'][0]['vm_offset'] = 4080
        with self.assertRaisesRegex(ValueError, 'mapping'):
            self.reconcile(rows)

    def test_duplicate_api_path_and_backing_identity_rejected(self):
        rows = self.rows()
        with self.assertRaises(ValueError):
            self.reconcile(rows, [str(self.root), str(self.child), str(self.child)])
        rows[1]['canonical_path'] = rows[0]['canonical_path']
        with self.assertRaisesRegex(ValueError, 'duplicate cache backing'):
            self.reconcile(rows)

    def test_exact_metadata_bytes_and_digest_retained(self):
        for row in self.rows():
            self.assertTrue(row['complete'])
            for r in row['retained_metadata']:
                raw = bytes.fromhex(r['hex'])
                self.assertEqual(len(raw), r['bytes'])
                self.assertEqual(hashlib.sha256(raw).hexdigest(), r['sha256'])

    def test_metadata_mutation_is_not_hidden_by_same_length(self):
        before = self.rows()[0]
        raw = fixture(); raw[152] = 1; self.root.write_bytes(raw)
        after = self.rows()[0]
        self.assertTrue(after['complete'])
        self.assertNotEqual(before['retained_metadata'], after['retained_metadata'])

    def test_metadata_budget_and_deadline_fail_closed(self):
        for budget in ({'remaining_bytes': 23, 'deadline': time.monotonic() + 10},
                       {'remaining_bytes': probe.MAX_METADATA, 'deadline': time.monotonic() - 1}):
            row = probe.observe_cache(self.root, budget)
            self.assertFalse(row['complete'])
            self.assertIn('ceiling', row['error'])

    def test_selected_range_maps_to_owning_subcache(self):
        row = probe.segment_range(self.rows(), BASE, BASE + 0x100000, BASE + 0x101000, 256)
        self.assertEqual(row['path'], str(self.child))
        self.assertEqual(row['file_offset'], 0)
        self.assertFalse(row['content_identity_complete'])
        self.assertIn('zero-fill', row['unresolved'])

    def test_cross_mapping_zero_size_and_unmapped_range_unresolved(self):
        for address, size in ((BASE + 4080, 32), (BASE + 4096, 0), (BASE + 8192, 1), (BASE - 1, 1)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                probe.segment_range(self.rows(), BASE, BASE, address, size)


class Observations(unittest.TestCase):
    def prefix(self):
        return [{'event': 'prefix-hash', 'implementation': implementation, 'order': i,
                 'sha256': 'ab' * 32, 'bytes': probe.PREFIX_BYTES, 'seconds_including_pread': i + 0.5}
                for i, implementation in enumerate(('common/content_digest.hpp', 'CommonCrypto'))] + [
                    {'event': 'prefix-complete', 'identity_unchanged': True, 'digests_equal': True}]

    def test_equal_prefixes_do_not_claim_full_identity(self):
        self.assertFalse(probe.prefix_result(self.prefix())['whole_file_identity'])

    def test_changed_digest_short_read_false_identity_or_expired_time_rejected(self):
        for key, value in (('sha256', 'ff' * 32), ('bytes', 1), ('seconds_including_pread', 56), ('order', 0)):
            rows = self.prefix(); rows[1][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                probe.prefix_result(rows)
        rows = self.prefix(); rows[-1]['identity_unchanged'] = False
        with self.assertRaises(ValueError):
            probe.prefix_result(rows)

    def test_partial_or_duplicate_prefix_completion_rejected(self):
        for rows in (self.prefix()[:-1], self.prefix() + [self.prefix()[-1]],
                     self.prefix() + [{'event': 'incomplete'}]):
            with self.assertRaises(ValueError):
                probe.prefix_result(rows)

    def test_null_path_requires_positive_membership_and_inventory_match(self):
        rows = [{'event': 'loaded-image', 'round': i, 'index': 1, 'path': None,
                 'contains_install_name': True, 'inventory_name_uuid_match': True} for i in (0, 1)]
        self.assertTrue(all(not r['unresolved_indices'] for r in probe.membership(rows)['rounds']))
        rows[1]['contains_install_name'] = False; rows[1]['inventory_name_uuid_match'] = False
        self.assertEqual(probe.membership(rows)['rounds'][1]['unresolved_indices'], [1])

    def test_contradictory_membership_stays_unresolved_even_with_path(self):
        rows = [{'event': 'loaded-image', 'round': i, 'index': 1, 'path': '/library',
                 'contains_install_name': True, 'inventory_name_uuid_match': False} for i in (0, 1)]
        self.assertTrue(all(r['unresolved_indices'] == [1] for r in probe.membership(rows)['rounds']))

    def test_stable_individual_snapshots_do_not_hide_graph_expansion(self):
        rows = [{'event': 'loaded-image', 'round': i, 'index': 1, 'path': '/program',
                 'contains_install_name': False, 'inventory_name_uuid_match': False} for i in (0, 1)]
        for i in range(3):
            rows += [{'event': 'public-generation', 'round': i, 'before': i, 'after': i, 'overflow': False},
                     {'event': 'public-image', 'round': i, 'path': '/program'}]
            if i:
                rows.append({'event': 'public-image', 'round': i, 'path': '/SPI-added'})
        result = probe.membership(rows)
        self.assertTrue(result['public_rounds_individually_stable'])
        self.assertFalse(result['public_initial_to_initialized_equal'])
        self.assertTrue(result['public_initialized_to_final_equal'])


if __name__ == '__main__':
    unittest.main()
