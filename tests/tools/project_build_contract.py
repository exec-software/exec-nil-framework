#!/usr/bin/env python3
"""Exercise the shipped project driver with real mixed-language projects."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import signal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--builder', required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--interpreter', required=True)
    parser.add_argument('--stdlib', required=True)
    parser.add_argument('--llvm', required=True)
    args = parser.parse_args()
    args.builder = str(Path(args.builder).resolve())
    args.compiler = str(Path(args.compiler).resolve())
    args.interpreter = str(Path(args.interpreter).resolve())
    args.stdlib = str(Path(args.stdlib).resolve())
    if sys.platform == 'win32':
        print('nilbuild execution currently supports POSIX hosts; Windows contract is descriptive only')
        return
    count = 0
    with tempfile.TemporaryDirectory(prefix='nil project ; space ') as directory:
        root = Path(directory)
        project = root / 'nil.project.json'
        sources = root / 'src'
        library = root / 'library with spaces'
        sources.mkdir()
        library.mkdir()
        matrix = sources / 'main.matrix'
        ny = sources / 'main.ny'
        (library / 'nyhelper.ny').write_text(
            'module nyhelper\npub fn ny_answer() -> i32\n return 17\nend fn\n')
        (library / 'matrixhelper.matrix').write_text(
            'book matrixhelper\npublic matrix-answer returns override integer begin paragraph\n'
            '    give 25\nend paragraph\n')
        matrix.write_text(
            'book mixed\nread everything from nyhelper\nread everything from matrixhelper\n'
            'main returns override integer begin paragraph\n'
            '    say "program executed"\n'
            '    say (ny-answer with nothing) plus (matrix-answer with nothing)\n'
            '    give 0\nend paragraph\n')
        ny.write_text(
            'module mixed\nimport nyhelper\nimport matrixhelper\n'
            'fn main() -> i32\n println("program executed")\n'
            ' println(ny_answer() + matrix_answer())\n return 0\nend fn\n')
        base = {'schema': 'nil.project.v1', 'name': 'mixed',
                'entry': 'src/main.matrix', 'includePaths': ['library with spaces', '.']}

        def save(value=base, *, file=project):
            file.write_text(json.dumps(value), encoding='utf8')

        def invoke(command, *flags, status=0, cwd=root, explicit_tools=True, sdk=True,
                   project_flag=True, environment=None, timeout=45):
            nonlocal count
            call = [args.builder, command]
            if project_flag:
                call += ['--project', str(project)]
            if explicit_tools:
                call += ['--compiler', args.compiler, '--interpreter', args.interpreter]
            if sdk:
                call += ['--sdk', args.stdlib]
            call += list(map(str, flags))
            result = subprocess.run(call, cwd=cwd, text=True, capture_output=True,
                                    timeout=timeout, env=environment)
            assert result.returncode == status, (call, result.returncode, result.stdout, result.stderr)
            count += 1
            return result

        save()
        description = json.loads(invoke('describe', '--format', 'json', sdk=False).stdout)
        assert description == {
            'schema': 'nil.project.description.v1', 'projectFile': str(project),
            'projectDirectory': str(root), 'name': 'mixed', 'entry': str(matrix),
            'includePaths': [str(root), str(library)], 'sdkPath': None,
            'outputDirectory': str(root / 'build'), 'vmOutput': str(root / 'build/mixed.nil'),
            'nativeOutput': str(root / 'build/mixed'),
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
            (root / 'build').rmdir()
        save()
        if args.llvm.lower() in ('on', 'true', '1'):
            assert invoke('run', '--target', 'native').stdout == 'program executed\n42\n'
            assert (root / 'build/mixed').is_file()

        # Program arguments and exit codes survive both execution routes.
        ny.write_text(
            'import nil.std.system\nfn main() -> i32\n'
            ' println(arg_count())\n println(arg(0))\n println(arg(1))\n'
            ' println(arg(2))\n return 7\nend fn\n')
        save({**base, 'entry': 'src/main.ny'})
        targets = ['vm'] + (['native'] if args.llvm.lower() in ('on', 'true', '1') else [])
        for target in targets:
            result = invoke('run', '--target', target, '--', 'words with spaces', ';echo nope', '', status=7)
            assert result.stdout == '3\nwords with spaces\n;echo nope\n\n', result

        # Warning policy comes from the compiler and is shared by check/build.
        ny.write_text('fn main() -> i32\n println("λ🙂")  \n return 0\nend fn\n')
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
        ny.write_text('fn main() -> i32\n return "wrong type"\nend fn\n')
        invoke('run', status=1)
        assert artifact.read_bytes() == b'previous valid artifact'
        save()

        # Compiler failure after successful analysis must preserve existing
        # outputs and must never run the stale executable.
        fake = root / 'compiler with spaces'
        marker = root / 'stale program ran'
        native = root / 'build/mixed'
        native.write_text('#!' + sys.executable + '\nfrom pathlib import Path\nPath(' + repr(str(marker)) + ').touch()\n')
        native.chmod(0o755)
        original = native.read_bytes()
        report = json.dumps({'schema': 'nil.analysis.v1', 'success': True, 'diagnostics': []})

        def fake_compiler(body):
            fake.write_text('#!' + sys.executable + '\nimport sys\nfrom pathlib import Path\n'
                            'if "--analyze" in sys.argv:\n print(' + repr(report) + ')\n sys.exit(0)\n' + body)
            fake.chmod(0o755)

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

        # A successful run execs the program in place, preserving native
        # terminal/process semantics and allowing output beyond compiler limits.
        def fake_program(body):
            program = '#!' + sys.executable + '\n' + body
            fake_compiler('output = Path(sys.argv[sys.argv.index("--output") + 1])\n'
                          'output.write_text(' + repr(program) + ')\noutput.chmod(0o755)\n')

        fake_program('import sys\nsys.stdout.write("x" * (17 * 1024 * 1024))\n')
        result = invoke('run', '--compiler', fake, '--target', 'native', explicit_tools=False)
        assert len(result.stdout) == 17 * 1024 * 1024
        fake_program('import os\nprint(os.getpid(), flush=True)\nprint(input(), flush=True)\n')
        command = [args.builder, 'run', '--project', str(project), '--compiler', str(fake),
                   '--target', 'native', '--sdk', args.stdlib]
        process = subprocess.Popen(command, cwd=root, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        output, errors = process.communicate('interactive input\n', timeout=10)
        assert process.returncode == 0 and output == f'{process.pid}\ninteractive input\n', (output, errors)
        count += 1
        # A real terminal regression catches background-process-group SIGTTIN
        # bugs that a pipe-based stdin test cannot expose.
        import fcntl
        import pty
        import termios
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
                process.wait()
            os.close(master)
        count += 1

        # Compiler cancellation forwards signals to its process group, reaps
        # the child and removes staging without publishing partial artifacts.
        pid_file = root / 'compiler-pids.json'
        current_artifact = native.read_bytes()
        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            fake_compiler('import os, subprocess, time, json, signal\n'
                          'signal.signal(signal.SIGINT, signal.SIG_IGN)\n'
                          'signal.signal(signal.SIGTERM, signal.SIG_IGN)\n'
                          'child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
                          'Path(' + repr(str(pid_file)) + ').write_text(json.dumps([os.getpid(), child.pid]))\n'
                          'time.sleep(60)\n')
            process = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            deadline = time.monotonic() + 10
            while not pid_file.exists() and time.monotonic() < deadline:
                assert process.poll() is None, process.communicate()
                time.sleep(0.02)
            assert pid_file.exists(), 'compiler did not start'
            child_pids = json.loads(pid_file.read_text())
            process.send_signal(stop_signal)
            output, errors = process.communicate(timeout=10)
            assert process.returncode == 128 + stop_signal, (process.returncode, output, errors)
            assert native.read_bytes() == current_artifact
            assert not list(native.parent.glob('.nilbuild-*'))
            for pid in child_pids:
                stat = Path(f'/proc/{pid}/stat')
                if stat.exists():
                    assert stat.read_text().split(') ')[1].startswith('Z '), f'child {pid} survived cancellation'
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
        os.mkfifo(project)
        assert 'regular file' in invoke('describe', status=2, timeout=2).stderr
        project.unlink()
        save({**base, 'name': 'nil.project.json', 'outputDirectory': '.'})
        contents = project.read_bytes()
        assert 'overwrite' in invoke('build', '--target', 'native', status=2).stderr
        assert project.read_bytes() == contents
        custom = root / 'custom.json'
        save({**base, 'name': 'custom.json', 'outputDirectory': '.'}, file=custom)
        assert 'overwrite' in invoke('build', '--project', custom, '--target', 'native',
                                    project_flag=False, status=2).stderr
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
    main()
