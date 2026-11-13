#!/usr/bin/env python3
"""Replay installed acceptance from a sealed SDK; never build or publish it."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import stat
import subprocess
import sys
import tarfile
import time
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from sdk_qualification import source_identity

GiB = 1024**3
MAX_ENTRIES = 20000
TOTAL_SECONDS = 2100
REPOSITORY = 'exec-software/exec-nil-framework'
TOOLS = ('nilcompiler', 'nilinterpreter', 'nilassembler', 'nillinker', 'nillsp',
         'nildebugger', 'nilschema', 'nilagent', 'nilbuild', 'aperture', 'niltest')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def remaining(deadline, maximum=30):
    value = deadline - time.monotonic()
    require(value > 0, 'Replay deadline expired')
    return min(maximum, value)


def record(path, deadline, maximum=GiB):
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0))
    digest, count = hashlib.sha256(), 0
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= maximum, 'Nonregular or excessive evidence input')
        while block := stream.read(1024**2):
            remaining(deadline)
            count += len(block)
            require(count <= maximum, 'Input grew beyond bound')
            digest.update(block)
        after = os.fstat(stream.fileno())
        identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_mode)
        require(identity(before) == identity(after) and count == after.st_size, 'Input changed while reading')
    return {'bytes': count, 'sha256': digest.hexdigest()}


def read_json(path, deadline):
    expected = record(path, deadline, 32 * 1024**2)
    raw = path.read_bytes()
    require(len(raw) == expected['bytes'] and hashlib.sha256(raw).hexdigest() == expected['sha256'], 'JSON changed')
    return json.loads(raw)


def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')


def relative_name(raw):
    path = PurePosixPath(raw)
    require(raw and not path.is_absolute() and '..' not in path.parts and '\\' not in raw and
            not any(ord(c) < 32 for c in raw) and path.as_posix() == raw.rstrip('/') and
            path.as_posix() != '.', 'Unsafe or noncanonical archive/evidence path')
    return path.as_posix()


def namespace(root, deadline, *, modes=False):
    require(root.is_dir() and not root.is_symlink(), 'Expected a real evidence directory')
    result = {}
    count = 0
    for path in sorted(root.rglob('*')):
        remaining(deadline)
        count += 1
        require(count <= MAX_ENTRIES, 'Namespace entry bound exceeded')
        mode = path.lstat().st_mode
        require(stat.S_ISDIR(mode) or stat.S_ISREG(mode), 'Namespace contains links or special files')
        if stat.S_ISREG(mode):
            row = record(path, deadline)
            if modes:
                row['mode'] = stat.S_IMODE(mode)
            result[path.relative_to(root).as_posix()] = row
    require(bool(result), 'Empty evidence namespace')
    return result


def bind_transport(metadata, archive_record, revision, arch):
    artifact, run = metadata['artifact'], metadata['run']
    require(run['repository']['full_name'] == REPOSITORY and run['status'] == 'completed' and
            run['path'] == '.github/workflows/macos-sdk.yml' and run['event'] == 'workflow_dispatch' and
            run['head_sha'] == revision and artifact['workflow_run']['head_sha'] == revision and
            artifact['workflow_run']['id'] == run['id'], 'Artifact/run/source binding differs')
    require(type(artifact['id']) is int and artifact['id'] > 0 and artifact['expired'] is False and
            type(artifact['size_in_bytes']) is int and 0 < artifact['size_in_bytes'] <= GiB,
            'Invalid or expired artifact identity')
    match = re.fullmatch('macos-development-sdk-' + re.escape(arch) + '-' + revision + r'-([1-9][0-9]*)', artifact['name'])
    require(match is not None and type(run['run_attempt']) is int and
            str(run['run_attempt']) == match[1], 'Artifact name/source/architecture/attempt differs')
    require(artifact['digest'] == 'sha256:' + archive_record['sha256'], 'Downloaded ZIP differs from official digest')
    require(artifact['size_in_bytes'] == archive_record['bytes'], 'Downloaded ZIP size differs from official metadata')
    return {'artifact_id': artifact['id'], 'artifact_name': artifact['name'], 'run_id': str(run['id']),
            'attempt': match[1], 'sdk_revision': revision, 'architecture': arch,
            'original_run_conclusion': run['conclusion'], 'archive': archive_record}


def extract_zip(archive, destination, deadline):
    require(destination.is_dir() and not any(destination.iterdir()), 'ZIP destination must be fresh')
    with zipfile.ZipFile(archive) as source:
        members = source.infolist()
        require(0 < len(members) <= MAX_ENTRIES, 'ZIP member count exceeds bound')
        names, total = set(), 0
        for item in members:
            name = relative_name(item.filename)
            require(name not in names, 'Duplicate ZIP path')
            names.add(name)
            kind = stat.S_IFMT(item.external_attr >> 16)
            require(kind in ((0, stat.S_IFDIR) if item.is_dir() else (0, stat.S_IFREG)), 'ZIP links/special files rejected')
            require(not item.flag_bits & 1 and 0 <= item.file_size <= GiB, 'Encrypted or excessive ZIP member')
            total += item.file_size
            require(total <= 2 * GiB, 'ZIP expanded size exceeds bound')
        for item in members:
            remaining(deadline)
            path = destination.joinpath(*PurePosixPath(relative_name(item.filename)).parts)
            if item.is_dir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            copied = 0
            with source.open(item) as incoming, path.open('xb') as outgoing:
                while block := incoming.read(1024**2):
                    remaining(deadline)
                    copied += len(block)
                    require(copied <= item.file_size, 'ZIP member exceeds declared size')
                    outgoing.write(block)
            require(copied == item.file_size, 'Truncated ZIP member')
            path.chmod(0o600)  # Artifact evidence is data, never executed.
    return {'entries': len(members), 'expanded_bytes': total}


def verify_evidence(directory, binding, deadline):
    observed = namespace(directory, deadline)
    seal = read_json(directory / 'evidence.json', deadline)
    identity = read_json(directory / 'identity.json', deadline)
    require(seal['source_unchanged'] is True and seal['nonregular_evidence_entries'] == [] and
            seal['relocated_candidate_unchanged'] is True and seal['release_declaration'] is False and
            seal['platform_parity'] is False, 'Original collector did not preserve an unchanged development candidate')
    require(set(observed) == set(seal['files']) | {'evidence.json'}, 'Artifact namespace differs from collector')
    for name, expected in seal['files'].items():
        require(relative_name(name) == name and observed[name] == expected, 'Artifact evidence file differs')
    require(identity['source'] == seal['source'] and identity['source']['revision'] == binding['sdk_revision'] and
            identity['source']['dirty'] is False and identity['architecture'] == binding['architecture'] and
            identity['source']['fingerprint'] == hashlib.sha256(binding['sdk_revision'].encode() + b'\0').hexdigest() and
            identity['run_id'] == binding['run_id'] and identity['attempt'] == binding['attempt'],
            'Original source/run/host identity differs')
    for key, name in (('helper', 'scripts/ci/macos-sdk.py'), ('workflow', '.github/workflows/macos-sdk.yml')):
        raw = subprocess.check_output(['git', 'show', binding['sdk_revision'] + ':' + name], cwd=ROOT,
                                      timeout=remaining(deadline, 20))
        require(identity[key] == {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}, 'Original workflow/helper blob differs')
    return observed


def extract_sdk(archive, sdk, expected, deadline):
    require(sdk.is_dir() and not any(sdk.iterdir()), 'SDK extraction destination must be fresh')
    require(isinstance(expected, dict) and 0 < len(expected) < MAX_ENTRIES, 'Invalid SDK payload inventory')
    for name, value in expected.items():
        require(relative_name(name) == name and type(value['bytes']) is int and 0 <= value['bytes'] <= GiB and
                type(value['mode']) is int and value['mode'] & ~0o777 == 0 and
                re.fullmatch('[0-9a-f]{64}', value['sha256']), 'Invalid expected SDK payload record')
    seen, files, total = set(), {}, 0
    with tarfile.open(archive, 'r|gz') as source:
        for item in source:
            remaining(deadline)
            name = relative_name(item.name)
            require(name not in seen and len(seen) < MAX_ENTRIES, 'Duplicate/excess SDK tar entry')
            seen.add(name)
            path = PurePosixPath(name)
            require(path.parts[0] == 'sdk' and (item.isdir() or item.isfile()), 'SDK tar links/special/root rejected')
            relative = path.relative_to('sdk').as_posix()
            target = sdk.joinpath(*path.parts[1:])
            if item.isdir():
                require(name == 'sdk' or any(p.startswith(relative + '/') for p in expected), 'Unexpected SDK directory')
                target.mkdir(parents=True, exist_ok=True)
                continue
            require(relative in expected and relative not in files and item.size == expected[relative]['bytes'] and
                    item.mode == expected[relative]['mode'], 'SDK tar header differs from sealed payload')
            total += item.size
            require(total <= 2 * GiB, 'SDK expanded size exceeds bound')
            target.parent.mkdir(parents=True, exist_ok=True)
            digest, copied = hashlib.sha256(), 0
            with source.extractfile(item) as incoming, target.open('xb') as outgoing:
                while block := incoming.read(1024**2):
                    remaining(deadline)
                    copied += len(block)
                    require(copied <= item.size, 'SDK member exceeds declared size')
                    digest.update(block); outgoing.write(block)
            require(copied == item.size and digest.hexdigest() == expected[relative]['sha256'], 'SDK member bytes differ')
            target.chmod(item.mode)
            files[relative] = {**record(target, deadline), 'mode': stat.S_IMODE(target.stat().st_mode)}
    require(files == expected, 'SDK extraction inventory differs')
    return files


def verify_candidate(directory, sdk, binding, deadline):
    revision, arch = binding['sdk_revision'], binding['architecture']
    relocation = read_json(directory / 'relocation.json', deadline)
    capture = read_json(directory / 'candidate-capture.json', deadline)
    manifest = read_json(directory / 'candidate-manifest.json', deadline)
    snapshot = read_json(directory / 'sdk-build-evidence.json', deadline)
    require(capture == {'archive': 'development-candidate.tar.gz', 'error': None,
                        'path': relocation['new_path'], 'seal_unchanged': True} and
            relocation['old_path_absent'] is True, 'Original candidate capture/relocation failed')
    require(manifest['source'] == {'revision': revision, 'worktree_dirty': False} and
            manifest['host'] == {'system': 'Darwin', 'machine': arch} and
            manifest['build'] == {'llvm': 'ON', 'type': 'Release'} and
            manifest['qualification']['mode'] == 'development', 'Unsupported SDK source/host/build/scope')
    require(snapshot['source'] == {'revision': revision, 'dirty': False,
            'fingerprint': hashlib.sha256(revision.encode() + b'\0').hexdigest()}, 'Pre-test source snapshot differs')
    payload = extract_sdk(directory / capture['archive'], sdk, relocation['payload'], deadline)
    require(record(sdk / 'manifest.json', deadline) == record(directory / 'candidate-manifest.json', deadline),
            'Extracted manifest differs from original candidate')
    files = manifest['payload']['files']
    require(manifest['payload']['file_count'] == len(files) == len({row['path'] for row in files}) and
            {row['path'] for row in files} == set(payload) - {'manifest.json'}, 'Manifest payload inventory differs')
    for row in files:
        require({key: payload[row['path']][key] for key in ('bytes', 'sha256')} ==
                {key: row[key] for key in ('bytes', 'sha256')}, 'Manifest payload digest differs')
    require(set(snapshot['tools']) == set(TOOLS) and len(manifest['tools']) == len(TOOLS) and
            {row['name'] for row in manifest['tools']} == set(TOOLS), 'Tool inventory differs')
    for row in manifest['tools']:
        require(row['path'] == row['name'] and row['sha256'] == snapshot['tools'][row['name']] ==
                payload[row['path']]['sha256'] and payload[row['path']]['mode'] & 0o111, 'Tool snapshot or executable mode differs')
    for field, key in (('native_runtime', 'runtime_sha256'), ('native_shared_runtime', 'shared_runtime_sha256')):
        row = manifest[field]
        require(row['sha256'] == snapshot[key] == payload[row['path']]['sha256'], 'Runtime pre-test digest differs')
    origin = read_json(directory / 'relocation-origin.json', deadline)
    require(origin['source_revision'] == revision and origin['source_manifest_sha256'] ==
            payload['manifest.json']['sha256'], 'Original relocation manifest anchor differs')
    inventory = verify_ctest(directory, snapshot, revision, deadline)
    return payload, origin, inventory


def verify_ctest(directory, snapshot, revision, deadline):
    collector_name = 'scripts/ci/macos-evidence.py'
    old_collector = subprocess.check_output(['git', 'show', revision + ':' + collector_name], cwd=ROOT,
                                             timeout=remaining(deadline, 20))
    require((ROOT / collector_name).read_bytes() == old_collector, 'Replay requires the original unchanged CTest collector')
    collector = load('sdk_replay_ctest', collector_name)
    registration = read_json(directory / 'registration.json', deadline)
    require(registration.get('kind') == 'ctestInfo' and isinstance(registration.get('tests'), list) and
            all(isinstance(row, dict) and isinstance(row.get('name'), str) and row['name']
                for row in registration['tests']), 'Malformed CTest registration')
    junit = directory / 'ctest-results.xml'
    require(record(junit, deadline)['bytes'] <= 32 * 1024**2, 'JUnit exceeds bound')
    raw = junit.read_bytes()
    require(b'<!DOCTYPE' not in raw and b'<!ENTITY' not in raw, 'Unexpected XML entities')
    inventory = collector.summarize(registration, ET.fromstring(raw))
    require(not collector.inventory_errors(inventory) and sorted(row['name'] for row in registration['tests']) ==
            snapshot['registered_tests'], 'Incomplete CTest inventory or snapshot registration differs')
    captured = read_json(directory / 'ctest-inventory.json', deadline)
    require(captured['errors'] == [] and all(captured[key] == value for key, value in inventory.items()),
            'Recorded CTest summary differs from raw inventory')
    return inventory


def external_prerequisites(directory, deadline):
    declared = read_json(directory / 'loader-dependencies.json', deadline)['external_prerequisites']
    require(isinstance(declared, dict) and 0 < len(declared) <= 256, 'Invalid direct dependency inventory')
    observed = {}
    for name, item in declared.items():
        path = Path(name)
        require(path.is_absolute() and item['declared_name'] == name, 'Invalid declared dependency path')
        system = name.startswith(('/usr/lib/', '/System/Library/'))
        expected_kind = 'Apple system/shared-cache library' if system else 'external development prerequisite'
        require(item['kind'] == expected_kind, 'Dependency category differs')
        target = path.resolve(strict=True) if path.exists() or path.is_symlink() else None
        require(system or target is not None, 'External prerequisite is absent')
        current = {'declared_name': name, 'canonical_target': str(target) if target else None,
                   'kind': expected_kind, 'file': record(target, deadline) if target else None}
        require(current == item, 'Direct prerequisite differs from original native SDK observation: ' + name)
        observed[name] = current
    return {'scope': 'Direct recorded prerequisites only; no dyld shared-cache backing or transitive closure claim',
            'entries': observed}


def fresh_directories(output, work, inputs):
    require(not output.is_symlink() and not work.is_symlink(), 'Replay directories may not be symlinks')
    output, work = output.resolve(), work.resolve()
    for path in (output, work):
        require(not path.exists() and path != ROOT and path not in ROOT.parents, 'Fresh replay directory required')
        require(not any(path == value or path in value.parents or value in path.parents for value in inputs),
                'Replay directory overlaps artifact inputs')
    require(ROOT not in work.parents and output != work and output not in work.parents and work not in output.parents,
            'Work must be outside checkout and disjoint from evidence')
    require(ROOT not in output.parents or (ROOT / 'build') in output.parents, 'Evidence must be outside source inputs')
    output.mkdir(parents=True, mode=0o700); work.mkdir(parents=True, mode=0o700)
    return output, work


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('artifact-zip', 'artifact-metadata', 'work-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('sdk-revision', 'helper-revision'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--architecture', choices=('arm64', 'x86_64'), required=True)
    parser.add_argument('--desktop', action='store_true', help='Also accept both installed UI roots, desktop DAP and full editor')
    for name in ('desktop-helpers', 'editor', 'node'):
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args()
    require(all((args.desktop_helpers, args.editor, args.node)) == args.desktop and
            (args.desktop or not any((args.desktop_helpers, args.editor, args.node))),
            'Desktop replay requires all three explicit helper/editor/node inputs')
    require(sys.platform == 'darwin' and platform.machine() == args.architecture, 'Native matching macOS host required')
    for value in (args.sdk_revision, args.helper_revision):
        require(re.fullmatch('[0-9a-f]{40}', value), 'Exact source revision required')
    require(not args.artifact_zip.is_symlink() and not args.artifact_metadata.is_symlink(), 'Artifact inputs may not be links')
    archive, metadata_path = args.artifact_zip.resolve(strict=True), args.artifact_metadata.resolve(strict=True)
    output, work = fresh_directories(args.output, args.work_dir, (archive, metadata_path))
    total_seconds = TOTAL_SECONDS + (2500 if args.desktop else 0)
    started = time.monotonic(); deadline = started + total_seconds
    report = {'schema': 'nil.macos-sdk-debugger-replay.v1', 'status': 'FAIL', 'problems': [], 'phases': [],
              'sdk_revision': args.sdk_revision, 'helper_revision': args.helper_revision, 'architecture': args.architecture,
              'release_declaration': False, 'platform_parity': False,
              'desktop_requested': args.desktop, 'whole_seconds': total_seconds,
              'scope': ('Installed debugger and same-process execution checkpoints' +
                        ('; installed nilstd/nillib UI, desktop DAP and full editor' if args.desktop else ' only') +
                        '; no SDK build, activation, source provider/lifecycle-depth or release qualification')}
    before = payload = original = dependencies = None
    sdk, copied = work / 'SDK replay λ', work / 'artifact'
    sdk.mkdir(); copied.mkdir()
    try:
        report['helper_source'] = source_identity(ROOT)
        require(report['helper_source']['revision'] == args.helper_revision and not report['helper_source']['dirty'],
                'Replay helper checkout is not the exact clean requested source')
        before = {'zip': record(archive, deadline), 'metadata': record(metadata_path, deadline)}
        binding = bind_transport(read_json(metadata_path, deadline), before['zip'], args.sdk_revision, args.architecture)
        report['transport'] = binding
        report['zip_extraction'] = extract_zip(archive, copied, deadline - 20)
        original = verify_evidence(copied, binding, deadline - 20)
        payload, origin, inventory = verify_candidate(copied, sdk, binding, deadline - 20)
        report['original_ctest'] = inventory
        report['candidate_manifest'] = record(sdk / 'manifest.json', deadline)
        report['sdk_files'] = len(payload)
        dependencies = external_prerequisites(copied, deadline - 20)
        report['prerequisites_before'] = dependencies
        anchor = output / 'sdk-origin.json'; save(anchor, origin)
        support = load('sdk_replay_capture', 'tests/tools/ordinary_io_parity_contract.py')
        environment = load('sdk_replay_environment', 'scripts/check-sdk-routes.py').consumer_environment()
        for name in ('GH_TOKEN', 'GITHUB_TOKEN', 'ACTIONS_RUNTIME_TOKEN'):
            environment.pop(name, None)
        row = support.run_process(support.load_ownership(ROOT), [sys.executable, '-B', str(ROOT / 'scripts/ci/macos-debugger.py'),
            '--sdk', str(sdk), '--origin', str(anchor), '--expected-revision', args.sdk_revision,
            '--output', str(output / 'debugger')], work, environment, output, 'installed-debugger', remaining(deadline - 20, 1500))
        report['phases'].append(row)
        require(row['status'] == 'PASS' and row['cleanup_verified'], 'Installed debugger/checkpoint acceptance failed')
        acceptance = read_json(output / 'debugger/report.json', deadline)
        require(acceptance['status'] == 'PASS' and acceptance['expected_revision'] == args.sdk_revision,
                'Debugger result is not bound to the selected SDK')
        report['debugger_report'] = record(output / 'debugger/report.json', deadline)
        if args.desktop:
            row = support.run_process(support.load_ownership(ROOT), [sys.executable, '-B', str(ROOT / 'scripts/ci/macos-desktop.py'),
                '--sdk', str(sdk), '--origin', str(anchor), '--expected-revision', args.sdk_revision,
                '--helpers', str(args.desktop_helpers.resolve(strict=True)), '--editor', str(args.editor.resolve(strict=True)),
                '--node', str(args.node.resolve(strict=True)), '--output', str(output / 'desktop')],
                work, environment, output, 'installed-desktop', remaining(deadline - 20, 2450))
            report['phases'].append(row)
            require(row['status'] == 'PASS' and row['cleanup_verified'], 'Installed desktop/editor acceptance failed')
            desktop = read_json(output / 'desktop/report.json', deadline)
            require(desktop['status'] == 'PASS' and desktop['expected_revision'] == args.sdk_revision and
                    desktop['sdk'] == str(sdk) and desktop['inputs_unchanged'] is True and desktop['cleanup_verified'] is True,
                    'Desktop result is not bound to the unchanged selected SDK')
            report['desktop_report'] = record(output / 'desktop/report.json', deadline)
        report['status'] = 'PASS'
    except Exception as error:
        report['problems'].append(type(error).__name__ + ': ' + str(error))
    finally:
        checks = {}
        for name, check in (
            ('artifact_inputs', lambda: before is not None and before == {'zip': record(archive, deadline), 'metadata': record(metadata_path, deadline)}),
            ('copied_evidence', lambda: original is not None and namespace(copied, deadline) == original),
            ('sdk_payload', lambda: payload is not None and namespace(sdk, deadline, modes=True) == payload),
            ('direct_prerequisites', lambda: dependencies is not None and external_prerequisites(copied, deadline) == dependencies),
            ('helper_source', lambda: source_identity(ROOT) == report.get('helper_source')),
        ):
            try:
                require(check(), 'Unverified or changed replay input: ' + name)
                checks[name] = {'status': 'PASS'}
            except Exception as error:
                checks[name] = {'status': 'FAIL', 'error': str(error)}
                report['problems'].append(name + ': ' + str(error))
        report['after_checks'] = checks
        report['cleanup_verified'] = bool(report['phases']) and all(row['cleanup_verified'] for row in report['phases'])
        report['elapsed_seconds'] = time.monotonic() - started
        if report['problems'] or not report['cleanup_verified'] or report['elapsed_seconds'] > total_seconds:
            report['status'] = 'FAIL'
        save(output / 'report.json', report)
    print(json.dumps({key: report[key] for key in ('status', 'sdk_revision', 'helper_revision', 'problems')}))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
