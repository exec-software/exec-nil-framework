#!/usr/bin/env python3
"""Exercise the shipped project driver with real mixed-language projects."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import signal

from incremental_build_contract import windows_launcher
from process_contract_support import (assert_stopped, cancel_process,
                                      cleanup_process, console_options,
                                      process_alive)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_output_staging import native_output_path


def posix_process_contract(builder):
    """Keep exec image replacement, controlling terminals and FIFOs explicit."""
    if os.name == 'nt':
        print('POSIX exec image replacement, controlling terminals and FIFOs '
              'are not Windows process semantics; skipped')
        return 77
    import fcntl
    import pty
    import termios

    with tempfile.TemporaryDirectory(prefix='nil project posix ; ') as directory:
        root = Path(directory)
        project = root / 'nil.project.json'
        project.write_text(json.dumps({'schema': 'nil.project.v1', 'name': 'terminal',
                                       'entry': 'main.ny'}), encoding='utf-8')
        (root / 'main.ny').write_text('fn main() -> i32\n return 0\nend fn\n', encoding='utf-8')
        compiler = root / 'compiler with spaces'
        program = ('#!' + sys.executable + '\nimport os\n'
                   'print(os.getpid(), flush=True)\nprint(input(), flush=True)\n')
        report = json.dumps({'schema': 'nil.analysis.v1', 'success': True, 'diagnostics': []})
        compiler.write_text('#!' + sys.executable + '\nimport sys\nfrom pathlib import Path\n'
                            'if "--analyze" in sys.argv:\n print(' + repr(report) + ')\n sys.exit(0)\n'
                            'output=Path(sys.argv[sys.argv.index("--output")+1])\n'
                            'output.write_text(' + repr(program) + ')\noutput.chmod(0o755)\n',
                            encoding='utf-8', newline='\n')
        compiler.chmod(0o755)
        command = [builder, 'run', '--project', str(project), '--compiler', str(compiler),
                   '--target', 'native']
        process = subprocess.Popen(command, cwd=root, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            output, errors = process.communicate('interactive input\n', timeout=10)
            assert process.returncode == 0 and output == f'{process.pid}\ninteractive input\n', (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)

        master, slave = pty.openpty()
        def acquire_terminal():
            os.setsid()
            fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
        process = subprocess.Popen(command, cwd=root, stdin=slave,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   preexec_fn=acquire_terminal)
        os.close(slave)
        try:
            os.write(master, b'from terminal\n')
            output, errors = process.communicate(timeout=10)
            assert process.returncode == 0 and output == f'{process.pid}\nfrom terminal\n', (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            os.close(master)

        project.unlink()
        os.mkfifo(project)
        result = subprocess.run([builder, 'describe', '--project', str(project)],
                                cwd=root, capture_output=True, text=True, timeout=2)
        assert result.returncode == 2 and 'regular file' in result.stderr, result
    print('nilbuild: POSIX exec PID, controlling-terminal stdin and FIFO rejection passed')
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--builder', required=True)
    parser.add_argument('--compiler')
    parser.add_argument('--interpreter')
    parser.add_argument('--tester')
    parser.add_argument('--stdlib')
    parser.add_argument('--llvm')
    parser.add_argument('--cxx', help='Windows C compiler for real fixture launchers')
    parser.add_argument('--posix-process-only', action='store_true')
    args = parser.parse_args()
    args.builder = str(Path(args.builder).resolve())
    if args.posix_process_only:
        return posix_process_contract(args.builder)
    for name in ('compiler', 'interpreter', 'tester', 'stdlib', 'llvm'):
        if getattr(args, name) is None:
            parser.error('--' + name + ' is required for the common project contract')
    if os.name == 'nt' and args.cxx is None:
        parser.error('--cxx is required on Windows')
    args.compiler = str(Path(args.compiler).resolve())
    args.interpreter = str(Path(args.interpreter).resolve())
    args.tester = str(Path(args.tester).resolve())
    args.stdlib = str(Path(args.stdlib).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='nil project ; space ') as directory:
        root = Path(directory).resolve(strict=True)
        project = root / 'nil.project.json'
        sources = root / 'src'
        library = root / 'library with spaces'
        sources.mkdir()
        library.mkdir()
        matrix = sources / 'main.matrix'
        ny = sources / 'main.ny'
        (library / 'nyhelper.ny').write_text(
            'module nyhelper\npub fn ny_answer() -> i32\n return 17\nend fn\n', newline='\n')
        (library / 'matrixhelper.matrix').write_text(
            'book matrixhelper\npublic matrix-answer returns override integer begin paragraph\n'
            '    give 25\nend paragraph\n', newline='\n')
        matrix.write_text(
            'book mixed\nread everything from nyhelper\nread everything from matrixhelper\n'
            'main returns override integer begin paragraph\n'
            '    say "program executed"\n'
            '    say (ny-answer with nothing) plus (matrix-answer with nothing)\n'
            '    give 0\nend paragraph\n', newline='\n')
        ny.write_text(
            'module mixed\nimport nyhelper\nimport matrixhelper\n'
            'fn main() -> i32\n println("program executed")\n'
            ' println(ny_answer() + matrix_answer())\n return 0\nend fn\n', newline='\n')
        base = {'schema': 'nil.project.v1', 'name': 'mixed',
                'entry': 'src/main.matrix', 'includePaths': ['library with spaces', '.']}

        def save(value=base, *, file=project):
            file.write_text(json.dumps(value), encoding='utf8', newline='\n')

        def invoke(command, *flags, status=0, cwd=root, explicit_tools=True, sdk=True,
                   project_flag=True, environment=None, timeout=None):
            nonlocal count
            measured_build = sys.platform == 'darwin' and command in ('build', 'run') and explicit_tools
            if timeout is None:
                # Darwin independently hashes full loaded-image/shared-cache
                # inputs before and after real builds. Hosted VM builds have
                # exceeded 45 seconds; native builds measured 144 seconds before execution.
                native = any(left == '--target' and right == 'native' for left, right in zip(flags, flags[1:]))
                timeout = (300 if native else 180) if measured_build else 45
            call = [args.builder, command]
            if project_flag:
                call += ['--project', str(project)]
            if explicit_tools:
                call += ['--compiler', args.compiler, '--interpreter', args.interpreter]
            if sdk:
                call += ['--sdk', args.stdlib]
            call += list(map(str, flags))
            if measured_build:
                print(f'nilbuild contract: starting {command} ({timeout}s bound)', flush=True)
            started = time.monotonic()
            try:
                result = subprocess.run(call, cwd=cwd, text=True, capture_output=True,
                                        encoding='utf-8', timeout=timeout, env=environment)
            finally:
                if measured_build:
                    print(f'nilbuild contract: {command} elapsed {time.monotonic() - started:.3f}s', flush=True)
            assert result.returncode == status, (call, result.returncode, result.stdout, result.stderr)
            count += 1
            return result

        save()
        description = json.loads(invoke('describe', '--format', 'json', sdk=False).stdout)
        assert description == {
            'schema': 'nil.project.description.v1', 'projectFile': str(project),
            'projectDirectory': str(root), 'name': 'mixed', 'entry': str(matrix),
            # NIL-0026. Always present, and 'original' whenever the manifest
            # omits it -- that default is the backward-compatibility promise,
            # so it is asserted here rather than left implied.
            'edition': 'original',
            'includePaths': [str(root), str(library)], 'sdkPath': None,
            'outputDirectory': str(root / 'build'), 'vmOutput': str(root / 'build/mixed.nil'),
            'nativeOutput': str(native_output_path(root / 'build/mixed')),
            'analysis': {'disabledRules': [], 'warningsAsErrors': False}}, description
        assert json.loads(invoke('describe', '--format', 'json').stdout)['sdkPath'] == args.stdlib
        nested = sources / 'nested'
        nested.mkdir()
        discovered = json.loads(invoke('describe', '--format', 'json', cwd=nested,
                                       project_flag=False).stdout)
        assert discovered['projectFile'] == str(project)
        nearer = sources / 'nil.project.json'
        save({**base, 'entry': 'main.matrix', 'name': 'nearer'}, file=nearer)
        assert json.loads(invoke('describe', '--format', 'json', cwd=nested,
                                 project_flag=False).stdout)['name'] == 'nearer'
        nearer.unlink()
        override = {**base, 'sdkPath': 'nonexistent sdk'}
        save(override)
        assert json.loads(invoke('describe', '--format', 'json', sdk=False,
                                 *['--sdk', '../relative sdk'], cwd=sources).stdout)['sdkPath'] == str(root / 'relative sdk')
        save()

        for entry in ('src/main.matrix', 'src/main.ny'):
            save({**base, 'entry': entry})
            checked = json.loads(invoke('check', '--format', 'json').stdout)
            assert checked['schema'] == 'nil.analysis.v1' and checked['success']
            assert checked['diagnostics'] == []
            assert not (root / 'build').exists(), 'checking must not produce artifacts'
            built = invoke('build')
            assert built.stdout.strip() == str(root / 'build/mixed.nil')
            assert 'program executed' not in built.stderr, 'build must not execute main'
            assert (root / 'build/mixed.nil').is_file()
            ran = invoke('run', '--', 'argument with spaces', '; not a shell', '')
            assert ran.stdout == 'program executed\n42\n', ran
            # Test sibling tool discovery as installed SDKs depend on it.
            assert invoke('check', explicit_tools=False).returncode == 0
            (root / 'build/mixed.nil').unlink()
            shutil.rmtree(root / 'build/.nilbuild')
            (root / 'build').rmdir()
        save()
        if args.llvm.lower() in ('on', 'true', '1'):
            assert invoke('run', '--target', 'native').stdout == 'program executed\n42\n'
            assert native_output_path(root / 'build/mixed').is_file()

        # Project testing shares resolved SDK/include settings while keeping
        # application artifacts untouched. Both the project and import root
        # contain spaces/shell punctuation, so successful imports also verify
        # that delegation preserves individual path arguments.
        test_source = sources / 'project checks.matrix'
        test_source.write_text('''book project.checks
read everything from nyhelper
read everything from matrixhelper
read nil.std.core.option

project-pass begin test
 value present as Option of number is Option.Some holding 42
 expect (ny-answer with nothing) plus (matrix-answer with nothing) equals 42
 say "first project test"
end test

project-second begin test
 expect yes
 say "second project test"
end test

project-failure begin test
 expect no
 say "UNREACHABLE_PROJECT_FAILURE"
end test

main returns number begin paragraph
 say "APPLICATION_ENTRY_MUST_NOT_RUN"
 give 0
end paragraph
''', encoding='utf-8', newline='\n')
        save({**base, 'entry': 'src/project checks.matrix', 'sdkPath': args.stdlib})
        application_outputs = [root / 'build/mixed.nil', native_output_path(root / 'build/mixed')]
        previous_outputs = {path: path.read_bytes() if path.is_file() else None
                            for path in application_outputs}
        expected_routes = {'vm', 'native'} if args.llvm.lower() in ('on', 'true', '1') else {'vm'}
        test_target = 'all' if 'native' in expected_routes else 'vm'
        test_flags = ['--tester', args.tester, '--target', test_target,
                      '--format', 'json', '--jobs', '2', '--timeout', '5']
        result = invoke('test', *test_flags, '--filter', 'project-', status=1, sdk=False, timeout=120)
        assert result.stderr == '', result
        report = json.loads(result.stdout)
        assert report['schema'] == 'nil.test.results.v1' and report['errors'] == [], report
        assert len(report['tests']) == 3 * len(expected_routes), report
        assert {case['route'] for case in report['tests']} == expected_routes, report
        test_ids = {}
        for case in report['tests']:
            assert Path(case['file']) == test_source, case
            test_ids[case['name']] = case['id']
            if case['name'] == 'project-failure':
                assert case['status'] == 'failed' and case['stdout'] == '', case
                assert 'assertion failed' in case['stderr'], case
            else:
                assert case['status'] == 'passed' and case['exitCode'] == 77, case
                expected_output = 'first project test\n' if case['name'] == 'project-pass' else 'second project test\n'
                assert case['stdout'] == expected_output and case['stderr'] == '', case
        assert set(test_ids) == {'project-pass', 'project-second', 'project-failure'}, test_ids
        assert 'APPLICATION_ENTRY_MUST_NOT_RUN' not in result.stdout
        assert 'UNREACHABLE_PROJECT_FAILURE' not in result.stdout

        result = invoke('test', *test_flags, '--test-id', test_ids['project-pass'],
                        '--test-id', test_ids['project-second'], sdk=False, timeout=120)
        report = json.loads(result.stdout)
        assert report['summary']['passed'] == 2 * len(expected_routes), report
        assert all(case['status'] == 'passed' for case in report['tests']), report
        result = invoke('test', *test_flags, '--filter', 'project-pass',
                        '--test-id', test_ids['project-pass'], '--test-id', test_ids['project-second'],
                        sdk=False, timeout=120)
        report = json.loads(result.stdout)
        assert len(report['tests']) == len(expected_routes), report
        assert all(case['id'] == test_ids['project-pass'] and case['status'] == 'passed'
                   for case in report['tests']), report
        for path, previous in previous_outputs.items():
            assert (path.read_bytes() if path.is_file() else None) == previous, path
        save()

        # Program arguments and exit codes survive both execution routes.
        ny.write_text(
            'import nil.std.system\nfn main() -> i32\n'
            ' println(arg_count())\n println(arg(0))\n println(arg(1))\n'
            ' println(arg(2))\n return 7\nend fn\n', newline='\n')
        save({**base, 'entry': 'src/main.ny'})
        targets = ['vm'] + (['native'] if args.llvm.lower() in ('on', 'true', '1') else [])
        for target in targets:
            result = invoke('run', '--target', target, '--', 'words with spaces', ';echo nope', '', status=7)
            assert result.stdout == '3\nwords with spaces\n;echo nope\n\n', result

        # Warning policy comes from the compiler and is shared by check/build.
        ny.write_text('fn main() -> i32\n println("λ🙂")  \n return 0\nend fn\n', encoding='utf-8', newline='\n')
        warning = json.loads(invoke('check', '--format', 'json').stdout)
        assert any(d['code'] == 'NIL1001' for d in warning['diagnostics'])
        assert f'{ny}:2:16:' in invoke('check').stderr
        save({**base, 'entry': 'src/main.ny', 'analysis': {'warningsAsErrors': True}})
        strict = json.loads(invoke('check', '--format', 'json', status=1).stdout)
        assert not strict['success']
        artifact = root / 'build/mixed.nil'
        artifact.parent.mkdir(exist_ok=True)
        artifact.write_bytes(b'previous valid artifact')
        invoke('build', status=1)
        assert artifact.read_bytes() == b'previous valid artifact'
        invoke('run', status=1)
        assert artifact.read_bytes() == b'previous valid artifact'
        save({**base, 'entry': 'src/main.ny', 'analysis': {
            'disabledRules': ['NIL1001'], 'warningsAsErrors': True}})
        assert json.loads(invoke('check', '--format', 'json').stdout)['diagnostics'] == []
        ny.write_text('fn main() -> i32\n return "wrong type"\nend fn\n', newline='\n')
        invoke('run', status=1)
        assert artifact.read_bytes() == b'previous valid artifact'
        save()

        # Compiler failure after successful analysis must preserve existing
        # outputs and must never run the stale executable.
        fake_script = root / 'compiler with spaces.py'
        fake = fake_script
        marker = root / 'stale program ran'
        native = native_output_path(root / 'build/mixed')
        program_script = root / 'program with spaces.py'
        program_launcher = root / 'program launcher.exe'
        if os.name == 'nt':
            fake = root / 'compiler with spaces.exe'
            windows_launcher(args.cxx, fake, fake_script, root)
            windows_launcher(args.cxx, program_launcher, program_script, root)

        def install_program(body, destination):
            program = '#!' + sys.executable + '\n' + body
            if os.name == 'nt':
                program_script.write_text(program, encoding='utf-8', newline='\n')
                shutil.copyfile(program_launcher, destination)
            else:
                destination.write_text(program, encoding='utf-8', newline='\n')
                destination.chmod(0o755)

        install_program('from pathlib import Path\nPath(' + repr(str(marker)) + ').touch()\n', native)
        original = native.read_bytes()
        report = json.dumps({'schema': 'nil.analysis.v1', 'success': True, 'diagnostics': []})

        def fake_compiler(body):
            fake_script.write_text('#!' + sys.executable + '\nimport sys\nfrom pathlib import Path\n'
                                   'if "--analyze" in sys.argv:\n print(' + repr(report) + ')\n sys.exit(0)\n' + body,
                                   encoding='utf-8', newline='\n')
            fake_script.chmod(0o755)

        fake_compiler('output = Path(sys.argv[sys.argv.index("--output") + 1])\n'
                      'output.write_text("partial output")\nsys.exit(42)\n')
        for command in ('build', 'run'):
            result = invoke(command, '--compiler', fake, '--target', 'native',
                            explicit_tools=False, status=42)
            assert native.read_bytes() == original and not marker.exists()
            assert not list(native.parent.glob('.nilbuild-*'))
        fake_compiler('sys.exit(0)\n')
        assert 'without creating' in invoke('build', '--compiler', fake,
                                            explicit_tools=False, status=2).stderr
        assert artifact.read_bytes() == b'previous valid artifact'
        fake_compiler('Path(sys.argv[sys.argv.index("--output") + 1]).symlink_to(' + repr(str(matrix)) + ')\n')
        invoke('build', '--compiler', fake, explicit_tools=False, status=2)
        assert artifact.read_bytes() == b'previous valid artifact'
        assert not list(native.parent.glob('.nilbuild-*'))
        # The output bound covers both channels and does not publish a partial result.
        fake_compiler('sys.stdout.write("x" * (17 * 1024 * 1024))\n')
        assert 'exceeds 16 MiB' in invoke('build', '--compiler', fake,
                                         explicit_tools=False, status=2).stderr
        assert artifact.read_bytes() == b'previous valid artifact'
        assert not list(native.parent.glob('.nilbuild-*'))

        # The program inherits stdin/stdout and has no compiler output cap on
        # either host. POSIX exec/controlling-terminal identity is a separate
        # contract because Windows keeps a parent to forward the exit status.
        def fake_program(body):
            program = '#!' + sys.executable + '\n' + body
            if os.name == 'nt':
                program_script.write_text(program, encoding='utf-8', newline='\n')
                fake_compiler('import shutil\n'
                              'shutil.copyfile(' + repr(str(program_launcher)) + ', '
                              'sys.argv[sys.argv.index("--output") + 1])\n')
            else:
                fake_compiler('output = Path(sys.argv[sys.argv.index("--output") + 1])\n'
                              'output.write_text(' + repr(program) + ')\noutput.chmod(0o755)\n')

        fake_program('import sys\nsys.stdout.write("x" * (17 * 1024 * 1024))\n')
        result = invoke('run', '--compiler', fake, '--target', 'native', explicit_tools=False)
        assert len(result.stdout) == 17 * 1024 * 1024
        program_ready = root / 'program-ready.json'
        fake_program('import json, os, sys\nfrom pathlib import Path\n'
                     'sys.path.insert(0, ' + repr(str(Path(__file__).resolve().parent)) + ')\n'
                     'from process_contract_support import record_processes\n'
                     'record_processes(' + repr(str(program_ready)) + ', os.getpid(), '
                     '*([os.getppid()] if os.name == "nt" else []))\n'
                     'print(os.getpid(), flush=True)\nprint(input(), flush=True)\n'
                     'print("program stderr", file=sys.stderr, flush=True)\nsys.exit(19)\n')
        command = [args.builder, 'run', '--project', str(project), '--compiler', str(fake),
                   '--target', 'native', '--sdk', args.stdlib]
        process = subprocess.Popen(command, cwd=root, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   encoding='utf-8')
        try:
            deadline = time.monotonic() + 10
            while not program_ready.exists() and time.monotonic() < deadline:
                assert process.poll() is None, process.communicate()
                time.sleep(0.02)
            assert program_ready.exists(), 'program did not start'
            child_identities = json.loads(program_ready.read_text())
            child_pid = child_identities[0]['pid']
            assert child_pid > 0 and process.poll() is None, 'driver must wait for its running program'
            if os.name == 'nt':
                # The payload runs below the native launcher; unlike POSIX
                # exec, nilbuild must remain alive as a distinct parent and
                # forward stdin, output and the exact nonzero program status.
                assert all(identity['pid'] != process.pid for identity in child_identities)
            output, errors = process.communicate('interactive input\n', timeout=10)
            assert process.returncode == 19 and output == f'{child_pid}\ninteractive input\n', (output, errors)
            assert errors == 'nilbuild: building mixed (native)\nprogram stderr\n', errors
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            if program_ready.exists():
                for identity in json.loads(program_ready.read_text()):
                    if process_alive(identity):
                        cleanup_process(identity)
        count += 1

        # Compiler cancellation forwards signals to its process group, reaps
        # the child and removes staging without publishing partial artifacts.
        pid_file = root / 'compiler-pids.json'
        current_artifact = native.read_bytes()
        stop_signals = (signal.SIGBREAK,) if os.name == 'nt' else (signal.SIGINT, signal.SIGTERM)
        for stop_signal in stop_signals:
            fake_compiler('import os, subprocess, time, json, signal\n'
                          'sys.path.insert(0, ' + repr(str(Path(__file__).resolve().parent)) + ')\n'
                          'from process_contract_support import record_processes\n'
                          'signal.signal(signal.SIGINT, signal.SIG_IGN)\n'
                          'signal.signal(signal.SIGTERM, signal.SIG_IGN)\n'
                          'if os.name == "nt": signal.signal(signal.SIGBREAK, signal.SIG_IGN)\n'
                          'child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
                          'record_processes(' + repr(str(pid_file)) + ', os.getpid(), child.pid)\n'
                          'time.sleep(60)\n')
            process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       **console_options())
            child_pids = []
            try:
                deadline = time.monotonic() + 10
                while not pid_file.exists() and time.monotonic() < deadline:
                    assert process.poll() is None, process.communicate()
                    time.sleep(0.02)
                assert pid_file.exists(), 'compiler did not start'
                child_pids = json.loads(pid_file.read_text())
                cancel_process(process, stop_signal)
                output, errors = process.communicate(timeout=10)
                expected = 130 if os.name == 'nt' else 128 + stop_signal
                assert process.returncode == expected, (process.returncode, output, errors)
                assert native.read_bytes() == current_artifact
                assert not list(native.parent.glob('.nilbuild-*'))
                for identity in child_pids:
                    assert_stopped(identity)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
                for identity in child_pids:
                    if process_alive(identity):
                        cleanup_process(identity)
            pid_file.unlink()
            count += 1

        invalid_values = [
            [], None, {**base, 'schema': 'nil.project.v2'},
            {key: value for key, value in base.items() if key != 'entry'},
            {**base, 'command': 'touch anything'}, {**base, 'name': '../escape'},
            {**base, 'name': 'main.NY'}, {**base, 'name': 'x' * 129},
            {**base, 'name': 'shell;command'}, {**base, 'name': ''},
            {**base, 'entry': 'main.cpp'}, {**base, 'entry': None},
            {**base, 'entry': 'src/main\x00.matrix'}, {**base, 'includePaths': 'src'},
            {**base, 'includePaths': [1]}, {**base, 'includePaths': ['']},
            {**base, 'sdkPath': ''}, {**base, 'outputDirectory': False},
            {**base, 'analysis': None}, {**base, 'analysis': {'unknown': True}},
            {**base, 'analysis': {'warningsAsErrors': 'false'}},
            {**base, 'analysis': {'disabledRules': ['NIL1001', 'NIL1001']}},
            {**base, 'analysis': {'disabledRules': ['NIL1']}},
        ]
        for invalid in invalid_values:
            save(invalid)
            assert not invoke('describe', '--format', 'json', status=2).stdout
        raw_invalid = [b'\xef\xbb\xbf{}', b'{"schema":"nil.project.v1","schema":"nil.project.v1"}',
                       b'{"schema":true,"sch\\u0065ma":false}', b'\xff',
                       b'{"entry":"\\ud800"}', b'[' * 34 + b']' * 34,
                       b' ' * (1024 * 1024 + 1), b'{"name":01}',
                       b'{"name":true,}', b'{"name":NaN}', b'{} trailing']
        for raw in raw_invalid:
            project.write_bytes(raw)
            assert not invoke('describe', '--format', 'json', status=2).stdout
        project.unlink()
        project.mkdir()
        assert 'regular file' in invoke('describe', status=2, timeout=2).stderr
        project.rmdir()
        for name in ('nil.project.json', 'custom.json'):
            collision = native_output_path(root / name)
            save({**base, 'name': name, 'outputDirectory': '.'}, file=collision)
            contents = collision.read_bytes()
            assert 'overwrite' in invoke('build', '--project', collision, '--target', 'native',
                                        project_flag=False, status=2).stderr
            assert collision.read_bytes() == contents
        save()
        artifact.unlink()
        artifact.symlink_to(matrix)
        assert 'overwrite' in invoke('build', status=2).stderr
        artifact.unlink()
        os.link(matrix, artifact)
        assert 'overwrite' in invoke('build', status=2).stderr
        artifact.unlink()
        for flags in (('--target', 'javascript'), ('--format', 'json'),
                      ('--sdk', ''), ('--unknown', 'anything'), ('--', 'argument')):
            invoke('build', *flags, sdk=False, status=2)
        version = subprocess.run([args.builder, '--version'], text=True, capture_output=True, check=True)
        assert version.stdout.startswith('nilbuild ') and not version.stderr
    print(f'nilbuild: {count} project, mixed-language, policy, atomic output and strict manifest checks passed')


if __name__ == '__main__':
    # main() returns 77 to report a skip, so its value has to reach the exit
    # status; calling it bare would turn a skip back into a silent pass.
    raise SystemExit(main())
