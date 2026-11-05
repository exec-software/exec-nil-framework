#!/usr/bin/env python3
"""Owned installed macOS JSONL/DAP and execution-checkpoint acceptance; no activation."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'validation/ny-3d'
TOTAL_SECONDS = 1400
DEBUGGER_NAMES = 'c893ff9145c348931b500cc1d7b828da3aeaed4c2b84472a9c94edf3ca91d8cb'
CHECKPOINT_OUTPUT = (b'AI checkpoint contract: capture/mutate/reusable restore/continuation, '
                     b'rejection and explicit-exit cleanup passed\n')


def require(value, message):
    if not value:
        raise ValueError(message)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def record(path, deadline, limit=1024**3):
    digest = hashlib.sha256()
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0))
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and before.st_size <= limit,
                'Input must be an independent bounded regular file: ' + str(path))
        total = 0
        while block := stream.read(1024**2):
            require(time.monotonic() < deadline, 'Evidence hashing deadline expired')
            total += len(block)
            require(total <= limit, 'Input grew beyond file bound')
            digest.update(block)
        after = os.fstat(stream.fileno())
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns,
                                  value.st_ctime_ns, value.st_mode, value.st_nlink)
        require(identity(before) == identity(after) and total == after.st_size, 'Input changed while hashing: ' + str(path))
    return {'bytes': after.st_size, 'sha256': digest.hexdigest(), 'mode': stat.S_IMODE(after.st_mode)}


def inventory(directory, deadline):
    require(directory.is_dir() and not directory.is_symlink(), 'Expected independent directory')
    result = {}
    for path in sorted(directory.rglob('*')):
        require(not path.is_symlink(), 'Inventory contains a symlink')
        if path.is_dir():
            continue
        result[path.relative_to(directory).as_posix()] = record(path, deadline)
        require(len(result) <= 10000, 'Inventory count exceeds bound')
    require(bool(result), 'Empty inventory')
    return result


def read_json(path):
    require(path.stat().st_size <= 16 * 1024**2, 'Oversized JSON report')
    return json.loads(path.read_bytes())


def write_json(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')


def project_sources():
    return [PROJECT / 'scripts' / name for name in
            ('validate_debugger.py', 'debugger_process.py', 'sdk_integrity.py')] + [
                *sorted((PROJECT / 'src').rglob('*.ny')),
                *sorted((PROJECT / 'probes/debugger').glob('*.ny'))]


def verify_origin(state, sdk, revision, arch):
    require(state['status'] == 'PASS' and state['errors'] == [] and state['anchor'] == 'origin file' and
            state['sdk'] == str(sdk) and state['manifest_sha256'] == state['origin_manifest_sha256'],
            'SDK lacks the matching external manifest anchor')
    source = state['baseline']['source']
    require(source['revision'] == revision and source['dirty'] is False,
            'SDK source does not match the exact clean requested revision')
    require(state['baseline']['host'] == {'system': 'Darwin', 'machine': arch}, 'SDK host mismatch')


def debugger_graph(work, private, sdk):
    tools = {name: sdk / ('nil' + name) for name in ('compiler', 'interpreter', 'debugger')}
    graph = {}
    def compile(label, source, binary=False, debug=True, native=False):
        artifact = work / (label + ('.native' if native else '.nil'))
        argv = [tools['compiler'], '-q', '--sdk', sdk, '-I', private / 'src',
                '-I', source.parent, '--no-bundle']
        if debug:
            argv.append('--debug-info')
        if binary:
            argv.append('--emit-binary-nil')
        if native:
            argv.append('--native')
        graph['compile-' + label] = list(map(str, [*argv, '-o', artifact, source]))
        return artifact
    fixture = private / 'probes/debugger/session.ny'
    for encoding in ('text', 'binary'):
        artifact = compile('fixture-' + encoding, fixture, encoding == 'binary')
        graph['fixture-interpreter-' + encoding] = list(map(str, [tools['interpreter'], artifact]))
    artifact = compile('fixture-native', fixture, debug=False, native=True)
    graph['fixture-native-execution'] = [str(artifact)]
    compile('fixture-unmapped', fixture, debug=False)
    for category, source, arguments in (
            ('mesh', private / 'src/mesh_renderer.ny', ['8', '8']),
            ('scene', private / 'src/scene_player.ny', ['8', '8', '3']),
            ('stdlib', private / 'probes/debugger/stdlib.ny', [])):
        for encoding in ('text', 'binary'):
            artifact = compile(category + '-' + encoding, source, encoding == 'binary')
            graph[category + '-interpreter-' + encoding] = list(map(str, [tools['interpreter'], artifact, *arguments]))
    compile('pause', private / 'probes/debugger/pause.ny')
    return graph


def session_graph(work, sdk):
    result = {}
    for encoding in ('text', 'binary'):
        result['jsonl-' + encoding] = [str(sdk / 'nildebugger'), '--json', 'run', str(work / ('fixture-' + encoding + '.nil'))]
        result['mesh-jsonl-' + encoding] = [str(sdk / 'nildebugger'), '--json', 'run', str(work / ('mesh-' + encoding + '.nil')), '--', '8', '8']
        for prefix in ('dap-', 'mesh-dap-', 'scene-dap-', 'stdlib-dap-'):
            result[prefix + encoding] = [str(sdk / 'nildebugger'), '--dap']
    for name in ('native-debug-boundary', 'unmapped-debug-boundary', 'dap-pause-terminate'):
        result[name] = [str(sdk / 'nildebugger'), '--dap']
    return result


def audit_debugger(report, private, sdk, origin, state, sources, deadline):
    require(report['schema'] == 1 and report['status'] == 'PASS' and report['completed'] is True and
            report['counts'] == {'PASS': 146, 'FAIL': 0}, 'Incomplete debugger146 result')
    names = [row['name'] for row in report['checks']]
    require(len(names) == len(set(names)) == 146 and all(row['status'] == 'PASS' for row in report['checks']) and
            hashlib.sha256(json.dumps(sorted(names), separators=(',', ':'), ensure_ascii=False).encode()).hexdigest() == DEBUGGER_NAMES,
            'Debugger named check inventory differs')
    require(report['source_hashes_before'] == report['source_hashes'] == sources, 'Debugger source copy changed')
    require(report['host'] == state['baseline']['host'], 'Debugger host differs')
    require(report['origin_path'] == str(origin) and report['origin_file_sha256_before'] ==
            report['origin_file_sha256_after'] == record(origin, deadline)['sha256'], 'Debugger origin differs')
    require(report['sdk_before'] == report['sdk_after'] == state, 'Debugger SDK seal differs')
    work = Path(report['work'])
    require(work.parent == private / 'work/debugger' and not work.is_symlink(), 'Debugger work escaped private copy')
    graph = debugger_graph(work, private, sdk)
    require([row['label'] for row in report['commands']] == list(graph) and len(graph) == 20,
            'Debugger command inventory differs')
    for row in report['commands']:
        name = row['label']
        require(row['command'] == graph[name] and row['cwd'] == str(work / name) and
                row['exit_code'] == 0 and row['timeout'] is False, 'Debugger command differs: ' + name)
        for stream in ('stdout', 'stderr'):
            path = work / name / (stream + '.txt')
            require(row[stream] == str(path) and record(path, deadline, 16 * 1024**2)['sha256'] == row[stream + '_sha256'],
                    'Debugger command raw stream differs')
    sessions = session_graph(work, sdk)
    transcripts = list(work.glob('*/transcript.jsonl'))
    require(len(transcripts) == 15 and {p.parent.name for p in transcripts} == set(sessions), 'Protocol inventory differs')
    for path in transcripts:
        record(path, deadline, 16 * 1024**2)
        messages = [json.loads(line) for line in path.read_bytes().splitlines()]
        require(messages[0] == {'direction': 'start', 'message': {'command': sessions[path.parent.name], 'cwd': str(path.parent)}} and
                messages[-1] == {'direction': 'exit', 'message': {'code': 0}}, 'Protocol ownership/exit differs')
    artifacts = {str(work / (label.removeprefix('compile-') + ('.native' if label == 'compile-fixture-native' else '.nil')))
                 for label in graph if label.startswith('compile-')}
    require(set(report['artifacts']) == artifacts, 'Compiled artifact inventory differs')
    for name, digest in report['artifacts'].items():
        require(record(Path(name), deadline)['sha256'] == digest, 'Compiled artifact changed')
    return {'checks': 146, 'commands': 20, 'protocol_sessions': 15, 'source_files': len(sources),
            'retained_work': inventory(work, deadline)}


def paths(sdk, origin, output):
    require(sdk.is_absolute() and origin.is_absolute() and output.is_absolute(), 'Absolute paths required')
    require(not sdk.is_symlink() and not origin.is_symlink(), 'SDK/origin symlink rejected')
    sdk, origin = sdk.resolve(strict=True), origin.resolve(strict=True)
    output = output.resolve()
    require(sdk.is_dir() and origin.is_file() and not origin.is_relative_to(sdk), 'Independent SDK and external origin required')
    require(not sdk.is_relative_to(ROOT) and not ROOT.is_relative_to(sdk), 'SDK must be relocated outside checkout')
    require(not output.exists() and not output.is_relative_to(sdk) and not sdk.is_relative_to(output) and
            not origin.is_relative_to(output), 'Output must be fresh and outside SDK/origin')
    protected = [ROOT / name for name in ('scripts', 'validation', 'tests')]
    require(not any(output.is_relative_to(p) or p.is_relative_to(output) for p in protected), 'Output overlaps source inputs')
    return sdk, origin, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--origin', type=Path, required=True)
    parser.add_argument('--expected-revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() in ('arm64', 'x86_64'), 'Native macOS host required')
    require(re.fullmatch('[0-9a-f]{40}', args.expected_revision), 'Exact source SHA required')
    sdk, origin, output = paths(args.sdk, args.origin, args.output)
    output.mkdir(parents=True)
    started = time.monotonic(); deadline = started + TOTAL_SECONDS
    phases, problems = [], []
    report = {'schema': 'nil.macos-installed-debugger.v1', 'status': 'FAIL', 'phases': phases, 'problems': problems,
              'sdk': str(sdk), 'origin': str(origin), 'expected_revision': args.expected_revision,
              'host': {'system': platform.system(), 'machine': platform.machine()},
              'limits': {'whole_seconds': TOTAL_SECONDS, 'debugger_seconds': 1200, 'checkpoint_seconds': 120},
              'scope': 'Installed JSONL/DAP and same-process execution checkpoints; no editor UI, native debugging, SDK activation or release qualification.'}
    files = project_sources() + [Path(__file__), ROOT / 'tests/tools/ordinary_io_parity_contract.py',
                                ROOT / 'tests/interpreter/ai_checkpoint_contract.py']
    before = seal = anchor = state = None
    private = output / 'debugger-consumer'
    try:
        before = {str(p.relative_to(ROOT)): record(p, deadline) for p in files}
        seal, anchor = inventory(sdk, deadline), record(origin, deadline, 1024**2)
        verify = load(PROJECT / 'scripts/sdk_integrity.py', 'macos_debugger_integrity').verify
        state = verify(sdk, origin)
        verify_origin(state, sdk, args.expected_revision, platform.machine())
        report.update(source_before=before, sdk_before=seal, origin_before=anchor, sdk_integrity_before=state)
        for source in project_sources():
            target = private / source.relative_to(PROJECT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            require(record(target, deadline)['sha256'] == before[str(source.relative_to(ROOT))]['sha256'], 'Copied debugger source differs')
        checkpoint = output / 'checkpoints/ai_checkpoint_contract.py'
        checkpoint.parent.mkdir()
        shutil.copyfile(ROOT / 'tests/interpreter/ai_checkpoint_contract.py', checkpoint)
        require(record(checkpoint, deadline)['sha256'] == before['tests/interpreter/ai_checkpoint_contract.py']['sha256'], 'Copied checkpoint source differs')
        support = load(ROOT / 'tests/tools/ordinary_io_parity_contract.py', 'macos_debugger_capture')
        owner = support.load_ownership(ROOT)
        # Import the same clean environment used by the copied debugger consumer.
        sys.path.insert(0, str(PROJECT / 'scripts'))
        try:
            consumer = load(PROJECT / 'scripts/validate_debugger.py', 'macos_debugger_consumer')
        finally:
            sys.path.pop(0)
        environment = consumer.clean_env()
        environment.update(PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(output / 'temporary'))
        (output / 'temporary').mkdir()
        report['environment_policy'] = 'Apple system tools, framework/loader/toolchain overrides removed; private TMPDIR.'
        commands = [
            ('debugger', [sys.executable, '-B', '-E', '-s', str(private / 'scripts/validate_debugger.py'),
                          '--sdk', str(sdk), '--sdk-origin', str(origin), '--timeout', '30',
                          '--report', str(output / 'debugger-report.json')], 1200, private),
            ('execution-checkpoints', [sys.executable, '-B', '-E', '-s', str(checkpoint), '--interpreter', str(sdk / 'nilinterpreter')], 120, checkpoint.parent)]
        for name, argv, bound, cwd in commands:
            remaining = deadline - time.monotonic() - 20
            require(remaining > 0, 'Whole deadline exhausted')
            row = support.run_process(owner, argv, cwd, environment, output, name, min(bound, remaining))
            phases.append(row)
            write_json(output / (name + '-phase.json'), row)
            require(row['cleanup_verified'], 'Owned cleanup unverified; further commands stopped')
            if row['status'] != 'PASS':
                problems.append(name + ' phase failed; raw evidence retained')
        require(not problems, 'One or more installed acceptance phases failed')
        sources = {str(p.relative_to(PROJECT)): before[str(p.relative_to(ROOT))]['sha256'] for p in project_sources()}
        report['debugger'] = audit_debugger(read_json(output / 'debugger-report.json'), private, sdk, origin, state, sources, deadline)
        require((output / 'execution-checkpoints.stdout').read_bytes() == CHECKPOINT_OUTPUT and
                (output / 'execution-checkpoints.stderr').read_bytes() == b'', 'Checkpoint output contract differs')
        report['execution_checkpoints'] = {'status': 'PASS', 'test_source': before['tests/interpreter/ai_checkpoint_contract.py'],
            'scope': 'Same-process execution capture/mutation/reusable restore/continuation, rejected requests and explicit-exit cleanup.',
            'raw_ipc_frames_retained': False}
    except (Exception, KeyboardInterrupt) as error:
        problems.append(type(error).__name__ + ': ' + str(error))
    finally:
        try:
            after = {str(p.relative_to(ROOT)): record(p, deadline) for p in files}
            report['source_after'] = after
            report['source_unchanged'] = before is not None and before == after
            report['sdk_after'] = inventory(sdk, deadline)
            report['origin_after'] = record(origin, deadline, 1024**2)
            report['sdk_unchanged'] = seal is not None and seal == report['sdk_after']
            report['origin_unchanged'] = anchor is not None and anchor == report['origin_after']
            if state is not None:
                report['sdk_integrity_after'] = verify(sdk, origin)
                require(report['sdk_integrity_after'] == state, 'SDK integrity result changed')
            require(report['source_unchanged'] and report['sdk_unchanged'] and report['origin_unchanged'], 'Source, SDK or external anchor changed')
            if private.exists():
                report['retained_consumer'] = inventory(private, deadline)
                for source in project_sources():
                    copied = record(private / source.relative_to(PROJECT), deadline)
                    require(copied['sha256'] == before[str(source.relative_to(ROOT))]['sha256'], 'Copied debugger source changed')
            if (output / 'checkpoints/ai_checkpoint_contract.py').exists():
                require(record(output / 'checkpoints/ai_checkpoint_contract.py', deadline)['sha256'] ==
                        before['tests/interpreter/ai_checkpoint_contract.py']['sha256'], 'Copied checkpoint source changed')
            for phase in phases:
                for stream in ('stdout', 'stderr'):
                    actual = record(output / phase[stream], deadline, 8 * 1024**2)
                    require({k: actual[k] for k in ('bytes', 'sha256')} == phase[stream + '_record'], 'Owned raw stream changed')
        except Exception as error:
            problems.append('Final seals: ' + str(error))
        report['cleanup_verified'] = bool(phases) and all(p['cleanup_verified'] for p in phases)
        report['all_required_phases_completed'] = [p['name'] for p in phases] == ['debugger', 'execution-checkpoints']
        report['elapsed_seconds'] = time.monotonic() - started
        report['status'] = 'PASS' if not problems and report['cleanup_verified'] and report['all_required_phases_completed'] and report['elapsed_seconds'] <= TOTAL_SECONDS else 'FAIL'
        write_json(output / 'report.json', report)
    print(json.dumps({'status': report['status'], 'report': str(output / 'report.json'), 'problems': problems}))
    return int(report['status'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
