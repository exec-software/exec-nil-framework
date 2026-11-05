#!/usr/bin/env python3
"""Build a pinned, private GTK provider and observe its public macOS API only."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shlex
import stat
import struct
import sys
import tarfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / 'scripts/ci/macos-gtk-provider-lock.json'
TOTAL_SECONDS, RESERVE_SECONDS = 1800, 30
MiB = 1024**2
MAX_OUTPUT, MAX_TOTAL_OUTPUT = 8 * MiB, 128 * MiB
MAX_ENTRIES, MAX_EXPANDED, MAX_FILE = 20000, 256 * MiB, 16 * MiB
INPUTS = ('scripts/ci/macos-gtk-provider.py', 'scripts/ci/macos-gtk-provider-lock.json',
          'scripts/ci/macos-gtk-provider-smoke.mm', 'scripts/ci/macos-ui-probes.py',
          'tests/tools/ordinary_io_parity_contract.py', 'validation/ny-3d/scripts/debugger_process.py')
BUILD_ORDER = ('fribidi-1.0.16', 'pango-1.56.4', 'gdk-pixbuf-2.42.12',
               'graphene-1.10.8', 'libepoxy-1.5.10', 'gtk-4.18.6')
IMAGE_SOURCE = r'''
#include <png.h>
#include <tiffio.h>
#include <jpeglib.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
static bool origin(const char *name, const void *symbol) {
  Dl_info d{}; if (!dladdr(symbol, &d) || !d.dli_fname) return false;
  std::printf("%s_path=%s\n", name, d.dli_fname); return true;
}
int main() {
  // Encode then decode actual RGBA pixels through the selected PNG runtime.
  const unsigned char pixels[4] = {17, 29, 43, 255};
  unsigned char encoded[1024]{}, decoded[4]{};
  png_alloc_size_t bytes = sizeof(encoded);
  png_image image{}; image.version = PNG_IMAGE_VERSION; image.width = image.height = 1;
  image.format = PNG_FORMAT_RGBA;
  bool ok = png_image_write_to_memory(&image, encoded, &bytes, 0, pixels, 0, nullptr) != 0;
  png_image read{}; read.version = PNG_IMAGE_VERSION;
  ok = ok && png_image_begin_read_from_memory(&read, encoded, bytes);
  read.format = PNG_FORMAT_RGBA;
  ok = ok && read.width == 1 && read.height == 1 &&
       png_image_finish_read(&read, nullptr, decoded, 0, nullptr) && !std::memcmp(pixels, decoded, 4);
  png_image_free(&read); png_image_free(&image);
  jpeg_decompress_struct jpeg{}; jpeg_error_mgr errors{};
  jpeg.err = jpeg_std_error(&errors); jpeg_create_decompress(&jpeg); jpeg_destroy_decompress(&jpeg);
  std::printf("png_header=%lu\npng_runtime=%lu\npng_roundtrip=%s\njpeg_header=%d\njpeg_abi=passed\ntiff_header=%d\ntiff_runtime=%d\n",
      static_cast<unsigned long>(PNG_LIBPNG_VER), static_cast<unsigned long>(png_access_version_number()),
      ok ? "passed" : "failed", JPEG_LIB_VERSION, TIFFLIB_VERSION,
      std::strncmp(TIFFGetVersion(), TIFFLIB_VERSION_STR, std::strlen(TIFFLIB_VERSION_STR)) == 0);
  bool paths = origin("png", reinterpret_cast<const void *>(&png_access_version_number)) &&
      origin("tiff", reinterpret_cast<const void *>(&TIFFGetVersion)) &&
      origin("jpeg", reinterpret_cast<const void *>(&jpeg_std_error));
  return ok && paths && PNG_LIBPNG_VER == png_access_version_number() &&
      std::strncmp(TIFFGetVersion(), TIFFLIB_VERSION_STR, std::strlen(TIFFLIB_VERSION_STR)) == 0 ? 0 : 2;
}
'''


def require(value, message):
    if not value:
        raise ValueError(message)


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')


def digest_json(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def remaining(deadline, limit):
    left = deadline - time.monotonic()
    require(left > 0, 'Whole provider deadline exceeded')
    return min(left, limit)


def safe_name(raw):
    path = PurePosixPath(raw)
    require(raw and not path.is_absolute() and '..' not in path.parts and '\\' not in raw and
            not any(ord(c) < 32 for c in raw) and path.as_posix() == raw.rstrip('/'), 'Unsafe archive member name')
    return path


def member_manifest(members, root):
    require(0 < len(members) <= MAX_ENTRIES, 'Archive entry bound exceeded')
    rows, names, total = [], {}, 0
    for member in members:
        path = safe_name(member.name)
        require(path.parts[0] == root and path.as_posix() not in names, 'Archive root/duplicate differs')
        require(member.isdir() or member.isfile() or member.issym(), 'Archive contains unsupported type')
        require(not member.mode & 0o7000 and type(member.mtime) in (int, float) and
                math.isfinite(member.mtime) and 0 <= member.mtime <= 4102444800, 'Invalid archive mode/time')
        require(0 <= member.size <= MAX_FILE and (member.isfile() or member.size == 0), 'Archive member size exceeds bound')
        total += member.size
        require(total <= MAX_EXPANDED, 'Archive expanded size exceeds bound')
        names[path.as_posix()] = member
        rows.append({'name': member.name, 'kind': 'directory' if member.isdir() else 'file' if member.isfile() else 'symlink',
                     'size': member.size, 'mode': member.mode, 'mtime': member.mtime, 'link': member.linkname})
    for name, member in names.items():
        path = PurePosixPath(name)
        for parent in path.parents:
            require(str(parent) not in names or names[str(parent)].isdir(), 'Archive parent is not a directory')
        if member.issym():
            # The locked GdkPixbuf aliases are single-basename references to
            # existing regular test images, never traversed during extraction.
            link = safe_name(member.linkname)
            target = (path.parent / link).as_posix()
            require(len(link.parts) == 1 and target in names and names[target].isfile(), 'Archive alias target is not a sibling regular file')
    require(root in names and names[root].isdir(), 'Archive root directory missing')
    return rows, {'entries': len(rows), 'expanded_bytes': total,
                  'maximum_file_bytes': max(m.size for m in members), 'manifest_sha256': digest_json(rows)}


def extract_source(archive, destination, item, deadline, record):
    require(record(archive, deadline)['sha256'] == item['sha256'] and archive.stat().st_size == item['bytes'], 'Release archive digest/size differs')
    require(destination.is_dir() and not any(destination.iterdir()), 'Fresh extraction directory required')
    with tarfile.open(archive, 'r:xz') as source:
        members = []
        for member in source:
            remaining(deadline, 1); members.append(member)
            require(len(members) <= MAX_ENTRIES, 'Archive entry bound exceeded')
        rows, inventory = member_manifest(members, item['root'])
        require(inventory == item['members'], 'Locked release member inventory differs')
        for member in members:
            remaining(deadline, 1)
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as incoming, target.open('xb') as outgoing:
                    copied = 0
                    while block := incoming.read(MiB):
                        remaining(deadline, 1); copied += len(block)
                        require(copied <= member.size, 'Archive member grew')
                        outgoing.write(block)
                    require(copied == member.size, 'Archive member truncated')
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
                os.utime(target, (member.mtime, member.mtime), follow_symlinks=False)
        # All links were checked against regular, non-link archive members.
        for member in members:
            if member.issym():
                destination.joinpath(*PurePosixPath(member.name).parts).symlink_to(member.linkname)
        for member in reversed(members):
            if member.isdir():
                os.utime(destination / member.name, (member.mtime, member.mtime), follow_symlinks=False)
        for name, expected in item['required_files'].items():
            actual = record(destination / name, deadline)
            require({k: actual[k] for k in ('bytes', 'sha256')} == expected, 'Required generated/build input differs: ' + name)
    return inventory


def extract_meson(archive, destination, item, deadline, record):
    actual = record(archive, deadline)
    require({k: actual[k] for k in ('bytes', 'sha256')} == {k: item[k] for k in ('bytes', 'sha256')}, 'Meson wheel digest/size differs')
    with zipfile.ZipFile(archive) as source:
        members = source.infolist()
        require(0 < len(members) <= 2000, 'Meson wheel entry bound exceeded')
        rows, names, total = [], set(), 0
        for entry in members:
            remaining(deadline, 1)
            path = safe_name(entry.filename)
            mode = entry.external_attr >> 16
            require(path.as_posix() not in names and (stat.S_ISREG(mode) or mode == 0) and not entry.is_dir() and
                    0 <= entry.file_size <= MAX_FILE and not mode & 0o7000, 'Invalid Meson wheel member')
            names.add(path.as_posix()); total += entry.file_size
            require(total <= 32 * MiB, 'Meson expanded bound exceeded')
            rows.append({'name': entry.filename, 'bytes': entry.file_size, 'mode': mode, 'compressed_bytes': entry.compress_size})
        require({'entries': len(rows), 'expanded_bytes': total, 'manifest_sha256': digest_json(rows)} == item['members'], 'Locked wheel inventory differs')
        for entry in members:
            target = destination / entry.filename
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as incoming, target.open('xb') as outgoing:
                copied = 0
                while block := incoming.read(MiB):
                    remaining(deadline, 1); copied += len(block)
                    require(copied <= entry.file_size, 'Meson member grew'); outgoing.write(block)
                require(copied == entry.file_size, 'Meson member truncated')
            target.chmod(0o644)
    require((destination / 'mesonbuild/mesonmain.py').is_file(), 'Meson entry point absent')


def tree_seal(root, deadline, record):
    result, total = {}, 0
    # os.walk never follows directory aliases; internal file symlinks remain
    # explicit records, including dangling and external aliases as errors.
    for parent, dirs, files in os.walk(root, followlinks=False, onerror=lambda e: (_ for _ in ()).throw(e)):
        for name in sorted(dirs + files):
            remaining(deadline, 1)
            path = Path(parent) / name; mode = path.lstat().st_mode
            require(len(result) < 40000, 'Tree entry bound exceeded')
            key = path.relative_to(root).as_posix()
            if stat.S_ISDIR(mode):
                result[key] = {'directory': True, 'mode': stat.S_IMODE(mode)}
            elif stat.S_ISLNK(mode):
                target = path.resolve(strict=True)
                require(root in target.parents and target.is_file(), 'Tree alias leaves prefix or names a directory')
                result[key] = {'link': os.readlink(path), 'target': target.relative_to(root).as_posix()}
            else:
                require(stat.S_ISREG(mode), 'Tree contains special file')
                row = record(path, deadline)
                result[key] = {k: row[k] for k in ('bytes', 'sha256', 'mode')}
                total += row['bytes']; require(total <= 2 * 1024**3, 'Tree byte bound exceeded')
    require(result, 'Empty tree seal')
    return result


def environment(work, inherited=None):
    inherited = os.environ if inherited is None else inherited
    env = {k: inherited[k] for k in ('HOME', 'USER', 'LOGNAME', '__CF_USER_TEXT_ENCODING') if k in inherited}
    env.update(PATH=str(work / 'tool-bin') + ':/usr/bin:/bin:/usr/sbin:/sbin',
               LC_ALL='C', LANG='C', TZ='UTC', TMPDIR=str(work / 'tmp'), PYTHONDONTWRITEBYTECODE='1',
               ZERO_AR_DATE='1', GIT_OPTIONAL_LOCKS='0')
    return env


def setup_arguments(meson, source, build, prefix, native_file, item):
    return [*meson, 'setup', str(build), str(source), '--prefix=' + str(prefix), '--libdir=lib',
            '--buildtype=release', '--default-library=shared', '--wrap-mode=nofallback',
            '--native-file=' + str(native_file), *item['options']]


def image_result(raw):
    result = {}
    for line in raw.decode().splitlines():
        key, sep, value = line.partition('=')
        require(sep and key not in result and value, 'Invalid image prerequisite record'); result[key] = value
    require(set(result) == {'png_header', 'png_runtime', 'png_roundtrip', 'jpeg_header', 'jpeg_abi',
                           'tiff_header', 'tiff_runtime', 'png_path', 'tiff_path', 'jpeg_path'}, 'Image prerequisite fields differ')
    require(result['png_header'].isdigit() and result['png_header'] == result['png_runtime'] and
            result['png_roundtrip'] == result['jpeg_abi'] == 'passed' and result['tiff_runtime'] == '1' and
            result['jpeg_header'].isdigit() and result['tiff_header'].isdigit(), 'Image header/runtime operation differs')
    return result


def smoke_result(row):
    require(isinstance(row, dict) and row.get('schema') == 'nil.macos-gtk-provider-smoke.v1' and
            row.get('scope') == 'private-provider-own-process' and row.get('status') == 'PASS' and
            row.get('language_acceptance') is False and row.get('main_thread') is True and
            'error' not in row, 'GTK smoke identity/status differs')
    require(row.get('backend') == 'GdkMacosDisplay' and row.get('renderer') == 'GskCairoRenderer',
            'GTK backend/renderer differs')
    versions = row['versions']
    for name, version in (('gtk','4.18.6'),('pango','1.56.4'),('pixbuf','2.42.12')):
        require(versions[name+'_header'] == versions[name+'_runtime'] == version, 'GTK provider version differs')
    require(isinstance(versions['png_header'], str) and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', versions['png_header']) and
            versions['png_header'] == versions['png_runtime'], 'PNG header/runtime differs')
    integer = lambda value, expected: type(value) is int and value == expected
    cleanup = row['cleanup']
    require(cleanup.get('verified') is True and cleanup.get('own_window_hidden') is True and integer(cleanup.get('gtk_toplevel_count'), 0), 'GTK cleanup not proven')
    require(integer(row.get('unexpected_keys'), 0) and integer(row.get('close_requests'), 1), 'Unexpected key/close count')
    elapsed = row.get('elapsed_seconds')
    require(type(elapsed) in (int, float) and math.isfinite(elapsed) and 0 <= elapsed <= 29, 'GTK elapsed bound differs')
    widgets = row['widgets']
    require(widgets.get('entry_text') == 'n' and widgets.get('checkbox_active') is True and
            all(integer(widgets.get(key), 1) for key in ('entry_changed','button_clicked','checkbox_toggled')),
            'GTK widget outcome differs')
    png = row['png']
    require(png.get('decoded') is True and integer(png.get('width'), 1) and integer(png.get('height'), 1) and
            integer(png.get('channels'), 4) and png.get('rgba') == [17,34,51,255] and
            all(type(x) is int for x in png['rgba']), 'GTK PNG pixel oracle differs')
    font = row['font']
    require(font.get('quartz') is True and font.get('coretext') is True and
            all(type(font.get(key)) is int and 0 < font[key] < 4096 for key in ('width','height')),
            'Pango CoreText/layout proof missing')
    require(len(row['events']) == len(row['observed_keys']) == 3, 'GTK event inventory differs')
    windows, seconds_seen = set(), []
    for index, event in enumerate(row['events']):
        require(all(integer(event.get(key), value) for key, value in (
            ('stage',index), ('keycode',45 if index == 0 else 49), ('keyval',110 if index == 0 else 32),
            ('modifiers',0), ('queued_count',2))), 'GTK queued event differs')
        seconds = event['timestamp_seconds']
        require(type(seconds) in (int,float) and math.isfinite(seconds) and 1 <= seconds <= 4294967296 and
                math.floor(seconds) == seconds and float.fromhex(event['timestamp_hex']) == seconds and
                integer(event.get('gdk_time_ms'), int(seconds*1000) % 2**32), 'GTK event timestamp differs')
        require(type(event.get('window_number')) is int and event['window_number'] > 0, 'GTK event window differs')
        windows.add(event['window_number']); seconds_seen.append(seconds)
        require(all(integer(row['observed_keys'][index].get(key), 1) for key in ('pressed','released')),
                'GTK translated key dispatch missing or duplicated')
    require(len(windows) == 1 and seconds_seen == [seconds_seen[0]+i for i in range(3)], 'GTK event identity sequence differs')
    require(len(row['dispatched_keys']) == 6, 'GTK actual dispatch inventory differs')
    for index, key in enumerate(row['dispatched_keys']):
        event = row['events'][index//2]
        require(all(integer(key.get(name), event[name]) for name in ('stage','keyval','keycode','modifiers','gdk_time_ms')) and
                key.get('release') is bool(index%2) and key.get('own_surface') is True and key.get('matches') is True,
                'GTK dispatched event metadata differs from own queued event')
    origins = row['origins']
    require(set(origins) == {'gtk','pango','pangocairo','pixbuf','glib','gobject','cairo','png','harfbuzz','coretext','appkit'},
            'GTK runtime origin inventory differs')
    for key, value in origins.items():
        require(isinstance(value, dict) and isinstance(value.get('declared'), str) and
                value['declared'].startswith('/') and len(value['declared']) <= 4096, 'GTK declared origin invalid')
        if key in ('coretext','appkit'):
            require(system_library(value['declared']) and type(value.get('file_backed')) is bool and
                    (value['canonical'] is None) == (not value['file_backed']), 'Apple framework origin differs')
        else:
            require(value.get('file_backed') is True and isinstance(value.get('canonical'), str) and
                    value['canonical'].startswith('/'), 'GTK file-backed origin absent')
    return row


def macho_header(raw, architecture):
    require(len(raw) == 32, 'Truncated Mach-O header')
    magic, cpu, subtype, kind, commands, size, flags, reserved = struct.unpack('<8I', raw)
    require(magic == 0xfeedfacf and cpu == {'arm64':0x100000c,'x86_64':0x1000007}[architecture] and
            kind in (2,6,8) and 0 < commands <= 4096 and 0 < size <= 16*MiB,
            'Loader file is not the declared native64 executable/dylib/bundle')
    return {'architecture':architecture,'cpu':cpu,'subtype':subtype,'file_type':kind,
            'load_commands':commands,'command_bytes':size,'flags':flags,'reserved':reserved}


def loader_commands(raw):
    commands, current = [], None
    for line in raw.decode().splitlines():
        value = line.strip()
        if value.startswith('cmd '):
            current = {'command': value[4:]}
            if current['command'] in ('LC_ID_DYLIB', 'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                                      'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB', 'LC_RPATH'):
                commands.append(current)
        elif current in commands and (value.startswith('name ') or value.startswith('path ')):
            current['path'] = value.split(' ', 1)[1].rsplit(' (offset ', 1)[0]
    require(len(commands) <= 256 and all(x.get('path') for x in commands), 'Malformed loader command inventory')
    return commands


def system_library(name):
    return name.startswith('/usr/lib/') or name.startswith('/System/Library/')


def resolve_load(name, owner, rpaths, executable):
    def expand(value):
        if value == '@loader_path':
            return owner.parent
        if value == '@executable_path':
            return executable.parent
        if value.startswith('@loader_path/'):
            return owner.parent / value[len('@loader_path/'):]
        if value.startswith('@executable_path/'):
            return executable.parent / value[len('@executable_path/'):]
        require(value.startswith('/'), 'Unresolved loader placeholder: ' + value)
        return Path(value)
    if name.startswith('@rpath/'):
        candidates = [expand(value) / name[len('@rpath/'):] for value in rpaths]
        existing = [path for path in candidates if path.is_file()]
        require(existing, 'Unresolved @rpath dependency: ' + name)
        return existing[0].resolve(strict=True)
    return expand(name).resolve(strict=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--architecture', required=True, choices=('arm64', 'x86_64'))
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() == args.architecture and platform.mac_ver()[0].split('.')[0] == '15', 'Native macOS15 matching architecture required')
    require(re.fullmatch('[0-9a-f]{40}', args.revision), 'Exact source revision required')
    ui = load('gtk_provider_ui_support', 'scripts/ci/macos-ui-probes.py')
    output, work = ui.paths(args.output, args.work_dir)
    output.mkdir(parents=True, mode=0o700); work.mkdir(mode=0o700)
    logs, prefix = output / 'logs', work / 'prefix'
    for path in (logs, prefix, work / 'tmp', work / 'tool-bin', work / 'sources', work / 'builds', work / 'meson'):
        path.mkdir(mode=0o700)
    started = time.monotonic(); final = started + TOTAL_SECONDS; deadline = final - RESERVE_SECONDS
    report = {'schema': 'nil.macos.gtk-provider.v1', 'status': 'FAIL', 'qualification': False,
              'sdk_replaced': False, 'product_runtime_admitted': False,
              'scope': 'Private prefix build and public GTK API observation only; host-dependent, not redistributed',
              'revision': args.revision, 'architecture': args.architecture, 'prefix': str(prefix),
              'work_dir': str(work), 'work_retained': True, 'phases': [], 'problems': [], 'phase': 'initialize',
              'limits': {'whole_seconds': TOTAL_SECONDS, 'reserve_seconds': RESERVE_SECONDS, 'parallel_jobs': 2,
                         'phase_output_bytes': MAX_OUTPUT, 'aggregate_output_bytes': MAX_TOTAL_OUTPUT}}
    inputs, releases, sources, source_seals, external = {}, {}, {}, {}, {}
    support = ownership = None; prefix_before = None; total_output = 0
    variables = environment(work)
    homebrew = Path('/opt/homebrew' if args.architecture == 'arm64' else '/usr/local')
    def seal(path):
        row = ui.record(Path(path), deadline)
        require(str(path) not in inputs or inputs[str(path)] == row, 'Previously sealed input changed: ' + str(path))
        inputs[str(path)] = row
        return row
    def run(name, argv, seconds=30, *, cwd=work, env=None, final_check=False, check_exit=True):
        nonlocal total_output
        if not final_check:
            report['phase'] = name
        row = support.run_process(ownership, [str(x) for x in argv], cwd, env or variables, logs,
                                  name, remaining(final - 5 if final_check else deadline, seconds))
        report['phases'].append(row); save(logs / (name + '.json'), row)
        for key in ('stdout_record', 'stderr_record'):
            total_output += row.get(key, {}).get('bytes', 0)
        require(total_output <= MAX_TOTAL_OUTPUT, 'Aggregate output bound exceeded')
        require(row.get('cleanup_verified') is True and not row.get('error') and (not check_exit or row.get('exit_code') == 0),
                name + ': owned phase failed; raw evidence retained')
        return (logs / row['stdout']).read_bytes()
    def text(name, argv, seconds=10): return run(name, argv, seconds).decode().strip()
    def flags(name, argv):
        result = shlex.split(text(name, argv))
        require(len(result) <= 256 and not any('\0' in x or '\n' in x for x in result), 'Compiler flag inventory invalid')
        return result
    def header_seal(name, compiler, source, cflags):
        target = output / (name + '-headers.d')
        run(name + '-headers', [compiler, '-isysroot', sdk, '-M', '-MF', target, source, *cflags], 30)
        raw = target.read_text().replace('\\\n', '')
        require(len(raw) <= MiB and ':' in raw, 'Invalid header dependency output')
        headers = shlex.split(raw.split(':', 1)[1]); require(0 < len(headers) <= 5000, 'Header inventory bound exceeded')
        for header in headers:
            path = Path(header).resolve(strict=True)
            # SDK/system headers are covered by declared selected SDK metadata;
            # every non-SDK header participating in the probe is byte-sealed.
            if sdk not in path.parents and not str(path).startswith('/Library/Developer/CommandLineTools/SDKs/'):
                seal(path)
        return headers
    try:
        for name in INPUTS: seal(ROOT / name)
        lock = json.loads(LOCK.read_bytes())
        require(lock['schema'] == 'nil.macos.gtk-provider-lock.v1' and tuple(x['root'] for x in lock['sources']) == BUILD_ORDER, 'Provider lock/profile differs')
        support = load('gtk_provider_capture', 'tests/tools/ordinary_io_parity_contract.py')
        support.MAX_OUTPUT = MAX_OUTPUT; ownership = support.load_ownership(ROOT)
        require(text('revision-before', ['/usr/bin/git', '-C', ROOT, 'rev-parse', 'HEAD']) == args.revision, 'Source revision differs')
        require(not text('source-before', ['/usr/bin/git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=no']), 'Clean source required')
        baseline = lock['baseline'][args.architecture]
        sdk = Path(text('apple-sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'])).resolve(strict=True)
        compiler = Path(text('compiler', ['/usr/bin/xcrun', '--find', 'clang++']))
        ccompiler = Path(text('c-compiler', ['/usr/bin/xcrun', '--find', 'clang']))
        for path in (Path(sys.executable), compiler, ccompiler, sdk / 'SDKSettings.json', Path('/usr/bin/xcrun')): seal(path)
        report['toolchain'] = {'sdk': str(sdk), 'compiler': str(compiler), 'c_compiler': str(ccompiler),
                               'version': text('compiler-version', [compiler, '--version'])}
        # Only explicit build tools are visible in addition to Apple's system
        # commands. No incidental Meson/Sass/other package-manager tools enter PATH.
        tools = {'python3': Path(sys.executable), 'pkg-config': homebrew/'bin/pkg-config',
                 'ninja': homebrew/'bin/ninja', **{name: homebrew/'bin'/name for name in (
                 'glib-compile-resources', 'glib-compile-schemas', 'glib-genmarshal', 'glib-mkenums',
                 'gdbus-codegen', 'gio-querymodules', 'msgfmt', 'msgmerge', 'xgettext')}}
        for name, path in list(tools.items()):
            if name in ('msgfmt', 'msgmerge', 'xgettext') and not path.exists():
                path = homebrew/'opt/gettext/bin'/name
                tools[name] = path
            seal(path); require(os.access(path, os.X_OK), 'Declared tool not executable: ' + name)
            (work / 'tool-bin' / name).symlink_to(path)
        pkg = tools['pkg-config']; selected_pkg = ui.pkg_command(pkg, sdk)
        compatibility = ui.compatibility_directory(text('pkg-search', [pkg, '--variable=pc_path', 'pkg-config']), homebrew, '15')
        require(str(compatibility) == baseline['pkg_config_compatibility'], 'Observed compatibility directory changed')
        variables['PKG_CONFIG_LIBDIR'] = ':'.join(str(x) for x in (
            prefix/'lib/pkgconfig', prefix/'share/pkgconfig', homebrew/'lib/pkgconfig', homebrew/'share/pkgconfig', compatibility))
        # Meson invokes this wrapper, so its own dependency resolution uses the
        # same selected SDK substitution as the explicit preflight commands.
        wrapper = work/'pkg-config'
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([str(x) for x in selected_pkg]) + ' "$@"\n'); wrapper.chmod(0o700); seal(wrapper)
        variables['PKG_CONFIG'] = str(wrapper)
        variables['CC'], variables['CXX'] = str(ccompiler), str(compiler)
        variables['OBJC'], variables['OBJCXX'] = str(ccompiler), str(compiler)
        variables['SDKROOT'] = str(sdk)
        variables['MACOSX_DEPLOYMENT_TARGET'] = '15.0'
        variables['CFLAGS'] = variables['CXXFLAGS'] = variables['OBJCFLAGS'] = variables['OBJCXXFLAGS'] = '-isysroot ' + shlex.quote(str(sdk))
        report['build_environment'] = dict(variables)
        report['build_tools'] = {name: inputs[str(path)] for name, path in tools.items()}
        report['required_codec_policy'] = {'source': 'gtk-4.18.6/meson.build',
            'required': ['libpng', 'libtiff-4', 'libjpeg'], 'gdk_pixbuf_builtin_loaders': ['png'],
            'source_sha256': lock['sources'][-1]['required_files']['gtk-4.18.6/meson.build']['sha256']}
        packages = {}
        for module in (*ui.REQUIRED, 'libpng', 'libtiff-4', 'libjpeg'):
            version = text('version-' + module, [*selected_pkg, '--modversion', module])
            packages[module] = {'version': version}
            pcdir = Path(text('pcdir-' + module, [*selected_pkg, '--variable=pcfiledir', module]))
            require(homebrew.resolve() in pcdir.resolve(strict=True).parents, 'Prerequisite metadata outside selected Homebrew prefix')
            seal(pcdir / (module + '.pc'))
        for module in ('libffi', 'zlib', 'bzip2', 'expat'):
            pcdir = Path(text('system-pcdir-' + module, [*selected_pkg, '--variable=pcfiledir', module]))
            require(pcdir.resolve(strict=True) == compatibility.resolve(), 'System pkg metadata origin differs'); seal(pcdir/(module+'.pc'))
        for name, expected in baseline['glib_tools'].items():
            require(text(name + '-version', [tools[name], '--version']) == expected['version'], 'GLib tool version differs')
        for name, module in (('glib', 'glib-2.0'), ('cairo', 'cairo'), ('harfbuzz', 'harfbuzz')):
            require(packages[module]['version'] == baseline['versions'][name], 'Pinned prerequisite version differs')
        dependencies = work/'dependencies.cpp'; dependencies.write_text(ui.DEPENDENCY_SOURCE); seal(dependencies)
        deps_flags = flags('dependency-flags', [*selected_pkg, '--cflags', '--libs', *ui.REQUIRED])
        header_seal('dependencies', compiler, dependencies, deps_flags)
        binary = work/'dependencies'
        run('compile-dependencies', [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-isysroot', sdk,
                                    dependencies, *deps_flags, '-framework', 'CoreText', '-framework', 'CoreGraphics', '-o', binary], 45)
        seal(binary); observed, matched = ui.dependency_result(run('load-dependencies', [binary], 10), packages)
        require(matched and observed['operations'] == 'passed', 'Prerequisite headers/runtime/operations differ')
        loaded = {}
        for name, value in observed.items():
            if name.endswith('_path'):
                row = seal(Path(value)); loaded[row['canonical_path']] = row
        require(set(loaded) == set(baseline['loaded_files']), 'Pinned prerequisite loaded origins differ')
        for name, expected in baseline['loaded_files'].items():
            require(all(loaded[name][key] == expected[key] for key in ('bytes', 'sha256', 'mode')), 'Pinned prerequisite bytes differ')
        report['prerequisites'] = {'packages': packages, 'operations': observed, 'loaded': loaded}
        images = work/'images.cpp'; images.write_text(IMAGE_SOURCE); seal(images)
        image_flags = flags('image-flags', [*selected_pkg, '--cflags', '--libs', 'libpng', 'libtiff-4', 'libjpeg'])
        header_seal('images', compiler, images, image_flags)
        image_binary = work/'images'
        run('compile-images', [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-isysroot', sdk,
                              images, *image_flags, '-o', image_binary], 45)
        seal(image_binary); report['images'] = image_result(run('image-preflight', [image_binary], 10))
        png_version = packages['libpng']['version'].split('.')
        require(len(png_version) == 3 and all(x.isdigit() for x in png_version) and
                int(report['images']['png_header']) == int(png_version[0])*10000+int(png_version[1])*100+int(png_version[2]),
                'PNG metadata/header/runtime version differs')
        for name in ('png', 'tiff', 'jpeg'):
            path = Path(report['images'][name+'_path']); require(homebrew.resolve() in path.resolve(strict=True).parents, 'Image runtime leaves declared Homebrew prefix'); seal(path)
        # All archives and the wheel are verified/extracted before any of their
        # executable build code is run. No fetch fallback or package installation.
        items = [*lock['sources'], {**lock['meson'], 'name': lock['meson']['filename']}]
        for index, item in enumerate(items):
            archive = work/item['name']
            run('download-' + str(index), ['/usr/bin/curl', '--disable', '--fail', '--location', '--silent', '--show-error',
                '--proto', '=https', '--proto-redir', '=https', '--connect-timeout', '15', '--max-time', '55',
                '--max-filesize', str(item['bytes']), '--output', archive, item['url']], 60)
            releases[item['name']] = seal(archive)
            if 'root' in item:
                dest = work/'sources'/item['root']; dest.mkdir()
                extract_source(archive, dest, item, deadline, ui.record)
                source = dest/item['root']; sources[item['root']] = source
                source_seals[item['root']] = tree_seal(source, deadline, ui.record)
            else:
                extract_meson(archive, work/'meson', item, deadline, ui.record)
                sources['meson-wheel'] = work/'meson'
                source_seals['meson-wheel'] = tree_seal(work/'meson', deadline, ui.record)
        save(output/'release-inputs.json', {'archives': releases, 'sources': source_seals, 'lock': inputs[str(LOCK)]})
        launcher = work/'meson-launch.py'
        launcher.write_text('import runpy,sys\nsys.path.insert(0,' + repr(str(work/'meson')) + ')\nrunpy.run_module("mesonbuild.mesonmain",run_name="__main__")\n'); seal(launcher)
        meson = [sys.executable, '-I', '-B', launcher]
        require(text('meson-version', [*meson, '--version']) == lock['meson']['version'], 'Pinned Meson version differs')
        native = work/'native.ini'
        native.write_text('[binaries]\nc = '+repr(str(ccompiler))+'\ncpp = '+repr(str(compiler))+'\nobjc = '+repr(str(ccompiler))+'\nobjcpp = '+repr(str(compiler))+'\npkg-config = '+repr(str(wrapper))+'\n')
        seal(native)
        for item in lock['sources']:
            name = item['root']; build = work/'builds'/name
            run(name+'-configure', setup_arguments(meson, sources[name], build, prefix, native, item), 90)
            run(name+'-build', [*meson, 'compile', '-C', build, '--jobs=2'], 600)
            run(name+'-install', [*meson, 'install', '-C', build, '--no-rebuild'], 60)
        report['provider_packages'] = {}
        for module, version in (('fribidi','1.0.16'),('pango','1.56.4'),('pangocairo','1.56.4'),('gdk-pixbuf-2.0','2.42.12'),('graphene-gobject-1.0','1.10.8'),('epoxy','1.5.10'),('gtk4','4.18.6')):
            actual = text('installed-version-'+module, [*selected_pkg, '--modversion', module])
            pcdir = Path(text('installed-pcdir-'+module, [*selected_pkg,'--variable=pcfiledir',module])).resolve(strict=True)
            require(actual == version and prefix in pcdir.parents, 'Installed provider metadata differs')
            report['provider_packages'][module] = {'version': actual, 'metadata': seal(pcdir/(module+'.pc'))}
        require((prefix/'share/glib-2.0/schemas/gschemas.compiled').is_file(), 'Compiled GTK schemas missing')
        prefix_before = tree_seal(prefix, deadline, ui.record); save(output/'prefix-before.json', prefix_before)
        smoke_source = ROOT/'scripts/ci/macos-gtk-provider-smoke.mm'
        smoke_flags = flags('smoke-flags', [*selected_pkg, '--cflags', '--libs', 'gtk4-macos', 'pangocairo', 'gdk-pixbuf-2.0', 'libpng', 'harfbuzz'])
        smoke = work/'gtk-smoke'
        header_seal('smoke', compiler, smoke_source, smoke_flags)
        run('compile-smoke', [compiler, '-std=c++17', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
            '-isysroot', sdk, '-mmacosx-version-min=15.0', smoke_source, *smoke_flags,
            '-framework', 'AppKit', '-framework', 'CoreText', '-o', smoke], 60)
        seal(smoke)
        # Walk every private Mach-O and its complete non-system dependency graph.
        # Apple cache/framework loads are explicit boundaries, not hashed closure.
        pending = [smoke]
        for key, item in prefix_before.items():
            if 'sha256' in item:
                path = prefix/key
                with path.open('rb') as stream: magic = stream.read(4)
                if magic in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
                    pending.append(path)
        closure = {}; executable_rpaths = []
        while pending:
            path = pending.pop(0).resolve(strict=True)
            if str(path) in closure: continue
            require(len(closure) < 256, 'Loader closure bound exceeded')
            with path.open('rb') as stream:
                header = macho_header(stream.read(32), args.architecture)
            commands = loader_commands(run('loader-'+str(len(closure)), ['/usr/bin/otool', '-l', path], 10))
            rpaths = [x['path'] for x in commands if x['command'] == 'LC_RPATH']
            if path == smoke: executable_rpaths = rpaths
            row = {'file': seal(path), 'header': header, 'commands': commands, 'loads': []}; closure[str(path)] = row
            for command in commands:
                if command['command'] in ('LC_ID_DYLIB','LC_RPATH'): continue
                name = command['path']
                if system_library(name):
                    row['loads'].append({'name': name, 'system_cache_or_framework_boundary': True}); continue
                target = resolve_load(name, path, rpaths+executable_rpaths, smoke)
                require(prefix in target.parents or homebrew.resolve() in target.parents, 'Loader dependency leaves declared roots')
                row['loads'].append({'name': name, 'target': str(target)})
                external[str(target)] = seal(target); pending.append(target)
        save(output/'loader-closure.json', closure)
        smoke_env = dict(variables, GSK_RENDERER='cairo', GDK_BACKEND='macos',
                         GSETTINGS_SCHEMA_DIR=str(prefix/'share/glib-2.0/schemas'), GSETTINGS_BACKEND='memory',
                         XDG_DATA_DIRS=str(prefix/'share'), XDG_CACHE_HOME=str(work/'cache'),
                         XDG_CONFIG_HOME=str(work/'config'), XDG_DATA_HOME=str(work/'data'))
        report['smoke_environment'] = {k: smoke_env[k] for k in ('GSK_RENDERER','GDK_BACKEND','GSETTINGS_SCHEMA_DIR','GSETTINGS_BACKEND','XDG_DATA_DIRS','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME')}
        raw = run('gtk-smoke', [smoke], 30, env=smoke_env, check_exit=False)
        observed = json.loads(raw)
        report['smoke'] = observed  # retain a failed own-window oracle too
        smoke_result(observed)
        require(report['phases'][-1]['exit_code'] == 0, 'GTK smoke nonzero exit')
        for key, value in observed['origins'].items():
            if key in ('coretext','appkit'):
                continue  # explicit system-cache/framework boundary
            path = Path(value['declared']); row = seal(path)
            require(row['canonical_path'] == value['canonical'], 'Observed runtime alias changed')
            require(str(path.resolve(strict=True)) in closure, 'Loaded runtime absent from sealed loader closure')
            if key in ('gtk','pango','pangocairo','pixbuf'):
                require(prefix in path.resolve(strict=True).parents, 'GTK runtime escaped private prefix')
            else:
                expected = (observed['origins'][key]['canonical'])
                original = report['images']['png_path'] if key == 'png' else report['prerequisites']['operations'][key+'_path']
                require(Path(original).resolve(strict=True) == Path(expected), 'GTK loaded prerequisite changed')
        report['status'] = 'PASS'; report['phase'] = 'complete'
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        # Keep partial configuration and introspection records on every failure;
        # work/build trees are retained and never automatically removed.
        try:
            for build in sorted((work/'builds').iterdir()):
                for rel in ('meson-logs/meson-log.txt', 'meson-info/intro-buildoptions.json',
                            'meson-info/intro-dependencies.json', 'meson-info/intro-installed.json'):
                    src = build/rel
                    if src.exists() or src.is_symlink():
                        row = ui.record(src, final-5); require(row['bytes'] <= MAX_OUTPUT, 'Meson diagnostic exceeds bound')
                        data = src.read_bytes(); require(hashlib.sha256(data).hexdigest() == row['sha256'], 'Meson diagnostic changed')
                        (output/(build.name+'-'+Path(rel).name)).write_bytes(data)
        except Exception as error: report['problems'].append('Partial build evidence: '+str(error))
        for label, action in (
            ('inputs', lambda: {name: ui.record(Path(name), final-5) for name in inputs}),
            ('release_sources', lambda: {name: tree_seal(path, final-5, ui.record) for name,path in sources.items()}),
            ('prefix', lambda: tree_seal(prefix, final-5, ui.record) if any(prefix.iterdir()) else {})):
            try:
                value = action(); save(output/(label+'-after.json'), value)
                expected = inputs if label == 'inputs' else source_seals if label == 'release_sources' else prefix_before
                report[label+'_unchanged'] = value == expected
            except Exception as error:
                report[label+'_unchanged'] = False; report['problems'].append(label+' final seal: '+str(error))
        report['inputs_before'] = inputs
        try:
            report['source_unchanged'] = (run('revision-after', ['/usr/bin/git','-C',ROOT,'rev-parse','HEAD'], 5, final_check=True).decode().strip() == args.revision and
                not run('source-after', ['/usr/bin/git','-C',ROOT,'status','--porcelain','--untracked-files=no'], 5, final_check=True).strip())
        except Exception as error:
            report['source_unchanged'] = False; report['problems'].append('Source final seal: '+str(error))
        report['cleanup_verified'] = bool(report['phases']) and all(x.get('cleanup_verified') is True for x in report['phases'])
        report['raw_output_bytes'] = total_output; report['elapsed_seconds'] = time.monotonic()-started
        if (report['problems'] or report['elapsed_seconds'] > TOTAL_SECONDS or not report['cleanup_verified'] or
                not all(report.get(k) is True for k in ('source_unchanged','inputs_unchanged','release_sources_unchanged','prefix_unchanged'))):
            report['status'] = 'FAIL'
        save(output/'report.json', report)
    print(json.dumps({k:report[k] for k in ('status','phase','prefix','problems','cleanup_verified','elapsed_seconds')}))
    return 0 if report['status'] == 'PASS' else 1

if __name__ == '__main__':
    raise SystemExit(main())
