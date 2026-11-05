#!/usr/bin/env python3
"""Bounded native macOS memory-cap observations; no lifetime-test admission."""
import argparse
import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys
import time

MiB = 1024**2
CAP96 = 96 * MiB
MAX_RESERVATION = 256 * MiB
MAX_TOUCH = 128 * MiB
MODES = ('reserve', 'free-loop', 'retain-mmap', 'retain-calloc')
SOURCES = [
    'https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/kern_resource.c',
    'https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/kern_exec.c',
    'https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/vm_map.c',
    'https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/kern/task.c',
]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(MiB), b''):
            h.update(block)
    return h.hexdigest()


def read_events(raw):
    if len(raw) > MiB:
        raise ValueError('event bytes exceed bound')
    rows = [json.loads(line) for line in raw.splitlines()]
    if len(rows) > 256 or any(not isinstance(r, dict) or not isinstance(r.get('event'), str) for r in rows):
        raise ValueError('invalid/bounded event inventory')
    return rows


def exactly(rows, name):
    found = [r for r in rows if r['event'] == name]
    if len(found) != 1:
        raise ValueError('missing or repeated ' + name)
    return found[0]


def metric(row):
    if (row.get('limit_errno') != 0 or row.get('task_result') != 0 or
            type(row.get('task_count')) is not int or row['task_count'] <= 0 or
            row['task_count'] != row.get('expected_task_count')):
        raise ValueError('native metric API/count unavailable')
    for key in ('adjusted_virtual_bytes', 'resident_bytes', 'soft', 'hard', 'startup_pad_bytes'):
        if type(row.get(key)) is not int or not 0 <= row[key] < 2**64:
            raise ValueError('invalid native metric ' + key)
    return row


def classify_case(rows, exit_code, profile, mode):
    """Setup, exec and actual allocation outcomes remain independent facts."""
    before = metric(exactly(rows, 'setup-before'))
    setup = exactly(rows, 'setup-result')
    if setup.get('profile') != profile or setup.get('mode') != mode or mode not in MODES:
        raise ValueError('profile/mode mismatch')
    cap = setup.get('cap_bytes')
    expected = CAP96 if profile == 'fixed96' else before['adjusted_virtual_bytes'] + 64 * MiB
    if profile not in ('fixed96', 'diagnostic-map-plus64') or type(cap) is not int or cap != expected or not 0 < cap <= 8 * 1024**4:
        raise ValueError('unexpected cap; no silent profile substitution')
    result = {'profile': profile, 'mode': mode, 'cap_bytes': cap, 'setup_before': before,
              'status': 'INCOMPLETE', 'enforcement_observed': False, 'qualification': False,
              'metric_caveat': 'MACH_TASK_BASIC_INFO virtual_size uses adjusted map size; RLIMIT_AS uses raw map size. Exotic reserved regions can differ.'}
    if setup.get('setrlimit_result') == -1:
        if exit_code != 20 or type(setup.get('errno')) is not int or setup['errno'] <= 0 or any(r['event'] in ('exec-start', 'child-start', 'child-result') for r in rows):
            raise ValueError('invalid setup rejection evidence')
        return {**result, 'status': 'SETUP_REJECTED', 'setup_errno': setup['errno']}
    if setup.get('setrlimit_result') != 0 or setup.get('errno') != 0:
        raise ValueError('invalid setrlimit result')
    after = metric(exactly(rows, 'setup-after'))
    if (after['soft'], after['hard']) != (cap, cap):
        raise ValueError('launcher limit round trip differs')
    executed = exactly(rows, 'exec-start')
    if executed.get('pid') != before.get('pid'):
        raise ValueError('exec identity differs')
    exec_failure = [r for r in rows if r['event'] == 'exec-failed']
    if exec_failure:
        if len(exec_failure) != 1 or exit_code != 21 or any(r['event'] == 'child-start' for r in rows):
            raise ValueError('invalid exec failure evidence')
        return {**result, 'status': 'EXEC_FAILED', 'exec_errno': exec_failure[0].get('errno')}
    if not any(r['event'] == 'child-start' for r in rows):
        return {**result, 'status': 'CHILD_START_NOT_OBSERVED', 'exit_code': exit_code}
    child = metric(exactly(rows, 'child-start'))
    if child.get('pid') != before.get('pid') or (child['soft'], child['hard']) != (cap, cap):
        raise ValueError('post-exec inherited identity/limits differ')
    result.update(child_start=child, nominal_headroom_bytes=max(0, cap - child['adjusted_virtual_bytes']),
                  child_adjusted_map_exceeds_cap=child['adjusted_virtual_bytes'] > cap)
    if not any(r['event'] == 'child-result' for r in rows):
        return {**result, 'status': 'CHILD_RESULT_NOT_OBSERVED', 'exit_code': exit_code}
    ended = metric(exactly(rows, 'child-end'))
    final = exactly(rows, 'child-result')
    if final.get('mode') != mode or final.get('result') != exit_code or (ended['soft'], ended['hard']) != (cap, cap):
        raise ValueError('child outcome/inherited limit changed')
    result['child_end'] = ended
    if mode == 'reserve':
        reservation = exactly(rows, 'reservation'); result['reservation'] = reservation
        if reservation.get('outcome') == 'INCONCLUSIVE_BOUND':
            if (exit_code != 25 or reservation.get('headroom_estimate') != result['nominal_headroom_bytes'] or
                    result['nominal_headroom_bytes'] <= MAX_RESERVATION - MiB):
                raise ValueError('inconclusive reservation has wrong exit')
            return {**result, 'status': 'INCONCLUSIVE_RESERVATION_BOUND'}
        size = reservation.get('bytes')
        if (type(size) is not int or not 0 < size <= MAX_RESERVATION or
                reservation.get('headroom_estimate') != result['nominal_headroom_bytes'] or
                size <= result['nominal_headroom_bytes'] or reservation.get('touched_bytes') != 0 or
                reservation.get('munmap_errno') != 0):
            raise ValueError('invalid reservation bounds or cleanup')
        succeeded, error, enomem = reservation.get('succeeded'), reservation.get('errno'), reservation.get('enomem')
        if (type(succeeded) is not bool or type(error) is not int or type(enomem) is not bool or
                enomem != (error == errno.ENOMEM) or error < 0 or (succeeded and error != 0) or (not succeeded and error == 0)):
            raise ValueError('reservation success/errno contradiction')
        denied = not succeeded and enomem
        if (denied and exit_code != 0) or (not denied and exit_code == 0):
            raise ValueError('reservation result/exit contradiction')
        result.update(status='RESERVATION_REJECTED' if denied else ('RESERVATION_NOT_REJECTED' if succeeded else 'RESERVATION_ERROR'),
                      enforcement_observed=denied)
    else:
        allocation = exactly(rows, 'allocations'); result['allocations'] = allocation
        metric(exactly(rows, 'allocations-before-release'))
        if allocation.get('mode') != mode or allocation.get('cleanup_ok') is not True:
            raise ValueError('allocation mode/cleanup differs')
        if (type(allocation.get('errno')) is not int or allocation['errno'] < 0 or
                type(allocation.get('enomem')) is not bool or allocation['enomem'] != (allocation['errno'] == errno.ENOMEM)):
            raise ValueError('allocation errno contradiction')
        for key in ('attempts', 'retained_blocks', 'released_blocks', 'physical_touch_budget_bytes', 'cumulative_requested_success_bytes'):
            if type(allocation.get(key)) is not int or allocation[key] < 0:
                raise ValueError('invalid allocation count')
        if allocation['physical_touch_budget_bytes'] > MAX_TOUCH:
            raise ValueError('physical touch ceiling exceeded')
        if mode == 'free-loop':
            if (allocation['attempts'] > 96 or allocation['retained_blocks'] or allocation['released_blocks'] > allocation['attempts'] or
                    allocation['cumulative_requested_success_bytes'] != allocation['released_blocks'] * 2 * MiB):
                raise ValueError('free-loop count bound differs')
            success = (allocation['attempts'] == allocation['released_blocks'] == 96 and
                       allocation.get('rejected') is False and allocation.get('deadline_hit') is False)
            result.update(status='FREE_LOOP_COMPLETED' if success else 'FREE_LOOP_FAILED', successful_reuse=success)
        else:
            if (allocation['attempts'] > 64 or allocation['retained_blocks'] > 64 or
                    allocation['released_blocks'] != allocation['retained_blocks'] or
                    allocation['cumulative_requested_success_bytes'] != allocation['retained_blocks'] * 2 * MiB):
                raise ValueError('retained allocation/release bound differs')
            success = (allocation.get('rejected') is True and allocation.get('enomem') is True and
                       allocation['retained_blocks'] > 0 and allocation.get('deadline_hit') is False)
            result.update(status='RETAINED_ALLOCATION_REJECTED' if success else 'RETAINED_CONTROL_NOT_PROVEN', enforcement_observed=success)
        if (success and exit_code != 0) or (not success and exit_code == 0):
            raise ValueError('allocation result/exit contradiction')
    return result


def profile_result(cases, profile, uncapped_success):
    rows = [r for r in cases if r['profile'] == profile]
    if sorted(r['mode'] for r in rows) != sorted(MODES):
        raise ValueError('profile lacks exact four controls')
    by_mode = {r['mode']: r for r in rows}
    demonstrated = (uncapped_success and by_mode['reserve']['enforcement_observed'] and
                    by_mode['free-loop'].get('successful_reuse') is True and
                    all(by_mode[m]['enforcement_observed'] for m in ('retain-mmap', 'retain-calloc')))
    return {'profile': profile, 'bounded_helper_controls_demonstrated': bool(demonstrated),
            'case_statuses': {m: by_mode[m]['status'] for m in MODES}, 'qualification': False,
            'scope': 'Only this probe image/allocator/kernel; no Ny fixture or process-tree aggregate qualification'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() not in ('arm64', 'x86_64'):
        raise SystemExit('Native macOS arm64/x86_64 required')
    root = Path(__file__).resolve().parents[2]
    output = args.output.absolute(); output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    temporary = output / 'temp'; temporary.mkdir()
    source = Path(__file__).with_suffix('.cpp')
    support_path = root / 'tests/tools/ordinary_io_parity_contract.py'
    inputs = [Path(__file__).resolve(), source, support_path, root / 'validation/ny-3d/scripts/debugger_process.py']
    pins = {str(p.relative_to(root)): digest(p) for p in inputs}
    spec = importlib.util.spec_from_file_location('memory_probe_process', support_path)
    support = importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
    support.MAX_OUTPUT = MiB
    ownership = support.load_ownership(root)
    started = time.monotonic(); deadline = started + 160  # 20s reserved for cleanup/evidence.
    phases, problems, cases = [], [], []
    report = {'schema': 'nil.macos-memory-probe.v1', 'status': 'INCOMPLETE', 'qualification': False,
              'scope': 'Own-child RLIMIT_AS diagnosis only; no runtime/admission/lifetime-test changes',
              'architecture': platform.machine(), 'host': platform.platform(), 'python': sys.version,
              'host_image': {k: os.environ.get(k) for k in ('ImageVersion', 'ImageOS', 'GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')},
              'source_before': pins, 'api_sources': SOURCES, 'phases': phases, 'problems': problems,
              'cases': cases, 'limits': {'whole_seconds': 180, 'per_child_seconds': 20,
                                      'physical_touch_bytes_per_child': MAX_TOUCH,
                                      'single_reservation_bytes': MAX_RESERVATION,
                                      'retained_allocation_bytes_per_child': MAX_TOUCH,
                                      'output_bytes_per_child': MiB},
              'limitations': ['Adjusted task virtual size may differ from raw map size for exotic reserved mappings.',
                              'Inherited getrlimit values do not prove post-exec enforcement.',
                              'The named larger profile is diagnostic and never substitutes for fixed96.',
                              'RSS/resident size is supplementary, not a committed-memory or hard-cap proof.']}
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(temporary))
    def run(name, argv, *, allow_case=False, timeout=20, expected_timeout=False):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('whole diagnostic work deadline reached')
        row = support.run_process(ownership, argv, output, environment, logs, name, min(timeout, remaining))
        row['expected_timeout_control'] = expected_timeout
        phases.append(row)
        raw = (logs / row['stdout']).read_bytes()
        if not row['cleanup_verified']:
            raise RuntimeError(name + ': owned cleanup unverified')
        if expected_timeout:
            if row.get('timed_out') is not True:
                raise RuntimeError('bounded wait control did not time out')
        elif row.get('error') or (not allow_case and row.get('exit_code') != 0):
            raise RuntimeError(name + ': command failed; raw evidence retained')
        return row, raw
    try:
        tools = {}
        for name, argv in (('os', ['/usr/bin/sw_vers']), ('kernel', ['/usr/bin/uname', '-a']),
                           ('compiler', ['/usr/bin/clang++', '--version']),
                           ('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'])):
            _, raw = run(name, argv); tools[name] = raw.decode('utf-8').strip()
        report['tools'] = tools
        normal, padded = output / 'memory-probe', output / 'memory-probe-startup192'
        for name, target, flags in (('compile-probe', normal, []), ('compile-padded-probe', padded, ['-DMEMORY_PROBE_STARTUP_PAD=192'])):
            run(name, ['/usr/bin/clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-mmacosx-version-min=15.0', '-isysroot', tools['sdk'], *flags, str(source), '-o', str(target)])
        report['binaries_before'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (normal, padded)}
        _, raw = run('baseline', [str(normal), '--observe']); report['baseline'] = metric(exactly(read_events(raw), 'baseline'))
        row, raw = run('uncapped-reservation', [str(normal), '--uncapped'], allow_case=True)
        uncapped = read_events(raw); report['uncapped'] = uncapped
        reservation = exactly(uncapped, 'reservation')
        uncapped_success = (row['exit_code'] == 0 and reservation.get('succeeded') is True and
                            reservation.get('bytes') == MAX_RESERVATION and reservation.get('munmap_errno') == 0)
        report['uncapped_reservation_succeeded'] = uncapped_success
        for profile in ('fixed96', 'diagnostic-map-plus64'):
            for mode in MODES:
                row, raw = run(profile + '-' + mode, [str(normal), '--launch', profile, str(normal), mode], allow_case=True)
                observed = read_events(raw)
                result = classify_case(observed, row['exit_code'], profile, mode)
                result['raw_events'] = observed; result['exit_code'] = row['exit_code']; cases.append(result)
        report['profiles'] = [profile_result(cases, p, uncapped_success) for p in ('fixed96', 'diagnostic-map-plus64')]
        row, raw = run('diagnostic-startup192-under-measured-cap', [str(normal), '--launch', 'diagnostic-map-plus64', str(padded), 'reserve'], allow_case=True)
        startup_events = read_events(raw)
        startup = classify_case(startup_events, row['exit_code'], 'diagnostic-map-plus64', 'reserve')
        startup['raw_events'] = startup_events; startup['exit_code'] = row['exit_code']
        startup['exec_caveat_observed'] = (startup.get('child_adjusted_map_exceeds_cap') is True and
                                          startup['status'] == 'RESERVATION_NOT_REJECTED' and
                                          startup.get('reservation', {}).get('succeeded') is True)
        report['startup192_control'] = startup
        row, raw = run('owned-timeout-control', [str(normal), '--wait'], timeout=2, expected_timeout=True)
        metric(exactly(read_events(raw), 'wait-started'))
        report['timeout_cleanup_verified'] = row['cleanup_verified'] and row['timed_out']
        report['binaries_after'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (normal, padded)}
        report['binaries_unchanged'] = report['binaries_before'] == report['binaries_after']
        if not report['binaries_unchanged']:
            raise RuntimeError('Private binaries changed during diagnostic')
        # OBSERVED includes explicit setup rejection or nonenforcement. Neither
        # those outcomes nor a larger diagnostic profile qualify fixed96.
        report['status'] = 'OBSERVED'
    except Exception as error:
        problems.append(type(error).__name__ + ': ' + str(error))
    finally:
        report['source_after'] = {str(p.relative_to(root)): digest(p) for p in inputs}
        report['source_unchanged'] = pins == report['source_after']
        report['cleanup_verified'] = bool(phases) and all(r['cleanup_verified'] for r in phases)
        report['elapsed_seconds'] = time.monotonic() - started
        if not report['source_unchanged'] or not report['cleanup_verified'] or problems:
            report['status'] = 'INCOMPLETE'
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report.get(k) for k in ('status', 'problems', 'profiles', 'qualification', 'source_unchanged', 'cleanup_verified', 'elapsed_seconds')}))
    return 0 if report['status'] == 'OBSERVED' else 2


if __name__ == '__main__':
    raise SystemExit(main())
