#!/usr/bin/env python3
"""Isolated Windows GTK loader controls; no display or installed SDK mutation."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'validation/ny-3d/scripts'))
from debugger_process import OwnedProcess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--gtk-directory', type=Path,
                        help='Optional real provider; when supplied, positive and search-isolation controls are mandatory.')
    args = parser.parse_args()
    if sys.platform != 'win32':
        print('SKIP: Windows DLL loader contract')
        return 77
    probe = args.probe.resolve(strict=True)
    configured = args.gtk_directory or os.environ.get('NIL_UI_GTK_DIRECTORY')
    runtime = Path(configured).resolve(strict=True) if configured else None
    if runtime is not None and not runtime.is_dir():
        raise AssertionError('GTK runtime must be a directory')
    root = Path(tempfile.mkdtemp(prefix='nil-ui-loader-café-中文-'))
    cases = []
    succeeded = False
    try:
        # The probe links nilruntime statically. Keep GTK out of its executable
        # directory so PATH/CWD controls cannot accidentally use that fallback.
        executable_dir = root / 'application'
        executable_dir.mkdir()
        executable = executable_dir / probe.name
        shutil.copyfile(probe, executable)
        clean = {key: value for key, value in os.environ.items()
                 if key.upper() not in ('NIL_UI_GTK_DIRECTORY', 'GDK_BACKEND')}
        selected = root / 'selected café 中文'
        selected.mkdir()

        def invoke(name, directory=None, present=False, cwd=selected, path_prefix=None):
            environment = dict(clean)
            if directory is not None:
                environment['NIL_UI_GTK_DIRECTORY'] = str(directory)
            if path_prefix:
                environment['PATH'] = str(path_prefix) + os.pathsep + environment.get('PATH', '')
            mode = '--provider-present' if present else '--provider-absent'
            child = OwnedProcess([executable, mode], cwd=cwd, env=environment,
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = child.communicate(timeout=15)
            record = {'name': name, 'exit': child.process.returncode,
                      'stdout': stdout.decode('utf-8', errors='replace'),
                      'stderr': stderr.decode('utf-8', errors='replace')}
            cases.append(record)
            if child.process.returncode != 0:
                raise AssertionError(record)

        invoke('missing optional provider')
        invoke('explicit relative directory rejected', '.')
        invoke('explicit empty directory rejected', '')
        invoke('explicit missing directory rejected', selected / 'not present')
        marker = root / 'not-a-directory'
        marker.write_bytes(b'test-only ordinary file')
        invoke('explicit regular file rejected', marker)
        for name in ('libgtk-4-1.dll', 'libgobject-2.0-0.dll', 'libglib-2.0-0.dll'):
            (selected / name).write_bytes(b'test-only invalid DLL, never executable')
        invoke('invalid DLL payload rejected', selected)

        if runtime is not None:
            invoke('explicit real runtime available without PATH', runtime, present=True)
            invoke('real runtime in CWD and PATH is not searched', cwd=runtime, path_prefix=runtime)
            invoke('invalid explicit directory does not use CWD or PATH', selected / 'missing',
                   cwd=runtime, path_prefix=runtime)
            # All three direct libraries are present but their other runtime
            # dependencies are not. Real dependencies in PATH/CWD must not be
            # silently used by this provider's restricted LoadLibraryEx call.
            incomplete = root / 'incomplete runtime'
            incomplete.mkdir()
            for stem in ('gtk-4-1.dll', 'gobject-2.0-0.dll', 'glib-2.0-0.dll'):
                found = [runtime / name for name in (stem, 'lib' + stem) if (runtime / name).is_file()]
                if len(found) != 1:
                    raise AssertionError(f'Expected one unambiguous runtime library: {stem}')
                shutil.copyfile(found[0], incomplete / found[0].name)
            invoke('dependencies do not fall back to CWD or PATH', incomplete,
                   cwd=runtime, path_prefix=runtime)
        succeeded = True
        print(json.dumps({'status': 'PASS', 'checks': len(cases),
                          'realProviderControls': runtime is not None, 'cases': cases}, indent=2))
        return 0
    finally:
        if succeeded:
            shutil.rmtree(root)
        else:
            (root / 'failure-evidence.json').write_text(json.dumps(cases, indent=2), encoding='utf-8')
            print(f'Windows GTK loader failure evidence retained: {root}', file=sys.stderr)


if __name__ == '__main__':
    raise SystemExit(main())
