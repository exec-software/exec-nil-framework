#!/usr/bin/env python3
"""Run an independent end-user Ny validation matrix against a NIL SDK.

The SDK is the one built at the repository root, or the directory named by
$NIL_SDK. This project ships no SDK of its own.

All subprocesses run outside the SDK, without inherited Ny or loader overrides.
An absent feature is reported as UNSUPPORTED and makes the validation fail.
Exact output, exit codes, commands, generated programs and images are retained.
"""
from __future__ import annotations
import argparse
import collections
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from sdk_integrity import digest, verify
from check_render import check as check_render

ROOT = Path(__file__).resolve().parents[1]
# Consume the SDK built at the repository root, or point NIL_SDK at any other
# build. The project no longer carries its own copied SDK distribution.
SDK = Path(os.environ.get('NIL_SDK') or ROOT.parents[1] / 'sdk').resolve()
ROUTES = ('nil-text', 'nil-binary', 'bundle-text', 'bundle-binary', 'jit', 'aot',
          'assemble-text', 'assemble-binary')
NATIVE = {'jit', 'aot', 'assemble-text', 'assemble-binary'}
RENDERERS = [
    {'name': 'renderer', 'source': 'src/renderer.ny', 'kind': 'render',
     'area': '3D ray tracing', 'description': 'Sphere scene, raw pixel buffers, geometry and exact image parity'},
    {'name': 'mesh-renderer', 'source': 'src/mesh_renderer.ny', 'kind': 'render',
     'area': '3D indexed mesh', 'description': 'Array-backed vertices/indices, integer triangle rasterization and depth buffering'},
    {'name': 'scene-player', 'source': 'src/scene_player.ny', 'kind': 'render',
     'area': 'Persistent 3D scene', 'description': 'Multiple frames with shared mesh updates, reused raw buffers and an atomic binary checkpoint',
     'output_artifacts': ['scene-state.bin']},
]
# These are SDK diagnostics for an explicitly unavailable backend capability.
UNSUPPORTED = ('unsupported reachable intrinsic', 'unsupported intrinsic', 'unsupported native annotation',
               'native backend is not available', 'llvm backend is not available',
               'built without llvm', 'llvm support is not enabled')


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def image_oracle(name, image_path):
    if name == 'renderer':
        return check_render(image_path)
    if name == 'mesh-renderer':
        from check_mesh_render import check
        return check(image_path)
    if name == 'scene-player':
        from check_scene_player import check
        return check(image_path)
    raise ValueError('Renderer has no independent oracle: ' + name)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def clean_environment(run_root):
    env = dict(os.environ)
    overrides = {'LD_LIBRARY_PATH', 'LD_PRELOAD', 'DYLD_LIBRARY_PATH', 'DYLD_INSERT_LIBRARIES',
                 'LIBRARY_PATH', 'CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH'}
    removed = sorted(k for k in env if k.upper().startswith('NY') or k in overrides)
    for key in removed:
        del env[key]
    # Native AOT legitimately needs the installed host C++ driver/CRT.
    env['PATH'] = '/usr/local/bin:/usr/bin:/bin'
    env['LC_ALL'] = 'C'
    env['TMPDIR'] = str(run_root / 'tmp')
    (run_root / 'tmp').mkdir()
    return env, removed


def command(argv, cwd, label, timeout, env):
    """Bound the process group and preserve binary stdout/stderr without alteration."""
    stdout_path, stderr_path = cwd / f'{label}.stdout', cwd / f'{label}.stderr'
    started = time.monotonic()
    record = {'argv': [str(a) for a in argv], 'cwd': str(cwd), 'phase': label,
              'command': shlex.join(str(a) for a in argv), 'timeout_seconds': timeout,
              'exit_code': None, 'timed_out': False, 'spawn_error': None,
              'stdout_path': str(stdout_path), 'stderr_path': str(stderr_path)}
    with stdout_path.open('wb') as out, stderr_path.open('wb') as err:
        try:
            process = subprocess.Popen(record['argv'], cwd=cwd, env=env,
                                       stdout=out, stderr=err, start_new_session=True)
            try:
                record['exit_code'] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                record['timed_out'] = True
                os.killpg(process.pid, signal.SIGKILL)
                record['exit_code'] = process.wait()
        except OSError as exc:
            record['spawn_error'] = str(exc)
    record['duration_seconds'] = round(time.monotonic() - started, 6)
    for name, path in [('stdout', stdout_path), ('stderr', stderr_path)]:
        record[name + '_bytes'] = path.stat().st_size
        record[name + '_sha256'] = digest(path)
        # Files are authoritative. Small diagnostic/output strings aid report readers.
        if path.stat().st_size <= 20000:
            record[name] = path.read_bytes().decode('utf-8', errors='replace')
    write_json(cwd / f'{label}.command.json', record)
    return record


def succeeded(record):
    return record['exit_code'] == 0 and not record['timed_out'] and not record['spawn_error']


def diagnostic(record):
    return Path(record['stderr_path']).read_text(errors='replace')


def build_steps(route, source, cwd, program_args, timeout):
    compiler = [str(SDK / 'nilcompiler'), '-q', '--sdk', str(SDK), '-I', str(source.parent)]
    nil = cwd / 'program.nil'
    executable = cwd / 'program'
    object_file = cwd / 'program.o'
    binary = ['--emit-binary-nil'] if route in {'nil-binary', 'bundle-binary', 'assemble-binary'} else []
    if route == 'jit':
        return [], compiler + ['--native', '--jit', '-o', str(executable), str(source), '--'] + program_args
    if route == 'aot':
        return [('compile', compiler + ['--native', '-o', str(executable), str(source)])], [str(executable)] + program_args
    if route.startswith('bundle-'):
        return [('compile', compiler + binary + ['--bundle', '-o', str(executable), str(source)])], [str(executable)] + program_args
    steps = [('compile', compiler + binary + ['--no-bundle', '-o', str(nil), str(source)])]
    if route.startswith('assemble-'):
        steps += [('assemble', [str(SDK / 'nilassembler'), '-o', str(object_file), str(nil)]),
                  ('link', [str(SDK / 'nillinker'), '--runtime', str(SDK),
                            '--driver-timeout-ms', str(max(100, min(3600000, int(timeout * 1000)))),
                            '-o', str(executable), str(object_file)])]
        return steps, [str(executable)] + program_args
    return steps, [str(SDK / 'nilinterpreter'), str(nil), '--'] + program_args


def ppm_metrics(data, png_path=None):
    # PPM comments (including workload counters) are part of exact-output parity.
    text = data.decode('ascii')
    comments = re.findall(r'#[^\r\n]*', text)
    tokens = re.sub(r'#[^\r\n]*', '', text).split()
    if len(tokens) < 4 or tokens[0] != 'P3':
        raise ValueError('Renderer must output a complete ASCII P3 PPM on stdout')
    width, height, maxval = map(int, tokens[1:4])
    if not (1 <= width <= 8192 and 1 <= height <= 8192 and maxval == 255):
        raise ValueError(f'Invalid PPM dimensions/max value: {width}x{height}/{maxval}')
    samples = list(map(int, tokens[4:]))
    if len(samples) != width * height * 3:
        raise ValueError(f'PPM needs {width*height*3} samples; got {len(samples)}')
    if any(x < 0 or x > maxval for x in samples):
        raise ValueError('PPM sample out of range')
    pixels = bytes(samples)
    colors = collections.Counter(zip(samples[::3], samples[1::3], samples[2::3]))
    if len(colors) < 8:
        raise ValueError(f'Rendered image has only {len(colors)} colors; expected a nontrivial 3D scene')
    if png_path:
        def chunk(kind, payload):
            return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)
        scanlines = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
        png_path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b''))
    return {'width': width, 'height': height, 'pixel_count': width * height,
            'unique_colors': len(colors), 'rgb_sha256': hashlib.sha256(pixels).hexdigest(),
            'ppm_sha256': hashlib.sha256(data).hexdigest(), 'comments': comments,
            'minimum_channel': min(samples), 'maximum_channel': max(samples),
            'png_path': str(png_path) if png_path else None}


def run_case(case, route, run_root, args, env):
    cwd = run_root / case['name'] / route
    cwd.mkdir(parents=True)
    source = (ROOT / case['source']).resolve()
    row = {'name': case['name'], 'route': route, 'kind': case['kind'], 'area': case['area'],
           'description': case.get('description', ''), 'source': str(source), 'commands': [],
           'status': 'FAIL', 'reason': '', 'work_directory': str(cwd),
           'known_issue': case.get('known_issue'),
           'expectation': {key: case[key] for key in ('expected_stdout', 'expected_stderr', 'stderr_contains', 'stderr_by_route', 'expected_files') if key in case}}
    if not source.is_relative_to(ROOT) or source.is_relative_to(SDK) or not source.is_file():
        row['reason'] = 'Case source must be a project source file outside sdk/'
        return row
    row['source_sha256'] = digest(source)
    program_args = [str(args.width), str(args.height)] if case['kind'] == 'render' else [str(x) for x in case.get('args', [])]
    if case['name'] == 'scene-player':
        program_args.append(str(args.frames))
    steps, run = build_steps(route, source, cwd, program_args, args.timeout)
    if route == 'jit' and case['kind'] == 'compile-reject':
        # JIT has a combined command; still execute it to establish rejection.
        steps = [('compile', run)]
    if route == 'jit' and case['kind'] == 'runtime-reject':
        # JIT combines compilation/execution; prove this source reaches native
        # execution by requiring a successful native compile first.
        steps, _ = build_steps('aot', source, cwd, [], args.timeout)
        steps = [('native-preflight', cmd) for _, cmd in steps]
    for label, argv in steps:
        result = command(argv, cwd, label, args.timeout, env)
        row['commands'].append(result)
        if not succeeded(result):
            expected = case.get('stderr_contains')
            ordinary_failure = result['exit_code'] is not None and result['exit_code'] > 0 and not result['timed_out'] and not result['spawn_error']
            if case['kind'] == 'compile-reject' and label == 'compile' and ordinary_failure and (not expected or expected in diagnostic(result)):
                row.update(status='PASS', reason='Compiler rejected the invalid program with the expected diagnostic')
            else:
                unsupported = route in NATIVE and any(marker in diagnostic(result).lower() for marker in UNSUPPORTED)
                row.update(status='UNSUPPORTED' if unsupported else 'FAIL', reason=f'{label} did not succeed' + (': ' + diagnostic(result).strip()[:1000] if diagnostic(result) else ''))
            return row
    if case['kind'] == 'compile-reject':
        row['reason'] = 'Compiler accepted a program that must be rejected'
        return row
    repeats = args.stress if case['kind'] == 'render' else 1
    first_output = None
    for iteration in range(repeats):
        # A previous successful run must never supply this run's checkpoint.
        # Seed a distinct invalid file, also exercising atomic replacement.
        artifact_preconditions = {}
        for filename in case.get('output_artifacts', []):
            artifact = cwd / filename
            if (Path(filename).name != filename or artifact.is_symlink()
                    or (artifact.exists() and not artifact.is_file())):
                row.update(status='FAIL', reason=f'Invalid renderer artifact destination: {filename}')
                return row
            sentinel = f'NYVAL stale output: {case["name"]}/{route}/{iteration + 1}\n'.encode()
            artifact.write_bytes(sentinel)
            artifact_preconditions[filename] = hashlib.sha256(sentinel).hexdigest()
        label = 'jit' if route == 'jit' else 'run'
        if repeats > 1:
            label += f'-{iteration+1:03d}'
        result = command(run, cwd, label, args.timeout, env)
        row['commands'].append(result)
        output = Path(result['stdout_path']).read_bytes()
        errors = []
        if result['timed_out'] or result['spawn_error']:
            errors.append('Execution timed out or could not start')
        elif case['kind'] == 'runtime-reject':
            expected = case.get('stderr_contains')
            if result['exit_code'] == 0:
                errors.append('Invalid runtime operation completed successfully')
            if expected and expected not in diagnostic(result):
                errors.append(f'Expected runtime diagnostic not found: {expected!r}')
        elif result['exit_code'] != 0:
            errors.append(f'Execution returned {result["exit_code"]}')
        if case.get('expected_stdout') is not None and output != case['expected_stdout'].encode():
            errors.append('stdout differs from the exact expected output')
        if case['kind'] != 'runtime-reject':
            expected_stderr = case.get('stderr_by_route', {}).get(route, case.get('expected_stderr'))
            if expected_stderr is not None:
                if diagnostic(result) != expected_stderr:
                    errors.append('stderr differs from exact expected output')
            elif case.get('stderr_contains'):
                if case['stderr_contains'] not in diagnostic(result):
                    errors.append('Expected stderr diagnostic was absent')
            elif diagnostic(result):
                errors.append('Execution wrote unexpected stderr (preserved in log)')
        if case.get('expected_files') and not errors:
            row['generated_files'] = []
            for filename, expected_hex in case['expected_files'].items():
                expected_bytes = bytes.fromhex(expected_hex)
                file_path = cwd / filename
                file_result = {'name': filename, 'path': str(file_path),
                               'expected_bytes': len(expected_bytes),
                               'expected_sha256': hashlib.sha256(expected_bytes).hexdigest(),
                               'status': 'FAIL'}
                if (Path(filename).is_absolute() or '..' in Path(filename).parts
                        or file_path.is_symlink() or not file_path.is_file()
                        or not file_path.resolve().is_relative_to(cwd)):
                    errors.append(f'Expected output is absent or not a regular file inside the run directory: {filename}')
                else:
                    actual_bytes = file_path.read_bytes()
                    file_result.update(bytes=len(actual_bytes), sha256=hashlib.sha256(actual_bytes).hexdigest())
                    if actual_bytes != expected_bytes:
                        errors.append(f'Generated file differs from independently expected bytes: {filename}')
                    else:
                        file_result['status'] = 'PASS'
                row['generated_files'].append(file_result)
        if case['kind'] == 'render' and not errors:
            try:
                metrics = ppm_metrics(output, cwd / f'frame-{iteration+1:03d}.png')
                if (metrics['width'], metrics['height']) != (args.width, args.height):
                    errors.append('Renderer did not honor requested dimensions')
                if first_output is not None and output != first_output:
                    errors.append('Repeated renderer output changed within the same route')
                row['image'] = metrics
                (cwd / f'frame-{iteration+1:03d}.ppm').write_bytes(output)
                for filename in case.get('output_artifacts', []):
                    artifact = cwd / filename
                    if (Path(filename).name != filename or artifact.is_symlink() or not artifact.is_file()
                            or not artifact.resolve().is_relative_to(cwd)):
                        raise ValueError(f'Missing or invalid renderer artifact: {filename}')
                    if artifact.stat().st_size > 16 * 1024 * 1024:
                        raise ValueError(f'Renderer artifact exceeds the validation size limit: {filename}')
                    data = artifact.read_bytes()
                    retained = cwd / f'frame-{iteration+1:03d}-{filename}'
                    retained.write_bytes(data)
                    row.setdefault('render_artifacts', []).append({
                        'name': filename, 'iteration': iteration + 1, 'path': str(retained),
                        'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                        'pre_run_sentinel_sha256': artifact_preconditions[filename],
                        'status': 'UNVERIFIED',
                    })
                if first_output is None:
                    first_output = output
            except (ValueError, UnicodeError) as exc:
                errors.append(str(exc))
        if errors:
            unsupported = route in NATIVE and any(marker in diagnostic(result).lower() for marker in UNSUPPORTED)
            if diagnostic(result):
                errors.append(diagnostic(result).strip()[:1000])
            row.update(status='UNSUPPORTED' if unsupported else 'FAIL', reason='; '.join(errors))
            return row
    row.update(status='PASS', reason='Expected runtime rejection observed' if case['kind'] == 'runtime-reject' else 'Exact output and exit checks passed')
    return row


def summary_text(report):
    counts = report.get('counts', {})
    lines = ['Ny standalone validation', f'Run: {report["run_id"]}',
             f'Status: {report["status"]}',
             'Results: ' + ', '.join(f'{key}={counts.get(key, 0)}' for key in ('PASS', 'FAIL', 'UNSUPPORTED', 'SKIP')),
             f'SDK integrity: before={report["sdk_integrity_before"]["status"]}, after={report.get("sdk_integrity_after", {}).get("status", "not checked")}',
             'Library resolution: SDK nilstd sources; application binary NIL is not binary-stdlib validation.',
             f'Artifacts: {report["run_directory"]}', '', 'Case / route / status / reason']
    lines += [f'{r["name"]} / {r["route"]} / {r["status"]} / {r["reason"]}' for r in report.get('results', [])]
    if report.get('error'):
        lines += ['', 'Harness error: ' + report['error']]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quick', action='store_true', help='Use interpreter and native AOT routes, 80x60 renderer unless dimensions specified')
    parser.add_argument('--routes', default=None, help='Comma-separated route IDs or all: ' + ','.join(ROUTES))
    parser.add_argument('--timeout', type=float, default=60, help='Per-command timeout in seconds (default 60)')
    parser.add_argument('--width', type=int, default=None)
    parser.add_argument('--height', type=int, default=None)
    parser.add_argument('--frames', type=int, default=12, help='Frames inside each scene-player process (1..120; default 12)')
    parser.add_argument('--stress', type=int, default=1, metavar='REPEATS', help='Repeat each renderer execution and require identical bytes')
    parser.add_argument('--only', help='Comma-separated exact case names, including renderer, mesh-renderer and scene-player (default all)')
    parser.add_argument('--report', type=Path, default=ROOT / 'reports' / 'validation.json', help='JSON report destination')
    args = parser.parse_args()
    args.width = args.width if args.width is not None else (80 if args.quick else 128)
    args.height = args.height if args.height is not None else (60 if args.quick else 96)
    if args.timeout <= 0 or args.stress < 1 or not (8 <= args.width <= 2048 and 8 <= args.height <= 2048) or not 1 <= args.frames <= 120:
        parser.error('Timeout and repeats must be positive; width/height must be 8..2048; frames must be 1..120')
    selected = list(ROUTES) if args.routes in ('all', None) else args.routes.split(',')
    if args.quick and args.routes is None:
        selected = ['nil-text', 'aot']
    if not selected or len(set(selected)) != len(selected) or set(selected) - set(ROUTES):
        parser.error('Unknown or repeated route; choose from ' + ', '.join(ROUTES))
    args.report = args.report.resolve()
    if args.report.is_relative_to(SDK):
        parser.error('Reports cannot be written into the immutable SDK')
    runs = ROOT / 'work' / 'runs'
    runs.mkdir(parents=True, exist_ok=True)
    run_root = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-'), dir=runs))
    env, removed = clean_environment(run_root)
    report = {'schema': 'ny.end-user.validation.v1', 'run_id': run_root.name, 'started_utc': utc_now(),
              'status': 'FAIL', 'matrix_completed': False, 'root': str(ROOT), 'sdk': str(SDK), 'run_directory': str(run_root),
              'invocation': sys.argv, 'routes': selected, 'dimensions': [args.width, args.height],
              'renderer_repeats': args.stress, 'scene_frames_per_process': args.frames,
              'library_resolution': 'SDK nilstd source standard library',
              'removed_environment_names': removed, 'subprocess_environment': {'PATH': env['PATH'], 'LC_ALL': env['LC_ALL'], 'TMPDIR': env['TMPDIR']},
              'isolation': 'Absolute SDK tool paths; project-local empty work directories; no compiler-checkout lookups by harness; filesystem access is not sandboxed.',
              'sdk_integrity_before': verify(SDK, ROOT / 'sdk-origin.json'), 'results': [],
              'untested_areas': ['compiled-NIL standard-library resolution', 'Windows/macOS hosts',
                                 'editor/LSP integration (debugger has a separate protocol audit)', 'native executable debugging', 'FFI and external libraries',
                                 'network/database integrations', 'exhaustive language conformance']}
    try:
        if report['sdk_integrity_before']['status'] != 'PASS':
            raise ValueError('Copied SDK integrity check failed; refusing to execute it')
        manifest_paths = [ROOT / 'probes' / 'manifest.json',
                          *sorted((ROOT / 'probes').glob('*/manifest.json'))]
        # The probe corpus is optional; without it the three renderers are the
        # whole matrix.
        manifest_paths = [path for path in manifest_paths if path.is_file()]
        cases = list(RENDERERS)
        for manifest_path in manifest_paths:
            manifest = json.loads(manifest_path.read_text())
            cases.extend(manifest if isinstance(manifest, list) else manifest['probes'])
        names = [case['name'] for case in cases]
        if len(set(names)) != len(names) or any(not re.fullmatch(r'[a-zA-Z0-9_-]+', name) for name in names):
            raise ValueError('Case names must be unique simple identifiers')
        if args.only:
            wanted = set(args.only.split(','))
            if wanted - set(names):
                raise ValueError('Unknown case names: ' + ', '.join(sorted(wanted - set(names))))
            cases = [case for case in cases if case['name'] in wanted]
        if manifest_paths:
            report['probe_manifest_sha256'] = digest(manifest_paths[0])
            report['probe_manifests'] = {str(path.relative_to(ROOT)): digest(path) for path in manifest_paths}
        report['runner_sha256'] = digest(Path(__file__))
        report['oracle_sha256'] = digest(ROOT / 'scripts' / 'check_render.py')
        report['oracle_sources'] = {str(path.relative_to(ROOT)): digest(path)
                                    for path in sorted({*(ROOT / 'scripts').glob('check_*render.py'),
                                                        ROOT / 'scripts' / 'check_scene_player.py'}) if path.is_file()}
        report['application_sources'] = {str(path.relative_to(ROOT)): digest(path)
                                         for path in sorted((ROOT / 'src').rglob('*.ny'))}
        report['known_issue_ids'] = sorted({case['known_issue'] for case in cases if case.get('known_issue')})
        report['exercised_areas'] = sorted({case['area'] for case in cases})
        for case in cases:
            if case['kind'] not in {'run', 'render', 'compile-reject', 'runtime-reject'}:
                raise ValueError('Unsupported manifest case kind: ' + case['kind'])
            for route in selected:
                if case.get('routes') and route not in case['routes']:
                    row = {'name': case['name'], 'route': route, 'kind': case['kind'], 'area': case['area'],
                           'status': 'SKIP', 'reason': 'Outside the route scope declared by this probe', 'commands': []}
                else:
                    row = run_case(case, route, run_root, args, env)
                report['results'].append(row)
                print(f'{row["status"]:11} {case["name"]:35} {route}', flush=True)
                write_json(run_root / 'progress.json', report)
        report['renderers'] = {}
        for render_case in [case for case in cases if case['kind'] == 'render']:
            images = [row for row in report['results']
                      if row['name'] == render_case['name'] and row['status'] == 'PASS']
            if not images:
                continue
            reference = next((row for row in images if row['route'] == 'nil-text'), images[0])
            oracle = image_oracle(render_case['name'], Path(reference['work_directory']) / 'frame-001.ppm')
            if render_case['name'] == 'scene-player' and oracle.get('frames') != args.frames:
                oracle.update(status='FAIL', frame_count_error='Scene did not honor the requested frame count')
            report['renderers'][render_case['name']] = {'reference_route': reference['route'], 'oracle': oracle}
            if render_case['name'] == 'renderer':
                # Retain the original report fields for existing baseline readers.
                report['renderer_reference_route'] = reference['route']
                report['renderer_oracle'] = oracle
            if oracle['status'] != 'PASS':
                for row in images:
                    row.update(status='FAIL', reason='Independent Python geometry/pixel/row-hash oracle disagrees with the reference; route correctness is not established')
            for row in images:
                expected_artifacts = oracle.get('artifacts', {})
                artifact_names = render_case.get('output_artifacts', [])
                actual_artifacts = row.get('render_artifacts', [])
                if len(actual_artifacts) != len(artifact_names) * args.stress:
                    row.update(status='FAIL', reason='Renderer artifact coverage is incomplete')
                for artifact in actual_artifacts:
                    expected_hash = expected_artifacts.get(artifact['name'], {}).get('expected_sha256')
                    artifact['expected_sha256'] = expected_hash
                    artifact['status'] = 'PASS' if artifact['sha256'] == expected_hash else 'FAIL'
                    if artifact['status'] != 'PASS':
                        row.update(status='FAIL', reason=f'Renderer artifact differs from independent expected bytes: {artifact["name"]}')
                row['image']['parity_reference_route'] = reference['route']
                row['image']['exact_route_parity'] = row['image']['ppm_sha256'] == reference['image']['ppm_sha256']
                if not row['image']['exact_route_parity']:
                    row.update(status='FAIL', reason=f'Renderer PPM bytes differ from {reference["route"]}; no output normalization applied')
        report['matrix_completed'] = True
    except KeyboardInterrupt:
        report['error'] = 'Validation interrupted before completion'
    except (OSError, ValueError, KeyError, TypeError) as exc:
        report['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        report['sdk_integrity_after'] = verify(SDK, ROOT / 'sdk-origin.json')
        before, after = report['sdk_integrity_before'], report['sdk_integrity_after']
        if before.get('manifest_sha256') != after.get('manifest_sha256') or before.get('origin_manifest_sha256') != after.get('origin_manifest_sha256'):
            after['status'] = 'FAIL'
            after['errors'].append('SDK manifest or immutable origin anchor changed during the run')
        report['completed_utc'] = utc_now()
        report['counts'] = dict(collections.Counter(row['status'] for row in report['results']))
        report['status'] = 'PASS' if (not report.get('error') and report['matrix_completed'] and report['counts'].get('PASS', 0) > 0 and not any(report['counts'].get(x, 0) for x in ('FAIL', 'UNSUPPORTED')) and report['sdk_integrity_before']['status'] == report['sdk_integrity_after']['status'] == 'PASS') else 'FAIL'
        write_json(run_root / 'validation.json', report)
        write_json(args.report, report)
        summary = summary_text(report)
        args.report.with_suffix('.txt').write_text(summary)
        (run_root / 'validation.txt').write_text(summary)
        print(f'\n{report["status"]}: {report["counts"]}\nReport: {args.report}', flush=True)
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
