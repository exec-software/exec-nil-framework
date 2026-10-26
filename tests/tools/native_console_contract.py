#!/usr/bin/env python3
"""Console line input preserves bytes and fails explicitly at native ABI limits."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'assembler', 'linker', 'runtime', 'stdlib'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    tools = {key: str(value.resolve(strict=True)) for key, value in vars(args).items()}
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    fixtures = Path(__file__).resolve().parent / 'fixtures'
    with tempfile.TemporaryDirectory(prefix='nil console contract ') as directory:
        root = Path(directory)

        def run(command, *, data=b'', status=0, env=None, stdin=None):
            nonlocal checks
            result = subprocess.run(list(map(str, command)), cwd=root,
                                    env=environment if env is None else env,
                                    input=data if stdin is None else None, stdin=stdin,
                                    capture_output=True, timeout=120)
            assert result.returncode == status, (command, result)
            checks += 1
            return result

        def build_native(nil, name, unmanaged=False):
            obj, executable = root / (name + '.o'), root / name
            flags = ['--unmanaged'] if unmanaged else []
            run([tools['assembler'], *flags, '-o', obj, nil])
            run([tools['linker'], '--runtime', tools['runtime'], '-o', executable, obj])
            return [executable]

        def check_prompt(command):
            nonlocal checks
            if os.name == 'nt':
                return
            with subprocess.Popen(list(map(str, command)), cwd=root, env=environment,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE) as child:
                try:
                    with selectors.DefaultSelector() as selector:
                        selector.register(child.stdout, selectors.EVENT_READ)
                        assert selector.select(timeout=10), 'console prompt was not flushed'
                    assert os.read(child.stdout.fileno(), 7) == b'input> '
                    output, error = child.communicate(b'answer\n', timeout=10)
                    assert child.returncode == 0 and output == b'answer\n' and not error, (output, error)
                    checks += 1
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=10)

        cases = [b'\n', b'  spaced words  \n', 'snowman-☃ café\n'.encode(),
                 b'carriage\r\n', b'last line', b'x' * 200000 + b'\n']
        native_commands = []
        for language in ('ny', 'matrix'):
            source = fixtures / ('console-input.' + language)
            nil = root / (language + '.nil')
            binary_nil = root / (language + '-binary.nil')
            compile_flags = [tools['compiler'], '--quiet', '--no-bundle', '--no-stdlib',
                             '--sdk-path', tools['stdlib']]
            run([*compile_flags, '--debug-info', '-o', nil, source])
            run([*compile_flags, '--debug-info', '--emit-binary-nil', '-o', binary_nil, source])
            for mode in ('safe', 'unsafe'):
                vm = [tools['interpreter'], '--no-ai', '--memory-mode=' + mode, nil]
                binary_vm = [tools['interpreter'], '--no-ai', '--memory-mode=' + mode, binary_nil]
                native = build_native(nil, language + '-' + mode, mode == 'unsafe')
                native_commands.append(native)
                for command in (vm, binary_vm, native):
                    for data in cases:
                        expected = b'input> ' + (data[:-1] if data.endswith(b'\n') else data) + b'\n'
                        result = run(command, data=data)
                        assert result.stdout == expected and not result.stderr, result
                    result = run(command, status=1)
                    assert result.stdout == b'input> ', result
                    assert b'console.read_line: input is unavailable or reached EOF' in result.stderr, result

                # JIT must resolve the same runtime symbols as linked objects.
                jit = [*compile_flags, '--native', '--jit',
                       *(['--unmanaged'] if mode == 'unsafe' else []),
                       '-o', root / (language + '-' + mode + '.jit'), source]
                result = run(jit, data='JIT café\n'.encode())
                assert result.stdout == 'input> JIT café\n'.encode() and not result.stderr, result
                result = run(jit, status=1)
                assert b'console.read_line: input is unavailable or reached EOF' in result.stderr, result

                # The VM can hold embedded NUL. Native text currently cannot;
                # its read must fail rather than return the innocent prefix.
                binary = b'before\0private-after\n'
                for command in (vm, binary_vm):
                    result = run(command, data=binary)
                    assert result.stdout == b'input> ' + binary and not result.stderr, result
                result = run(native, data=binary, status=1)
                assert result.stdout == b'input> ' and b'embedded NUL' in result.stderr, result
                assert b'private-after' not in result.stderr, result

                # Verify the explicit flush while the child is still blocked
                # on stdin. Reading only after process exit would miss it.
                check_prompt(native)

                # The generated wrapper must root the returned input text and
                # attribute its one allocation to the actual read_line body.
                report = root / (language + '-' + mode + '.json')
                result = run(native, data=b'profiled\n', env={**environment, 'NIL_MEMORY_PROFILE': str(report)})
                assert result.stdout == b'input> profiled\n' and not result.stderr, result
                profile = json.loads(report.read_text())
                assert profile['completed'] and not profile['errors'], profile
                sites = [site for site in profile['sites'] if site['kind'] == 'text']
                assert len(sites) == 1 and sites[0]['allocated_bytes'] == 9, sites
                assert 'read_line' in sites[0]['function'], sites

        # A directory descriptor is readable at the OS level but cannot yield
        # stream input. Exercise host read errors separately from clean EOF.
        if os.name == 'posix':
            descriptor = os.open(root, os.O_RDONLY)
            try:
                for command in native_commands:
                    result = run(command, status=1, stdin=descriptor)
                    assert b'input is unavailable or reached EOF' in result.stderr, result
            finally:
                os.close(descriptor)

        direct = root / 'direct.nil'
        direct.write_text('''module console_direct
function main() -> i32:
INTEROP_CALL intrinsic console.flush 0 void
INTEROP_CALL intrinsic console.read_line 0 text
CALL println 1
INTEROP_CALL intrinsic console.read_line 0 text
CALL println 1
PUSH_CONST 0
RET
end function
''')
        for mode in ('safe', 'unsafe'):
            command = build_native(direct, 'direct-' + mode, mode == 'unsafe')
            result = run(command, data=b'one\ntwo')
            assert result.stdout == b'one\ntwo\n' and not result.stderr, result
            result = run(command, data=b'one\n', status=1)
            assert result.stdout == b'one\n' and b'reached EOF' in result.stderr, result

        # read_line also publishes a preceding prompt without an explicit
        # flush, independent of the host C++ stream synchronization setting.
        prompt = root / 'implicit-flush.nil'
        prompt.write_text('''module console_prompt
const prompt = "input> "
@intrinsic("console.write")
function output(value: text) -> void:
end function
function main() -> i32:
PUSH_CONST_STR prompt
CALL output 1
INTEROP_CALL intrinsic console.read_line 0 text
CALL println 1
PUSH_CONST 0
RET
end function
''')
        for mode in ('safe', 'unsafe'):
            command = build_native(prompt, 'implicit-flush-' + mode, mode == 'unsafe')
            check_prompt(command)

        # Check flush itself before a host wait, with no read_line that could
        # otherwise publish the buffered prompt on its behalf.
        if os.name != 'nt':
            explicit = root / 'explicit-flush.nil'
            explicit.write_text(prompt.read_text().replace(
                'INTEROP_CALL intrinsic console.read_line 0 text\nCALL println 1',
                'INTEROP_CALL intrinsic console.flush 0 void\nPUSH_CONST 60000\nTIME_SLEEP'))
            for mode in ('safe', 'unsafe'):
                command = build_native(explicit, 'explicit-flush-' + mode, mode == 'unsafe')
                with subprocess.Popen(list(map(str, command)), cwd=root, env=environment,
                                      stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE) as child:
                    try:
                        with selectors.DefaultSelector() as selector:
                            selector.register(child.stdout, selectors.EVENT_READ)
                            assert selector.select(timeout=10), 'console.flush did not publish output before waiting'
                        assert os.read(child.stdout.fileno(), 7) == b'input> '
                        assert child.poll() is None, 'flush check ended before its host wait'
                        checks += 1
                    finally:
                        if child.poll() is None:
                            child.terminate()
                            child.wait(timeout=10)

        invalid = {
            'read-extra-argument': ('', 'PUSH_CONST 1\nINTEROP_CALL intrinsic console.read_line 1 text\nPOP'),
            'read-extra-metadata': ('', 'INTEROP_CALL intrinsic console.read_line 0 text i32\nPOP'),
            'read-wrong-return': ('', 'INTEROP_CALL intrinsic console.read_line 0 ptr<u8>\nPOP'),
            'read-missing-return': ('', 'INTEROP_CALL intrinsic console.read_line 0'),
            'flush-wrong-return': ('', 'INTEROP_CALL intrinsic console.flush 0 text\nPOP'),
            'flush-extra-argument': ('', 'PUSH_CONST 1\nINTEROP_CALL intrinsic console.flush 1 void'),
            'wrapper-read-argument': ('@intrinsic("console.read_line")\nfunction wrong(value: i32) -> text:\nend function', 'PUSH_CONST 1\nCALL wrong 1\nPOP'),
            'wrapper-read-return': ('@intrinsic("console.read_line")\nfunction wrong() -> ptr<u8>:\nend function', 'CALL wrong 0\nPOP'),
            'wrapper-flush-return': ('@intrinsic("console.flush")\nfunction wrong() -> text:\nend function', 'CALL wrong 0\nPOP'),
            'wrapper-flush-argument': ('@intrinsic("console.flush")\nfunction wrong(value: i32) -> void:\nend function', 'PUSH_CONST 1\nCALL wrong 1'),
        }
        for name, (declaration, body) in invalid.items():
            nil, output = root / (name + '.nil'), root / (name + '.o')
            nil.write_text('module rejected_console\n' + declaration +
                           '\nfunction main() -> i32:\n' + body + '\nPUSH_CONST 0\nRET\nend function\n')
            result = run([tools['assembler'], '-o', output, nil], status=1)
            assert b'requires the exact () ->' in result.stderr, result
            assert not output.exists(), output
    print(f'native console contract: {checks} executions/checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
