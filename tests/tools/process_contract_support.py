"""Host mechanics for process contracts; never changes production execution."""

import os
import json
import errno
from functools import lru_cache
from pathlib import Path
import signal
import subprocess
import sys
import time


def console_options():
    # Give cancellation fixtures a private console even when CTest runs over
    # SSH/WinRM without one. Events must never reach the caller's console.
    return {"creationflags": subprocess.CREATE_NEW_CONSOLE} if os.name == "nt" else {}


def cancel_process(process, stop_signal):
    if os.name != "nt":
        process.send_signal(stop_signal)
        return
    # A short-lived helper attaches only to the fixture's private console.
    # Python's send_signal(CTRL_BREAK_EVENT) requires a shared console, which
    # a headless CTest parent does not have. Exercise the real console handler,
    # not a test-only write to the production cancellation flag.
    helper = r'''
import ctypes as c, sys, threading
from ctypes import wintypes as w
k = c.WinDLL("kernel32", use_last_error=True)
k.FreeConsole()
k.AttachConsole.argtypes = [w.DWORD]
k.AttachConsole.restype = w.BOOL
if not k.AttachConsole(int(sys.argv[1])): raise c.WinError(c.get_last_error())
handler_type = c.WINFUNCTYPE(w.BOOL, w.DWORD)
handled = threading.Event()
def handle(event):
    handled.set()
    return True
handler = handler_type(handle)
k.SetConsoleCtrlHandler.argtypes = [handler_type, w.BOOL]
k.SetConsoleCtrlHandler.restype = w.BOOL
if not k.SetConsoleCtrlHandler(handler, True): raise c.WinError(c.get_last_error())
k.GenerateConsoleCtrlEvent.argtypes = [w.DWORD, w.DWORD]
k.GenerateConsoleCtrlEvent.restype = w.BOOL
if not k.GenerateConsoleCtrlEvent(1, 0): raise c.WinError(c.get_last_error())
# Delivery is asynchronous. FreeConsole resets this process's handler table;
# detaching immediately after GenerateConsoleCtrlEvent can make our own queued
# BREAK hit the default handler and terminate this helper instead of returning
# success. Keep the handler/console alive through delivery, then let ordinary
# process exit perform the detach.
if not handled.wait(5): raise RuntimeError("control event did not reach helper")
'''
    result = subprocess.run([sys.executable, "-c", helper, str(process.pid)],
                            capture_output=True, text=True, timeout=10,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)


def _windows_process(pid, access):
    import ctypes as c
    from ctypes import wintypes as w
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.WaitForSingleObject.restype = w.DWORD
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.TerminateProcess.restype = w.BOOL
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    kernel.GetProcessTimes.argtypes = [w.HANDLE, c.POINTER(w.FILETIME),
                                      c.POINTER(w.FILETIME), c.POINTER(w.FILETIME),
                                      c.POINTER(w.FILETIME)]
    kernel.GetProcessTimes.restype = w.BOOL
    handle = kernel.OpenProcess(access, False, pid)
    if not handle and c.get_last_error() != 87:  # Already exited/unknown PID.
        raise c.WinError(c.get_last_error())
    return kernel, handle


def _windows_creation(kernel, handle):
    import ctypes as c
    from ctypes import wintypes as w
    created, exited, system, user = (w.FILETIME() for _ in range(4))
    if not kernel.GetProcessTimes(handle, c.byref(created), c.byref(exited),
                                  c.byref(system), c.byref(user)):
        raise c.WinError(c.get_last_error())
    return (created.dwHighDateTime << 32) | created.dwLowDateTime


@lru_cache(maxsize=1)
def _darwin_api():
    import ctypes as c

    # XNU 11417 (macOS 15) libproc/proc_info ABI; these private test-harness
    # interfaces are checked at runtime, not assumed to exist on older hosts.
    class BsdInfo(c.Structure):
        _fields_ = [(name, c.c_uint32) for name in
                    ('flags', 'status', 'exit_status', 'pid', 'ppid', 'uid', 'gid',
                     'ruid', 'rgid', 'svuid', 'svgid', 'reserved')] + [
            ('comm', c.c_char * 16), ('name', c.c_char * 32)] + [
            (name, c.c_uint32) for name in ('nfiles', 'pgid', 'job_count', 'tty', 'tty_pgid')] + [
            ('nice', c.c_int32), ('start_seconds', c.c_uint64), ('start_useconds', c.c_uint64)]

    class UniqueInfo(c.Structure):
        _fields_ = [('uuid', c.c_uint8 * 16), ('unique_id', c.c_uint64),
                    ('parent_id', c.c_uint64), ('version', c.c_int32),
                    ('parent_version', c.c_int32), ('reserved2', c.c_uint64),
                    ('reserved3', c.c_uint64)]

    class Info(c.Structure):
        _fields_ = [('bsd', BsdInfo), ('unique', UniqueInfo)]

    sizes = (c.sizeof(BsdInfo), c.sizeof(UniqueInfo), c.sizeof(Info))
    if sizes != (136, 56, 192):
        raise RuntimeError(f'unsupported Darwin process identity layout: {sizes}')
    lib = c.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = [c.c_int, c.c_int, c.c_uint64, c.c_void_p, c.c_int]
    lib.proc_pidinfo.restype = c.c_int
    # This XNU API checks PID + version while retaining the process in-kernel.
    # If unavailable, fail explicitly; a check followed by os.kill is not safe.
    lib.proc_signal_with_audittoken.argtypes = [c.c_void_p, c.c_int]
    lib.proc_signal_with_audittoken.restype = c.c_int
    return c, lib, Info


def _darwin_identity(pid):
    c, lib, Info = _darwin_api()
    if type(pid) is not int or not 0 < pid <= 0x7fffffff:
        raise ValueError('invalid Darwin process identity PID')
    info = Info()
    c.set_errno(0)
    size = lib.proc_pidinfo(pid, 18, 1, c.byref(info), c.sizeof(info))
    if size == 0 and c.get_errno():
        raise OSError(c.get_errno(), 'Darwin process identity lookup failed')
    if (size != c.sizeof(info) or info.bsd.pid != pid or
            info.bsd.status not in (1, 2, 3, 4, 5) or not info.unique.unique_id):
        raise RuntimeError(f'Darwin process identity was not established: bytes={size}/192, '
                           f'pid={info.bsd.pid}/{pid}, state={info.bsd.status}')
    # Keep the existing two-field fixture record. The opaque creation token
    # includes the unique lifetime ID and the exact PID version used to signal.
    created = (info.unique.unique_id << 32) | (info.unique.version & 0xffffffff)
    return created, 'Z' if info.bsd.status == 5 else 'R'


def _darwin_cleanup(identity):
    if not process_alive(identity):
        return
    c, lib, _ = _darwin_api()
    token = (c.c_uint32 * 8)()
    token[5] = identity['pid']
    token[7] = identity['created'] & 0xffffffff
    # Use the RECORDED version, never a fresh replacement process's version.
    # libproc returns an errno value directly (not -1/ctypes.get_errno()).
    error = lib.proc_signal_with_audittoken(c.byref(token), signal.SIGKILL)
    if error not in (0, errno.ESRCH):
        raise OSError(error, 'Darwin identity-qualified process cleanup failed')


def _posix_identity(pid):
    if sys.platform == 'darwin':
        return _darwin_identity(pid)
    # On Linux field 22 is the creation tick;
    # split after ')' because the process name itself may contain whitespace.
    fields = (Path('/proc') / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()
    return int(fields[19]), fields[0]


def process_identity(pid):
    """Capture identity while the fixture still owns a live process."""
    if os.name == 'nt':
        kernel, handle = _windows_process(pid, 0x1000)  # QUERY_LIMITED_INFORMATION
        if not handle:
            raise ProcessLookupError(pid)
        try:
            created = _windows_creation(kernel, handle)
        finally:
            kernel.CloseHandle(handle)
    else:
        created, _ = _posix_identity(pid)
    return {'pid': pid, 'created': created}


def record_processes(path, *pids):
    """Publish complete identity records, never an unqualified reusable PID."""
    path = Path(path)
    temporary = path.with_name(path.name + '.pending')
    temporary.write_text(json.dumps([process_identity(pid) for pid in pids]),
                         encoding='utf-8', newline='\n')
    temporary.replace(path)


def process_alive(identity):
    pid = identity['pid']
    if os.name == "nt":
        kernel, handle = _windows_process(pid, 0x101000)  # SYNCHRONIZE | QUERY
        if not handle:
            return False
        try:
            if _windows_creation(kernel, handle) != identity['created']:
                return False  # PID reused; the recorded fixture is gone.
            status = kernel.WaitForSingleObject(handle, 0)
            assert status in (0, 258), (pid, status)
            return status == 258
        finally:
            kernel.CloseHandle(handle)
    try:
        created, state = _posix_identity(pid)
        # Zombies cannot execute and can outlive our reaped parent under PID 1.
        return created == identity['created'] and state != 'Z'
    except (FileNotFoundError, ProcessLookupError):
        return False


def assert_stopped(identity):
    deadline = time.monotonic() + 5
    while process_alive(identity) and time.monotonic() < deadline:
        time.sleep(.02)
    assert not process_alive(identity), f"test child {identity} survived runner completion"


def cleanup_process(identity):
    pid = identity['pid']
    if os.name == "nt":
        kernel, handle = _windows_process(pid, 0x101001)  # SYNCHRONIZE | QUERY | TERMINATE
        if handle:
            try:
                # Verify and terminate through the SAME handle; PID reuse
                # between a separate liveness check and OpenProcess is unsafe.
                if (_windows_creation(kernel, handle) == identity['created'] and
                        kernel.WaitForSingleObject(handle, 0) == 258):
                    assert kernel.TerminateProcess(handle, 1), pid
                    assert kernel.WaitForSingleObject(handle, 5000) == 0, pid
            finally:
                kernel.CloseHandle(handle)
    elif sys.platform == 'darwin':
        _darwin_cleanup(identity)
    else:
        try:
            handle = os.pidfd_open(pid)
        except ProcessLookupError:
            return
        try:
            if process_alive(identity):
                try:
                    signal.pidfd_send_signal(handle, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        finally:
            os.close(handle)


def check_process_identity_guards():
    """A stale identity must never observe or terminate a reused live PID."""
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    try:
        identity = process_identity(child.pid)
        stale = {**identity, 'created': identity['created'] + 1}
        assert process_alive(identity)
        assert not process_alive(stale)
        assert_stopped(stale)
        cleanup_process(stale)
        assert child.poll() is None and process_alive(identity), 'stale identity killed a live process'
        cleanup_process(identity)
        assert_stopped(identity)
        child.wait(timeout=5)
    finally:
        # Popen retains its own handle/parent-owned child identity.
        if child.poll() is None:
            child.kill()
        child.wait(timeout=5)
