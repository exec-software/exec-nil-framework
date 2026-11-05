#!/usr/bin/env python3
"""Bounded native Mac UI prerequisites; no GTK build or product qualification."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import stat
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOTAL_SECONDS = 180
RESERVE_SECONDS = 20
MAX_OUTPUT = 256 * 1024
REQUIRED = ('glib-2.0', 'gobject-2.0', 'gio-2.0', 'gmodule-2.0', 'cairo',
            'cairo-gobject', 'cairo-quartz', 'harfbuzz', 'harfbuzz-subset')
FUTURE = ('fribidi', 'pango', 'pangocairo', 'gdk-pixbuf-2.0', 'graphene-gobject-1.0', 'epoxy', 'gtk4')
DEPENDENCY_SOURCE = r'''
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <cairo-quartz.h>
#include <hb.h>
#include <hb-subset.h>
#include <hb-coretext.h>
#include <CoreText/CoreText.h>
#include <dlfcn.h>
#include <cstdio>
static bool origin(const char *key, const void *symbol) {
  Dl_info info{};
  if (!dladdr(symbol, &info) || !info.dli_fname) return false;
  std::printf("%s=%s\n", key, info.dli_fname);
  return true;
}
int main() {
  if (glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION)) return 2;
  GObject *object = G_OBJECT(g_object_new(G_TYPE_OBJECT, nullptr));
  GFile *file = g_file_new_for_path("/");
  cairo_surface_t *surface = cairo_quartz_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica"), 12, nullptr);
  hb_font_t *hbfont = font ? hb_coretext_font_create(font) : nullptr;
  hb_subset_input_t *subset = hb_subset_input_create_or_fail();
  const bool ok = object && file && surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS &&
      hbfont && hbfont != hb_font_get_empty() && subset;
  if (subset) hb_subset_input_destroy(subset);
  if (hbfont) hb_font_destroy(hbfont);
  if (font) CFRelease(font);
  if (surface) cairo_surface_destroy(surface);
  if (file) g_object_unref(file);
  if (object) g_object_unref(object);
  std::printf("glib_header=%u.%u.%u\nglib_runtime=%u.%u.%u\ncairo_header=%s\ncairo_runtime=%s\nharfbuzz_header=%s\nharfbuzz_runtime=%s\noperations=%s\n",
      GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION, glib_major_version, glib_minor_version, glib_micro_version,
      CAIRO_VERSION_STRING, cairo_version_string(), HB_VERSION_STRING, hb_version_string(), ok ? "passed" : "failed");
  const bool origins = origin("glib_path", reinterpret_cast<const void *>(&glib_check_version)) &&
      origin("gobject_path", reinterpret_cast<const void *>(&g_object_new)) &&
      origin("gio_path", reinterpret_cast<const void *>(&g_file_new_for_path)) &&
      origin("cairo_path", reinterpret_cast<const void *>(&cairo_quartz_surface_create)) &&
      origin("harfbuzz_path", reinterpret_cast<const void *>(&hb_coretext_font_create)) &&
      origin("harfbuzz_subset_path", reinterpret_cast<const void *>(&hb_subset_input_create_or_fail));
  return ok && origins ? 0 : 2;
}
'''


def require(value, message):
    if not value:
        raise ValueError(message)


def paths(output, work, root=ROOT):
    require(not output.is_symlink() and not work.is_symlink(), 'Output/work may not be symlinks')
    output, work, root = output.resolve(), work.resolve(), root.resolve()
    require(not output.exists() and not work.exists(), 'Output/work must be fresh')
    require(output != work and output not in work.parents and work not in output.parents, 'Output/work must be disjoint')
    require(work != root and root not in work.parents and work not in root.parents,
            'Work must be outside the checkout')
    require(output != root and output not in root.parents and
            (root not in output.parents or root/'build' in output.parents),
            'Checkout evidence must be under build')
    return output, work


def environment(work, prefix, compatibility=None):
    values = {key: os.environ[key] for key in ('HOME', 'USER', 'LOGNAME', 'DEVELOPER_DIR') if key in os.environ}
    values.update(PATH=str(prefix/'bin') + ':/usr/bin:/bin:/usr/sbin:/sbin', LC_ALL='C',
                  TMPDIR=str(work/'tmp'), PYTHONDONTWRITEBYTECODE='1',
                  PKG_CONFIG_LIBDIR=str(prefix/'lib/pkgconfig') + ':' + str(prefix/'share/pkgconfig'))
    if compatibility is not None:
        values['PKG_CONFIG_LIBDIR'] += ':' + str(compatibility)
    return values



def compatibility_directory(raw, prefix, major):
    require(major == '15', 'Only the observed macOS15 compatibility profile is admitted')
    values = raw.split(':')
    require(0 < len(values) <= 32 and '\0' not in raw and '\n' not in raw, 'Invalid compiled pkg-config path')
    matches = {}
    suffix = ('Library', 'Homebrew', 'os', 'mac', 'pkgconfig', major)
    for value in values:
        path = Path(value)
        if path.parts[-len(suffix):] != suffix:
            continue
        require(path.is_absolute() and '..' not in path.parts and path.is_dir() and
                prefix.resolve() in path.resolve().parents, 'Untrusted macOS compatibility path')
        matches[path.resolve()] = path
    require(len(matches) == 1, 'One matching installed macOS compatibility directory required')
    return next(iter(matches.values()))


def pkg_command(program, sdk):
    # This variable exists only in Homebrew's Apple system compatibility files.
    # Unlike PKG_CONFIG_SYSROOT_DIR or prefix, it does not rebase Homebrew paths.
    return [program, '--define-variable=homebrew_sdkroot=' + str(sdk)]


def record(path, deadline):
    original = Path(path)
    path = original.resolve(strict=True)
    require(path.is_absolute(), 'Nonabsolute input')
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
    digest, total = hashlib.sha256(), 0
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= 512 * 1024**2, 'Unbounded/nonregular input')
        while block := stream.read(1024**2):
            require(time.monotonic() < deadline, 'Seal deadline expired')
            total += len(block)
            require(total <= 512 * 1024**2, 'Input grew beyond bound')
            digest.update(block)
        after = os.fstat(stream.fileno())
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_mode)
    require(identity(before) == identity(after) == identity(path.stat()) and total == after.st_size and
            original.resolve(strict=True) == path, 'Input changed while sealing')
    return {'path': str(original), 'canonical_path': str(path), 'bytes': total,
            'sha256': digest.hexdigest(), 'mode': stat.S_IMODE(after.st_mode)}


def parse_lines(raw):
    require(len(raw) <= MAX_OUTPUT, 'Probe output exceeds bound')
    result = {}
    for line in raw.decode('utf-8').splitlines():
        key, separator, value = line.partition('=')
        require(separator and re.fullmatch('[a-z_]+', key) and key not in result and value and '\0' not in value,
                'Malformed or duplicate native observation')
        result[key] = value
    return result


def cocoa_result(raw):
    require(len(raw) <= 8192, 'Cocoa report exceeds bound')
    value = json.loads(raw)
    require(value.get('schema') == 1 and value.get('qualification') is False, 'Invalid Cocoa report scope')
    for name in ('main_thread', 'session', 'on_console', 'login_done', 'post_event_access',
                 'ax_trusted', 'window_created', 'cleanup_verified'):
        require(type(value.get(name)) is bool, 'Missing Cocoa observation: ' + name)
    expected = value.get('queued_event', {})
    for name in ('queued_event', 'app_event', 'view_event'):
        event = value.get(name, {})
        require(isinstance(event, dict), 'Invalid Cocoa event observation')
        for key in ('characters', 'characters_ignoring_modifiers'):
            require(key not in event or (type(event[key]) is str and len(event[key].encode('utf-16-le')) <= 16),
                    'Cocoa event character observation exceeds bound')
    for name in ('app_unmatched', 'view_unmatched'):
        rows = value.get(name, [])
        require(type(rows) is list and len(rows) <= 4, 'Unmatched event observation count exceeds bound')
        for row in rows:
            require(type(row) is dict and set(row) == {'published', 'type', 'window_number', 'key_code',
                'has_reference_time', 'time_delta_seconds', 'time_delta_clamped'}, 'Unexpected unmatched event data')
            require(all(type(row[key]) is bool for key in ('published', 'has_reference_time', 'time_delta_clamped')) and
                    row['published'] == row['has_reference_time'], 'Invalid unmatched event phase')
            require(type(row['type']) is int and row['type'] == 10 and
                    type(row['window_number']) is int and abs(row['window_number']) < 2**31 and
                    type(row['key_code']) is int and 0 <= row['key_code'] <= 65535, 'Invalid unmatched event numbers')
            delta = row['time_delta_seconds']
            require(type(delta) in (int, float) and math.isfinite(delta) and abs(delta) <= 3600 and
                    (row['published'] or delta == 0), 'Unmatched event time delta exceeds bound')
    exact_event = (expected.get('synthetic_identity_match') is True and expected.get('strings_bounded') is True and
        type(expected.get('window_number')) is int and expected['window_number'] > 0 and
        all(type(expected.get(key)) is int and expected[key] == number for key, number in
            (('key_code', 45), ('type', 10), ('modifier_flags', 0))) and
        expected.get('characters') == expected.get('characters_ignoring_modifiers') == 'n' and
        expected == value.get('app_event') == value.get('view_event'))
    value['own_window_feasible'] = all(value.get(name) is True for name in
        ('main_thread', 'session', 'login_done', 'observer_subclass', 'window_created', 'visible', 'event_ready', 'event_queued',
         'own_event_delivered', 'own_close_delivered', 'cleanup_verified')) and value.get('exception') is False and (
             type(value.get('dispatch_count')) is int and value['dispatch_count'] == 1 and
             type(value.get('view_delivery_count')) is int and value['view_delivery_count'] == 1 and
             value.get('view_during_owned_dispatch') is True and value.get('wrong_target') is False and exact_event)
    return value



def dependency_result(raw, packages):
    observed = parse_lines(raw)
    names = ('glib', 'gobject', 'gio', 'cairo', 'harfbuzz', 'harfbuzz_subset')
    require(set(observed) == {'operations', 'glib_header', 'glib_runtime', 'cairo_header',
        'cairo_runtime', 'harfbuzz_header', 'harfbuzz_runtime', *(name + '_path' for name in names)},
        'Incomplete dependency probe output')
    require(observed['operations'] in ('passed', 'failed'), 'Invalid dependency operation status')
    matched = all(observed[name + '_header'] == observed[name + '_runtime'] ==
        packages[module]['version'] for name, module in
        (('glib', 'glib-2.0'), ('cairo', 'cairo'), ('harfbuzz', 'harfbuzz')))
    return observed, matched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() == args.architecture, 'Matching native macOS host required')
    require(re.fullmatch('[0-9a-f]{40}', args.revision), 'Exact revision required')
    output, work = paths(args.output, args.work_dir)
    output.mkdir(parents=True, mode=0o700); work.mkdir(parents=True, mode=0o700)
    logs = output/'logs'; logs.mkdir(); (work/'tmp').mkdir()
    started = time.monotonic(); deadline = started + TOTAL_SECONDS
    prefix = Path('/opt/homebrew' if args.architecture == 'arm64' else '/usr/local')
    variables = environment(work, prefix)
    input_paths = [Path(__file__).resolve(), Path(__file__).with_suffix('.mm'),
                   ROOT/'tests/tools/ordinary_io_parity_contract.py',
                   ROOT/'validation/ny-3d/scripts/debugger_process.py']
    report = {'schema': 1, 'status': 'INCOMPLETE', 'qualification': False,
              'scope': 'Host prerequisite and own-process AppKit event feasibility only; no GTK or Ny UI acceptance',
              'revision': args.revision, 'architecture': args.architecture, 'platform': platform.platform(),
              'image': {key: os.environ.get(key) for key in ('ImageOS', 'ImageVersion', 'GITHUB_RUN_ID', 'GITHUB_SHA')},
              'total_seconds_limit': TOTAL_SECONDS, 'child_seconds_limit': 30,
              'output_limit_bytes': 8 * 1024**2, 'phases': [], 'problems': [],
              'external_keyboard_or_close_verified': False, 'gtk_provider_built': False}
    seals = {}; output_bytes = 0
    report['seal_observed_after_phase'] = {}
    def seal(path):
        key = str(Path(path).absolute())
        if key not in seals:
            seals[key] = record(Path(key), deadline - RESERVE_SECONDS)
            report['seal_observed_after_phase'][key] = len(report['phases'])
    def run(name, argv, seconds=10):
        nonlocal output_bytes
        remaining = deadline - RESERVE_SECONDS - time.monotonic()
        require(remaining > 0, 'Whole probe deadline exhausted')
        row = support.run_process(ownership, list(map(str, argv)), work, variables, logs, name,
                                  min(30, seconds, remaining))
        report['phases'].append(row)
        raw = (logs/row['stdout']).read_bytes(); errors = (logs/row['stderr']).read_bytes()
        output_bytes += len(raw) + len(errors)
        require(output_bytes <= report['output_limit_bytes'], 'Aggregate raw-output bound exceeded')
        require(row.get('cleanup_verified') is True and not row.get('error') and not row.get('timed_out'),
                name + ': process/cleanup failure; raw logs retained')
        return row['exit_code'], raw, errors
    def checked(name, argv, seconds=10):
        code, raw, _ = run(name, argv, seconds)
        require(code == 0, name + ': nonzero exit; raw logs retained')
        return raw.decode('utf-8').strip()
    try:
        for path in input_paths: seal(path)
        spec = importlib.util.spec_from_file_location('macos_ui_owned', input_paths[2])
        support = importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
        support.MAX_OUTPUT = MAX_OUTPUT
        ownership = support.load_ownership(ROOT)
        actual = checked('source-revision', ['/usr/bin/git', '-C', ROOT, 'rev-parse', 'HEAD'])
        require(actual == args.revision, 'Checkout revision mismatch')
        sdk = Path(checked('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']))
        compiler = Path(checked('compiler', ['/usr/bin/xcrun', '--find', 'clang++']))
        require(sdk.is_absolute() and compiler.is_absolute(), 'Nonabsolute toolchain selection')
        pkg = prefix/'bin/pkg-config'
        for path in (Path(sys.executable), Path('/usr/bin/xcrun'), compiler, pkg, sdk/'SDKSettings.json'):
            seal(path)
        report['toolchain'] = {'sdk': str(sdk), 'compiler': str(compiler), 'pkg_config': str(pkg),
                               'compiler_version': checked('compiler-version', [compiler, '--version']),
                               'pkg_config_version': checked('pkg-version', [pkg, '--version'])}
        compiled_paths = checked('pkg-compiled-path', [pkg, '--variable=pc_path', 'pkg-config'], 5)
        compatibility = compatibility_directory(compiled_paths, prefix, platform.mac_ver()[0].split('.')[0])
        variables.update(environment(work, prefix, compatibility))
        report['pkg_config_directories'] = variables['PKG_CONFIG_LIBDIR'].split(':')
        report['pkg_config_compatibility'] = str(compatibility)
        selected_pkg = pkg_command(pkg, sdk)
        executable = work/'cocoa-probe'
        code, _, _ = run('compile-cocoa', [compiler, '-std=c++17', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
            '-isysroot', sdk, '-mmacosx-version-min=15.0', input_paths[1], '-framework', 'AppKit',
            '-framework', 'ApplicationServices', '-o', executable], 30)
        if code == 0:
            seal(executable)
            code, raw, _ = run('cocoa-window', [executable], 15)
            report['cocoa'] = cocoa_result(raw)
            require(code in (0, 2) and (code == 0) == report['cocoa']['own_window_feasible'], 'Cocoa exit/report mismatch')
        else:
            report['problems'].append('Cocoa compilation failed; native feasibility is unknown')
        for module in ('libffi', 'zlib', 'bzip2', 'expat'):
            directory = Path(checked('sdk-pcdir-' + module, [*selected_pkg, '--variable=pcfiledir', module], 5))
            require(directory.is_absolute() and directory.resolve() == compatibility.resolve(),
                    'System dependency metadata must come from selected Homebrew compatibility directory')
            seal(directory/(module + '.pc'))
        report['required_packages'] = {}
        all_present = True
        for module in REQUIRED:
            code, raw, _ = run('pkg-' + module, [*selected_pkg, '--modversion', module], 5)
            require(code in (0, 1), 'Unexpected pkg-config status')
            report['required_packages'][module] = {'available': code == 0, 'version': raw.decode('utf-8').strip() if code == 0 else None}
            all_present = all_present and code == 0
            if code == 0:
                directory = Path(checked('pcdir-' + module, [*selected_pkg, '--variable=pcfiledir', module], 5))
                require(directory.is_absolute(), 'Nonabsolute pkg-config directory')
                seal(directory/(module + '.pc'))
        report['future_packages'] = {}
        for module in FUTURE:
            code, raw, _ = run('future-' + module, [*selected_pkg, '--modversion', module], 5)
            require(code in (0, 1), 'Unexpected optional pkg-config status')
            report['future_packages'][module] = {'available': code == 0, 'version': raw.decode('utf-8').strip() if code == 0 else None}
        code, raw, _ = run('meson-availability', [sys.executable, '-I', '-m', 'mesonbuild.mesonmain', '--version'], 5)
        report['meson'] = {'available': code == 0, 'version': raw.decode('utf-8').strip() if code == 0 else None}
        report['glib_tools'] = {}
        for tool in ('glib-compile-resources', 'glib-compile-schemas'):
            path = prefix/'bin'/tool
            if path.is_file():
                seal(path)
                report['glib_tools'][tool] = {'path': str(path), 'version': checked(tool, [path, '--version'], 5)}
            else:
                report['glib_tools'][tool] = {'available': False}
        if all_present:
            flags = shlex.split(checked('dependency-flags', [*selected_pkg, '--cflags', '--libs', *REQUIRED], 5))
            require(len(flags) <= 256 and all('\0' not in f and '\n' not in f for f in flags), 'Invalid compiler flag inventory')
            source = work/'dependencies.cpp'; source.write_text(DEPENDENCY_SOURCE)
            seal(source)
            binary = work/'dependencies'
            code, _, _ = run('compile-dependencies', [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-isysroot', sdk, source, *flags, '-framework', 'CoreText', '-framework', 'CoreGraphics',
                '-o', binary], 30)
            if code == 0:
                seal(binary)
                code, raw, _ = run('load-dependencies', [binary], 10)
                observed, versions_match = dependency_result(raw, report['required_packages'])
                report['dependencies'] = observed
                report['dependency_versions_match'] = versions_match
                report['dependency_operations_passed'] = code == 0 and observed['operations'] == 'passed' and versions_match
                for key, value in report['dependencies'].items():
                    if key.endswith('_path'):
                        path = Path(value)
                        require(path.is_absolute() and prefix.resolve() in path.resolve(strict=True).parents,
                                'Loaded dependency is outside the declared prefix')
                        seal(path)
            else:
                report['problems'].append('Installed dependency compile/link failed; raw logs retained')
        report['observations_completed'] = True
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        report['inputs_before'] = seals
        try:
            report['inputs_after'] = {key: record(Path(key), deadline) for key in seals}
            report['inputs_unchanged'] = report['inputs_after'] == seals
        except Exception as error:
            report['inputs_unchanged'] = False
            report['problems'].append('Final seals: ' + type(error).__name__ + ': ' + str(error))
        report['cleanup_verified'] = all(row.get('cleanup_verified') is True for row in report['phases']) and (
            'cocoa' not in report or report['cocoa']['cleanup_verified'] is True)
        report['elapsed_seconds'] = time.monotonic() - started
        report['raw_output_bytes'] = output_bytes
        report['host_feasible'] = bool(report.get('cocoa', {}).get('own_window_feasible') and
                                      report.get('dependency_operations_passed') and
                                      all('version' in row for row in report.get('glib_tools', {}).values()))
        report['status'] = ('PASS' if report['host_feasible'] else 'PREREQUISITE_UNAVAILABLE') if (
            report.get('observations_completed') and report['inputs_unchanged'] and report['cleanup_verified'] and
            not report['problems'] and report['elapsed_seconds'] < TOTAL_SECONDS) else 'FAIL'
        (output/'report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
