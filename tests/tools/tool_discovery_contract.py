#!/usr/bin/env python3
"""Exercise adjacent SDK tools and PATH overrides without shell expansion."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('builder', 'tester', 'compiler', 'interpreter', 'stdlib'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    builder, tester, compiler, interpreter, stdlib = (
        getattr(args, name).resolve(strict=True)
        for name in ('builder', 'tester', 'compiler', 'interpreter', 'stdlib'))
    tool_directories = {tool.parent for tool in (builder, tester, compiler, interpreter)}
    assert len(tool_directories) == 1, 'SDK discovery requires adjacent tool fixtures'
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    # Windows environment keys are case insensitive; avoid keeping both Path
    # and PATH when this test is launched by a developer prompt.
    old_path = next((value for key, value in environment.items()
                     if key.upper() == 'PATH'), '')
    environment = {key: value for key, value in environment.items()
                   if key.upper() != 'PATH'}
    retained_path = [item for item in old_path.split(os.pathsep)
                     if item and Path(item.strip('"')).resolve() not in tool_directories]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil discovery space ') as temporary:
        work = Path(temporary)
        source = work / 'main.ny'
        source.write_text('''fn main() -> i32
 println("discovered application")
 return 0
end fn
test "discovered case"
 assert.equal(6 * 7, 42)
end test
''', encoding='utf-8', newline='\n')
        project = work / 'nil.project.json'
        project.write_text(json.dumps({'schema': 'nil.project.v1',
                                      'name': 'discovery', 'entry': 'main.ny'}),
                           encoding='utf-8', newline='\n')
        adjacent_environment = {**environment,
                                'PATH': os.pathsep.join([str(work / 'no tools'), *retained_path])}
        path_environment = {**environment, 'PATH': os.pathsep.join(
            [str(work / 'missing tool directory'), str(compiler.parent), *retained_path])}

        def run(command, *, env=adjacent_environment, status=0, executable=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    text=True, timeout=60, executable=executable)
            assert result.returncode == status, (command, result.returncode,
                                                  result.stdout, result.stderr)
            checks += 1
            return result

        build_options = ['--project', project, '--sdk', stdlib]
        test_options = ['--no-stdlib', '--format', 'json', source]

        def verify_build_tools(build_command, flags=(), *, env=adjacent_environment):
            checked = run([build_command, 'check', *build_options, '--format', 'json',
                           *flags], env=env)
            assert json.loads(checked.stdout)['success']
            ran = run([build_command, 'run', *build_options, *flags], env=env)
            assert ran.stdout == 'discovered application\n', ran
            tested = run([build_command, 'test', *build_options, '--format', 'json',
                          *flags], env=env)
            assert json.loads(tested.stdout)['summary']['passed'] == 1, tested

        def verify_test_tools(test_command, flags=(), *, env=adjacent_environment, executable=None):
            listed = run([test_command, 'list', *test_options, *flags], env=env, executable=executable)
            assert len(json.loads(listed.stdout)['tests']) == 1, listed
            tested = run([test_command, 'run', *test_options, *flags], env=env, executable=executable)
            assert json.loads(tested.stdout)['summary']['passed'] == 1, tested

        # Neither command receives a tool override, and PATH cannot resolve
        # the tool directory: discovery must find .exe siblings on Windows.
        verify_build_tools(builder)
        verify_test_tools(tester)

        # A basename override must use the platform PATH separator and find
        # the platform executable suffix. On Windows, the first PATH element
        # has a drive colon and the actual tool directory follows a semicolon.
        verify_build_tools(builder, ['--compiler', 'nilcompiler',
                                    '--interpreter', 'nilinterpreter'], env=path_environment)
        verify_test_tools(tester, ['--compiler', 'nilcompiler',
                                  '--interpreter', 'nilinterpreter'], env=path_environment)
        tested = run([builder, 'test', *build_options, '--format', 'json',
                      '--tester', 'niltest'], env=path_environment)
        assert json.loads(tested.stdout)['summary']['passed'] == 1

        # Explicit paths still work, including an omitted .exe suffix. They
        # must never silently fall back to adjacent tools when invalid.
        explicit_compiler = compiler.with_suffix('') if os.name == 'nt' else compiler
        explicit_interpreter = interpreter.with_suffix('') if os.name == 'nt' else interpreter
        verify_test_tools(tester, ['--compiler', explicit_compiler,
                                  '--interpreter', explicit_interpreter])
        checked = run([builder, 'check', *build_options, '--compiler', compiler,
                       '--format', 'json'])
        assert json.loads(checked.stdout)['success']
        for bad in (work / 'absent-compiler', 'nil-absent-tool-for-discovery'):
            result = run([builder, 'check', *build_options, '--compiler', bad], status=2)
            assert 'cannot execute specified nilcompiler' in result.stderr
            result = run([tester, 'list', *test_options, '--compiler', bad], status=2)
            assert 'cannot execute specified nilcompiler' in result.stdout

        # A bare argv[0] must not make the project working directory the SDK
        # location. Pass the image separately: Windows CreateProcess does not
        # use subprocess's replacement env PATH to find its application.
        result = run([builder.name, 'check', *build_options, '--format', 'json'],
                     env=path_environment, executable=builder)
        assert json.loads(result.stdout)['success']
        verify_test_tools(tester.name, env=path_environment, executable=tester)

    print(f'tool discovery: {checks} sibling, PATH, explicit override and failure checks passed')


if __name__ == '__main__':
    main()
