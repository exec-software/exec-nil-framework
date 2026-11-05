#!/usr/bin/env python3
"""Bounded Darwin host diagnostics using private fixtures, without a framework build."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys
import tempfile
import threading
import time
import traceback


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


SUITES = {
    'ordinary-native': ('tests/tools/test_ordinary_io_native_contract.py', 30),
    'ordinary-parity': ('tests/tools/test_ordinary_io_parity_contract.py', 45),
    'process-identity': ('tests/tools/test_process_contract_support.py', 30),
    'process-consumer': ('validation/ny-3d/scripts/test_debugger_consumer.py', 90),
    'parallel-process': ('tests/tools/parallel_process_contract.py', 240),
    'sdk-publication': ('tests/publish_sdk_concurrency.py', 30),
}


def run_suite(name, output):
    """Use the existing owned-process capture and the suite's CTest deadline."""
    repository = Path(__file__).resolve().parents[2]
    support_path = repository / 'tests/tools/ordinary_io_parity_contract.py'
    spec = importlib.util.spec_from_file_location('host_probe_runner', support_path)
    support = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(support)
    ownership = support.load_ownership(repository)
    relative, timeout = SUITES[name]
    source_files = [Path(__file__).resolve(), support_path, repository / relative,
                    repository / 'validation/ny-3d/scripts/debugger_process.py',
                    repository / 'tests/tools/process_contract_support.py']
    pins = {str(path.relative_to(repository)): sha(path) for path in source_files}
    argv = [sys.executable, '-B', str(repository / relative)]
    if name == 'parallel-process':
        argv += ['--root', str(repository), '--cxx', '/usr/bin/clang++']
    elif name == 'sdk-publication':
        argv += ['--publisher', str(repository / 'scripts/publish-sdk.py')]
        source_files.append(repository / 'scripts/publish-sdk.py')
        pins = {str(path.relative_to(repository)): sha(path) for path in source_files}
    else:
        argv.append('-v')
    output.mkdir(exist_ok=False)
    row = support.run_process(ownership, argv, repository, os.environ.copy(), output, name, timeout)
    row['scope'] = 'Focused host regression only; no SDK qualification'
    row['source_sha256'] = pins
    row['source_unchanged'] = pins == {str(path.relative_to(repository)): sha(path) for path in source_files}
    if not row['source_unchanged']:
        row['status'] = 'FAIL'
    (output / 'report.json').write_text(json.dumps(row, indent=2) + '\n')
    print(json.dumps(row, indent=2))
    return 0 if row['status'] == 'PASS' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publisher', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'publish-sdk.py')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--suite', choices=tuple(SUITES))
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('Darwin-only diagnostic; no platform substitution')
    if args.suite:
        return run_suite(args.suite, args.output.absolute())
    publisher_path = args.publisher.resolve(strict=True)
    source_sha = sha(publisher_path)
    probe_sha = sha(Path(__file__).resolve())
    output = args.output.absolute()
    output.mkdir(exist_ok=False)
    spec = importlib.util.spec_from_file_location('publisher', publisher_path)
    publisher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publisher)
    api = ctypes.CDLL(None, use_errno=True)
    api.renamex_np.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    api.renamex_np.restype = ctypes.c_int
    api.linkat.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    api.linkat.restype = ctypes.c_int

    def swap(source, destination):
        # Darwin stdio.h RENAME_SWAP = 0x2. Unsupported API/filesystem fails.
        if api.renamex_np(os.fsencode(source), os.fsencode(destination), 2):
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(destination))

    cases = []
    whole_start = time.monotonic()
    for mode in ('production-replace', 'unretained-replace', 'swap-unlink', 'swap-retain', 'hardlink-retain'):
        started = time.monotonic()
        row = dict(mode=mode, replacements=0, reads=0, saw_old=False, saw_new=False,
                   failures=[], deadline_seconds=15, maximum_replacements=5000,
                   role=('production' if mode == 'production-replace' else
                         'negative-baseline' if mode in ('unretained-replace', 'swap-unlink') else 'comparison'))
        with tempfile.TemporaryDirectory(prefix='nil-publish-diagnostic-') as raw:
            root = Path(raw).resolve(strict=True) / 'space λ'
            root.mkdir()
            for name in ('old', 'new'):
                (root / name).mkdir()
                (root / name / 'manifest.json').write_bytes((name + '\n').encode())
            expected = {b'old\n', b'new\n'}
            active = root / 'sdk'
            active.symlink_to('old', target_is_directory=True)
            retired = root / 'retired'
            retired.mkdir()
            ready, stop = threading.Event(), threading.Event()

            def reader():
                while not stop.is_set():
                    try:
                        data = (active / 'manifest.json').read_bytes()
                        if data not in expected:
                            raise AssertionError('incomplete generation: ' + repr(data))
                        row['reads'] += 1
                        row['saw_old'] |= data == b'old\n'
                        row['saw_new'] |= data == b'new\n'
                        ready.set()
                    except BaseException:
                        row['failures'].append(dict(operation='reader-open-read', traceback=traceback.format_exc()))
                        stop.set()
                        ready.set()
                        break

            worker = threading.Thread(target=reader, daemon=True)
            parent_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            retired_fd = os.open(retired, os.O_RDONLY | os.O_DIRECTORY)
            with (active / 'manifest.json').open('rb') as held:
                worker.start()
                try:
                    if not ready.wait(3):
                        raise AssertionError('reader did not start')
                    for index in range(5000):
                        if stop.is_set():
                            break
                        if time.monotonic() - started > 15 or time.monotonic() - whole_start > 80:
                            raise TimeoutError('bounded diagnostic deadline reached')
                        target = 'new' if index % 2 == 0 else 'old'
                        if mode == 'production-replace':
                            publisher.replace_link(active, target)
                        else:
                            temporary = root / 'next-link'
                            temporary.symlink_to(target, target_is_directory=True)
                            if mode == 'unretained-replace':
                                os.replace(temporary, active)
                            elif mode == 'hardlink-retain':
                                # macOS15 linkat accepts AT_SYMLINK_FOLLOW or 0;
                                # 0 selects NOFOLLOW for the final component.
                                # AT_SYMLINK_NOFOLLOW_ANY is not accepted by
                                # linkat on this target and must not be sent here.
                                name = str(index).encode()
                                if api.linkat(parent_fd, b'sdk', retired_fd, name, 0):
                                    error = ctypes.get_errno()
                                    raise OSError(error, os.strerror(error), str(active))
                                anchor = retired / str(index)
                                before, pinned = active.lstat(), anchor.lstat()
                                if not anchor.is_symlink() or (before.st_dev, before.st_ino) != (pinned.st_dev, pinned.st_ino):
                                    raise AssertionError('anchor did not retain the old symlink inode')
                                os.replace(temporary, active)
                            else:
                                swap(temporary, active)
                                if mode == 'swap-unlink':
                                    temporary.unlink()
                                else:
                                    os.rename(temporary, retired / str(index))
                        row['replacements'] += 1
                    row['held_old_bytes_unchanged'] = held.read() == b'old\n'
                except BaseException:
                    row['failures'].append(dict(operation='writer', traceback=traceback.format_exc()))
                finally:
                    stop.set()
                    worker.join(3)
                    os.close(parent_fd)
                    os.close(retired_fd)
                if worker.is_alive():
                    # Do not remove a fixture while a reader still owns it.
                    (output / 'stuck-reader.json').write_text(json.dumps(row, indent=2))
                    os._exit(2)
            row['retained_links'] = len(list(retired.iterdir()))
            production_retention = publisher.darwin_retention_directory(active)
            row['production_retained_links'] = (len(list(production_retention.iterdir()))
                                                if production_retention.is_dir() else 0)
            row['status'] = ('PASS' if not row['failures'] and row['replacements'] == 5000 and
                             row['saw_old'] and row['saw_new'] and row['held_old_bytes_unchanged'] else 'FAIL')
        row['elapsed_seconds'] = time.monotonic() - started
        cases.append(row)
        (output / (mode + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    report = dict(scope='Private scratch publication diagnosis, not SDK acceptance',
                  platform=platform.platform(), python=sys.version, publisher=str(publisher_path),
                  source_sha256=source_sha, probe_sha256=probe_sha,
                  source_unchanged=(source_sha == sha(publisher_path) and
                                    probe_sha == sha(Path(__file__).resolve())),
                  cases=cases, deadline_seconds=80, elapsed_seconds=time.monotonic() - whole_start)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({row['mode']: {'status': row['status'], 'reads': row['reads'],
                                   'replacements': row['replacements']} for row in cases}))
    # A diagnostic outcome is not acceptance: preserve every mode's failure
    # and fail the step after all modes have had their bounded opportunity.
    return 0 if report['source_unchanged'] and all(row['status'] == 'PASS' for row in cases) else 1


if __name__ == '__main__':
    raise SystemExit(main())
