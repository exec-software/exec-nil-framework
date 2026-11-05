#!/usr/bin/env python3
"""Bounded host-only Darwin cache detail observations; never enables reuse."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import stat
import struct
import sys
import time

# Apple dyld_cache_format.h and DyldSharedCache.cpp::getSubCacheVmOffset.
# Modern/v1 selection uses mappingOffset > offsetof(cacheSubType), not its value.
APPLE_LAYOUT = 'https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld_cache_format.h'
APPLE_SUBCACHE = 'https://github.com/apple-oss-distributions/dyld/blob/main/common/DyldSharedCache.cpp'
APPLE_SPI = 'https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld_introspection.h'
APPLE_CRYPTO = 'https://github.com/apple-oss-distributions/CommonCrypto/blob/main/include/CommonDigest.h'
MAX_METADATA = 1024 * 1024
PREFIX_BYTES = 128 * 1024**2
MAX_U64 = (1 << 64) - 1


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def span(offset, size, ceiling):
    if (type(offset) is not int or type(size) is not int or offset < 0 or size < 0 or
            offset > MAX_U64 or size > MAX_U64 - offset or offset + size > ceiling):
        raise ValueError('range exceeds file/address bounds')
    return offset + size


def disjoint(rows, start, size):
    ordered = sorted((r[start], span(r[start], r[size], MAX_U64)) for r in rows)
    if any(b[0] < a[1] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('overlapping mapping ranges')


def identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def parse_cache(read, file_size):
    """Parse bounded metadata only. read(offset,size) retains exact raw bytes."""
    first = read(0, 24)
    if len(first) != 24:
        raise ValueError('truncated cache header')
    magic = first[:16].rstrip(b'\0 ').decode('ascii', 'strict')
    if not re.fullmatch(r'dyld_v1 +(arm64e?|x86_64h?)', magic):
        raise ValueError('unsupported cache magic/architecture')
    mapping_offset, mapping_count = struct.unpack_from('<II', first, 16)
    if not 400 <= mapping_offset <= 4096 or not 1 <= mapping_count <= 128:
        raise ValueError('unsupported header size or mapping count')
    header = read(0, mapping_offset)
    if len(header) != mapping_offset:
        raise ValueError('truncated cache header')
    u32 = lambda offset: struct.unpack_from('<I', header, offset)[0]
    u64 = lambda offset: struct.unpack_from('<Q', header, offset)[0]
    uuid = header[88:104].hex()
    if uuid == '0' * 32:
        raise ValueError('missing cache UUID')
    def table(offset, count, fmt, names, maximum=128):
        if not 0 <= count <= maximum:
            raise ValueError('table count exceeds bound')
        size = struct.calcsize(fmt)
        span(offset, size * count, file_size)
        raw = read(offset, size * count)
        if len(raw) != size * count:
            raise ValueError('truncated cache table')
        return [dict(zip(names, values)) for values in struct.iter_unpack(fmt, raw)]
    names = ('address', 'size', 'file_offset', 'max_prot', 'init_prot')
    mappings = table(mapping_offset, mapping_count, '<QQQII', names)
    for row in mappings:
        if row['size'] == 0 or row['init_prot'] & ~row['max_prot']:
            raise ValueError('invalid mapping size/protection')
        span(row['file_offset'], row['size'], file_size)
        span(row['address'], row['size'], MAX_U64)
    disjoint(mappings, 'address', 'size')
    disjoint(mappings, 'file_offset', 'size')
    slide = table(u32(312), u32(316), '<QQQQQQII',
                  ('address', 'size', 'file_offset', 'slide_offset', 'slide_size', 'flags', 'max_prot', 'init_prot'))
    if not slide or [{k: r[k] for k in names} for r in slide] != mappings:
        raise ValueError('mapping/slide tables disagree or slide table absent')
    for row in slide:
        span(row['slide_offset'], row['slide_size'], file_size)
    modern = mapping_offset > 456
    sub = table(u32(392), u32(396), '<16sQ32s' if modern else '<16sQ',
                ('uuid', 'vm_offset', 'raw_suffix') if modern else ('uuid', 'vm_offset'))
    for i, row in enumerate(sub):
        row['uuid'] = row['uuid'].hex()
        if modern:
            raw = row.pop('raw_suffix')
            if b'\0' not in raw:
                raise ValueError('unterminated subcache suffix')
            suffix = raw.split(b'\0', 1)[0].decode('ascii', 'strict')
            if not re.fullmatch(r'\.[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*', suffix):
                raise ValueError('invalid subcache suffix')
        else:
            suffix = '.' + str(i + 1)
        row['suffix'] = suffix
        if row['uuid'] == '0' * 32 or row['uuid'] == uuid or row['vm_offset'] == 0:
            raise ValueError('invalid subcache UUID/VM offset')
    for key in ('uuid', 'suffix', 'vm_offset'):
        if len({r[key] for r in sub}) != len(sub):
            raise ValueError('duplicate subcache ' + key)
    span(u64(40), u64(48), file_size)
    span(u64(72), u64(80), file_size)
    span(u64(224), u64(232), MAX_U64)
    return {'magic': magic, 'uuid': uuid, 'mappings': mappings, 'slide_mappings': slide,
            'subcaches': sub, 'subcache_entry_bytes': 56 if modern else 24,
            'shared_region_start': u64(224), 'shared_region_size': u64(232),
            'metadata_ranges': {'code_signature': {'file_offset': u64(40), 'size': u64(48)},
                                'local_symbols': {'file_offset': u64(72), 'size': u64(80)},
                                'patch_info': {'unslid_address': u64(152), 'size': u64(160)},
                                'dylibs_prebuilt_loader_set': {'unslid_address': u64(328), 'size': None}},
            'range_identity_complete': False,
            'unresolved': ['Slide/fixup and loader metadata bytes are described, not hashed or proven complete.',
                           'VM segment size does not establish file content size or zero-fill boundaries.']}


def observe_cache(path, budget):
    row = {'path': str(path), 'complete': False}
    records = []
    row['retained_metadata'] = records
    try:
        path = Path(path)
        if not path.is_absolute():
            raise ValueError('nonabsolute cache backing path')
        row['canonical_path'] = str(path.resolve(strict=True))
        with path.open('rb') as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise ValueError('cache backing is not regular')
            row.update(file_bytes=before.st_size, file_identity=list(identity(before)))
            def read(offset, size):
                span(offset, size, before.st_size)
                if time.monotonic() >= budget['deadline'] or size > budget['remaining_bytes']:
                    raise ValueError('metadata deadline/byte ceiling reached')
                budget['remaining_bytes'] -= size
                stream.seek(offset)
                raw = stream.read(size)
                if len(raw) != size:
                    raise ValueError('metadata read shortened')
                records.append({'offset': offset, 'bytes': size, 'hex': raw.hex(),
                                'sha256': hashlib.sha256(raw).hexdigest()})
                return raw
            row.update(parse_cache(read, before.st_size))
            after = os.fstat(stream.fileno())
        if identity(before) != identity(after) or identity(after) != identity(path.stat()):
            raise ValueError('cache backing changed during metadata observation')
        row['complete'] = True
    except Exception as error:
        row['error'] = type(error).__name__ + ': ' + str(error)
    return row


def cache_correspondence(files, active_uuid, api_paths):
    if not files or len(files) > 128 or any(not r['complete'] for r in files):
        raise ValueError('incomplete cache file metadata')
    paths = [r['path'] for r in files]
    if len(paths) != len(set(paths)) or len(api_paths) != len(set(api_paths)) or set(paths) != set(api_paths):
        raise ValueError('cache API/file list disagreement or duplicate path')
    if len({r['canonical_path'] for r in files}) != len(files) or len({r['uuid'] for r in files}) != len(files):
        raise ValueError('duplicate cache backing identity/UUID')
    roots = [r for r in files if r['uuid'] == active_uuid]
    if len(roots) != 1:
        raise ValueError('active root UUID absent or ambiguous')
    root = roots[0]
    by_path = {r['path']: r for r in files}
    if set(paths) != {root['path'], *(root['path'] + r['suffix'] for r in root['subcaches'])}:
        raise ValueError('API file list differs from root subcache table')
    base = min(m['address'] for m in root['mappings'])
    if base != root['shared_region_start']:
        raise ValueError('root mapping base differs from shared region start')
    matches = []
    all_mappings = []
    for file in files:
        for mapping in file['mappings']:
            if mapping['address'] < base:
                raise ValueError('cache mapping precedes root')
            span(mapping['address'] - base, mapping['size'], root['shared_region_size'])
            all_mappings.append(mapping)
    disjoint(all_mappings, 'address', 'size')
    for entry in root['subcaches']:
        child = by_path[root['path'] + entry['suffix']]
        address = span(base, entry['vm_offset'], MAX_U64)
        if (child['uuid'] != entry['uuid'] or child['subcaches'] or child['magic'] != root['magic'] or
                min(r['address'] for r in child['mappings']) != address):
            raise ValueError('subcache header UUID, mapping or nesting mismatch')
        matches.append({'path': child['path'], 'uuid': child['uuid'], 'vm_offset': entry['vm_offset'],
                        'first_mapping_address': address})
    return {'root_path': root['path'], 'root_uuid': root['uuid'], 'unslid_base': base,
            'subcache_matches': matches, 'api_table_files_equal': True,
            'scope': 'UUID and bounded metadata correspondence only; no whole-cache content identity'}


def segment_range(files, base, live_base, address, size):
    """Map reported active segment VM bounds; never infer zero-fill/fixup coverage."""
    if live_base < base or address < live_base or size == 0:
        raise ValueError('segment cannot be related to active cache base')
    unslid = address - (live_base - base)
    end = span(unslid, size, MAX_U64)
    candidates = []
    for file in files:
        for mapping in file['mappings']:
            if unslid >= mapping['address'] and end <= mapping['address'] + mapping['size']:
                offset = mapping['file_offset'] + unslid - mapping['address']
                span(offset, size, file['file_bytes'])
                candidates.append({'path': file['path'], 'file_offset': offset, 'vm_bytes': size})
    if len(candidates) != 1:
        raise ValueError('segment spans unmapped or multiple backing ranges')
    return {**candidates[0], 'assuming_reported_address_is_live': True, 'content_identity_complete': False,
            'unresolved': 'No file-size/zero-fill, slide/fixup, patch or transitive loader closure proof'}


def prefix_result(events):
    rows = [r for r in events if r.get('event') == 'prefix-hash']
    final = [r for r in events if r.get('event') == 'prefix-complete']
    if len(rows) != 2 or len(final) != 1 or any(r.get('event') == 'incomplete' for r in events):
        raise ValueError('incomplete prefix comparison')
    if ([r.get('implementation') for r in rows] != ['common/content_digest.hpp', 'CommonCrypto'] or
            [r.get('order') for r in rows] != [0, 1] or any(r.get('bytes') != PREFIX_BYTES for r in rows) or
            any(not re.fullmatch('[0-9a-f]{64}', r.get('sha256', '')) for r in rows) or
            rows[0]['sha256'] != rows[1]['sha256'] or not final[0].get('identity_unchanged') or
            not final[0].get('digests_equal')):
        raise ValueError('prefix digests/bounds/identity differ')
    for r in rows:
        seconds = r.get('seconds_including_pread')
        if type(seconds) not in (int, float) or not 0 <= seconds <= 55:
            raise ValueError('invalid hash elapsed time')
    return {'passes': rows, 'file': final[0], 'whole_file_identity': False,
            'limitations': 'Scalar first, CommonCrypto second; each includes I/O. Warm-cache/order effects are not normalized.'}


def membership(events):
    rounds = []
    for i in (0, 1):
        rows = [e for e in events if e.get('event') == 'loaded-image' and e.get('round') == i]
        if not rows or len(rows) > 16384:
            raise ValueError('missing/bounded loaded image inventory')
        unresolved = [r['index'] for r in rows if
                      (r.get('contains_install_name') is not r.get('inventory_name_uuid_match')) or
                      (not r.get('path') and not (r.get('contains_install_name') is True and r.get('inventory_name_uuid_match') is True))]
        rounds.append({'round': i, 'loaded': len(rows), 'inventory_name_uuid_matches': sum(r.get('contains_install_name') is True and r.get('inventory_name_uuid_match') is True for r in rows),
                       'unresolved_indices': unresolved})
    generations = [r for r in events if r.get('event') == 'public-generation']
    paths = [sorted(r.get('path') or '' for r in events if r.get('event') == 'public-image' and r.get('round') == i) for i in range(3)]
    return {'rounds': rounds, 'public_generations': generations,
            'public_rounds_individually_stable': len(generations) == 3 and all(r['before'] == r['after'] and not r['overflow'] for r in generations),
            'public_initial_to_initialized_equal': paths[0] == paths[1],
            'public_initialized_to_final_equal': paths[1] == paths[2],
            'loaded_backing_complete': False,
            'note': 'Name/UUID correspondence is not proof of active backing: overrides/copies can preserve both. SPI initialization can expand the loaded graph. Whole cache inventory is not the loaded graph.'}


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
    old_path = Path(__file__).with_name('macos-cache-probes.py')
    support_path = root / 'tests/tools/ordinary_io_parity_contract.py'
    inputs = [Path(__file__).resolve(), source, old_path, support_path,
              root / 'validation/ny-3d/scripts/debugger_process.py', root / 'common/content_digest.hpp']
    pins = {str(p.relative_to(root)): digest(p) for p in inputs}
    old = load(old_path, 'old_cache_detail_support')
    support = load(support_path, 'cache_detail_process'); support.MAX_OUTPUT = 8 * 1024**2
    ownership = support.load_ownership(root)
    # Reserve the final 20s of the whole bound for owned cleanup and evidence.
    started = time.monotonic(); deadline = started + 400
    phases, errors = [], []
    report = {'schema': 'nil.macos-cache-detail.v1', 'status': 'INCOMPLETE',
              'scope': 'Host-only observations; production compilerFilesComplete/cache eligibility untouched',
              'identity_complete': False, 'architecture': platform.machine(), 'platform': platform.platform(),
              'host_image': {k: os.environ.get(k) for k in ('ImageVersion', 'ImageOS', 'RUNNER_OS', 'RUNNER_ARCH', 'GITHUB_SHA', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')},
              'selection_environment': {k: os.environ.get(k) for k in ('DEVELOPER_DIR', 'TOOLCHAINS', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'DYLD_INSERT_LIBRARIES')},
              'source_before': pins, 'phases': phases, 'errors': errors, 'whole_seconds': 420,
              'metadata_byte_ceiling': MAX_METADATA, 'hash_prefix_bytes': PREFIX_BYTES,
              'api_sources': [APPLE_LAYOUT, APPLE_SUBCACHE, APPLE_SPI, APPLE_CRYPTO],
              'unresolved': ['No production compiler/runtime closure is sampled.',
                             'Matching cached install names/UUIDs do not prove active backing for every loaded image.',
                             'Segment zero-fill, slide/fixup and transitive loader/search metadata remain incomplete.',
                             'UUID/header agreement and prefix hashes do not establish whole-cache identity.']}
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(temporary))
    def run(name, argv, seconds=30, allowed=(0,), overrides=None):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('whole diagnostic deadline exhausted')
        env = dict(environment); env.update(overrides or {})
        row = support.run_process(ownership, argv, output, env, logs, name, min(seconds, remaining))
        phases.append(row)
        if not row['cleanup_verified'] or row.get('error') or row.get('exit_code') not in allowed:
            raise RuntimeError(name + ' failed; raw evidence retained')
        return (logs / row['stdout']).read_bytes(), (logs / row['stderr']).read_bytes()
    def events(raw):
        rows = [json.loads(line) for line in raw.splitlines()]
        if len(rows) > 65536:
            raise ValueError('event count exceeds bound')
        return rows
    def attempt(name, operation):
        try:
            report[name] = operation()
        except Exception as error:
            errors.append(name + ': ' + type(error).__name__ + ': ' + str(error))
        return report.get(name)
    try:
        tools = {}
        for name, argv in (('os', ['/usr/bin/sw_vers']), ('xcode', ['/usr/bin/xcodebuild', '-version']),
                           ('developer', ['/usr/bin/xcode-select', '-p']),
                           ('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']),
                           ('driver', ['/usr/bin/xcrun', '--find', 'clang++']),
                           ('linker', ['/usr/bin/xcrun', '--find', 'ld']),
                           ('compiler-version', ['/usr/bin/clang++', '--version'])):
            raw, _ = run(name, argv); tools[name] = raw.decode('utf-8').strip()
        report['tools'] = tools
        sdk = Path(tools['sdk']); driver = Path(tools['driver']); linker = Path(tools['linker'])
        plugin = driver.parent.parent / 'lib/libLTO.dylib'
        selected = [Path('/usr/bin/clang++'), driver, linker, plugin, sdk / 'SDKSettings.json', sdk / 'SDKSettings.plist']
        selection_budget = {'remaining_bytes': 512 * 1024**2, 'deadline': min(deadline, time.monotonic() + 30)}
        report['selected_before'] = [old.file_observation(p, full_hash=True, budget=selection_budget) for p in selected]
        executable = output / 'cache-detail-probe'
        run('compile-probe', ['/usr/bin/clang++', '-std=c++17', '-O2', '-fblocks', '-Wall', '-Wextra', '-Werror',
                              '-mmacosx-version-min=15.0', '-isysroot', str(sdk), str(source), '-o', str(executable)], 60)
        raw, _ = run('images', [str(executable)], 60, allowed=(0, 2))
        image_events = events(raw); report['image_events'] = image_events
        report['membership'] = membership(image_events)
        snapshots = [[e['path'] for e in image_events if e.get('event') == 'cache-file' and e.get('round') == i] for i in (0, 1)]
        caches = [e for e in image_events if e.get('event') == 'shared-cache']
        if (len(caches) != 2 or caches[0]['uuid'] != caches[1]['uuid'] or caches[0]['base'] != caches[1]['base'] or
                not snapshots[0] or any(not isinstance(p, str) for group in snapshots for p in group) or
                sorted(snapshots[0]) != sorted(snapshots[1]) or len(snapshots[0]) > 128):
            raise ValueError('active cache snapshot disagreement or unavailable backing paths')
        budget = {'remaining_bytes': MAX_METADATA, 'deadline': min(deadline, time.monotonic() + 20)}
        files = [observe_cache(p, budget) for p in snapshots[1]]
        report['cache_files_before'] = files
        correspondence = attempt('cache_correspondence', lambda: cache_correspondence(files, caches[1]['uuid'], snapshots[1]))
        if correspondence:
            ranges = []
            for e in image_events:
                if e.get('event') != 'selected-segment' or e.get('round') != 1:
                    continue
                row = dict(e)
                try:
                    row['nominal_mapping'] = segment_range(files, correspondence['unslid_base'], caches[1]['base'], e['address'], e['size'])
                except ValueError as error:
                    row['unresolved_range'] = str(error)
                ranges.append(row)
            report['selected_segment_ranges'] = ranges
            hash_paths = [correspondence['root_path']] + [r['path'] for r in correspondence['subcache_matches'][:1]]
            for i, path in enumerate(hash_paths):
                attempt('prefix_' + str(i), lambda path=path, i=i: prefix_result(events(run('prefix-' + str(i), [str(executable), '--hash-prefix', str(Path(path).resolve(strict=True))], 60)[0])))
        for name, path in (('driver', driver), ('linker', linker), ('lto', plugin), ('probe', executable)):
            attempt('otool_' + name, lambda name=name, path=path: run('otool-' + name, ['/usr/bin/otool', '-L', str(path)])[0].decode('utf-8', 'replace'))
        fixture = output / 'loader-fixture.cpp'; fixture.write_text('#include <cstdio>\nint main(){return std::puts("cache detail probe")<0;}\n')
        obj, linked, deps = output / 'loader-fixture.o', output / 'loader-fixture', output / 'link-dependencies.bin'
        run('compile-object', [str(driver), '-std=c++17', '-isysroot', str(sdk), '-c', str(fixture), '-o', str(obj)])
        argv = [str(driver), '-isysroot', str(sdk), '-pthread', '-o', str(linked), str(obj)]
        run('driver-plan', [*argv, '-###'])
        _, trace = run('link-loader-trace', [*argv, '-Wl,-dependency_info,' + str(deps)], overrides={'DYLD_PRINT_LIBRARIES': '1'})
        trace_lines = [line for line in trace.decode('utf-8', 'replace').splitlines() if line.startswith('dyld[')]
        report['loader_trace'] = {'observed_lines': trace_lines, 'observed': bool(trace_lines),
                                  'scope': 'Selected host driver and its children; protections may suppress tracing. This is not a compiler closure.'}
        records = old.dependency_records(deps.read_bytes())
        report['link_dependencies'] = {'sha256': digest(deps), 'records': records}
        link_budget = {'remaining_bytes': 512 * 1024**2, 'deadline': min(deadline, time.monotonic() + 30)}
        report['link_input_files'] = [old.file_observation(p, full_hash=True, budget=link_budget) for p in sorted({r['value'] for r in records if r['opcode'] == 16})]
        report['link_failed_searches'] = [{'path': r['value'], 'lexists': os.path.lexists(r['value'])} for r in records if r['opcode'] == 17]
        after_budget = {'remaining_bytes': MAX_METADATA, 'deadline': min(deadline, time.monotonic() + 20)}
        after = [observe_cache(p, after_budget) for p in snapshots[1]]
        report['cache_files_after'] = after
        report['cache_metadata_unchanged'] = files == after
        if not report['cache_metadata_unchanged']:
            errors.append('cache metadata/identity changed during diagnostic')
        selection_budget = {'remaining_bytes': 512 * 1024**2, 'deadline': min(deadline, time.monotonic() + 30)}
        report['selected_after'] = [old.file_observation(p, full_hash=True, budget=selection_budget) for p in selected]
        seal = lambda rows: [{k: v for k, v in row.items() if k != 'hash_seconds'} for row in rows]
        report['selected_unchanged'] = seal(report['selected_before']) == seal(report['selected_after'])
        if not report['selected_unchanged'] or not all(r['complete'] for r in report['selected_before'] + report['selected_after'] + report['link_input_files']):
            errors.append('selected tool/SDK/link input identity incomplete or changed')
        ended = [e for e in image_events if e.get('event') == 'collection-ended']
        if len(ended) != 1 or not ended[0]['observations_complete'] or any(r['unresolved_indices'] for r in report['membership']['rounds']):
            errors.append('loaded image membership observations incomplete')
        if not report['membership']['public_rounds_individually_stable']:
            errors.append('public image snapshot generation changed during enumeration')
        report['artifacts'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (executable, fixture, obj, linked, deps)}
        if not errors:
            report['status'] = 'OBSERVED'
    except Exception as error:
        errors.append(type(error).__name__ + ': ' + str(error))
    finally:
        report['source_after'] = {str(p.relative_to(root)): digest(p) for p in inputs}
        report['source_unchanged'] = pins == report['source_after']
        report['cleanup_verified'] = bool(phases) and all(p['cleanup_verified'] for p in phases)
        if not report['source_unchanged'] or not report['cleanup_verified']:
            report['status'] = 'INCOMPLETE'
        report['elapsed_seconds'] = time.monotonic() - started
        (output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print(json.dumps({k: report[k] for k in ('status', 'errors', 'architecture', 'identity_complete', 'source_unchanged', 'cleanup_verified', 'elapsed_seconds')}))
    return 0 if report['status'] == 'OBSERVED' else 2


if __name__ == '__main__':
    raise SystemExit(main())
