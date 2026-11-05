#!/usr/bin/env python3
"""Bounded Darwin cache-identity diagnosis; never enables artifact reuse."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import stat
import sys
import time

MAX_BYTES = 16 * 1024**3
HASH_SECONDS = 120


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def dependency_records(data):
    """Apple ld dependency_info: one opcode then one NUL-terminated string."""
    if len(data) > 8 * 1024**2:
        raise ValueError('dependency stream exceeds bound')
    records, offset = [], 0
    while offset < len(data):
        opcode = data[offset]
        end = data.find(b'\0', offset + 1)
        if opcode not in (0x00, 0x10, 0x11, 0x40) or end < 0 or end == offset + 1:
            raise ValueError('unknown, empty or truncated dependency record')
        value = os.fsdecode(data[offset + 1:end])
        records.append({'opcode': opcode, 'value': value})
        if len(records) > 16384:
            raise ValueError('dependency count exceeds bound')
        offset = end + 1
    if not records or records[0]['opcode'] != 0 or any(r['opcode'] == 0 for r in records[1:]):
        raise ValueError('missing or repeated linker version')
    return records


def file_observation(path, *, full_hash=False, budget=None):
    """Retain aliases and exact bytes; failure/partial hashing is explicit."""
    path = Path(path)
    row = {'path': str(path), 'complete': False}
    try:
        if not path.is_absolute():
            raise ValueError('nonabsolute backing path')
        row['canonical_path'] = str(path.resolve(strict=True))
        with path.open('rb') as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise ValueError('backing input is not a regular file')
            row.update(bytes=before.st_size, device=before.st_dev, inode=before.st_ino,
                       mtime_ns=before.st_mtime_ns, ctime_ns=before.st_ctime_ns)
            head = stream.read(512)
            row['header_hex'] = head.hex()
            if head.startswith(b'dyld_v') and len(head) >= 104:
                row['cache_header_uuid'] = head[88:104].hex()
            if full_hash:
                if budget is None or before.st_size > budget['remaining_bytes']:
                    raise ValueError('aggregate hash byte ceiling reached')
                stream.seek(0)
                h, count, started = hashlib.sha256(), 0, time.monotonic()
                while True:
                    if time.monotonic() >= budget['deadline']:
                        raise TimeoutError('aggregate hash deadline reached')
                    block = stream.read(min(1024 * 1024, budget['remaining_bytes'] + 1))
                    if not block:
                        break
                    count += len(block)
                    budget['remaining_bytes'] -= len(block)
                    if budget['remaining_bytes'] < 0:
                        raise ValueError('backing file grew beyond hash byte ceiling')
                    h.update(block)
                row.update(sha256=h.hexdigest(), hashed_bytes=count,
                           hash_seconds=time.monotonic() - started)
                if count != before.st_size:
                    raise ValueError('backing file length changed while hashing')
            after = os.fstat(stream.fileno())
        current = path.stat()
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)
        if identity(before) != identity(after) or identity(after) != identity(current):
            raise ValueError('backing file identity changed during observation')
        row['complete'] = True
    except Exception as error:
        row['error'] = type(error).__name__ + ': ' + str(error)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() not in ('arm64', 'x86_64'):
        raise SystemExit('Native macOS arm64/x86_64 required')
    root = Path(__file__).resolve().parents[2]
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'; logs.mkdir()
    temporary = output / 'temp'; temporary.mkdir()
    source = Path(__file__).with_suffix('.cpp')
    support_path = root / 'tests/tools/ordinary_io_parity_contract.py'
    helper = root / 'validation/ny-3d/scripts/debugger_process.py'
    inputs = [Path(__file__).resolve(), source, support_path, helper]
    pins = {str(p.relative_to(root)): digest(p) for p in inputs}
    spec = importlib.util.spec_from_file_location('cache_probe_process', support_path)
    support = importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
    support.MAX_OUTPUT = 8 * 1024**2
    ownership = support.load_ownership(root)
    started, phases = time.monotonic(), []
    deadline = started + 420
    report = {'scope': 'Host-only diagnostic; no compiler or SDK cache eligibility changed',
              'status': 'INCOMPLETE', 'architecture': platform.machine(), 'platform': platform.platform(),
              'python': sys.version, 'host_image': {k: os.environ.get(k) for k in ('ImageVersion', 'ImageOS', 'RUNNER_OS', 'RUNNER_ARCH', 'GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')},
              'selection_environment': {k: os.environ.get(k) for k in ('DEVELOPER_DIR', 'TOOLCHAINS', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'DYLD_INSERT_LIBRARIES')},
              'source_before': pins, 'phases': phases, 'hash_ceiling_bytes': MAX_BYTES,
              'hash_ceiling_seconds': HASH_SECONDS, 'whole_seconds': 420}
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(temporary))
    def run(name, argv, seconds=30, allow_incomplete=False):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('whole diagnostic deadline exhausted')
        row = support.run_process(ownership, argv, output, environment, logs, name, min(seconds, remaining))
        phases.append(row)
        if not row['cleanup_verified'] or row.get('error') or row.get('exit_code') not in ((0, 2) if allow_incomplete else (0,)):
            raise RuntimeError(name + ' failed; raw evidence retained')
        return (logs / row['stdout']).read_bytes(), (logs / row['stderr']).read_bytes()
    try:
        tools = {}
        for name, argv in (
            ('os', ['/usr/bin/sw_vers']), ('xcode', ['/usr/bin/xcodebuild', '-version']),
            ('developer', ['/usr/bin/xcode-select', '-p']),
            ('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']),
            ('driver', ['/usr/bin/xcrun', '--find', 'clang++']),
            ('linker', ['/usr/bin/xcrun', '--find', 'ld']),
            ('compiler-version', ['/usr/bin/clang++', '--version']),
            ('resource', ['/usr/bin/clang++', '-print-resource-dir']),
        ):
            stdout, _ = run(name, argv)
            tools[name] = stdout.decode('utf-8').strip()
        report['tools'] = tools
        sdk = Path(tools['sdk'])
        selection_budget = {'remaining_bytes': 512 * 1024**2, 'deadline': min(deadline, time.monotonic() + 30)}
        report['selection_files'] = [file_observation(p, full_hash=True, budget=selection_budget) for p in (
            '/usr/bin/clang++', tools['driver'], tools['linker'],
            sdk / 'SDKSettings.json', sdk / 'SDKSettings.plist')]
        executable = output / 'loaded-images-probe'
        run('compile-probe', ['/usr/bin/clang++', '-std=c++17', '-fblocks', '-Wall', '-Wextra', '-Werror',
                             '-mmacosx-version-min=15.0', '-isysroot', str(sdk), str(source), '-o', str(executable)], 60)
        raw, _ = run('loaded-images', [str(executable)], 30, allow_incomplete=True)
        events = [json.loads(line) for line in raw.splitlines()]
        if len(events) > 40000:
            raise ValueError('image event inventory exceeds bound')
        report['image_events'] = events
        public_generations = [e for e in events if e['event'] == 'public-generation']
        report['public_generations_stable'] = len(public_generations) == 2 and all(e['before'] == e['after'] and not e['overflow'] for e in public_generations)
        public_paths = [sorted(e['path'] for e in events if e['event'] == 'public-image' and e['round'] == i and isinstance(e.get('path'), str)) for i in (0, 1)]
        report['public_snapshot_paths_equal'] = public_paths[0] == public_paths[1]
        cache_uuids = [e['uuid'] for e in events if e['event'] == 'shared-cache']
        report['cache_snapshot_uuids_equal'] = len(cache_uuids) == 2 and cache_uuids[0] == cache_uuids[1]
        snapshots = [[e['path'] for e in events if e['event'] == 'cache-file' and e['round'] == i] for i in (0, 1)]
        report['cache_snapshot_paths_equal'] = bool(snapshots[0]) and all(isinstance(p, str) for group in snapshots for p in group) and sorted(snapshots[0]) == sorted(snapshots[1])
        cache_paths = sorted(set(p for group in snapshots for p in group if isinstance(p, str)))
        budget = {'remaining_bytes': MAX_BYTES, 'deadline': min(deadline, time.monotonic() + HASH_SECONDS)}
        report['cache_backing_files'] = [file_observation(p, full_hash=True, budget=budget) for p in cache_paths]
        report['cache_hashed_bytes'] = MAX_BYTES - budget['remaining_bytes']
        report['active_cache_uuid_observed_in_files'] = bool(cache_uuids) and cache_uuids[0] in {row.get('cache_header_uuid') for row in report['cache_backing_files']}
        other_paths = sorted(set(e['path'] for e in events if e['event'] == 'spi-image' and e.get('path') and not e.get('shared_cache')))
        report['noncache_images'] = [file_observation(p) for p in other_paths]
        fixture = output / 'link-fixture.cpp'
        fixture.write_text('#include <cstdio>\nint main(){return std::puts("cache identity probe")<0;}\n')
        obj, linked, deps = output / 'link-fixture.o', output / 'link-fixture', output / 'link-dependencies.bin'
        run('compile-object', ['/usr/bin/clang++', '-std=c++17', '-c', str(fixture), '-o', str(obj)], 30)
        base = ['/usr/bin/clang++', '-pthread', '-o', str(linked), str(obj)]
        run('driver-plan', [*base, '-###'])
        run('link', [*base, '-Wl,-dependency_info,' + str(deps)])
        data = deps.read_bytes()
        records = dependency_records(data)
        report['link_dependencies'] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'records': records}
        link_budget = {'remaining_bytes': 512 * 1024**2, 'deadline': min(deadline, time.monotonic() + 30)}
        report['link_inputs'] = [file_observation(p, full_hash=True, budget=link_budget) for p in sorted(set(r['value'] for r in records if r['opcode'] == 0x10))]
        report['link_missing_searches'] = [{'path': r['value'], 'lexists': os.path.lexists(r['value'])} for r in records if r['opcode'] == 0x11]
        report['artifacts'] = {str(p.relative_to(output)): {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (executable, fixture, obj, linked, deps)}
        collected = any(e['event'] == 'collection-ended' and e['spi_complete'] for e in events)
        if collected and report['cache_snapshot_paths_equal'] and all(r['complete'] for r in report['cache_backing_files'] + report['noncache_images'] + report['link_inputs']):
            report['status'] = 'OBSERVED'
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        report['source_after'] = {str(p.relative_to(root)): digest(p) for p in inputs}
        report['source_unchanged'] = report['source_before'] == report['source_after']
        report['cleanup_verified'] = bool(phases) and all(p['cleanup_verified'] for p in phases)
        if not report['source_unchanged'] or not report['cleanup_verified']:
            report['status'] = 'INCOMPLETE'
        report['elapsed_seconds'] = time.monotonic() - started
        (output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print(json.dumps({k: report.get(k) for k in ('status', 'error', 'architecture', 'cache_hashed_bytes', 'cache_snapshot_paths_equal', 'source_unchanged', 'cleanup_verified', 'elapsed_seconds')}))
    return 0 if report['status'] == 'OBSERVED' else 2


if __name__ == '__main__':
    raise SystemExit(main())
