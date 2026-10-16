#!/usr/bin/env python3
"""Run all database routes against a private, disposable PostgreSQL server.

Requires server binaries explicitly via --postgres-bin or NIL_TEST_POSTGRES_BIN.
Never connects to an existing server, listens on TCP, or modifies system files.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--postgres-bin', type=Path, default=os.environ.get('NIL_TEST_POSTGRES_BIN'))
    parser.add_argument('--postgres-share', type=Path, default=os.environ.get('NIL_TEST_POSTGRES_SHARE'))
    parser.add_argument('--provider-test', type=Path)
    parser.add_argument('--report-dir', type=Path, required=True)
    args = parser.parse_args()
    if not args.postgres_bin or not (args.postgres_bin / 'initdb').is_file():
        print('SKIP: provide PostgreSQL server binaries with --postgres-bin')
        return 77
    args.postgres_bin = args.postgres_bin.resolve()
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[1]
    env = {k: v for k, v in os.environ.items() if not k.startswith('PG')}
    env['NIL_STDLIB_PATH'] = str(args.stdlib.resolve())
    with tempfile.TemporaryDirectory(prefix='nil-db-pg-') as temporary:
        root = Path(temporary)
        socket = root / 'socket'
        socket.mkdir(mode=0o700)
        data = root / 'data'
        log = root / 'postgres.log'
        init = [str(args.postgres_bin / 'initdb'), '-D', str(data), '--auth=trust', '--no-locale', '--encoding=UTF8', '--username=nil_test']
        if args.postgres_share:
            init += ['-L', str(args.postgres_share.resolve())]
        initialized = subprocess.run(init, env=env, capture_output=True, text=True, timeout=30)
        (report / 'initdb.log').write_text(initialized.stdout + initialized.stderr)
        if initialized.returncode:
            print(initialized.stderr, file=sys.stderr)
            return 1
        options = "-c listen_addresses='' -c unix_socket_directories=" + shlex.quote(str(socket)) + " -c unix_socket_permissions=0700 -c shared_preload_libraries=''"
        control = [str(args.postgres_bin / 'pg_ctl'), '-D', str(data)]
        started = False
        try:
            startup = subprocess.run(control + ['-l', str(log), '-o', options, '-w', 'start'], env=env, capture_output=True, text=True, timeout=30)
            if startup.returncode:
                print(startup.stderr, file=sys.stderr)
                return 1
            started = True
            address = f'host={socket} dbname=postgres user=nil_test'
            if args.provider_test:
                checked = subprocess.run([str(args.provider_test.resolve()), address], env=env, capture_output=True, text=True, timeout=30)
                (report / 'provider.log').write_text(checked.stdout + checked.stderr)
                if checked.returncode:
                    print(checked.stderr, file=sys.stderr)
                    return 1
            # Bundled executables currently receive no program-arg from the
            # route harness. Embed only this runner-created private address.
            source = root / 'postgresql.ny'
            fixture = (repo / 'tests/language/database-postgresql-runtime.ny').read_text()
            source.write_text(fixture.replace('arg(0)', json.dumps(address)))
            command = [sys.executable, str(repo / 'tests/routes/ny_route_matrix.py'), '--compiler', str(args.compiler.resolve()), '--interpreter', str(args.interpreter.resolve()), '--source', str(source), '--expected-stdout', str(repo / 'tests/language/database-postgresql-runtime.stdout'), '--expected-exit', '0', '--routes', 'all', '--with-stdlib', '--timeout', '30', '--work-dir', str(report / 'routes'), '--report-json', str(report / 'routes.json')]
            run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=180)
            (report / 'routes.log').write_text(run.stdout + run.stderr)
            print(run.stdout, end='')
            if run.returncode:
                print(run.stderr, file=sys.stderr)
            return run.returncode
        finally:
            if started:
                stopped = subprocess.run(control + ['-m', 'immediate', '-w', 'stop'], env=env, capture_output=True, text=True, timeout=30)
                (report / 'shutdown.log').write_text(stopped.stdout + stopped.stderr)
            if log.exists():
                shutil.copyfile(log, report / 'postgres.log')


if __name__ == '__main__':
    raise SystemExit(main())
