#!/usr/bin/env python3
"""Bounded read-only SDK/link-plan evidence; this does not enable build reuse."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert sys.platform == 'darwin'
    args.output.mkdir(parents=True, exist_ok=False)
    deadline = time.monotonic() + 90
    report = {'status': 'FAIL', 'scope': 'SDK stub census and dry link plan only', 'commands': []}
    # Only explicit tool-selection inputs; never dump the runner environment.
    report['selection_environment'] = {name: os.environ.get(name) for name in (
        'LIBRARY_PATH', 'COMPILER_PATH', 'SDKROOT', 'DEVELOPER_DIR', 'TOOLCHAINS',
        'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH', 'LDFLAGS')}

    def run(name, argv):
        remaining = deadline - time.monotonic()
        assert remaining > 0, 'census deadline exhausted'
        result = subprocess.run([str(value) for value in argv], stdin=subprocess.DEVNULL,
            capture_output=True, timeout=min(20, remaining))
        for kind, data in [('stdout', result.stdout), ('stderr', result.stderr)]:
            assert len(data) <= 2*1024*1024, 'census output exceeded bound'
            (args.output/(name+'.'+kind)).write_bytes(data)
        report['commands'].append({'name': name, 'argv': [str(value) for value in argv],
                                   'exit_code': result.returncode})
        result.check_returncode()
        return result.stdout.decode('utf-8').strip()

    try:
        cache = (args.build/'CMakeCache.txt').read_text()
        drivers = [line.split('=', 1)[1] for line in cache.splitlines()
                   if line.startswith('NIL_NATIVE_LINKER_DRIVER:STRING=')]
        assert len(drivers) == 1
        driver = Path(drivers[0]); assert driver.is_absolute() and driver.is_file()
        sdk = Path(run('sdk', ['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path']))
        assert sdk.is_absolute() and sdk.is_dir()
        archive = (args.build/'lib/libnil.a').resolve(strict=True)
        run('selected-driver', [driver, '--version'])
        run('native-link-plan', [driver, '-###', '-pthread', '-o', '/dev/null', archive,
                                '-framework', 'Cocoa', '-framework', 'CoreGraphics'])
        run('linker', ['/usr/bin/xcrun', '--find', 'ld'])
        rows, entries, total = [], 0, 0
        for root in [sdk/'usr/lib', sdk/'System/Library/Frameworks']:
            for directory, directories, files in os.walk(root, followlinks=False):
                assert time.monotonic() < deadline, 'SDK census deadline exhausted'
                entries += len(directories) + len(files)
                assert entries <= 250000, 'SDK namespace exceeds census bound'
                for name in sorted(files):
                    path = Path(directory)/name
                    if path.suffix != '.tbd':
                        continue
                    assert len(rows) < 20000, 'too many SDK stubs'
                    with path.open('rb') as stream:
                        prefix = stream.read(2048)
                    total += len(prefix)
                    assert total <= 32*1024*1024, 'SDK prefix budget exceeded'
                    rows.append({'path': path.relative_to(sdk).as_posix(),
                                 'resolved': str(path.resolve()), 'bytes': path.stat().st_size,
                                 'prefix': prefix.decode('utf-8', errors='replace'),
                                 'prefix_sha256': hashlib.sha256(prefix).hexdigest()})
        assert rows, 'no SDK stubs observed'
        report.update(status='PASS', sdk=str(sdk), driver=str(driver), runtime=str(archive),
                      entries=entries, prefix_bytes=total, stubs=rows)
    except Exception as error:
        report['error'] = str(error)
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(report['status'] + ': ' + str(args.output/'report.json'))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
