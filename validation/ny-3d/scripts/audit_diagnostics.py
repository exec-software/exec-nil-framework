#!/usr/bin/env python3
"""Audit rejection correctness separately from observable diagnostic detail.

Uses only the copied SDK and paired valid controls. Retains every command and
byte of output; expected rejections must be normal nonzero exits, not crashes.
Quality fields are literal observations, not a subjective numeric score.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import tempfile

from sdk_integrity import digest, verify
from validate import ROOT, SDK, build_steps, clean_environment, command, succeeded, write_json

ROUTES = ('nil-text', 'jit', 'aot')


def execute(source, route, directory, timeout, env, prove_native_build=False):
    directory.mkdir(parents=True)
    records = []
    steps, run = build_steps(route, source, directory, [], timeout)
    if route == 'jit' and prove_native_build:
        preflight, _ = build_steps('aot', source, directory, [], timeout)
        steps = [('native-preflight', argv) for _, argv in preflight]
    for label, argv in steps:
        record = command(argv, directory, label, timeout, env)
        records.append(record)
        if not succeeded(record):
            return records
    records.append(command(run, directory, 'jit' if route == 'jit' else 'run', timeout, env))
    return records


def quality(record, source, case):
    stderr = Path(record['stderr_path']).read_text(errors='replace')
    line_patterns = [r'\bline\s+(\d+)\b', re.escape(source.name) + r':(\d+)(?::|\b)']
    line_numbers = sorted({int(n) for pattern in line_patterns for n in re.findall(pattern, stderr)})
    columns = re.findall(re.escape(source.name) + r':\d+:(\d+)(?::|\b)', stderr)
    columns += re.findall(r'\bcol(?:umn)?\s+(\d+)\b', stderr, flags=re.I)
    detail_lines = [line for line in stderr.splitlines() if line.strip()
                    and not line.startswith(('Type checking failed:', 'Parse errors in ', 'Native code generation failed'))]
    return {
        'diagnostic_present': bool(stderr),
        'source_filename_present': source.name in stderr,
        'line_numbers': line_numbers,
        'line_present': bool(line_numbers),
        'column_present': bool(columns),
        'source_excerpt_present': any(line.strip() and line.strip() in stderr
                                      for line in source.read_text().splitlines()
                                      if line.strip() not in {'return 0', 'end fn', 'end struct'}),
        'construct_identified': bool(case['construct_tokens']) and all(token in stderr for token in case['construct_tokens']),
        'rule_explained': bool(case['rule_token']) and case['rule_token'] in stderr,
        'expected_and_actual_types_present': bool(re.search(r'Expected.+got', stderr)),
        'suggestion_present': bool(re.search(r'\b(help|hint|did you mean|try using|consider)\b', stderr, flags=re.I)),
        'detail_line_count': len(detail_lines),
        'additional_detail_lines': max(0, len(detail_lines) - 1),
        'reported_stage': 'parse' if 'Parse errors' in stderr else
                          'type-check' if 'Type checking failed' in stderr else
                          'native-codegen' if '[NativeCodegen]' in stderr else
                          'runtime' if 'runtime error' in stderr else None,
        'stderr': stderr,
    }


def assess(records, case, route, control):
    last = records[-1]
    ordinary_failure = (last['exit_code'] is not None and last['exit_code'] > 0
                        and not last['timed_out'] and not last['spawn_error'])
    stderr = Path(last['stderr_path']).read_text(errors='replace')
    stdout = Path(last['stdout_path']).read_text(errors='replace')
    expected_kind = 'run' if control else case['kind']
    if expected_kind == 'capability-boundary':
        expected_kind = 'run' if route == 'nil-text' else 'compile-reject'
    if expected_kind == 'documented-bounds-behavior':
        expected_kind = 'run'
    diagnostic_matches = not case['diagnostic_contains'] or case['diagnostic_contains'] in stderr
    if expected_kind == 'run':
        expected_stdout = case['control_stdout'] if control else case['expected_stdout']
        ok = all(succeeded(record) for record in records) and stdout == expected_stdout and not stderr
        reason = 'Exact stdout, empty stderr and successful exit' if ok else 'Valid execution/output expectation failed'
    elif expected_kind == 'compile-reject':
        at_compilation = last['phase'] == 'compile' or (route == 'jit' and last['phase'] == 'jit')
        ok = at_compilation and ordinary_failure and diagnostic_matches
        reason = 'Ordinary rejection with expected category' if ok else 'Invalid source was accepted or rejection did not match'
    else:
        reached_runtime = last['phase'] in {'run', 'jit'} and all(succeeded(record) for record in records[:-1])
        ok = reached_runtime and ordinary_failure and diagnostic_matches and not stdout
        reason = 'Successful compilation followed by expected runtime rejection' if ok else 'Expected runtime rejection not established'
    return {'status': 'PASS' if ok else 'FAIL', 'expected_kind': expected_kind,
            'reason': reason, 'observed_phase': last['phase'], 'stdout': stdout}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--report', type=Path, default=ROOT / 'reports/diagnostics.json')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('Timeout must be positive')
    args.report = args.report.resolve()
    if args.report.is_relative_to(SDK):
        parser.error('Reports cannot be written into the immutable SDK')
    work = ROOT / 'work/diagnostics'
    work.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-'), dir=work))
    env, removed = clean_environment(directory)
    case_path = ROOT / 'probes/diagnostics/cases.json'
    report = {'schema': 'ny.diagnostics.audit.v1', 'run_directory': str(directory),
              'started_utc': datetime.now(timezone.utc).isoformat(), 'routes': list(ROUTES),
              'status': 'FAIL', 'audit_completed': False, 'results': [],
              'cases_sha256': digest(case_path), 'auditor_sha256': digest(Path(__file__)),
              'runner_sha256': digest(ROOT / 'scripts/validate.py'),
              'removed_environment_names': removed,
              'sdk_integrity_before': verify(SDK, ROOT / 'sdk-origin.json'),
              'metric_limits': 'Literal substring/regex observations. Detail-line count is not a proof of root-cause count; suggestion/excerpt detection is heuristic. No quality score or assumed required format.'}
    try:
        if report['sdk_integrity_before']['status'] != 'PASS':
            raise ValueError('Copied SDK integrity failed')
        cases = json.loads(case_path.read_text())
        for case in cases:
            for route in ROUTES:
                for variant in ('control', 'source'):
                    source = (ROOT / case[variant]).resolve()
                    if not source.is_relative_to(ROOT) or source.is_relative_to(SDK):
                        raise ValueError('Case source must be outside sdk/ and inside project')
                    records = execute(source, route, directory / case['name'] / route / variant,
                                      args.timeout, env, prove_native_build=case['kind'] == 'runtime-reject')
                    row = {'name': case['name'], 'route': route, 'variant': variant,
                           'source': str(source), 'source_sha256': digest(source),
                           'known_issue': case.get('known_issue') if variant == 'source' else None,
                           'commands': records, **assess(records, case, route, variant == 'control')}
                    if variant == 'source':
                        row['diagnostic_quality'] = quality(records[-1], source, case)
                    report['results'].append(row)
                    print(f"{row['status']:4} {case['name']:30} {route:8} {variant}", flush=True)
        report['audit_completed'] = True
    except (OSError, ValueError, KeyError, KeyboardInterrupt) as exc:
        report['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        report['sdk_integrity_after'] = verify(SDK, ROOT / 'sdk-origin.json')
        before, after = report['sdk_integrity_before'], report['sdk_integrity_after']
        if before.get('manifest_sha256') != after.get('manifest_sha256') or before.get('origin_manifest_sha256') != after.get('origin_manifest_sha256'):
            after['status'] = 'FAIL'
            after['errors'].append('SDK or origin manifest changed during audit')
        report['completed_utc'] = datetime.now(timezone.utc).isoformat()
        report['counts'] = dict(Counter(row['status'] for row in report['results']))
        report['control_counts'] = dict(Counter(row['status'] for row in report['results'] if row['variant'] == 'control'))
        observed = [row['diagnostic_quality'] for row in report['results']
                    if row['variant'] == 'source' and row['diagnostic_quality']['diagnostic_present']]
        report['diagnostic_quality_summary'] = {'observed_diagnostics': len(observed), **{
            key: sum(bool(q[key]) for q in observed) for key in (
                'source_filename_present', 'line_present', 'column_present', 'source_excerpt_present',
                'construct_identified', 'rule_explained', 'expected_and_actual_types_present', 'suggestion_present')}}
        report['status'] = 'PASS' if (report['audit_completed'] and not report.get('error')
                                     and report['counts'].get('PASS', 0) and not report['counts'].get('FAIL', 0)
                                     and before['status'] == after['status'] == 'PASS') else 'FAIL'
        write_json(directory / 'report.json', report)
        write_json(args.report, report)
        lines = ['Ny paired diagnostic audit', 'Status: ' + report['status'],
                 'Results: ' + str(report['counts']), 'Valid controls: ' + str(report['control_counts']),
                 'Diagnostic observations: ' + str(report['diagnostic_quality_summary']),
                 'Artifacts: ' + str(directory), '']
        lines.extend(f"{r['name']} / {r['route']} / {r['variant']} / {r['status']} / {r['reason']}" for r in report['results'])
        summary = '\n'.join(lines) + '\n'
        args.report.with_suffix('.txt').write_text(summary)
        (directory / 'report.txt').write_text(summary)
        print(summary.split('Artifacts:')[0], flush=True)
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
