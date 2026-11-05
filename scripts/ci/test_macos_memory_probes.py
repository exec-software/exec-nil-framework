#!/usr/bin/env python3
"""Pure classification/bound controls; do not simulate native enforcement."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('memory_probe', Path(__file__).with_name('macos-memory-probes.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def metric(event, cap=probe.CAP96, virtual=16 * probe.MiB):
    return {'event': event, 'pid': 123, 'limit_errno': 0, 'soft': cap, 'hard': cap,
            'task_result': 0, 'task_count': 12, 'expected_task_count': 12,
            'adjusted_virtual_bytes': virtual, 'resident_bytes': 4 * probe.MiB,
            'rusage_errno': 0, 'peak_rss_bytes': 8 * probe.MiB, 'startup_pad_bytes': 0}


def case(mode='reserve', profile='fixed96'):
    cap = probe.CAP96 if profile == 'fixed96' else 80 * probe.MiB
    rows = [metric('setup-before'), {'event': 'setup-result', 'profile': profile, 'mode': mode,
                                    'cap_bytes': cap, 'setrlimit_result': 0, 'errno': 0},
            metric('setup-after', cap), {'event': 'exec-start', 'pid': 123}, metric('child-start', cap)]
    if mode == 'reserve':
        rows.append({'event': 'reservation', 'bytes': cap - 16 * probe.MiB + 4096,
                     'headroom_estimate': cap - 16 * probe.MiB, 'succeeded': False,
                     'errno': 12, 'enomem': True, 'munmap_errno': 0, 'touched_bytes': 0})
    else:
        rows.append(metric('allocations-before-release', cap, cap))
        reuse = mode == 'free-loop'
        count = 0 if reuse else 20
        rows.append({'event': 'allocations', 'mode': mode, 'attempts': 96 if reuse else count + 1,
                     'retained_blocks': count, 'released_blocks': 96 if reuse else count,
                     'rejected': not reuse, 'errno': 0 if reuse else 12, 'enomem': not reuse,
                     'cleanup_ok': True, 'deadline_hit': False,
                     'cumulative_requested_success_bytes': 192 * probe.MiB if reuse else count * 2 * probe.MiB,
                     'physical_touch_budget_bytes': 96 * 4096 if reuse else count * 2 * probe.MiB})
    rows += [metric('child-end', cap), {'event': 'child-result', 'mode': mode, 'result': 0}]
    return rows


class MemoryObservations(unittest.TestCase):
    def classify(self, rows=None, code=0, mode='reserve', profile='fixed96'):
        return probe.classify_case(case(mode, profile) if rows is None else rows, code, profile, mode)

    def test_actual_reservation_denial_is_distinct_from_inherited_numbers(self):
        row = self.classify()
        self.assertEqual(row['status'], 'RESERVATION_REJECTED')
        self.assertTrue(row['enforcement_observed'])
        self.assertFalse(row['qualification'])

    def test_getrlimit_round_trip_without_child_never_proves_enforcement(self):
        row = self.classify(case()[:4], code=-9)
        self.assertEqual(row['status'], 'CHILD_START_NOT_OBSERVED')
        self.assertFalse(row['enforcement_observed'])

    def test_child_entry_without_result_stays_unproven(self):
        row = self.classify(case()[:5], code=-9)
        self.assertEqual(row['status'], 'CHILD_RESULT_NOT_OBSERVED')
        self.assertFalse(row['enforcement_observed'])

    def test_setup_rejection_is_separate_and_cannot_include_exec(self):
        rows = case()[:2]; rows[1].update(setrlimit_result=-1, errno=22)
        result = self.classify(rows, code=20)
        self.assertEqual(result['status'], 'SETUP_REJECTED')
        self.assertFalse(result['enforcement_observed'])
        with self.assertRaises(ValueError):
            self.classify(rows + [{'event': 'exec-start', 'pid': 123}], code=20)

    def test_exec_failure_remains_distinct(self):
        rows = case()[:4] + [{'event': 'exec-failed', 'errno': 12}]
        result = self.classify(rows, code=21)
        self.assertEqual(result['status'], 'EXEC_FAILED')
        self.assertFalse(result['enforcement_observed'])

    def test_fixed96_cannot_silently_become_measured_profile(self):
        rows = case(); rows[1]['cap_bytes'] += probe.MiB
        with self.assertRaisesRegex(ValueError, 'silent profile'):
            self.classify(rows)
        result = self.classify(profile='diagnostic-map-plus64')
        self.assertEqual(result['cap_bytes'], 80 * probe.MiB)
        self.assertFalse(result['qualification'])

    def test_inherited_limits_and_pid_must_match_launcher(self):
        for key, value in (('soft', 1), ('hard', 1), ('pid', 124)):
            rows = case(); rows[4][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.classify(rows)

    def test_unknown_task_layout_or_api_error_never_supplies_headroom(self):
        for key, value in (('task_result', 5), ('task_count', 11), ('limit_errno', 1)):
            rows = case(); rows[4][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.classify(rows)

    def test_same_inherited_cap_but_mapping_allowed_is_non_enforcement(self):
        rows = case(); rows[5].update(succeeded=True, errno=0, enomem=False); rows[-1]['result'] = 24
        result = self.classify(rows, code=24)
        self.assertEqual(result['status'], 'RESERVATION_NOT_REJECTED')
        self.assertFalse(result['enforcement_observed'])

    def test_too_small_exec_map_caveat_remains_explicit(self):
        rows = case(); rows[4].update(adjusted_virtual_bytes=208 * probe.MiB, startup_pad_bytes=192 * probe.MiB)
        rows[5].update(bytes=4096, headroom_estimate=0, succeeded=True, errno=0, enomem=False)
        rows[-1]['result'] = 24
        result = self.classify(rows, code=24)
        self.assertTrue(result['child_adjusted_map_exceeds_cap'])
        self.assertFalse(result['enforcement_observed'])

    def test_reservation_request_cannot_exceed_virtual_ceiling(self):
        rows = case(); rows[5]['bytes'] = probe.MAX_RESERVATION + 1
        with self.assertRaisesRegex(ValueError, 'reservation bounds'):
            self.classify(rows)

    def test_reservation_inside_headroom_cannot_prove_cap(self):
        rows = case(); rows[5]['bytes'] = rows[5]['headroom_estimate']
        with self.assertRaises(ValueError):
            self.classify(rows)

    def test_reservation_cleanup_failure_is_not_success(self):
        rows = case(); rows[5]['munmap_errno'] = 1
        with self.assertRaises(ValueError):
            self.classify(rows)

    def test_unknown_reservation_error_is_not_enforcement(self):
        rows = case(); rows[5].update(errno=22, enomem=False); rows[-1]['result'] = 24
        result = self.classify(rows, code=24)
        self.assertFalse(result['enforcement_observed'])
        self.assertEqual(result['status'], 'RESERVATION_ERROR')

    def test_errno_flags_cannot_turn_a_failed_mapping_into_non_enforcement(self):
        for updates in ({'succeeded': True}, {'errno': 0}, {'enomem': False}):
            rows = case(); rows[5].update(updates)
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                self.classify(rows)

    def test_bounded_reservation_cannot_become_unbounded_fallback(self):
        rows = case(profile='diagnostic-map-plus64')
        rows[0]['adjusted_virtual_bytes'] = 512 * probe.MiB
        rows[1]['cap_bytes'] = 576 * probe.MiB
        for i in (2, 4, 6):
            rows[i].update(soft=576 * probe.MiB, hard=576 * probe.MiB)
        rows[5] = {'event': 'reservation', 'outcome': 'INCONCLUSIVE_BOUND', 'headroom_estimate': 560 * probe.MiB}
        rows[-1]['result'] = 25
        result = self.classify(rows, code=25, profile='diagnostic-map-plus64')
        self.assertEqual(result['status'], 'INCONCLUSIVE_RESERVATION_BOUND')
        self.assertFalse(result['enforcement_observed'])

    def test_free_loop_succeeds_but_is_not_retained_control(self):
        row = self.classify(mode='free-loop')
        self.assertTrue(row['successful_reuse'])
        self.assertFalse(row['enforcement_observed'])

    def test_retained_mmap_and_calloc_need_success_then_enomem(self):
        for mode in ('retain-mmap', 'retain-calloc'):
            self.assertTrue(self.classify(mode=mode)['enforcement_observed'])
            rows = case(mode); allocation = next(r for r in rows if r['event'] == 'allocations')
            allocation.update(rejected=False, enomem=False, errno=0)
            rows[-1]['result'] = 24
            self.assertFalse(self.classify(rows, code=24, mode=mode)['enforcement_observed'])

    def test_retained_limit_must_not_fail_before_any_usable_allocation(self):
        rows = case('retain-mmap'); allocation = next(r for r in rows if r['event'] == 'allocations')
        allocation.update(retained_blocks=0, released_blocks=0, cumulative_requested_success_bytes=0,
                          physical_touch_budget_bytes=0, attempts=1)
        rows[-1]['result'] = 24
        self.assertFalse(self.classify(rows, code=24, mode='retain-mmap')['enforcement_observed'])

    def test_allocation_count_physical_touch_and_release_bounds(self):
        for key, value in (('attempts', 65), ('physical_touch_budget_bytes', probe.MAX_TOUCH + 1),
                           ('released_blocks', 19), ('cleanup_ok', False), ('cumulative_requested_success_bytes', 1)):
            rows = case('retain-calloc'); allocation = next(r for r in rows if r['event'] == 'allocations'); allocation[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.classify(rows, mode='retain-calloc')

    def test_deadline_expiry_cannot_certify_retained_rejection(self):
        rows = case('retain-mmap'); next(r for r in rows if r['event'] == 'allocations')['deadline_hit'] = True
        rows[-1]['result'] = 27
        self.assertFalse(self.classify(rows, code=27, mode='retain-mmap')['enforcement_observed'])

    def test_profile_needs_all_controls_and_uncapped_success(self):
        rows = [self.classify(mode=mode) for mode in probe.MODES]
        self.assertTrue(probe.profile_result(rows, 'fixed96', True)['bounded_helper_controls_demonstrated'])
        self.assertFalse(probe.profile_result(rows, 'fixed96', False)['bounded_helper_controls_demonstrated'])
        self.assertFalse(probe.profile_result(rows, 'fixed96', True)['qualification'])
        with self.assertRaises(ValueError):
            probe.profile_result(rows[:-1], 'fixed96', True)

    def test_duplicate_events_malformed_json_and_excess_events_rejected(self):
        with self.assertRaises(ValueError):
            self.classify(case() + [case()[4]])
        with self.assertRaises(ValueError):
            probe.read_events(b'{bad json}')
        with self.assertRaises(ValueError):
            probe.read_events(b'{}\n' * 257)


if __name__ == '__main__':
    unittest.main()
