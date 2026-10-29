#!/usr/bin/env python3
"""Verify native cache identities include system linker-script dependencies."""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


DRIVER = r'''
#include "nilbuild/toolchain_inputs.hpp"
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  try {
    const auto files = nilbuild::native_toolchain_files(argv[1], std::filesystem::current_path());
    if (!files) return 1;
    for (const auto &file : *files) std::cout << file.string() << '\n';
    return 0;
  } catch (const nilbuild::process_cancelled &error) {
    return error.status;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
'''

FAKE_DRIVER = r'''
import json, os, sys, time
from pathlib import Path
root = Path(__file__).parent
option = sys.argv[-1]
if (root / 'delay').exists():
    (root / 'probe.pid').write_text(str(os.getpid()))
    time.sleep(60)
if option == '-print-sysroot':
    print('')
elif option == '-print-prog-name=ld':
    print(root / 'ld')
elif option.startswith('-print-file-name='):
    name = option.split('=', 1)[1]
    path = root / name
    print(path if path.is_file() else name)
else:
    sys.exit(2)
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--driver', default=shutil.which('c++'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nilbuild toolchain ') as temporary:
        root = Path(temporary)
        source = root / 'harness.cpp'
        harness = root / 'harness'
        source.write_text(DRIVER)
        subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I',
                        str(args.root), str(source), '-o', str(harness)], check=True, timeout=45)
        fake = root / 'c++'
        fake.write_text('#!' + sys.executable + '\n' + FAKE_DRIVER)
        fake.chmod(0o755)
        for name in ('crt1.o', 'crti.o', 'crtbegin.o', 'crtend.o', 'crtn.o', 'ld',
                     'libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1', 'libm.so.6',
                     'libmvec.so.1', 'ld-linux.so.2'):
            (root / name).write_bytes(b'\x7fELF' + b'\0' * 60)
        for name in ('libc_nonshared.a', 'libgcc.a', 'libdl.a', 'libpthread.a'):
            (root / name).write_bytes(b'!<arch>\n')
        (root / 'libstdc++.so').symlink_to('libstdc++.so.6')
        libc = root / 'libc.so'
        libc_text = ('/* script */ OUTPUT_FORMAT(elf64-x86-64) GROUP (' +
                     ' '.join(json.dumps(str(root / name)) for name in ('libc.so.6', 'libc_nonshared.a')) +
                     ' AS_NEEDED (' + json.dumps(str(root / 'ld-linux.so.2')) + '))\n')
        libc.write_text(libc_text)
        (root / 'libgcc_s.so').write_text('GROUP (libgcc_s.so.1 -lgcc)\n')
        (root / 'libm.so').write_text('GROUP (libm.so.6 AS_NEEDED (libmvec.so.1))\n')

        def inspect(expected=0, tool=fake):
            result = subprocess.run([str(harness), str(tool)], cwd=root, text=True,
                                    capture_output=True, timeout=15)
            assert result.returncode == expected, result
            return {Path(path) for path in result.stdout.splitlines()}

        files = inspect()
        for name in ('libc_nonshared.a', 'libgcc.a', 'libgcc_s.so.1', 'libmvec.so.1',
                     'libdl.a', 'libpthread.a', 'ld-linux.so.2', 'libstdc++.so', 'ld'):
            assert root / name in files, (name, files)
        assert root / 'libstdc++.so.6' not in files, 'the selected symlink identity was lost'

        libc.write_text('SEARCH_DIR(".") ' + libc_text)
        inspect(1)
        libc.write_text('GROUP(libc.so)')
        inspect(1)
        libc.write_text(libc_text)
        (root / 'libgcc.a').write_bytes(b'!<thin>\n')
        inspect(1)
        (root / 'libgcc.a').write_bytes(b'!<arch>\n')
        (root / 'libc_nonshared.a').unlink()
        inspect(1)
        (root / 'libc_nonshared.a').write_bytes(b'!<arch>\n')
        (root / 'specs').write_text('custom driver specs')
        inspect(1)
        (root / 'specs').unlink()

        (root / 'delay').touch()
        process = subprocess.Popen([str(harness), str(fake)], cwd=root, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while not (root / 'probe.pid').exists():
                assert process.poll() is None
                assert time.monotonic() < deadline
                time.sleep(0.01)
            process.send_signal(signal.SIGINT)
            output, errors = process.communicate(timeout=10)
            assert process.returncode == 128 + signal.SIGINT, (output, errors)
            pid = int((root / 'probe.pid').read_text())
            assert not Path(f'/proc/{pid}').exists(), 'toolchain probe survived cancellation'
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        if args.driver:
            files = inspect(tool=Path(args.driver).resolve())
            names = {path.name for path in files}
            assert {'crt1.o', 'libc_nonshared.a', 'libgcc.a', 'libstdc++.so', 'libm.so'} <= names, names

    print('nilbuild: native toolchain, linker-script closure, symlink identities and cancellation passed')


if __name__ == '__main__':
    main()
