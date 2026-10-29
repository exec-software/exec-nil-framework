#!/usr/bin/env python3
"""Exercise nilbuild's parallel child ownership without real compiler timing."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


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
    if (std::getenv("NIL_TEST_PENDING_SIGNAL")) {
      outer = std::make_unique<nilbuild::cancellation_scope>();
      raise(SIGTERM);
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
import fcntl, json, os, signal, subprocess, sys, time
from pathlib import Path

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
        with open("ledger", "a+") as file:
            fcntl.flock(file, fcntl.LOCK_EX)
            file.seek(0)
            data = json.loads(file.read() or '{"active": 0, "maximum": 0}')
            data["active"] += delta
            data["maximum"] = max(data["maximum"], data["active"])
            file.seek(0)
            file.truncate()
            file.write(json.dumps(data))
    count(1)
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
    descendant = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    Path(f"pids-{index}.json").write_text(json.dumps([os.getpid(), descendant.pid]))
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
        assert process.poll() is None, process.communicate()
        assert time.monotonic() < deadline, "children did not start"
        time.sleep(0.01)


def assert_children_stopped(directory):
    for pid_file in directory.glob('pids-*.json'):
        for pid in json.loads(pid_file.read_text()):
            stat = Path(f'/proc/{pid}/stat')
            if stat.exists():
                assert stat.read_text().split(') ')[1].startswith('Z '), f'child {pid} survived'
            else:
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError(f'child {pid} survived')


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nilbuild parallel ') as temporary:
        directory = Path(temporary)
        source = directory / 'driver.cpp'
        driver = directory / 'driver'
        worker = directory / 'worker.py'
        source.write_text(DRIVER)
        worker.write_text(WORKER)
        subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I', str(args.root), str(source), '-o', str(driver)],
                       check=True, timeout=45)

        def command(name, jobs, modes):
            case = directory / name
            case.mkdir()
            return case, [str(driver), str(jobs), str(case), sys.executable,
                          str(worker), *modes]

        for jobs in (1, 2):
            case, call = command(f'success-{jobs}', jobs, ['success'] * 5)
            completed = subprocess.run(call, text=True, capture_output=True, timeout=10)
            assert completed.returncode == 0, completed.stderr
            records = results(completed.stdout)
            assert len(records) == 5
            for i, result in enumerate(records):
                assert result['started'] and not result['cancelled'] and result['status'] == 0
                assert result['out'] == f'output-{i}\n' and result['err'] == f'error-{i}\n'
            assert json.loads((case / 'ledger').read_text()) == {'active': 0, 'maximum': jobs}

        case, call = command('failure', 2, ['failure', 'stubborn', 'queued', 'queued'])
        completed = subprocess.run(call, text=True, capture_output=True, timeout=10)
        assert completed.returncode == 0, completed.stderr
        records = results(completed.stdout)
        assert records[0]['status'] == 23 and not records[0]['cancelled']
        assert records[1]['cancelled'] and records[1]['status'] == 128 + signal.SIGKILL
        assert not any(result['started'] for result in records[2:])
        assert not (case / 'started-2').exists() and not (case / 'started-3').exists()
        assert_children_stopped(case)

        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            case, call = command(f'signal-{stop_signal}', 2, ['stubborn', 'stubborn', 'queued'])
            process = subprocess.Popen(call, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_files(process, case / 'pids-0.json', case / 'pids-1.json')
                process.send_signal(stop_signal)
                output, errors = process.communicate(timeout=10)
                assert process.returncode == 0, errors
                records = results(output)
                assert all(result['cancelled'] and result['status'] == 128 + stop_signal for result in records)
                assert not records[2]['started'] and not (case / 'started-2').exists()
                assert_children_stopped(case)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()

        case, call = command('overflow', 2, ['overflow', 'stubborn', 'queued'])
        completed = subprocess.run(call, text=True, capture_output=True, timeout=10)
        assert completed.returncode == 2 and 'exceeds 16 MiB' in completed.stderr, completed
        assert not (case / 'started-2').exists()
        assert_children_stopped(case)

        _, call = command('individual-limits', 2, ['large', 'large'])
        completed = subprocess.run(call, text=True, capture_output=True, timeout=10)
        assert completed.returncode == 0, completed.stderr
        assert all(result['status'] == 0 and result['outsize'] == 9 * 1024 * 1024
                   for result in results(completed.stdout))

        _, call = command('invalid-jobs', 0, ['success'])
        completed = subprocess.run(call, text=True, capture_output=True, timeout=10)
        assert completed.returncode == 2 and 'greater than zero' in completed.stderr

        case, call = command('nested-cancellation', 1, ['queued'])
        completed = subprocess.run(call, text=True, capture_output=True, timeout=10,
                                   env={**os.environ, 'NIL_TEST_PENDING_SIGNAL': '1'})
        assert completed.returncode == 0, completed.stderr
        record = results(completed.stdout)[0]
        assert record['status'] == 128 + signal.SIGTERM and record['cancelled'] and not record['started']
        assert not (case / 'started-0').exists()

    print('nilbuild: parallel scheduling, ordered capture, failure, cancellation and output bounds passed')


if __name__ == '__main__':
    main()
