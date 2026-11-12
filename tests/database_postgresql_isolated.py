#!/usr/bin/env python3
"""Bounded private PostgreSQL fixture; never use an existing database/service.

POSIX uses only a private Unix socket. Windows uses numeric loopback, SCRAM and
a fresh random credential. The server is owned from creation, including children.
"""
from __future__ import annotations
import argparse
import csv
import io
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'validation/ny-3d/scripts'))
from debugger_process import CaptureBudget, OwnedProcess, PipeReader
from runtime_provider_environment import DECLARATION, provider_environments


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def clean_environment(environment):
    return {key: value for key, value in environment.items()
            if not key.upper().startswith(('PG', 'NIL_', 'NY_'))}


def run(command, env, cwd, timeout):
    owner = OwnedProcess([str(part) for part in command], cwd=cwd, env=env,
                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, error = owner.communicate(timeout)
    return owner.process.returncode, output + error


def private_permissions(root, env):
    if os.name != 'nt':
        root.chmod(0o700)
        return
    # Only this freshly-created directory changes. Resolve the actual token's
    # SID rather than trusting an ambient username; descendants inherit its ACL.
    status, output = run(['whoami.exe', '/user', '/fo', 'csv', '/nh'], env, root, 10)
    require(status == 0, 'could not identify fixture account')
    # whoami may use the console's OEM code page for the account name. Only
    # its ASCII SID is consumed; a replaced name cannot authorize anything.
    rows = list(csv.reader(io.StringIO(output.decode('utf-8', 'replace').strip())))
    require(len(rows) == 1 and len(rows[0]) == 2, 'unexpected fixture account identity')
    sid = rows[0][1]
    require(sid.startswith('S-1-') and all(part.isdigit() for part in sid[2:].split('-')),
            'invalid fixture account SID')
    status, _ = run(['icacls.exe', str(root), '/inheritance:r', '/grant:r', '*' + sid + ':(OI)(CI)F'], env, root, 10)
    require(status == 0, 'could not restrict private fixture permissions')


def config_quote(value):
    return "'" + str(value).replace('\\', '\\\\').replace("'", "''") + "'"


def conninfo_quote(value):
    return "'" + str(value).replace('\\', '\\\\').replace("'", "\\'") + "'"


def configuration(windows, socket_directory, port):
    values = {'listen_addresses': '127.0.0.1' if windows else '',
              'unix_socket_directories': '' if windows else str(socket_directory),
              'shared_preload_libraries': '', 'session_preload_libraries': '',
              'local_preload_libraries': '', 'log_statement': 'none',
              'log_min_error_statement': 'panic', 'log_min_messages': 'log' if windows else 'panic',
              'lc_messages': 'C',
              'password_encryption': 'scram-sha-256', 'ssl': 'off',
              'logging_collector': 'off', 'archive_mode': 'off'}
    content = '\n'.join(key + ' = ' + config_quote(value) for key, value in values.items())
    content += f'\nport = {port}\nmax_connections = 16\nunix_socket_permissions = 0700\n'
    hba = ('host all nil_test 127.0.0.1/32 scram-sha-256\nhost all all 0.0.0.0/0 reject\n'
           'host all all ::/0 reject\n' if windows else 'local all nil_test trust\nlocal all all reject\n')
    return content, hba


def validate_routes(matrix, require_all):
    names = {'text-interpreter', 'binary-interpreter', 'bundle-text', 'bundle-binary', 'llvm-jit', 'llvm-aot'}
    rows = matrix['results']
    require(len(rows) == 6 and {row['route'] for row in rows} == names,
            'PostgreSQL matrix has missing or duplicate routes')
    counts = {'PASS': 0, 'FAIL': 0, 'SKIP': 0, 'XFAIL': 0, 'XPASS': 0}
    for row in rows:
        require(row['status'] in ('PASS', 'SKIP'), 'PostgreSQL route did not pass')
        if row['status'] == 'SKIP':
            require(not require_all and row['route'] in ('llvm-jit', 'llvm-aot') and
                    row.get('skip_reason') == 'nilcompiler was built without a usable LLVM backend',
                    'unexpected PostgreSQL route exclusion')
        counts[row['status']] += 1
    require(matrix['summary'] == counts, 'PostgreSQL route counts differ from actual results')
    return counts


class Server:
    def __init__(self, command, env, root, *, launcher=False):
        self.launcher = launcher
        self.owner = OwnedProcess(command, cwd=root, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.output, self.error = io.BytesIO(), io.BytesIO()
        self.budget = CaptureBudget(self.owner.terminate)
        self.readers = []
        try:
            require(not launcher or self.owner.job is not None,
                    'restricted PostgreSQL launcher requires an owned Windows Job')
            self.readers.append(PipeReader(self.owner.process.stdout, self.budget, self.output))
            self.readers.append(PipeReader(self.owner.process.stderr, self.budget, self.error))
        except BaseException:
            self.close()
            raise

    def healthy(self):
        self.budget.check()
        for reader in self.readers:
            require(not reader.error, 'PostgreSQL output capture failed')
        with self.owner.lock:
            status = self.owner.poll()
            if self.launcher:
                # pg_ctl exits after readiness; its descendants remain owned by
                # our exact no-breakaway Job, not by a reusable launcher PID.
                require(status in (None, 0), 'private PostgreSQL launcher failed')
                require(self.owner.job is not None and self.owner.job.active() > 0,
                        'private PostgreSQL Job has no live server')
            else:
                require(status is None, 'private PostgreSQL exited prematurely')

    def ready(self, data, port):
        self.healthy()
        if self.launcher:
            status = self.owner.poll()
            if status is None:
                return False  # Require successful pg_ctl completion before identity retention.
            require(status == 0, 'private PostgreSQL launcher failed before readiness')
        pidfile = data / 'postmaster.pid'
        if not pidfile.is_file():
            return False
        require(not pidfile.is_symlink() and pidfile.stat().st_size < 8192, 'invalid private server identity file')
        lines = pidfile.read_text(encoding='utf-8').splitlines()
        if len(lines) < 8 or lines[7].strip() != 'ready':
            return False
        require(lines[0].isdigit() and int(lines[3]) == port and os.path.samefile(lines[1], data),
                'private server identity differs from fixture configuration')
        pid = int(lines[0])
        with self.owner.lock:
            if self.owner.job:
                # Retain same-handle Job-verified identities. The postmaster's
                # retained process handle pins the PID pg_ctl later consults.
                self.owner.job.retain_members()
                require(any(identity[0] == pid for identity in self.owner.job.members),
                        'private postmaster is not in the owned Job')
            else:
                require(pid == self.owner.process.pid, 'private postmaster is not the owned leader')
        return True

    def verify_startup_capture(self):
        # Call only after close() has drained both inherited pipes. This marker
        # comes from postgres, not pg_ctl's separate "server started" message.
        require(b'database system is ready to accept connections' in
                self.output.getvalue() + self.error.getvalue(),
                'private PostgreSQL startup output was not captured')

    def graceful_stop(self, control, env, root, record):
        self.healthy()
        with self.owner.lock:
            job = self.owner.job
            if job:
                population = job.accounting().total
                job.retain_members()
                require(job.accounting().total == population, 'server population changed before graceful stop')
        if job:
            status, output = run([*control, '-m', 'fast', '-w', '-t', '5', 'stop'], env, root, 10)
            record('shutdown.log', output)
            require(status == 0, 'private PostgreSQL graceful shutdown failed')
        else:
            with self.owner.lock:
                self.owner.process.send_signal(signal.SIGTERM)
        require(self.owner.wait(10) == 0, 'private PostgreSQL did not exit cleanly')
        with self.owner.lock:
            if job:
                require(job.accounting().total == population, 'server population changed during graceful stop')
                job.drain()
            self.owner.finish()

    def close(self):
        try:
            if self.owner.poll() is None or self.owner.job:
                self.owner.kill()
            else:
                self.owner.wait(5)
        finally:
            try:
                self.owner.finish()
            finally:
                failure = None
                for reader in self.readers:
                    try:
                        reader.close()
                    except Exception as error:
                        failure = failure or error
                for stream in (self.owner.process.stdout, self.owner.process.stderr):
                    if stream and not stream.closed:
                        stream.close()
                if failure:
                    raise failure


def run_fixture(args, report):
    env = clean_environment(os.environ)
    if DECLARATION in os.environ:
        env[DECLARATION] = os.environ[DECLARATION]
    env, runtime_env, declared_provider = provider_environments(env)
    env['NIL_STDLIB_PATH'] = str(args.stdlib.resolve())
    runtime_env['NIL_STDLIB_PATH'] = env['NIL_STDLIB_PATH']
    if declared_provider is not None:
        report['runtime_loader'] = {
            'dyld_fallback_library_path': declared_provider,
            'scope': 'server/client and route execution only; JIT combines compilation and execution',
            'compile_environment': 'no ambient DYLD overrides',
        }
    root = Path(tempfile.mkdtemp(prefix='nil-db-pg-'))
    report.update(work_directory=str(root), work_directory_retained=True)
    password, address, server = '', '', None
    pwfile = root / 'init-password'
    success = False
    shutdown_attempted = False

    def record(name, output):
        text = output.decode('utf-8', 'replace')
        for value in (password, address):
            if value:
                text = text.replace(value, '<private fixture value>')
        (args.report_dir / name).write_text(text, encoding='utf-8', newline='\n')

    try:
        report['phase'] = 'private-permissions'
        private_permissions(root, env)
        data, socket_directory = root / 'data', root / 'socket'
        socket_directory.mkdir(mode=0o700)
        suffix = '.exe' if os.name == 'nt' else ''
        binary = lambda name: str(args.postgres_bin / (name + suffix))
        password = secrets.token_urlsafe(32)
        with pwfile.open('x', encoding='ascii', newline='\n') as stream:
            stream.write(password + '\n')
        if os.name != 'nt':
            pwfile.chmod(0o600)
        init = [binary('initdb'), '-D', str(data), '--auth=scram-sha-256', '--no-locale',
                '--encoding=UTF8', '--username=nil_test', '--pwfile=' + str(pwfile)]
        if args.postgres_share:
            init += ['-L', str(args.postgres_share.resolve())]
        report['phase'] = 'initialize'
        status, output = run(init, runtime_env, root, 30)
        record('initdb.log', output)
        pwfile.unlink()
        require(status == 0, 'private PostgreSQL initialization failed')
        if os.name == 'nt':
            reservation = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                reservation.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                reservation.bind(('127.0.0.1', 0))
                port = reservation.getsockname()[1]
            finally:
                reservation.close()
            address = 'hostaddr=127.0.0.1 dbname=postgres user=nil_test'
            env.update(PGPASSWORD=password, PGPORT=str(port), PGSSLMODE='disable', PGGSSENCMODE='disable')
            runtime_env.update(PGPASSWORD=password, PGPORT=str(port), PGSSLMODE='disable', PGGSSENCMODE='disable')
        else:
            port = 5432  # Scoped by the newly-created private socket directory.
            address = 'host=' + conninfo_quote(socket_directory) + ' dbname=postgres user=nil_test'
        config, hba = configuration(os.name == 'nt', socket_directory, port)
        (data / 'postgresql.conf').write_text(config, encoding='utf-8', newline='\n')
        (data / 'pg_hba.conf').write_text(hba, encoding='ascii', newline='\n')
        # POSIX stays foreground. On Windows, PostgreSQL's supported launcher
        # removes administrator/power-user token groups before starting postgres.
        # Pinned reference: postgres/postgres REL_16_15 src/bin/pg_ctl/pg_ctl.c,
        # InheritStdHandles and CreateRestrictedProcess: inherited stdio handles,
        # CreateProcessAsUser(CREATE_SUSPENDED), no breakaway flag; an inherited
        # Job is preserved. OwnedProcess installs that Job before pg_ctl runs.
        # Omit -l: postgres inherits our bounded pipes, never an unbounded file.
        report['phase'] = 'startup'
        windows = os.name == 'nt'
        command = ([binary('pg_ctl'), '-D', str(data), '-w', '-t', '30', 'start'] if windows
                   else [binary('postgres'), '-D', str(data)])
        report['server_launcher'] = 'pg_ctl-restricted-token' if windows else 'postgres-foreground'
        server = Server(command, runtime_env, root, launcher=windows)
        deadline = time.monotonic() + 30
        while not server.ready(data, port):
            require(time.monotonic() < deadline, 'private PostgreSQL startup deadline exceeded')
            time.sleep(.02)
        report['server_owned_and_ready'] = True
        if windows:
            report['server_launcher_exit_code'] = server.owner.process.returncode
        if os.name == 'nt':
            # Prove a shared-loopback client cannot bypass this fixture's
            # credential, then positively authenticate before language tests.
            report['phase'] = 'authentication-controls'
            check = [binary('psql'), '-X', '-w', '-qAt', '-h', '127.0.0.1',
                     '-U', 'nil_test', '-d', 'postgres', '-c', 'SELECT 1']
            rejected, output = run(check, dict(runtime_env, PGPASSWORD=password + '-wrong'), root, 10)
            record('authentication-negative.log', output)
            require(rejected != 0, 'loopback server accepted an incorrect credential')
            accepted, output = run(check, runtime_env, root, 10)
            record('authentication-positive.log', output)
            require(accepted == 0 and output.replace(b'\r\n', b'\n') == b'1\n',
                    'loopback server did not accept the fixture credential')
            report['authentication_controls'] = 'wrong-password-rejected; correct-password-accepted'
        if args.provider_test:
            report['phase'] = 'provider-tests'
            status, output = run([str(args.provider_test.resolve()), address], runtime_env, root, 30)
            record('provider.log', output)
            require(status == 0, 'PostgreSQL provider lifecycle tests failed')
        source = root / 'postgresql.ny'
        fixture = (REPO / 'tests/language/database-postgresql-runtime.ny').read_text(encoding='utf-8')
        source.write_text(fixture.replace('arg(0)', json.dumps(address)), encoding='utf-8', newline='\n')
        command = [sys.executable, str(REPO / 'tests/routes/ny_route_matrix.py'),
                   '--compiler', str(args.compiler.resolve()), '--interpreter', str(args.interpreter.resolve()),
                   '--source', str(source), '--expected-stdout', str(REPO / 'tests/language/database-postgresql-runtime.stdout'),
                   '--expected-exit', '0', '--routes', 'all', '--with-stdlib', '--timeout', '30',
                   '--work-dir', str(args.report_dir / 'routes'), '--report-json', str(args.report_dir / 'routes.json')]
        report['phase'] = 'language-routes'
        route_env = dict(env)
        if declared_provider is not None:
            # Keep the route harness and its compiler/AOT-link children clean;
            # it applies this inert declaration only to execution phases.
            route_env[DECLARATION] = declared_provider
        status, output = run(command, route_env, root, 180)
        record('routes.log', output)
        require(status == 0, 'PostgreSQL language route matrix failed')
        matrix = json.loads((args.report_dir / 'routes.json').read_bytes())
        report['routes'] = validate_routes(matrix, args.require_all_routes)
        report['all_routes_executed'] = report['routes']['PASS'] == 6
        report['phase'] = 'shutdown'
        shutdown_attempted = True
        server.graceful_stop([binary('pg_ctl'), '-D', str(data)], runtime_env, root, record)
        report['graceful_shutdown'] = True
        success = True
    finally:
        try:
            if server:
                try:
                    # A failed provider/route still gets one bounded graceful
                    # attempt, but only after its postmaster identity was proven.
                    if report.get('server_owned_and_ready') and not shutdown_attempted:
                        shutdown_attempted = True
                        try:
                            server.graceful_stop([binary('pg_ctl'), '-D', str(data)], runtime_env, root, record)
                            report['graceful_shutdown'] = True
                        except Exception:
                            report['graceful_shutdown'] = False
                finally:
                    server.close()
                record('postgres.log', server.output.getvalue() + server.error.getvalue())
                report['owned_process_cleanup'] = True
                if server.launcher and report.get('server_owned_and_ready'):
                    server.verify_startup_capture()
                    report['server_output_capture_verified'] = True
        finally:
            pwfile.unlink(missing_ok=True)
            env.pop('PGPASSWORD', None)
            runtime_env.pop('PGPASSWORD', None)
        if success:
            shutil.rmtree(root)
            require(not root.exists(), 'private PostgreSQL directory survived cleanup')
            report['work_directory_retained'] = False
            report['phase'] = 'complete'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'interpreter', 'stdlib', 'report-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--postgres-bin', type=Path, default=os.environ.get('NIL_TEST_POSTGRES_BIN'))
    parser.add_argument('--postgres-share', type=Path, default=os.environ.get('NIL_TEST_POSTGRES_SHARE'))
    parser.add_argument('--provider-test', type=Path)
    parser.add_argument('--require-all-routes', action='store_true',
                        help='qualification gate: require six PASS routes, including LLVM JIT/AOT')
    args = parser.parse_args()
    suffix = '.exe' if os.name == 'nt' else ''
    if not args.postgres_bin or not (args.postgres_bin / ('initdb' + suffix)).is_file():
        print('SKIP: provide PostgreSQL server binaries with --postgres-bin')
        return 77
    args.postgres_bin = args.postgres_bin.resolve()
    args.report_dir = args.report_dir.resolve()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    report = {'schema': 'nil.database-postgresql-isolated.v1', 'status': 'FAIL',
              'transport': 'numeric-loopback-scram' if os.name == 'nt' else 'private-unix-socket',
              'boundary': 'Fresh isolated cluster only; no existing server or service.'}
    try:
        run_fixture(args, report)
        report['status'] = 'PASS'
    except Exception as error:
        # Exception strings can include argv; do not expose connection details.
        report['error_type'] = type(error).__name__
    (args.report_dir / 'fixture.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
