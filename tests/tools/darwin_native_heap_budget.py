"""Bounded Darwin-only requested-allocation retention proof for native fixtures.

This is not a whole-process address-space, committed-memory, or RSS limit.
The unchanged native fixture must independently demonstrate its expected calloc
payload count, and withheld real frees must make that same executable fail.
The tracker is injected only into fresh owned consumers, never the compiler or
the Python process-ownership gate. Raw successes and failures are retained.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import secrets
import sys
import tempfile
import time

import ordinary_io_parity_contract as support

ROOT = Path(__file__).resolve().parents[2]
BUDGET = 96 * 1024 * 1024
METRIC = '96 MiB live requested native heap allocations (Darwin test interposer)'
ALLOCATORS = {'malloc', 'calloc', 'realloc', 'free', 'aligned_alloc', 'posix_memalign', 'valloc'}
ALLOCATORS |= {'malloc_type_' + name for name in tuple(ALLOCATORS)}


def add_arguments(parser):
    parser.add_argument('--heap-budget-library', type=Path)
    parser.add_argument('--heap-budget-control', type=Path)
    parser.add_argument('--output-parent', type=Path,
                        help='Retain Darwin raw evidence in a fresh child directory')


class DarwinHeapBudget:
    def __init__(self, args, name, *, seconds, fixtures):
        if sys.platform != 'darwin':
            raise AssertionError('Darwin interposition requires actual macOS execution')
        if not args.heap_budget_library or not args.heap_budget_control:
            raise AssertionError('Darwin lifetime tests require the heap budget library and control executable')
        self.library = args.heap_budget_library.resolve(strict=True)
        self.control = args.heap_budget_control.resolve(strict=True)
        parent = args.output_parent.resolve() if args.output_parent else Path(tempfile.gettempdir())
        parent.mkdir(parents=True, exist_ok=True)
        self.output = Path(tempfile.mkdtemp(prefix='darwin-' + name + '-', dir=parent))
        self.work = self.output / 'work'
        self.logs = self.output / 'logs'
        self.work.mkdir(); self.logs.mkdir()
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith(('NY_', 'NIL_', 'DYLD_'))}
        self.deadline = time.monotonic() + seconds
        self.ownership = support.load_ownership(ROOT)
        self.expected_fixtures = fixtures
        self.report = {'schema': 'nil.darwin-native-heap-budget.v1', 'status': 'FAIL',
                       'metric': METRIC, 'budget_bytes': BUDGET,
                       'scope': 'intercepted malloc-family requested payload retention; excludes tracker storage, '
                                'allocator overhead, VM mappings and allocation paths bypassing these functions',
                       'whole_process_memory_parity': False, 'whole_seconds': seconds,
                       'commands': [], 'runs': [], 'imports': [], 'fixtures': [],
                       'inputs': {}}
        inputs = [self.library, self.control, args.compiler.resolve(strict=True), Path(__file__),
                  ROOT / ('tests/tools/native_' + name.replace('-', '_') + '.py'),
                  ROOT / 'tests/darwin_native_heap_budget.c',
                  ROOT / 'tests/darwin_native_heap_budget.h',
                  ROOT / 'tests/darwin_native_heap_budget_control.c',
                  ROOT / 'validation/ny-3d/scripts/debugger_process.py']
        for path in inputs:
            self.report['inputs'][str(path)] = support.file_record(path)

    def __enter__(self):
        # Preparation is inside the lifetime script's with block so its failure
        # also reaches __exit__ and retains the report.
        return str(self.work)

    def __exit__(self, kind, error, traceback):
        if error is not None:
            self.report['error'] = str(error)
        else:
            try:
                if len(self.report['fixtures']) != self.expected_fixtures:
                    raise AssertionError('not all requested native fixtures completed')
                for path, record in self.report['inputs'].items():
                    if support.file_record(Path(path)) != record:
                        raise AssertionError('contract input changed during execution: ' + path)
                self.report['status'] = 'PASS'
            except Exception as failure:
                self.report['error'] = str(failure)
        self.report['cleanup_verified'] = bool(self.report['commands']) and all(
            row.get('cleanup_verified') is True for row in self.report['commands'])
        if not self.report['cleanup_verified']:
            self.report['status'] = 'FAIL'
        path = self.output / 'report.json'
        path.write_text(json.dumps(self.report, indent=2) + '\n', encoding='utf-8')
        print(self.report['status'] + ': ' + str(path))
        if error is None and self.report['status'] != 'PASS':
            raise AssertionError(self.report.get('error', 'owned cleanup failed'))
        return False

    def command(self, name, command, *, stdout=b'', stderr=b'', exit_code=0, seconds=30):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError('whole Darwin heap contract deadline exhausted')
        row = support.run_process(self.ownership, [str(part) for part in command], self.work,
                                  self.environment, self.logs, name, min(seconds, remaining))
        row['expected_exit_code'] = exit_code
        self.report['commands'].append(row)
        actual = (self.logs / row['stdout']).read_bytes()
        errors = (self.logs / row['stderr']).read_bytes()
        if (row.get('exit_code') != exit_code or not row.get('cleanup_verified') or
                row.get('error') or (stdout is not None and actual != stdout) or errors != stderr):
            raise AssertionError(name + ': output/exit/deadline/owned-cleanup contract failed')
        row['status'] = 'PASS'  # Expected negative exits are evidence, not skipped tests.
        return actual

    def inspect(self, name, executable, *, fixture=False):
        imports = self.command(name + '-imports', ['/usr/bin/nm', '-u', executable], stdout=None)
        self.command(name + '-dependencies', ['/usr/bin/otool', '-L', executable], stdout=None)
        candidates = set(re.findall(rb'\b_((?:malloc|calloc|realloc|free|aligned_alloc|posix_memalign|valloc)[A-Za-z0-9_]*)\b', imports))
        candidates = {value.decode('ascii') for value in candidates}
        # Queries do not allocate. Zone allocation, reallocf, and future typed
        # entry points remain fail-closed until explicitly covered and tested.
        candidates -= {'malloc_size', 'malloc_good_size', 'malloc_zone_from_ptr', 'malloc_default_zone'}
        unsupported = candidates - ALLOCATORS
        row = {'name': name, 'path': str(executable), 'allocator_imports': sorted(candidates),
               'unsupported_allocator_imports': sorted(unsupported), **support.file_record(executable)}
        self.report['imports'].append(row)
        if unsupported:
            raise AssertionError(name + ': uncovered allocator imports: ' + ', '.join(sorted(unsupported)))
        if fixture and (not candidates & {'calloc', 'malloc_type_calloc'} or
                        not candidates & {'free', 'malloc_type_free'}):
            raise AssertionError(name + ': expected native runtime calloc/free imports absent')

    def injected(self, name, executable, arguments=(), *, target, withhold=False,
                 wanted=b'', negative=False):
        events_path = self.logs / (name + '.heap.jsonl')
        token = secrets.token_hex(16)
        # The ownership helper starts a Python gate. Supplying DYLD_* in its
        # environment would instrument that gate before exec. env assignments
        # are parsed only after the gate execs this system utility.
        command = ['/usr/bin/env', 'DYLD_INSERT_LIBRARIES=' + str(self.library),
                   'NIL_DARWIN_HEAP_REPORT=' + str(events_path),
                   'NIL_DARWIN_HEAP_TOKEN=' + token,
                   'NIL_DARWIN_HEAP_TARGET=' + str(target),
                   'NIL_DARWIN_HEAP_WITHHOLD=' + ('1' if withhold else '0'),
                   str(executable), *arguments]
        self.command(name, command, stdout=wanted, exit_code=86 if negative else 0, seconds=45)
        record = support.file_record(events_path, 8192)
        events = [json.loads(line) for line in events_path.read_text(encoding='ascii').splitlines()]
        if (len(events) != 2 or events[0].get('event') != 'ready' or
                events[1].get('event') != ('budget' if negative else 'final')):
            raise AssertionError(name + ': missing or malformed injection/final handshake')
        for event in events:
            if (event.get('token') != token or event.get('version') != 1 or
                    event.get('budget') != BUDGET or event.get('target_size') != target or
                    not isinstance(event.get('pid'), int) or event['pid'] <= 0 or
                    event['pid'] == os.getpid() or not 0 <= event.get('live', -1) <= event.get('peak', -1) <= BUDGET):
                raise AssertionError(name + ': wrong process/configuration/accounting handshake')
        initial, final = events
        if initial['pid'] != final['pid']:
            raise AssertionError(name + ': report crossed process identity')
        for key in ('allocations', 'frees', 'cumulative', 'target_allocations', 'target_frees', 'typed_calls'):
            if final[key] < initial[key]:
                raise AssertionError(name + ': allocator counters moved backwards')
        if negative:
            if (not 0 < final['denied_request'] <= target or
                    final['live'] < BUDGET - 2 * target or
                    final['target_allocations'] * target < BUDGET * .8 or
                    (withhold and final['withheld'] == 0)):
                raise AssertionError(name + ': retention did not cause the bounded allocation rejection')
        elif final['withheld'] or final['denied_request']:
            raise AssertionError(name + ': ordinary fixture used negative-control behavior')
        self.report['runs'].append({'name': name, 'expected_budget_rejection': negative,
                                    'withhold_target_frees': withhold, 'events': events,
                                    'report_file': events_path.name, **record})
        return final

    def prepare(self):
        self.command('host-version', ['/usr/bin/sw_vers'], stdout=None)
        self.command('host-machine', ['/usr/bin/uname', '-m'], stdout=None)
        self.command('clang-version', ['/usr/bin/xcrun', 'clang', '--version'], stdout=None)
        self.command('sdk-version', ['/usr/bin/xcrun', '--show-sdk-version'], stdout=None)
        self.inspect('interposer', self.library)
        self.inspect('control', self.control)
        self.command('injection-absent', [self.control, 'positive'], exit_code=90,
                     stderr=b'heap budget injection absent\n')
        positive = self.injected('control-positive', self.control, ['positive'], target=65536,
                                 wanted=b'heap-controls-ok\n')
        if (positive['cumulative'] <= 2 * 1024**3 or positive['target_allocations'] < 34000 or
                positive['target_frees'] < 34000 or positive['failures'] < 2):
            raise AssertionError('positive/realloc/failure control coverage insufficient')
        self.injected('control-retained-untouched', self.control, ['retain'], target=65536, negative=True)
        self.injected('control-withheld-free', self.control, ['positive'], target=65536,
                      withhold=True, negative=True)

    def compile(self, name, command):
        self.command(name, command, seconds=90)

    def fixture(self, name, source, executable, expected, *, target, count):
        source_record = support.file_record(source)
        executable_record = support.file_record(executable)
        self.inspect(name, executable, fixture=True)
        final = self.injected(name, executable, target=target, wanted=expected.encode('utf-8'))
        if (final['target_allocations'] < count or final['target_frees'] < count - 1 or
                final['cumulative'] < target * count):
            raise AssertionError(name + ': expected unchanged-loop payload coverage absent')
        # The exact same native executable must fail when actual target frees
        # are withheld. No Ny source mutation or runtime GC counters are used.
        self.injected(name + '-withheld-free', executable, target=target,
                      withhold=True, negative=True)
        if (support.file_record(source) != source_record or
                support.file_record(executable) != executable_record):
            raise AssertionError(name + ': source or executable changed between controls')
        self.report['fixtures'].append({'name': name, 'source': str(source),
                                        'source_record': source_record, 'executable_record': executable_record,
                                        'expected_target_allocations': count, 'target_bytes': target,
                                        'positive_and_withheld_free_replay': 'PASS'})
