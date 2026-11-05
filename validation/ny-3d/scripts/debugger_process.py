"""Bounded consumer-harness processes; no compiler-test or third-party dependency.

Windows launches a tiny, trusted Python gate inside a private Job before that
gate can launch the requested program. Assignment is therefore not racing a
running compiler/debugger. The retained Job handle owns all descendants; no
PID scan, taskkill, or optional process-name exception is used.
"""
from __future__ import annotations

import os
import io
import errno
from pathlib import Path
import queue
import signal
import subprocess
import sys
import threading
import time

CLEANUP_SECONDS = 5
OUTPUT_LIMIT = 16 * 1024 * 1024


def _darwin_group_snapshot(pgid, deadline):
    """Observe only the retained child's group; never scan or signal other PIDs."""
    if time.monotonic() >= deadline:
        raise RuntimeError('Darwin owned group observation exceeded cleanup deadline')
    import ctypes as c

    class ShortInfo(c.Structure):
        _fields_ = [(name, c.c_uint32) for name in ('pid', 'ppid', 'pgid', 'status')] + [
            ('comm', c.c_char * 16)] + [(name, c.c_uint32) for name in
                ('flags', 'uid', 'gid', 'ruid', 'rgid', 'svuid', 'svgid', 'reserved')]

    if c.sizeof(ShortInfo) != 64 or not 0 < pgid <= 0x7fffffff:
        raise RuntimeError('invalid Darwin process-group observation layout or identity')
    lib = c.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_listpids.argtypes = [c.c_uint32, c.c_uint32, c.c_void_p, c.c_int]
    lib.proc_listpids.restype = c.c_int
    lib.proc_pidinfo.argtypes = [c.c_int, c.c_int, c.c_uint64, c.c_void_p, c.c_int]
    lib.proc_pidinfo.restype = c.c_int
    # One extra entry detects truncation, rather than accepting an incomplete
    # fixed-size list as proof that every member has exited.
    ids = (c.c_int * 4097)()
    c.set_errno(0)
    size = lib.proc_listpids(2, pgid, ids, c.sizeof(ids))  # PROC_PGRP_ONLY
    error = c.get_errno()
    if size <= 0 and error:
        raise OSError(error, 'Darwin owned group lookup failed')
    if size <= 0 or size % c.sizeof(c.c_int) or size >= c.sizeof(ids):
        raise RuntimeError('Darwin owned group snapshot was empty or incomplete')
    pids = list(ids[:size // c.sizeof(c.c_int)])
    if len(set(pids)) != len(pids) or any(pid <= 0 for pid in pids):
        raise RuntimeError('Darwin owned group snapshot contained invalid identities')
    result = {}
    for pid in pids:
        if time.monotonic() >= deadline:
            raise RuntimeError('Darwin owned group observation exceeded cleanup deadline')
        info = ShortInfo()
        c.set_errno(0)
        # arg=1 includes zombies: the unreaped leader must remain observable.
        size = lib.proc_pidinfo(pid, 13, 1, c.byref(info), c.sizeof(info))
        if size == 0 and c.get_errno():
            raise OSError(c.get_errno(), 'Darwin owned group member lookup failed')
        if size != c.sizeof(info) or info.pid != pid or info.pgid != pgid:
            raise RuntimeError(f'Darwin owned group member identity was not established: '
                               f'bytes={size}/64, pid={info.pid}/{pid}, group={info.pgid}/{pgid}')
        result[pid] = info.status
    return result


def _darwin_group_exited(pgid, deadline):
    first = _darwin_group_snapshot(pgid, deadline)
    # A missing retained leader is uncertainty, not an empty owned group.
    if pgid not in first or any(state != 5 for state in first.values()):  # SZOMB
        return False
    # A member could fork before its first state observation. Require another
    # complete group snapshot after observing every first-snapshot member dead.
    # A previously observed zombie may be reaped between snapshots; no new or
    # live member may appear, and our retained leader must still be present.
    second = _darwin_group_snapshot(pgid, deadline)
    return (time.monotonic() < deadline and pgid in second and
            second.items() <= first.items())


def write_pipe(pipe, data, timeout, terminate):
    """A child that stops reading protocol input cannot block the harness."""
    done, errors = threading.Event(), []

    def write():
        try:
            pipe.write(data)
            pipe.flush()
        except BaseException as exc:
            errors.append(exc)
        finally:
            done.set()

    thread = threading.Thread(target=write, daemon=True)
    thread.start()
    if not done.wait(max(0, timeout)):
        terminate()
        thread.join(CLEANUP_SECONDS)
        if thread.is_alive():
            raise RuntimeError('stdin writer survived bounded process cleanup')
        raise TimeoutError('debugger protocol write timed out')
    thread.join()
    if errors:
        raise errors[0]


class CaptureBudget:
    def __init__(self, terminate, maximum=None):
        self.maximum, self.used = OUTPUT_LIMIT if maximum is None else maximum, 0
        self.terminate = terminate
        self.lock = threading.Lock()
        self.exceeded = threading.Event()

    def take(self, data):
        with self.lock:
            allowed = max(0, min(len(data), self.maximum - self.used))
            self.used += allowed
            overflow = allowed != len(data)
            if overflow:
                self.exceeded.set()
        if overflow:
            self.terminate()
        return data[:allowed]

    def check(self):
        if self.exceeded.is_set():
            raise RuntimeError(f'consumer output exceeded {self.maximum} byte session budget')


class PipeReader:
    """Portable deadline-aware pipe reads (Windows select only accepts sockets)."""
    def __init__(self, pipe, budget=None, sink=None):
        self.pipe = pipe
        self.budget, self.sink = budget, sink
        self.error = None
        self.done = threading.Event()
        self.queue = queue.Queue(maxsize=128)
        self.stopping = threading.Event()
        self.discarding = threading.Event()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _offer(self, value):
        while not self.stopping.is_set() and not self.discarding.is_set():
            try:
                self.queue.put(value, timeout=.05)
                return
            except queue.Full:
                pass

    def _read(self):
        try:
            while not self.stopping.is_set():
                chunk = os.read(self.pipe.fileno(), 65536)
                if self.budget:
                    chunk = self.budget.take(chunk)
                if self.sink is not None:
                    self.sink.write(chunk)
                    self.sink.flush()
                else:
                    self._offer(chunk)
                if not chunk:
                    return
        except BaseException as exc:
            self.error = exc
            if self.sink is None:
                self._offer(exc)
        finally:
            self.done.set()

    def read(self, timeout):
        if self.budget:
            self.budget.check()
        try:
            item = self.queue.get(timeout=max(0, timeout))
        except queue.Empty:
            raise TimeoutError('debugger message timed out') from None
        if isinstance(item, BaseException):
            raise item
        return item

    def close(self):
        # The owned process tree has exited. Drain through EOF (including late
        # stderr) for budget/error accounting, but do not block on an abandoned
        # stdout queue after the caller has stopped requesting protocol frames.
        self.discarding.set()
        self.thread.join(CLEANUP_SECONDS)
        if self.thread.is_alive():
            self.stopping.set()
            raise RuntimeError('stdout reader survived bounded process cleanup')
        self.pipe.close()
        if self.error:
            raise self.error


class WindowsJob:
    def __init__(self):
        import ctypes as c
        from ctypes import wintypes as w
        self.c, self.w = c, w
        self.k = k = c.WinDLL('kernel32', use_last_error=True)

        class BasicLimits(c.Structure):
            _fields_ = [('process_time', c.c_int64), ('job_time', c.c_int64),
                        ('flags', w.DWORD), ('minimum', c.c_size_t), ('maximum', c.c_size_t),
                        ('active_limit', w.DWORD), ('affinity', c.c_size_t),
                        ('priority', w.DWORD), ('scheduling', w.DWORD)]

        class IoCounters(c.Structure):
            _fields_ = [(name, c.c_uint64) for name in
                        ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]

        class ExtendedLimits(c.Structure):
            _fields_ = [('basic', BasicLimits), ('io', IoCounters),
                        ('process_memory', c.c_size_t), ('job_memory', c.c_size_t),
                        ('peak_process', c.c_size_t), ('peak_job', c.c_size_t)]

        class Accounting(c.Structure):
            _fields_ = [('user_time', c.c_int64), ('kernel_time', c.c_int64),
                        ('period_user', c.c_int64), ('period_kernel', c.c_int64),
                        ('faults', w.DWORD), ('total', w.DWORD),
                        ('active', w.DWORD), ('terminated', w.DWORD)]

        self.Accounting = Accounting
        self.members = {}
        self.cleanup_deadline = None
        self.cleanup_error = None
        self.cleanup_population = None
        signatures = {
            'CreateJobObjectW': ([c.c_void_p, w.LPCWSTR], w.HANDLE),
            'SetInformationJobObject': ([w.HANDLE, c.c_int, c.c_void_p, w.DWORD], w.BOOL),
            'QueryInformationJobObject': ([w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p], w.BOOL),
            'AssignProcessToJobObject': ([w.HANDLE, w.HANDLE], w.BOOL),
            'TerminateJobObject': ([w.HANDLE, w.UINT], w.BOOL),
            'CreateEventW': ([c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR], w.HANDLE),
            'SetEvent': ([w.HANDLE], w.BOOL),
            'CloseHandle': ([w.HANDLE], w.BOOL),
            'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'IsProcessInJob': ([w.HANDLE, w.HANDLE, c.POINTER(w.BOOL)], w.BOOL),
            'GetProcessTimes': ([w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME),
                                 c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)], w.BOOL),
            'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(k, name)
            function.argtypes, function.restype = arguments, result
        self.handle = k.CreateJobObjectW(None, None)
        if not self.handle:
            raise c.WinError(c.get_last_error())
        try:
            limits = ExtendedLimits()
            limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; no breakaway.
            if not k.SetInformationJobObject(self.handle, 9, c.byref(limits), c.sizeof(limits)):
                raise c.WinError(c.get_last_error())
        except BaseException:
            self.close()
            raise

    def start(self, command, **options):
        k, c = self.k, self.c
        gate = k.CreateEventW(None, True, False, None)
        if not gate:
            raise c.WinError(c.get_last_error())
        process = None
        try:
            os.set_handle_inheritable(gate, True)
            startup = subprocess.STARTUPINFO()
            startup.lpAttributeList = {'handle_list': [gate]}
            process = subprocess.Popen(
                [sys.executable, '-I', '-S', str(Path(__file__).resolve()), '--gate', str(gate), *command],
                startupinfo=startup, close_fds=True, **options)
            # Popen retains this exact process handle; never reopen a reusable PID.
            if not k.AssignProcessToJobObject(self.handle, int(process._handle)):
                raise c.WinError(c.get_last_error())
            if not k.SetEvent(gate):
                raise c.WinError(c.get_last_error())
            return process
        except BaseException:
            if process is not None:
                process.kill()
                process.wait(timeout=CLEANUP_SECONDS)
            raise
        finally:
            k.CloseHandle(gate)

    def accounting(self):
        info = self.Accounting()
        if not self.k.QueryInformationJobObject(self.handle, 1, self.c.byref(info),
                                                self.c.sizeof(info), None):
            raise self.c.WinError(self.c.get_last_error())
        return info

    def active(self):
        return self.accounting().active

    def _member_ids(self):
        # One bounded snapshot, never a system-wide PID scan. A growing or
        # oversized list is an unverifiable cleanup, not permission to guess.
        capacity = 4096
        class ProcessIds(self.c.Structure):
            _fields_ = [('assigned', self.w.DWORD), ('count', self.w.DWORD),
                        ('ids', self.c.c_size_t * capacity)]
        info = ProcessIds()
        if not self.k.QueryInformationJobObject(self.handle, 3, self.c.byref(info),
                                                self.c.sizeof(info), None):
            raise self.c.WinError(self.c.get_last_error())
        if info.count > capacity or info.count != info.assigned:
            raise RuntimeError('owned Windows Job process snapshot was incomplete')
        return list(info.ids[:info.count])

    def retain_members(self):
        # Query/synchronize access only. Every newly opened handle is verified
        # against this exact Job before retention; numeric PID reuse cannot
        # authorize signaling an unrelated process. Termination uses only Job.
        for pid in self._member_ids():
            if self.cleanup_deadline is not None and time.monotonic() >= self.cleanup_deadline:
                raise RuntimeError('owned process snapshot exceeded the cleanup deadline')
            member = self.k.OpenProcess(0x101000, False, pid)  # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
            if not member:
                raise self.c.WinError(self.c.get_last_error())
            try:
                belongs = self.w.BOOL()
                if not self.k.IsProcessInJob(member, self.handle, self.c.byref(belongs)) or not belongs.value:
                    raise RuntimeError('process snapshot no longer identifies an owned Windows Job member')
                created, exited, kernel, user = (self.w.FILETIME() for _ in range(4))
                if not self.k.GetProcessTimes(member, self.c.byref(created), self.c.byref(exited),
                                             self.c.byref(kernel), self.c.byref(user)):
                    raise self.c.WinError(self.c.get_last_error())
                identity = (pid, created.dwHighDateTime << 32 | created.dwLowDateTime)
                if identity not in self.members:
                    if len(self.members) >= 4096:
                        raise RuntimeError('owned Windows Job retained-process budget exceeded')
                    self.members[identity] = member
                    member = None
            finally:
                if member:
                    self.k.CloseHandle(member)

    def terminate(self):
        if self.cleanup_deadline is None:
            self.cleanup_deadline = time.monotonic() + CLEANUP_SECONDS
        try:
            before = self.accounting().total
            if self.cleanup_population is None:
                self.cleanup_population = before
            self.retain_members()
            after = self.accounting().total
            if before != self.cleanup_population or after != self.cleanup_population:
                raise RuntimeError('owned Windows Job population changed during process snapshot')
        except Exception as exc:
            self.cleanup_error = self.cleanup_error or f'owned process snapshot could not be verified: {exc}'
        # Even an unverifiable snapshot must stop the owned Job; report the
        # verification failure after best-effort, bounded resource cleanup.
        if not self.k.TerminateJobObject(self.handle, 1):
            self.cleanup_error = self.cleanup_error or str(self.c.WinError(self.c.get_last_error()))
        try:
            if self.cleanup_population is not None and self.accounting().total != self.cleanup_population:
                self.cleanup_error = self.cleanup_error or 'owned Windows Job population changed during termination'
        except Exception as exc:
            self.cleanup_error = self.cleanup_error or f'owned termination accounting could not be verified: {exc}'

    def drain(self):
        deadline = self.cleanup_deadline or time.monotonic() + CLEANUP_SECONDS
        while True:
            if self.cleanup_population is not None and self.accounting().total != self.cleanup_population:
                self.cleanup_error = self.cleanup_error or 'owned Windows Job population changed during cleanup'
            signaled = True
            for member in self.members.values():
                state = self.k.WaitForSingleObject(member, 0)
                if state not in (0, 258):  # WAIT_OBJECT_0, WAIT_TIMEOUT
                    raise self.c.WinError(self.c.get_last_error())
                signaled = signaled and state == 0
            if not self.active() and signaled:
                if self.cleanup_error:
                    raise RuntimeError(self.cleanup_error)
                return
            if time.monotonic() >= deadline:
                raise RuntimeError('owned Windows Job/process termination did not complete within 5 seconds')
            time.sleep(.01)

    def close(self):
        for member in self.members.values():
            self.k.CloseHandle(member)
        self.members.clear()
        if self.handle:
            self.k.CloseHandle(self.handle)
            self.handle = None


class OwnedProcess:
    def __init__(self, command, **options):
        self.lock = threading.RLock()
        self.windows = os.name == 'nt'
        self.posix_cleanup_deadline = None
        self.job = WindowsJob() if os.name == 'nt' else None
        try:
            self.process = (self.job.start([str(x) for x in command], **options) if self.job else
                            subprocess.Popen(command, start_new_session=True, **options))
        except BaseException:
            if self.job:
                self.job.close()
                self.job = None
            raise

    def poll(self):
        with self.lock:
            if self.windows or self.process.returncode is not None:
                return self.process.poll()
            # Keep an exited POSIX leader unreaped until group cleanup. Its PID
            # cannot be recycled while descendants still hold inherited pipes.
            info = os.waitid(os.P_PID, self.process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if info is None:
                return None
            return info.si_status if info.si_code == os.CLD_EXITED else -info.si_status

    def terminate(self):
        with self.lock:
            if self.windows:
                if self.job:
                    self.job.terminate()
                return
            # The direct child is not reaped until after timeout cleanup. Its
            # unreaped PID pins the process-group number against reuse.
            if self.process.returncode is None:
                if sys.platform == 'darwin' and self.posix_cleanup_deadline is None:
                    self.posix_cleanup_deadline = time.monotonic() + CLEANUP_SECONDS
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except PermissionError as error:
                    # XNU killpg1 excludes zombies and returns EPERM if none
                    # were signalable. Accept only that proven dead-group case;
                    # retain the leader (WNOWAIT) until both snapshots finish.
                    if sys.platform != 'darwin' or error.errno != errno.EPERM:
                        raise
                    # A previous callback may already have delivered SIGKILL,
                    # before waitid or libproc observes the completed exit.
                    # Every callback and snapshot shares the original deadline.
                    deadline = self.posix_cleanup_deadline
                    while time.monotonic() < deadline:
                        if self.poll() is not None:
                            try:
                                if _darwin_group_exited(self.process.pid, deadline):
                                    return
                            except ProcessLookupError:
                                # A listed member may be reaped before pidinfo.
                                # Retry the COMPLETE proof, never signal that PID.
                                pass
                        time.sleep(min(.01, max(0, deadline - time.monotonic())))
                    raise

    def kill(self):
        with self.lock:
            self.terminate()
            # Budget-reader callbacks share this lock: none may signal a
            # POSIX group in the interval between waitpid and returncode update.
            deadline = self.job.cleanup_deadline if self.job else self.posix_cleanup_deadline
            remaining = (max(.01, deadline - time.monotonic())
                         if deadline is not None else CLEANUP_SECONDS)
            self.process.wait(timeout=remaining)
            if self.job:
                self.job.drain()

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(self.process.args, timeout)
            time.sleep(.01)
        with self.lock:
            if not self.windows:
                self.terminate()  # Group number still pinned by the unreaped leader.
            return self.process.wait(timeout=max(.01, deadline - time.monotonic()))

    def finish(self):
        """Normal completion must leave no Windows descendant running."""
        with self.lock:
            try:
                if self.job and self.job.active():
                    self.kill()
                    raise RuntimeError('process exited with live descendants in its Windows Job')
                if self.job and self.job.cleanup_deadline is not None:
                    self.job.drain()
            finally:
                if self.job:
                    self.job.close()
                    self.job = None

    def communicate(self, timeout):
        stdout, stderr = io.BytesIO(), io.BytesIO()
        budget = CaptureBudget(self.terminate)
        readers = []
        deadline = time.monotonic() + timeout
        try:
            readers.append(PipeReader(self.process.stdout, budget, stdout))
            readers.append(PipeReader(self.process.stderr, budget, stderr))
            while not all(reader.done.is_set() for reader in readers):
                budget.check()
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(self.process.args, timeout,
                                                    output=stdout.getvalue(), stderr=stderr.getvalue())
                time.sleep(.01)
            budget.check()
            for reader in readers:
                if reader.error:
                    raise reader.error
            self.wait(max(.01, deadline - time.monotonic()))
        except BaseException:
            try:
                self.kill()
            finally:
                self.finish()
            raise
        finally:
            for reader in readers:
                reader.close()
            self.process.stdout.close()
            self.process.stderr.close()
        self.finish()
        return stdout.getvalue(), stderr.getvalue()


def _gate_main():
    import ctypes as c
    from ctypes import wintypes as w
    k = c.WinDLL('kernel32', use_last_error=True)
    k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    k.WaitForSingleObject.restype = w.DWORD
    k.CloseHandle.argtypes = [w.HANDLE]
    k.CloseHandle.restype = w.BOOL
    handle = int(sys.argv[2])
    # A failed parent cannot leave a gate waiting forever even before Job assignment.
    if k.WaitForSingleObject(handle, 10000) != 0:
        raise RuntimeError('consumer process gate was not assigned and released')
    k.CloseHandle(handle)
    result = subprocess.run(sys.argv[3:])
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    if os.name != 'nt' or len(sys.argv) < 4 or sys.argv[1] != '--gate':
        raise SystemExit('internal Windows consumer process gate only')
    _gate_main()
