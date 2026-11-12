#!/usr/bin/env python3
"""Linux --wrap regression for Darwin heap-oracle bookkeeping, not Mach-O proof."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

import ordinary_io_parity_contract as support

ROOT = Path(__file__).resolve().parents[2]
BUDGET = 96 * 1024 * 1024


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--output-parent', type=Path)
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        raise AssertionError('The portable --wrap control requires Linux; use real injection on Darwin')
    parent = args.output_parent.resolve() if args.output_parent else Path(tempfile.gettempdir())
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='nil-heap-accounting-', dir=parent))
    ownership = support.load_ownership(ROOT)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NIL_', 'NY_', 'DYLD_'))}
    report = {'schema': 'nil.heap-accounting-portable.v1', 'status': 'FAIL',
              'darwin_injection_qualified': False, 'commands': [], 'cases': []}

    def command(name, argv, *, variables=None, code=0, stdout=b''):
        row = support.run_process(ownership, list(map(str, argv)), ROOT,
                                  environment | (variables or {}), output, name, 30)
        report['commands'].append(row)
        actual = (output / row['stdout']).read_bytes()
        errors = (output / row['stderr']).read_bytes()
        if (row.get('exit_code') != code or not row.get('cleanup_verified') or
                row.get('error') or actual != stdout or errors):
            raise AssertionError(f'{name}: exit={row.get("exit_code")} expected={code}; '
                                 f'stdout={actual[:1024]!r}; stderr={errors[:1024]!r}')
        row['status'] = 'PASS'

    try:
        for nested in (False, True):
            kind = 'nested' if nested else 'ordinary'
            executable = output / kind
            argv = [args.cc, '-std=c11', '-D_DEFAULT_SOURCE', '-DNIL_HEAP_BUDGET_PORTABLE_TEST',
                    '-Wall', '-Wextra', '-Werror', '-O2', '-fno-builtin', '-rdynamic', '-pthread',
                    ROOT / 'tests/darwin_native_heap_budget.c',
                    ROOT / 'tests/darwin_native_heap_budget_control.c']
            if nested:
                argv += ['-DNIL_HEAP_BUDGET_NESTED_TEST', ROOT / 'tests/darwin_native_heap_budget_nested.c']
            argv += ['-ldl'] + ['-Wl,--wrap=' + name for name in
                               ('malloc', 'calloc', 'realloc', 'free', 'aligned_alloc', 'posix_memalign', 'valloc')]
            command(kind + '-build', argv + ['-o', executable])
            for case, mode, withhold, target in (
                    ('positive', 'positive', False, 65536), ('retained', 'retain', False, 65536),
                    ('withheld', 'positive', True, 65536),
                    ('guarded-positive', 'guarded-positive', False, 65540),
                    ('guarded-retained', 'guarded-retain', False, 65540),
                    ('guarded-withheld', 'guarded-positive', True, 65540)):
                name = kind + '-' + case
                events_path = output / (name + '.jsonl')
                negative = not case.endswith('positive')
                positive_output = b'heap-controls-ok\n' if target == 65536 else b'guarded-controls-ok\n'
                command(name, [executable, mode], code=86 if negative else 0,
                        stdout=b'' if negative else positive_output, variables={
                            'NIL_DARWIN_HEAP_REPORT': str(events_path), 'NIL_DARWIN_HEAP_TOKEN': 'a' * 32,
                            'NIL_DARWIN_HEAP_TARGET': str(target), 'NIL_DARWIN_HEAP_WITHHOLD': str(int(withhold))})
                events = [json.loads(line) for line in events_path.read_text().splitlines()]
                if len(events) != 2 or events[0]['event'] != 'ready':
                    raise AssertionError(name + ': initialization/final event pair absent')
                initial, final = events
                if final['event'] != ('budget' if negative else 'final') or final['budget'] != BUDGET:
                    raise AssertionError(name + ': wrong terminal event or budget')
                if nested and (initial['cumulative'] != 1024 or initial['allocations'] != 1 or initial['live']):
                    raise AssertionError(name + ': pre-initialization nested request was not counted exactly once')
                if negative:
                    if not (BUDGET - target <= final['live'] <= BUDGET and final['denied_request'] == target):
                        raise AssertionError(name + ': retained payload did not trigger the real budget')
                    if withhold and not final['withheld']:
                        raise AssertionError(name + ': actual target frees were not withheld')
                elif (final['live'] or final['cumulative'] - initial['cumulative'] <= 2 * 1024**3 or
                      final['target_allocations'] - initial['target_allocations'] <
                      34000 + (4 if nested and target == 65536 else 0)
                      or final['unknown_frees'] - initial['unknown_frees']):
                    raise AssertionError(name + ': sustained/nested/threaded allocation coverage differs')
                report['cases'].append({'name': name, 'events': events})
        report['status'] = 'PASS'
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['cleanup_verified'] = bool(report['commands']) and all(
            item.get('cleanup_verified') for item in report['commands'])
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(report['status'] + ': ' + str(output / 'report.json'))


if __name__ == '__main__':
    main()
