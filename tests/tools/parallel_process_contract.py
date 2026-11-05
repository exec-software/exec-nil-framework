#!/usr/bin/env python3
"""Exercise nilbuild's parallel child ownership without real compiler timing."""

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import time

from process_contract_support import (assert_stopped, cancel_process,
                                      check_process_identity_guards,
                                      cleanup_process, console_options,
                                      process_alive)

DRIVER = r'''
#include "nilbuild/process.hpp"
#include <iomanip>
#include <memory>
#include <sstream>

std::string prefix(const std::string &text) {
  if (text.empty()) return "-";
  std::ostringstream output;
  for (unsigned char c : text.substr(0, 32))
    output << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
  return output.str();
}
int main(int argc, char **argv) {
  try {
    std::unique_ptr<nilbuild::cancellation_scope> outer;
#ifdef _WIN32
    const bool pending = GetEnvironmentVariableA("NIL_TEST_PENDING_SIGNAL", nullptr, 0) != 0;
#else
    const bool pending = std::getenv("NIL_TEST_PENDING_SIGNAL") != nullptr;
#endif
    if (pending) {
      outer = std::make_unique<nilbuild::cancellation_scope>();
#ifdef _WIN32
      // Synchronous injection covers nested-scope preservation; the separate
      // cancellation cases deliver a real console event to the live driver.
      nilbuild::forward_console_event(CTRL_BREAK_EVENT);
#else
      raise(SIGTERM);
#endif
    }
    std::vector<nilbuild::process_command> commands;
    for (int i = 5; i < argc; ++i)
      commands.push_back({{argv[3], argv[4], argv[i], std::to_string(i - 5)}, argv[2]});
    auto results = nilbuild::execute_many(commands, std::stoul(argv[1]));
    for (const auto &result : results)
      std::cout << result.status << ' ' << result.started << ' ' << result.cancelled
                << ' ' << result.output.size() << ' ' << result.error.size()
                << ' ' << prefix(result.output) << ' ' << prefix(result.error) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
'''

WORKER = r'''
import json, os, signal, subprocess, sys, time
from pathlib import Path
from process_contract_support import record_processes
sys.stdout.reconfigure(newline="\n")
sys.stderr.reconfigure(newline="\n")

if os.name == "nt":
    import msvcrt
else:
    import fcntl

mode, index = sys.argv[1], int(sys.argv[2])
Path(f"started-{index}").write_text(mode)

def wait_for(file):
    deadline = time.monotonic() + 5
    while not Path(file).exists():
        if time.monotonic() > deadline:
            raise RuntimeError("sibling did not start")
        time.sleep(0.01)

if mode == "success":
    def count(delta):
        # The lock has its own byte, so truncating/updating the JSON never
        # changes the locked range. msvcrt's byte lock and flock both serialize
        # the read/modify/write; the concurrency assertions are unchanged.
        with open("ledger.lock", "a+b") as lock:
            if os.name == "nt":
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                with open("ledger", "a+") as file:
                    file.seek(0)
                    data = json.loads(file.read() or '{"active": 0, "maximum": 0}')
                    data["active"] += delta
                    data["maximum"] = max(data["maximum"], data["active"])
                    file.seek(0)
                    file.truncate()
                    file.write(json.dumps(data))
            finally:
                if os.name == "nt":
                    lock.seek(0)
                    msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    if index == 1 and Path("delay-second").exists():
        wait_for("active-0")
        # Deliberately exceed worker zero's old sleep-only overlap window.
        # The rendezvous below must prove concurrency despite startup skew.
        time.sleep(0.4)
    count(1)
    Path(f"active-{index}").touch()
    first_wave = int(Path("first-wave").read_text())
    if index < first_wave:
        # A short sleep does not prove parallelism: process startup can exceed
        # it under load, and Windows LK_LOCK retries contention only once per
        # second. Keep each initial worker counted until every initial slot
        # is occupied. A serial runner still fails the bounded rendezvous.
        for sibling in range(first_wave):
            wait_for(f"active-{sibling}")
    time.sleep(0.25 if index % 2 == 0 else 0.05)
    print(f"output-{index}")
    print(f"error-{index}", file=sys.stderr)
    count(-1)
elif mode == "failure":
    wait_for("pids-1.json")
    print("intentional failure", file=sys.stderr)
    sys.exit(23)
elif mode == "stubborn":
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    if os.name == "nt": signal.signal(signal.SIGBREAK, signal.SIG_IGN)
    descendant = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    record_processes(f"pids-{index}.json", os.getpid(), descendant.pid)
    time.sleep(60)
elif mode == "overflow":
    wait_for("pids-1.json")
    sys.stdout.buffer.write(b"x" * (9 * 1024 * 1024))
    sys.stderr.buffer.write(b"y" * (9 * 1024 * 1024))
elif mode == "large":
    sys.stdout.buffer.write(b"x" * (9 * 1024 * 1024))
elif mode == "queued":
    raise RuntimeError("queued command must never start after failure")
'''


def wait_for_files(process, *files):
    deadline = time.monotonic() + 5
    while not all(file.exists() for file in files):
        if process.poll() is not None:
            output, error = process.communicate()
            raise AssertionError(process_diagnostic(process.returncode, output, error))
        assert time.monotonic() < deadline, "children did not start"
        time.sleep(0.01)


def assert_children_stopped(directory):
    for pid_file in directory.glob('pids-*.json'):
        for pid in json.loads(pid_file.read_text()):
            assert_stopped(pid)


def results(output):
    parsed = []
    for line in output.splitlines():
        status, started, cancelled, outsize, errsize, out, err = line.split()
        parsed.append({
            'status': int(status), 'started': bool(int(started)),
            'cancelled': bool(int(cancelled)), 'outsize': int(outsize),
            'errsize': int(errsize),
            'out': bytes.fromhex(out).decode() if out != '-' else '',
            'err': bytes.fromhex(err).decode() if err != '-' else '',
        })
    return parsed


@contextmanager
def fixture_directory():
    directory = Path(tempfile.mkdtemp(prefix='nilbuild parallel '))
    succeeded = False
    try:
        yield directory
        succeeded = True
    finally:
        try:
            # Even a failed assertion must not leave the 60-second fixture
            # processes running or holding the Windows scratch directory open.
            for path in directory.rglob('pids-*.json'):
                try:
                    pids = json.loads(path.read_text())
                except json.JSONDecodeError:
                    continue
                for pid in pids:
                    if process_alive(pid):
                        cleanup_process(pid)
        except BaseException:
            succeeded = False
            raise
        finally:
            if succeeded:
                shutil.rmtree(directory)
            else:
                print(f'parallel process failure artifacts retained: {directory}',
                      file=sys.stderr, flush=True)


def process_diagnostic(returncode, output, error):
    return (f'driver exit={returncode} (0x{returncode & 0xffffffff:08x})\n'
            f'stdout={output!r}\nstderr={error!r}')


def run_driver(call, **kwargs):
    completed = subprocess.run(call, text=True, capture_output=True,
                               **console_options(), **kwargs)
    record_driver(call, completed.returncode, completed.stdout, completed.stderr)
    return completed


def record_driver(call, returncode, output, error):
    (Path(call[2]) / 'driver-result.json').write_text(json.dumps({
        'command': call, 'returncode': returncode,
        'exitHex': f'0x{returncode & 0xffffffff:08x}',
        'stdout': output, 'stderr': error,
    }, indent=2), encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    check_process_identity_guards()
    with fixture_directory() as directory:
        source = directory / 'driver.cpp'
        driver = directory / ('driver.exe' if os.name == 'nt' else 'driver')
        worker = directory / 'worker.py'
        source.write_text(DRIVER, newline='\n')
        worker.write_text(WORKER, newline='\n')
        shutil.copyfile(Path(__file__).with_name('process_contract_support.py'),
                        directory / 'process_contract_support.py')
        if Path(args.cxx).name.lower() in ('cl', 'cl.exe', 'clang-cl', 'clang-cl.exe'):
            compile_command = [args.cxx, '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX',
                               '/I' + str(args.root), str(source), '/Fe:' + str(driver)]
        else:
            # Repository VERSION must not shadow libc++ <version> on a
            # case-insensitive filesystem. Only the quoted fixture header
            # belongs in this search path; system headers keep their order.
            compile_command = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                               '-iquote', str(args.root), str(source), '-o', str(driver)]
        subprocess.run(compile_command, cwd=directory, check=True, timeout=120)

        def command(name, jobs, modes):
            case = directory / name
            case.mkdir()
            (case / 'ledger.lock').write_bytes(b'0')
            (case / 'first-wave').write_text(str(min(jobs, len(modes))))
            return case, [str(driver), str(jobs), str(case), sys.executable,
                          str(worker), *modes]

        for jobs, count, delayed in ((1, 5, False), (2, 5, False), (2, 2, True)):
            case, call = command(f'success-{jobs}' + ('-delayed' if delayed else ''),
                                 jobs, ['success'] * count)
            if delayed:
                (case / 'delay-second').touch()
            completed = run_driver(call, timeout=20)
            assert completed.returncode == 0, process_diagnostic(
                completed.returncode, completed.stdout, completed.stderr)
            records = results(completed.stdout)
            assert len(records) == count
            for i, result in enumerate(records):
                assert result['started'] and not result['cancelled'] and result['status'] == 0, result
                assert result['out'] == f'output-{i}\n' and result['err'] == f'error-{i}\n', result
            ledger = json.loads((case / 'ledger').read_text())
            assert ledger == {'active': 0, 'maximum': jobs}, (case, ledger, records)

        case, call = command('failure', 2, ['failure', 'stubborn', 'queued', 'queued'])
        completed = run_driver(call, timeout=20)
        assert completed.returncode == 0, process_diagnostic(
            completed.returncode, completed.stdout, completed.stderr)
        records = results(completed.stdout)
        assert records[0]['status'] == 23 and not records[0]['cancelled']
        assert records[1]['cancelled'] and records[1]['status'] == (130 if os.name == 'nt' else 128 + signal.SIGKILL)
        assert not any(result['started'] for result in records[2:])
        assert not (case / 'started-2').exists() and not (case / 'started-3').exists()
        assert_children_stopped(case)

        stop_signals = (signal.SIGBREAK,) if os.name == 'nt' else (signal.SIGINT, signal.SIGTERM)
        for stop_signal in stop_signals:
            case, call = command(f'signal-{stop_signal}', 2, ['stubborn', 'stubborn', 'queued'])
            process = subprocess.Popen(call, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       **console_options())
            try:
                wait_for_files(process, case / 'pids-0.json', case / 'pids-1.json')
                cancel_process(process, stop_signal)
                output, errors = process.communicate(timeout=10)
                record_driver(call, process.returncode, output, errors)
                assert process.returncode == 0, process_diagnostic(
                    process.returncode, output, errors)
                records = results(output)
                expected_status = 130 if os.name == 'nt' else 128 + stop_signal
                assert all(result['cancelled'] and result['status'] == expected_status for result in records)
                assert not records[2]['started'] and not (case / 'started-2').exists()
                assert_children_stopped(case)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()

        case, call = command('overflow', 2, ['overflow', 'stubborn', 'queued'])
        completed = run_driver(call, timeout=20)
        assert completed.returncode == 2 and 'exceeds 16 MiB' in completed.stderr, completed
        assert not (case / 'started-2').exists()
        assert_children_stopped(case)

        _, call = command('individual-limits', 2, ['large', 'large'])
        completed = run_driver(call, timeout=20)
        assert completed.returncode == 0, process_diagnostic(
            completed.returncode, completed.stdout, completed.stderr)
        assert all(result['status'] == 0 and result['outsize'] == 9 * 1024 * 1024
                   for result in results(completed.stdout))

        _, call = command('invalid-jobs', 0, ['success'])
        completed = run_driver(call, timeout=20)
        assert completed.returncode == 2 and 'greater than zero' in completed.stderr

        case, call = command('nested-cancellation', 1, ['queued'])
        completed = run_driver(call, timeout=10,
                               env={**os.environ, 'NIL_TEST_PENDING_SIGNAL': '1'})
        assert completed.returncode == 0, process_diagnostic(
            completed.returncode, completed.stdout, completed.stderr)
        record = results(completed.stdout)[0]
        assert record['status'] == (130 if os.name == 'nt' else 128 + signal.SIGTERM) and record['cancelled'] and not record['started']
        assert not (case / 'started-0').exists()

    print('nilbuild: parallel scheduling, ordered capture, failure, cancellation and output bounds passed')


if __name__ == '__main__':
    main()
