#!/usr/bin/env python3
"""Trace consumer file access to detect accidental use of the compiler checkout."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import tempfile

from sdk_integrity import verify
from validate import ROOT, SDK, clean_environment, command, succeeded, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, default=ROOT / 'reports/isolation.json')
    args = parser.parse_args()
    trace_tool = shutil.which('strace')
    if trace_tool is None:
        parser.error('strace is required for the optional file-access audit')
    origin_path = ROOT / 'sdk-origin.json'
    if not origin_path.is_file():
        raise SystemExit('Isolation audit needs an SDK origin anchor (sdk-origin.json); skipping.')
    origin = json.loads(origin_path.read_text())
    forbidden = origin['source_project']
    before = verify(SDK, ROOT / 'sdk-origin.json')
    if before['status'] != 'PASS':
        raise SystemExit('SDK integrity verification failed')
    (ROOT / 'work').mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='isolation-', dir=ROOT / 'work'))
    env, removed = clean_environment(work)
    source = ROOT / 'src/renderer.ny'
    compiler = [str(SDK / 'nilcompiler'), '-q', '--sdk', str(SDK)]
    nil = work / 'renderer.nil'
    native = work / 'renderer-native'
    steps = [
        ('compile-nil', compiler + ['--no-bundle', '-o', str(nil), str(source)]),
        ('run-nil', [str(SDK / 'nilinterpreter'), str(nil), '--', '32', '24']),
        ('compile-native', compiler + ['--native', '-o', str(native), str(source)]),
        ('run-native', [str(native), '32', '24']),
    ]
    evidence = []
    for label, argv in steps:
        log = work / f'{label}.trace'
        result = command([trace_tool, '-f', '-qq', '-e', 'trace=%file', '-o', str(log), '--'] + argv,
                         work, label, 60, env)
        matches = [line for line in log.read_text(errors='replace').splitlines()
                   if forbidden in line]
        result['file_access_trace'] = str(log)
        result['forbidden_path_matches'] = matches[:20]
        result['forbidden_path_match_count'] = len(matches)
        evidence.append(result)
        if not succeeded(result):
            break
    after = verify(SDK, ROOT / 'sdk-origin.json')
    parity = False
    if len(evidence) == 4 and all(succeeded(item) for item in evidence):
        parity = (work / 'run-nil.stdout').read_bytes() == (work / 'run-native.stdout').read_bytes()
    passed = (len(evidence) == 4 and all(succeeded(item) and not item['forbidden_path_match_count']
                                      for item in evidence) and parity and after['status'] == 'PASS')
    report = {'schema': 'ny.3d.isolation.v1', 'status': 'PASS' if passed else 'FAIL',
              'scope': 'Linux file syscalls during copied-SDK NIL/native compilation and renderer execution',
              'limitation': 'This audits these invocations; it is not a sandbox or proof about every tool path.',
              'forbidden_checkout': forbidden, 'environment_overrides_removed': removed,
              'image_parity': parity, 'sdk_before': before, 'sdk_after': after, 'commands': evidence}
    write_json(args.report.resolve(), report)
    print(f"{report['status']}: consumer file-access audit; report: {args.report.resolve()}")
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
