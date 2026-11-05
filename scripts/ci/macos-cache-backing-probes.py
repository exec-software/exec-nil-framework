#!/usr/bin/env python3
"""Bounded loaded-header/cache-table and whole-cache hashing observations only."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MAX_FILES, MAX_IMAGES, MAX_CACHE_IMAGES = 128, 4096, 16384
MAX_BYTES, MAX_METADATA, MAX_OUTPUT = 8 * 1024**3, 8 * 1024**2, 8 * 1024**2
TOTAL_SECONDS, RESERVE_SECONDS, HASH_SECONDS = 600, 20, 240
U64 = (1 << 64) - 1
APPLE = 'https://github.com/apple-oss-distributions/dyld/blob/dyld-1285.19/'


def require(value, message):
    if not value:
        raise ValueError(message)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def span(offset, size, ceiling=U64):
    require(type(offset) is int and type(size) is int and 0 <= offset <= U64 and
            0 <= size <= U64 - offset and offset + size <= ceiling, 'Range outside bounds')
    return offset + size


def identity(info):
    return [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns]


def paths(output, work, root=ROOT):
    require(not output.is_symlink() and not work.is_symlink(), 'Output/work may not be symlinks')
    output, work, root = output.resolve(), work.resolve(), root.resolve()
    require(not output.exists() and not work.exists(), 'Output/work must be fresh')
    require(output != work and output not in work.parents and work not in output.parents, 'Output/work must be disjoint')
    require(work != root and root not in work.parents and work not in root.parents, 'Work must be outside checkout')
    require(output != root and output not in root.parents and
            (root not in output.parents or root/'build' in output.parents), 'Checkout evidence must be under build')
    return output, work


def environment(work):
    values = {key: os.environ[key] for key in ('HOME', 'USER', 'LOGNAME') if key in os.environ}
    values.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', LC_ALL='C', TMPDIR=str(work/'tmp'),
                  PYTHONDONTWRITEBYTECODE='1', GIT_OPTIONAL_LOCKS='0')
    return values


def record(path, deadline, maximum=512 * 1024**2):
    declared = Path(path)
    require(declared.is_absolute(), 'Input path must be absolute')
    path = declared.resolve(strict=True)
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
    digest, size = hashlib.sha256(), 0
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= maximum, 'Input is nonregular or exceeds bound')
        while True:
            require(time.monotonic() < deadline, 'Input seal deadline')
            block = stream.read(1024**2)
            if not block:
                break
            size += len(block)
            require(size <= maximum, 'Input grew beyond bound')
            digest.update(block)
        after = os.fstat(stream.fileno())
    require(identity(before) == identity(after) == identity(path.stat()) and size == after.st_size and
            declared.resolve(strict=True) == path, 'Input changed during seal')
    return {'path': str(declared), 'canonical_path': str(path), 'bytes': size,
            'sha256': digest.hexdigest(), 'mode': stat.S_IMODE(after.st_mode)}


def image_table(read, size):
    header = read(0, 152)
    require(len(header) == 152, 'Truncated image-table header')
    offset, count = struct.unpack_from('<QQ', header, 136)
    require(0 < count <= MAX_CACHE_IMAGES, 'Missing or oversized cache image table')
    span(offset, count * 32, size)
    raw = read(offset, count * 32)
    require(len(raw) == count * 32, 'Truncated image table')
    result = []
    for uuid, address, text_size, path_offset in struct.iter_unpack('<16sQII', raw):
        require(uuid != bytes(16) and text_size > 0, 'Invalid image UUID/text size')
        span(address, text_size)
        name = bytearray()
        while len(name) < 4096:
            remaining = min(128, 4096 - len(name), size - path_offset - len(name))
            require(remaining > 0, 'Unterminated/outside image name')
            block = read(path_offset + len(name), remaining)
            require(len(block) == remaining, 'Truncated image name')
            if b'\0' in block:
                name.extend(block.split(b'\0', 1)[0])
                break
            name.extend(block)
        else:
            raise ValueError('Image name exceeds bound')
        text = name.decode('utf-8', 'strict')
        require(text.startswith('/') and '\n' not in text, 'Invalid image install name')
        result.append({'uuid': uuid.hex(), 'address': address, 'size': text_size, 'path': text})
    for key in ('address', 'path'):
        require(len({row[key] for row in result}) == len(result), 'Duplicate image ' + key)
    return result


def observe_cache(path, budget, detail, *, with_images=False):
    row = {'path': str(path), 'complete': False, 'retained_metadata': []}
    try:
        declared = Path(path)
        require(declared.is_absolute(), 'Nonabsolute cache path')
        path = declared.resolve(strict=True)
        row['canonical_path'] = str(path)
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
        with os.fdopen(fd, 'rb') as stream:
            before = os.fstat(stream.fileno())
            require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= MAX_BYTES, 'Cache size/type bound')
            row.update(file_bytes=before.st_size, file_identity=identity(before))
            def read(offset, size):
                span(offset, size, before.st_size)
                require(time.monotonic() < budget['deadline'] and size <= budget['remaining'], 'Metadata deadline/byte bound')
                budget['remaining'] -= size
                stream.seek(offset); raw = stream.read(size)
                require(len(raw) == size, 'Metadata read shortened')
                row['retained_metadata'].append({'offset': offset, 'bytes': size, 'hex': raw.hex(),
                                                'sha256': hashlib.sha256(raw).hexdigest()})
                return raw
            row.update(detail.parse_cache(read, before.st_size))
            if with_images:
                row['images'] = image_table(read, before.st_size)
            after = os.fstat(stream.fileno())
        require(identity(before) == identity(after) == identity(path.stat()) and
                declared.resolve(strict=True) == path, 'Cache changed during metadata read')
        row['complete'] = True
    except Exception as error:
        row['error'] = type(error).__name__ + ': ' + str(error)
    return row


def events(raw):
    require(len(raw) <= MAX_OUTPUT, 'Native output byte bound')
    rows = [json.loads(line) for line in raw.splitlines()]
    require(0 < len(rows) <= 32768 and all(type(row) is dict and type(row.get('event')) is str for row in rows),
            'Native event count/type bound')
    return rows


def image_observation(rows):
    allowed = {'hash-controls', 'cache', 'cache-file', 'spi-image', 'header', 'generation', 'images-ended'}
    require(all(row['event'] in allowed for row in rows), 'Unexpected/incomplete native image event')
    require(all(type(row.get('round')) is int and row['round'] in (0, 1)
                for row in rows if row['event'] not in ('hash-controls', 'images-ended')), 'Invalid image round')
    control = [row for row in rows if row['event'] == 'hash-controls']
    ended = [row for row in rows if row['event'] == 'images-ended']
    require(len(control) == 1 and control[0].get('passed') == 12 and control[0].get('algorithm') == 'SHA256' and
            len(ended) == 1 and ended[0].get('complete') is True, 'Incomplete image/digest controls')
    rounds = []
    for number in (0, 1):
        selected = [row for row in rows if row.get('round') == number]
        cache = [row for row in selected if row['event'] == 'cache']
        generations = [row for row in selected if row['event'] == 'generation']
        headers = [row for row in selected if row['event'] == 'header']
        spi = [row for row in selected if row['event'] == 'spi-image']
        files = [row.get('path') for row in selected if row['event'] == 'cache-file']
        require(len(cache) == len(generations) == 1 and 0 < len(headers) <= MAX_IMAGES and len(spi) == len(headers) and
                0 < len(files) <= MAX_FILES and all(type(p) is str and p.startswith('/') and len(p.encode()) < 4096 for p in files) and
                len(set(files)) == len(files), 'Incomplete/duplicate image snapshot inventory')
        generation = generations[0]
        require(generation.get('complete') is True and generation.get('overflow') is False and
                type(generation.get('before')) is int and generation['before'] == generation.get('after'), 'Unstable image generation')
        cache = cache[0]
        require(cache.get('private') is False, 'Private shared-cache profile not admitted by this diagnostic')
        for name in ('uuid', 'loader_cache_uuid'):
            require(type(cache.get(name)) is str and re.fullmatch('[0-9a-f]{32}', cache[name]) and cache[name] != '0' * 32,
                    'Invalid active cache UUID')
        require(cache['uuid'] == cache['loader_cache_uuid'] and cache['base'] == cache['loader_cache_base'], 'Loader/SPI cache mismatch')
        require(type(cache.get('loader_cache_slide')) is int and 0 <= cache['loader_cache_slide'] <= U64,
                'Invalid active cache slide')
        span(cache['base'], cache['size'])
        require(cache['base'] > 0 and cache['size'] > 0, 'Empty active cache range')
        by_address = {}
        for row in headers:
            require(row.get('valid') is True and row.get('origin') in ('public-callback', 'own-task-loader'), 'Invalid actual loaded header')
            for key in ('address', 'text_address', 'text_size', 'dladdr_base'):
                span(row[key], 0)
            require(row['address'] > 0 and row['text_size'] > 0 and row['address'] not in by_address,
                    'Missing/duplicate loaded header')
            require(type(row.get('slide')) is int and -(1 << 63) <= row['slide'] < (1 << 63), 'Invalid loaded image slide')
            require(row['address'] == row['text_address'] + row['slide'], 'Actual header/TEXT slide mismatch')
            require(row['cpu'] in (0x1000007, 0x100000c) and type(row['flags']) is int and 0 <= row['flags'] < 2**32 and
                    row['kind'] in (2, 6, 7, 8), 'Unsupported loaded Mach-O profile')
            require(row['uuid'] is None or (type(row['uuid']) is str and re.fullmatch('[0-9a-f]{32}', row['uuid'])), 'Invalid image UUID')
            by_address[row['address']] = row
        require(cache['loader_header'] in by_address, 'Actual dyld loader header omitted')
        require(all(row['origin'] != 'own-task-loader' or row['address'] == cache['loader_header'] for row in headers), 'Unexpected synthetic loader header')
        seen = set()
        for row in spi:
            address = row['text_address']
            require(address in by_address and address not in seen, 'SPI image has no unique actual loaded header')
            seen.add(address)
            require(row['uuid'] == by_address[address]['uuid'] and row['text_size'] == by_address[address]['text_size'],
                    'SPI/actual UUID or TEXT range mismatch')
        rounds.append({'cache': cache, 'generation': generation, 'headers': headers, 'spi': spi, 'files': sorted(files)})
    def without_round(value):
        return [{k: v for k, v in row.items() if k != 'round'} for row in value]
    require(without_round(rounds[0]['headers']) == without_round(rounds[1]['headers']) and
            without_round(rounds[0]['spi']) == without_round(rounds[1]['spi']) and rounds[0]['files'] == rounds[1]['files'] and
            without_round([rounds[0]['cache']]) == without_round([rounds[1]['cache']]) and
            rounds[0]['generation']['after'] == rounds[1]['generation']['before'], 'Image snapshots changed after warmup')
    require(sum(row.get('round') in (0, 1) for row in rows) == len(rows) - 2, 'Unknown image round')
    return rounds[1]


def classify_headers(observation, files, correspondence, architecture):
    cache = observation['cache']
    base = correspondence['unslid_base']; slide = cache['base'] - base
    require(slide >= 0 and slide == cache['loader_cache_slide'], 'Disk/active cache slide mismatch')
    root = next(row for row in files if row['path'] == correspondence['root_path'])
    require(cache['size'] == root['shared_region_size'], 'Active/disk shared-region size mismatch')
    require(len({tuple(row['file_identity'][:2]) for row in files}) == len(files), 'Duplicate opened cache identity')
    require(sum(row['file_bytes'] for row in files) <= MAX_BYTES, 'Whole-cache byte bound')
    require(all(('arm64' in row['magic']) == (architecture == 'arm64') for row in files), 'Cache CPU mismatch')
    mappings = [(file, mapping) for file in files for mapping in file['mappings']]
    images = {row['address']: row for row in root['images']}
    require(len(images) == len(root['images']), 'Duplicate image table address')
    spi = {row['text_address']: row for row in observation['spi']}
    result = []
    for header in observation['headers']:
        require(header['cpu'] == (0x100000c if architecture == 'arm64' else 0x1000007), 'Loaded header CPU mismatch')
        address = header['address']
        matches = [(file, mapping) for file, mapping in mappings
                   if mapping['address'] + slide <= address < span(mapping['address'] + slide, mapping['size'])]
        require(len(matches) <= 1, 'Ambiguous active mapping')
        row = {'header': address, 'uuid': header['uuid'], 'loader': address == cache['loader_header']}
        if matches:
            file, mapping = matches[0]
            unslid = address - slide
            require(unslid in images, 'Loaded cache header lacks exact disk image address')
            image = images[unslid]
            require(header['slide'] == slide and header['flags'] & 0x80000000 and header['uuid'] == image['uuid'] and
                    header['text_size'] == image['size'] and spi[address]['install_name'] == image['path'],
                    'Loaded cache header/disk table identity mismatch')
            require(span(unslid, image['size']) <= span(mapping['address'], mapping['size']) and mapping['init_prot'] & 5 == 5,
                    'Cache TEXT crosses/misses executable mapping')
            offset = mapping['file_offset'] + unslid - mapping['address']
            span(offset, image['size'], file['file_bytes'])
            row.update(kind='cache', path=image['path'], backing=file['canonical_path'], file_offset=offset,
                       unslid_address=unslid, text_bytes=image['size'])
        else:
            # Names, UUIDs, MH_DYLIB_IN_CACHE and contains_path alone never exempt
            # an outside-cache override/copy from ordinary file attribution.
            require(not cache['base'] <= address < cache['base'] + cache['size'], 'Header lies in unmapped cache-envelope gap')
            path = header['path']
            require(type(path) is str and Path(path).is_absolute() and '\0' not in path and len(path.encode()) < 4096 and
                    header['dladdr_base'] == address, 'Ordinary loaded image has no actual readable-file attribution')
            row.update(kind='ordinary', path=path, text_bytes=header['text_size'])
        result.append(row)
    require(any(row['kind'] == 'cache' for row in result) and any(row['loader'] for row in result), 'No cache/loader correspondence')
    return result


def hash_result(rows, files):
    require(all(row['event'] in ('hash-controls', 'whole-hash', 'hash-pass', 'hash-ended') for row in rows), 'Incomplete hash events')
    require(all(type(row.get('pass')) is int and row['pass'] in (0, 1)
                for row in rows if row['event'] in ('whole-hash', 'hash-pass')), 'Invalid hash pass')
    control = [row for row in rows if row['event'] == 'hash-controls']
    end = [row for row in rows if row['event'] == 'hash-ended']
    require(len(control) == 1 and control[0].get('passed') == 12 and control[0].get('algorithm') == 'SHA256' and
            len(end) == 1 and end[0].get('complete') is True, 'Incomplete whole hash controls')
    expected = {row['canonical_path']: row for row in files}
    require(0 < len(expected) == len(files) <= MAX_FILES, 'Duplicate/missing hash input')
    total = sum(row['file_bytes'] for row in files)
    require(0 < total <= MAX_BYTES, 'Hash aggregate bytes exceed bound')
    passes = []
    for number in (0, 1):
        results = [row for row in rows if row['event'] == 'whole-hash' and row.get('pass') == number]
        summaries = [row for row in rows if row['event'] == 'hash-pass' and row.get('pass') == number]
        require(len(results) == len(expected) and len({row.get('path') for row in results}) == len(results) and
                {row.get('path') for row in results} == set(expected) and len(summaries) == 1, 'Incomplete hash pass inventory')
        for row in results:
            original = expected[row['path']]
            require(type(row.get('bytes')) is int and row['bytes'] == original['file_bytes'] and
                    row.get('identity') == original['file_identity'] and row.get('identity_unchanged') is True and
                    re.fullmatch('[0-9a-f]{64}', row.get('sha256', '')), 'Hash file identity/digest mismatch')
        require(summaries[0].get('files') == len(expected) and summaries[0].get('bytes') == total, 'Hash pass summary mismatch')
        passes.append({'files': results, 'summary': summaries[0]})
    require({row['path']: row['sha256'] for row in passes[0]['files']} ==
            {row['path']: row['sha256'] for row in passes[1]['files']}, 'Complete hash passes differ')
    require(end[0].get('files') == len(files) and end[0].get('bytes_per_pass') == total and
            end[0].get('total_hashed_bytes') == 2 * total, 'Final hash accounting mismatch')
    for row in rows:
        if row['event'] in ('whole-hash', 'hash-pass'):
            require('seconds_including_pread' in row, 'Missing hash timing')
        if row['event'] == 'hash-ended':
            require('seconds' in row, 'Missing final hash timing')
        for key in ('seconds_including_pread', 'seconds'):
            if key in row:
                require(type(row[key]) in (int, float) and math.isfinite(row[key]) and 0 <= row[key] <= HASH_SECONDS,
                        'Invalid/expired hash timing')
    require(len(rows) == 4 + 2 * len(expected), 'Unexpected hash pass or duplicate event')
    return {'passes': passes, 'final': end[0], 'whole_file_hashes_observed': True,
            'qualification': False, 'timing_scope': 'First/second observed full reads; no cold-cache or production nilbuild cost claim'}


def final_status(report):
    elapsed = report.get('elapsed_seconds')
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or not 0 <= elapsed <= TOTAL_SECONDS:
        report['problems'].append('Whole diagnostic elapsed bound exceeded or unavailable')
    return 'OBSERVED' if (all(report.get(key) is True for key in
        ('observations_complete', 'cache_metadata_unchanged', 'inputs_unchanged', 'cleanup_verified')) and
        not report['problems']) else 'INCOMPLETE'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() == args.architecture, 'Matching native macOS host required')
    require(re.fullmatch('[0-9a-f]{40}', args.revision), 'Exact source revision required')
    output, work = paths(args.output, args.work_dir)
    output.mkdir(parents=True, mode=0o700); work.mkdir(parents=True, mode=0o700)
    logs = output/'logs'; logs.mkdir(); (work/'tmp').mkdir()
    started = time.monotonic(); deadline = started + TOTAL_SECONDS; working_deadline = deadline - RESERVE_SECONDS
    source = Path(__file__).with_suffix('.cpp')
    detail_path = Path(__file__).with_name('macos-cache-detail-probes.py')
    support_path = ROOT/'tests/tools/ordinary_io_parity_contract.py'
    inputs = [Path(__file__).resolve(), source, Path(__file__).with_name('test_macos_cache_backing_probes.py'),
              detail_path, support_path, ROOT/'validation/ny-3d/scripts/debugger_process.py', ROOT/'common/content_digest.hpp']
    report = {'schema': 'nil.macos-cache-backing.v1', 'status': 'INCOMPLETE', 'qualification': False,
              'production_cache_eligibility_changed': False,
              'scope': 'Standalone own-process loaded-header attribution and complete backing-file hash observations',
              'revision': args.revision, 'architecture': args.architecture, 'platform': platform.platform(),
              'image': {key: os.environ.get(key) for key in ('ImageOS', 'ImageVersion', 'GITHUB_RUN_ID', 'GITHUB_SHA', 'GITHUB_RUN_ATTEMPT')},
              'whole_seconds_limit': TOTAL_SECONDS, 'cleanup_reserve_seconds': RESERVE_SECONDS,
              'hash_seconds_limit': HASH_SECONDS, 'cache_bytes_per_pass_limit': MAX_BYTES,
              'cache_file_limit': MAX_FILES, 'loaded_image_limit': MAX_IMAGES, 'metadata_bytes_limit': MAX_METADATA,
              'api_sources': [APPLE + path for path in ('include/mach-o/dyld.h', 'include/mach-o/dyld_images.h',
                  'include/mach-o/dyld_introspection.h', 'include/mach-o/dyld_cache_format.h', 'common/DyldSharedCache.cpp')],
              'limitations': ['No relocated-memory or vnode attestation; ordinary loader/file replacement races remain.',
                  'This probe is not nilcompiler and does not establish native linker/toolchain closure.',
                  'Production nilbuild still repeats full hashes; these timings do not qualify its reuse path.'],
              'phases': [], 'problems': [], 'inputs_before': {}}
    seals = report['inputs_before']; variables = environment(work); output_bytes = 0
    detail = None; cache_files = None; observed_paths = None
    def seal(path):
        key = str(Path(path).absolute())
        if key not in seals:
            seals[key] = record(Path(key), working_deadline)
    def run(name, argv, seconds=15, *, final=False):
        nonlocal output_bytes
        # Final observations may use the reserved interval, still leaving 5s
        # for an owned child teardown and writing the partial report.
        remaining = (deadline - 5 if final else working_deadline) - time.monotonic()
        require(remaining > 0, 'Whole observation deadline exhausted')
        row = support.run_process(ownership, list(map(str, argv)), work, variables, logs, name, min(seconds, remaining))
        report['phases'].append(row)
        raw = (logs/row['stdout']).read_bytes(); error = (logs/row['stderr']).read_bytes()
        output_bytes += len(raw) + len(error)
        require(output_bytes <= 32 * 1024**2, 'Aggregate process output bound')
        require(row.get('cleanup_verified') is True and not row.get('error') and not row.get('timed_out'),
                name + ': owned process failure; raw/partial logs retained')
        require(row.get('exit_code') == 0, name + ': native operation failed; raw/partial logs retained')
        return raw
    def checked(name, argv, seconds=15, *, final=False):
        return run(name, argv, seconds, final=final).decode('utf-8').strip()
    def metadata(paths, uuid, *, final=False):
        budget = {'remaining': MAX_METADATA,
                  'deadline': min(deadline - 5 if final else working_deadline, time.monotonic() + 35)}
        rows = [observe_cache(path, budget, detail) for path in paths]
        # Only the active root has the complete image TEXT table.
        roots = [row for row in rows if row.get('uuid') == uuid]
        # Keep every error/partial raw read even when root identification fails;
        # cache_correspondence will reject the incomplete inventory afterward.
        if len(roots) == 1:
            index = rows.index(roots[0]); rows[index] = observe_cache(roots[0]['path'], budget, detail, with_images=True)
        return rows
    try:
        for path in inputs: seal(path)
        detail = load(detail_path, 'cache_backing_detail')
        support = load(support_path, 'cache_backing_owned'); support.MAX_OUTPUT = MAX_OUTPUT
        ownership = support.load_ownership(ROOT)
        require(checked('source-revision', ['/usr/bin/git', '-C', ROOT, 'rev-parse', 'HEAD']) == args.revision, 'Source revision mismatch')
        report['source_status_before'] = checked('source-status', ['/usr/bin/git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=no'])
        require(not report['source_status_before'], 'Tracked source is dirty')
        sdk = Path(checked('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']))
        compiler = Path(checked('compiler', ['/usr/bin/xcrun', '--find', 'clang++']))
        require(sdk.is_absolute() and compiler.is_absolute(), 'Nonabsolute selected toolchain')
        for path in (Path(sys.executable), Path('/usr/bin/xcrun'), compiler, sdk/'SDKSettings.json'): seal(path)
        report['toolchain'] = {'sdk': str(sdk), 'compiler': str(compiler),
                               'version': checked('compiler-version', [compiler, '--version']),
                               'os': checked('os', ['/usr/bin/sw_vers'])}
        binary = work/'cache-backing-probe'
        run('compile', [compiler, '-std=c++17', '-O2', '-fblocks', '-Wall', '-Wextra', '-Werror',
                        '-mmacosx-version-min=15.0', '-isysroot', sdk, source, '-o', binary], 90)
        seal(binary)
        retained_binary = output/'cache-backing-probe'
        shutil.copyfile(binary, retained_binary); retained_binary.chmod(0o700); seal(retained_binary)
        require(seals[str(binary)]['sha256'] == seals[str(retained_binary)]['sha256'], 'Retained probe differs from executed binary')
        rows = events(run('images-before', [binary], 40)); report['image_events_before'] = rows
        observation = image_observation(rows); report['images_before'] = observation
        observed_paths = observation['files']
        cache_files = metadata(observed_paths, observation['cache']['uuid']); report['cache_files_before'] = cache_files
        correspondence = detail.cache_correspondence(cache_files, observation['cache']['uuid'], observed_paths)
        report['cache_correspondence'] = correspondence
        classifications = None
        try:
            classifications = classify_headers(observation, cache_files, correspondence, args.architecture)
            report['header_correspondence_before'] = classifications
            for path in sorted({row['path'] for row in classifications if row['kind'] == 'ordinary'}): seal(Path(path))
        except Exception as error:
            # Validated backing-file metadata is sufficient for the separate
            # cost observation even if the loaded-header join remains partial.
            report['problems'].append('Initial header correspondence: ' + str(error))
        raw = run('whole-cache-hashes', [binary, '--hash-files', *[row['canonical_path'] for row in cache_files]], HASH_SECONDS)
        rows = events(raw); report['hash_events'] = rows; report['whole_hashes'] = hash_result(rows, cache_files)
        rows = events(run('images-after', [binary], 40)); report['image_events_after'] = rows
        after = image_observation(rows); report['images_after'] = after
        require(after['files'] == observed_paths and after['cache']['uuid'] == observation['cache']['uuid'], 'Active cache changed after hashes')
        report['header_correspondence_after'] = classify_headers(after, cache_files, correspondence, args.architecture)
        stable = lambda rows: sorted((row['kind'], row['path'], row['uuid'], row['loader'], row['text_bytes']) for row in rows)
        require(classifications is not None and stable(classifications) == stable(report['header_correspondence_after']),
                'Loaded module graph changed or initial join incomplete')
        report['observations_complete'] = True
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        # Independent after-checks run even after hash/observation failure.
        try:
            report['inputs_after'] = {key: record(Path(key), deadline - 5) for key in seals}
            report['inputs_unchanged'] = report['inputs_after'] == seals
            require(report['inputs_unchanged'], 'Source/tool/ordinary file seals changed')
        except Exception as error:
            report['inputs_unchanged'] = False; report['problems'].append('Final input seals: ' + str(error))
        if cache_files is not None:
            try:
                after = metadata(observed_paths, report['images_before']['cache']['uuid'], final=True)
                report['cache_files_after'] = after
                report['cache_metadata_unchanged'] = cache_files == after
                require(report['cache_metadata_unchanged'], 'Cache metadata changed after observation')
            except Exception as error:
                report['cache_metadata_unchanged'] = False; report['problems'].append('Final cache metadata: ' + str(error))
        try:
            if report['phases']:
                report['source_revision_after'] = checked('source-revision-after', ['/usr/bin/git', '-C', ROOT, 'rev-parse', 'HEAD'], final=True)
                report['source_status_after'] = checked('source-status-after', ['/usr/bin/git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=no'], final=True)
                require(report['source_revision_after'] == args.revision and report['source_status_after'] == '', 'Source checkout changed')
        except Exception as error:
            report['problems'].append('Final source state: ' + str(error))
        report['cleanup_verified'] = bool(report['phases']) and all(row.get('cleanup_verified') is True for row in report['phases'])
        report['elapsed_seconds'] = time.monotonic() - started
        report['status'] = final_status(report)
        (output/'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print(json.dumps({key: report.get(key) for key in ('status', 'qualification', 'architecture', 'problems', 'cleanup_verified', 'elapsed_seconds')}))
    return 0 if report['status'] == 'OBSERVED' else 2


if __name__ == '__main__':
    raise SystemExit(main())
