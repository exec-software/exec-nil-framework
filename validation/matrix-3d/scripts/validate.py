#!/usr/bin/env python3
"""Validate the Matrix 3D renderer across every execution route.

Compiles src/renderer.matrix through each route the SDK offers, captures the
PPM each route writes, and checks it two ways:

  * against an independently written float64 oracle, so a pass means the
    Matrix program and the reference agree exactly rather than merely
    agreeing with each other;
  * against every other route, so a pass means the interpreted and LLVM
    backends produce identical bytes.

The SDK is verified against its sealed manifest before and after the run, and
the harness refuses to execute an SDK whose payload changed underneath it.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import shlex
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_render import check as check_render
from ppm import PpmError, read_p3, write_png
from sdk_integrity import verify

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src' / 'renderer.matrix'
SDK = Path(os.environ.get('NIL_SDK') or ROOT.parents[1] / 'sdk').resolve()

ROUTES = ('nil-text', 'nil-binary', 'bundle-text', 'bundle-binary',
          'jit', 'aot', 'assemble-text', 'assemble-binary')
NATIVE = {'jit', 'aot', 'assemble-text', 'assemble-binary'}


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def clean_environment(run_root):
    env = dict(os.environ)
    removed = sorted(k for k in env
                     if k.upper().startswith('NY')
                     or k in {'LD_LIBRARY_PATH', 'LD_PRELOAD', 'DYLD_LIBRARY_PATH',
                              'DYLD_INSERT_LIBRARIES', 'LIBRARY_PATH', 'CPATH',
                              'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH'})
    for key in removed:
        del env[key]
    env['PATH'] = '/usr/local/bin:/usr/bin:/bin'
    env['LC_ALL'] = 'C'
    env['TMPDIR'] = str(run_root / 'tmp')
    (run_root / 'tmp').mkdir(parents=True, exist_ok=True)
    return env, removed


def build_steps(route, cwd):
    compiler = [str(SDK / 'nilcompiler'), '-q', '--sdk', str(SDK), '-I', str(ROOT / 'src')]
    nil = cwd / 'program.nil'
    executable = cwd / 'program'
    object_file = cwd / 'program.o'
    binary = ['--emit-binary-nil'] if route in {'nil-binary', 'bundle-binary', 'assemble-binary'} else []
    if route == 'jit':
        return [], compiler + ['--native', '--jit', '-o', str(executable), str(SOURCE)]
    if route == 'aot':
        return [('compile', compiler + ['--native', '-o', str(executable), str(SOURCE)])], [str(executable)]
    if route.startswith('bundle-'):
        return [('compile', compiler + binary + ['--bundle', '-o', str(executable), str(SOURCE)])], [str(executable)]
    steps = [('compile', compiler + binary + ['--no-bundle', '-o', str(nil), str(SOURCE)])]
    if route.startswith('assemble-'):
        steps += [('assemble', [str(SDK / 'nilassembler'), '-o', str(object_file), str(nil)]),
                  ('link', [str(SDK / 'nillinker'), '--runtime', str(SDK),
                            '-o', str(executable), str(object_file)])]
        return steps, [str(executable)]
    return steps, [str(SDK / 'nilinterpreter'), str(nil)]


def command(argv, cwd, label, timeout, env):
    stdout_path, stderr_path = cwd / f'{label}.stdout', cwd / f'{label}.stderr'
    started = time.monotonic()
    record = {'phase': label, 'command': shlex.join(str(a) for a in argv),
              'timeout_seconds': timeout, 'exit_code': None,
              'timed_out': False, 'spawn_error': None}
    with stdout_path.open('wb') as out, stderr_path.open('wb') as err:
        try:
            process = subprocess.Popen(argv, cwd=cwd, env=env, stdout=out, stderr=err,
                                       start_new_session=True)
            try:
                record['exit_code'] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                record['timed_out'] = True
                os.killpg(process.pid, signal.SIGKILL)
                record['exit_code'] = process.wait()
        except OSError as exc:
            record['spawn_error'] = str(exc)
    record['duration_seconds'] = round(time.monotonic() - started, 3)
    record['stdout_path'] = str(stdout_path)
    record['stderr_path'] = str(stderr_path)
    return record


def stderr_of(record):
    return Path(record['stderr_path']).read_text(errors='replace')


def run_route(route, timeout, env, work_root):
    cwd = work_root / route
    cwd.mkdir(parents=True, exist_ok=True)
    steps, executable = build_steps(route, cwd)
    row = {'name': 'renderer', 'route': route, 'native': route in NATIVE,
           'status': 'FAIL', 'reason': None, 'work_directory': str(cwd), 'steps': []}
    try:
        for label, argv in steps:
            record = command(argv, cwd, label, timeout, env)
            row['steps'].append(record)
            if record['exit_code'] != 0 or record['timed_out'] or record['spawn_error']:
                row['reason'] = f'{label} failed: ' + (stderr_of(record).strip().splitlines()
                                                      or ['no diagnostic'])[0]
                return row
        run = command(executable, cwd, 'run', timeout, env)
        row['steps'].append(run)
        row['duration_seconds'] = run['duration_seconds']
        if run['exit_code'] != 0 or run['timed_out'] or run['spawn_error']:
            row['reason'] = 'execution failed: ' + (stderr_of(run).strip().splitlines()
                                                    or ['no diagnostic'])[0]
            return row

        image = cwd / 'render.ppm'
        shutil.copyfile(run['stdout_path'], image)
        row['image'] = {'path': str(image)}
        try:
            width, height, samples = read_p3(image)
        except PpmError as exc:
            row['reason'] = str(exc)
            return row
        row['image'].update(width=width, height=height,
                            ppm_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                            rgb_sha256=hashlib.sha256(samples).hexdigest())
        write_png(cwd / 'render.png', width, height, samples)
        row['image']['png_path'] = str(cwd / 'render.png')

        oracle = check_render(image)
        row['oracle'] = oracle
        if oracle['status'] != 'PASS':
            row['reason'] = ('image differs from the independent float64 oracle at '
                             f"{oracle['differing_pixels']} pixel(s)")
            return row
        row['status'] = 'PASS'
    except (OSError, ValueError, KeyError, TypeError) as exc:
        row['reason'] = f'{type(exc).__name__}: {exc}'
    return row


def write_report(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n')


def write_text(path, report):
    lines = [f"Matrix 3D renderer validation -- {report['completed_utc']}",
             f"SDK: {report['sdk']}", '']
    for row in report['results']:
        lines.append(f"{row['status']:<5} {row['route']:<16} "
                     f"{row.get('image', {}).get('rgb_sha256', '-')[:16]:<18} "
                     f"{row.get('duration_seconds', 0):>7.3f}s  {row.get('reason') or ''}")
    lines += ['', f"counts: {report['counts']}", f"routes in agreement: {report['routes_in_agreement']}"]
    path.write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description='Validate the Matrix 3D renderer')
    parser.add_argument('--quick', action='store_true', help='Only the interpreter and LLVM native AOT routes')
    parser.add_argument('--only', help='Comma-separated route names to run')
    parser.add_argument('--timeout', type=float, default=120.0)
    parser.add_argument('--report', type=Path, default=ROOT / 'reports' / 'validation.json')
    args = parser.parse_args()

    routes = list(ROUTES)
    if args.quick:
        routes = ['nil-text', 'aot']
    if args.only:
        wanted = set(args.only.split(','))
        unknown = wanted - set(ROUTES)
        if unknown:
            parser.error(f'unknown routes: {sorted(unknown)}')
        routes = [r for r in ROUTES if r in wanted]

    report = {'schema': 'matrix.3d.validation.v1', 'started_utc': utc_now(),
              'sdk': str(SDK), 'source': str(SOURCE), 'routes_requested': routes,
              'results': [], 'error': None}
    if not SDK.is_dir():
        report['error'] = f'SDK not found at {SDK}; build it with ./scripts/build-sdk.sh'
        report['completed_utc'] = utc_now()
        report['counts'] = {}
        write_report(args.report, report)
        print(report['error'])
        return 1

    work_root = ROOT / 'work'
    env, removed = clean_environment(work_root)
    report['environment_overrides_removed'] = removed
    report['sdk_integrity_before'] = verify(SDK)

    try:
        if report['sdk_integrity_before']['status'] != 'PASS':
            raise ValueError('SDK integrity check failed; refusing to execute it')
        for route in routes:
            print(f'running {route} ...', flush=True)
            report['results'].append(run_route(route, args.timeout, env, work_root))
    except KeyboardInterrupt:
        report['error'] = 'interrupted'
    except (OSError, ValueError) as exc:
        report['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        report['sdk_integrity_after'] = verify(SDK)
        before, after = report['sdk_integrity_before'], report['sdk_integrity_after']
        if before.get('manifest_sha256') != after.get('manifest_sha256'):
            after['status'] = 'FAIL'
            after.setdefault('errors', []).append('SDK manifest changed during the run')
        report['completed_utc'] = utc_now()
        report['counts'] = dict(collections.Counter(r['status'] for r in report['results']))

        hashes = {r['image']['rgb_sha256'] for r in report['results'] if 'image' in r}
        report['routes_in_agreement'] = len(hashes) == 1 and len(report['results']) > 1
        if len(hashes) > 1:
            for row in report['results']:
                if row['status'] == 'PASS':
                    row['status'] = 'FAIL'
                    row['reason'] = 'route output differs from another route'
            report['counts'] = dict(collections.Counter(r['status'] for r in report['results']))

        write_report(args.report, report)
        write_text(args.report.with_suffix('.txt'), report)

    print()
    for row in report['results']:
        print(f"{row['status']:<5} {row['route']:<16} {row.get('reason') or 'exact oracle and route parity'}")
    print(f"\n{report['counts']}  |  routes in agreement: {report['routes_in_agreement']}")
    print(f'Report: {args.report}')
    return 0 if not report['error'] and report['counts'].get('FAIL', 0) == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
