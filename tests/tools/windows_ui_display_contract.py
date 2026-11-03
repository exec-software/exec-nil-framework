#!/usr/bin/env python3
"""Windows GTK display contracts with Job-owned, HWND-targeted input only.

No foreground-window, desktop-switching, global keyboard-state or SendInput API
is used. A successful PostMessage is not a successful test: the unchanged Ny
fixtures must report their text/toggle/click and close-request assertions passed.
The Win32 input path requires qualification against a real GTK desktop runtime.
"""
from __future__ import annotations

import argparse
import ctypes as c
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'validation/ny-3d/scripts'))
from debugger_process import CaptureBudget, OwnedProcess, PipeReader

KEYBOARD_TITLE = 'NIL2 UI keyboard probe'
CLOSE_TITLE = 'NIL2 UI close probe'
EXPECTED_TITLES = {KEYBOARD_TITLE, CLOSE_TITLE}
WM_CLOSE, WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x10, 0x100, 0x101, 0x102
VK_PACKET, VK_TAB, VK_SPACE = 0xE7, 0x09, 0x20
DESKTOP_NAMES = {'widgets_layout_and_model_binding', 'window_ownership_and_size_errors',
                 'dynamic_subtrees_and_effective_state'}
INTERACTION_NAMES = {'keyboard_events', 'close_request'}


class DisplayUnavailable(RuntimeError):
    """Known unsupported desktop environment, not a loader/API/fixture failure."""


def desktop_preflight(api, require_display=False):
    try:
        return {'status': 'READY', 'desktop': api.desktop()}
    except DisplayUnavailable as error:
        if require_display:
            raise
        return {'status': 'SKIP', 'reason': str(error)}


def configured_provider_selection(probe, environment):
    if 'NIL_UI_GTK_DIRECTORY' in environment:
        return 'environment'
    # The application directory is the provider's other supported selection.
    # Even a partial/broken deployment must be checked before a headless skip.
    for stem in ('gtk-4-1.dll', 'gobject-2.0-0.dll', 'glib-2.0-0.dll'):
        for name in (stem, 'lib' + stem):
            candidate = Path(probe).parent / name
            if candidate.exists() or candidate.is_symlink():
                return 'application-directory'
    return None


@dataclass(frozen=True)
class Member:
    pid: int
    created: int
    handle: int


@dataclass(frozen=True)
class Window:
    hwnd: int
    tid: int
    member: Member
    title: str

    def evidence(self):
        return {'hwnd': hex(self.hwnd), 'threadId': self.tid,
                'processId': self.member.pid, 'processCreationTime': self.member.created,
                'title': self.title}


class Win32:
    """Small query/post boundary; every retained process handle belongs to Job."""
    def __init__(self):
        from ctypes import wintypes as w
        self.w = w
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.u = c.WinDLL('user32', use_last_error=True)
        self.callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        signatures = {
            'GetCurrentThreadId': ([], w.DWORD),
            'GetCurrentProcessId': ([], w.DWORD),
            'ProcessIdToSessionId': ([w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
            'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'GetProcessId': ([w.HANDLE], w.DWORD),
            'GetProcessTimes': ([w.HANDLE, *[c.POINTER(w.FILETIME)] * 4], w.BOOL),
            'IsProcessInJob': ([w.HANDLE, w.HANDLE, c.POINTER(w.BOOL)], w.BOOL),
            'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
            'CloseHandle': ([w.HANDLE], w.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.k, name)
            function.argtypes, function.restype = arguments, result
        signatures = {
            'EnumWindows': ([self.callback_type, w.LPARAM], w.BOOL),
            'GetWindowThreadProcessId': ([w.HWND, c.POINTER(w.DWORD)], w.DWORD),
            'IsWindowVisible': ([w.HWND], w.BOOL),
            'GetWindowTextW': ([w.HWND, w.LPWSTR, c.c_int], c.c_int),
            'PostMessageW': ([w.HWND, w.UINT, w.WPARAM, w.LPARAM], w.BOOL),
            'MapVirtualKeyW': ([w.UINT, w.UINT], w.UINT),
            'GetProcessWindowStation': ([], w.HANDLE),
            'GetThreadDesktop': ([w.DWORD], w.HANDLE),
            'OpenInputDesktop': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'CloseDesktop': ([w.HANDLE], w.BOOL),
            'GetUserObjectInformationW': ([w.HANDLE, c.c_int, c.c_void_p, w.DWORD,
                                          c.POINTER(w.DWORD)], w.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.u, name)
            function.argtypes, function.restype = arguments, result

    def desktop(self):
        class Flags(c.Structure):
            _fields_ = [('inherit', self.w.BOOL), ('reserved', self.w.BOOL),
                        ('flags', self.w.DWORD)]
        flags, used, session = Flags(), self.w.DWORD(), self.w.DWORD()
        station = self.u.GetProcessWindowStation()  # Borrowed, never closed.
        if not station or not self.u.GetUserObjectInformationW(
                station, 1, c.byref(flags), c.sizeof(flags), c.byref(used)):
            raise c.WinError(c.get_last_error())
        if not self.k.ProcessIdToSessionId(self.k.GetCurrentProcessId(), c.byref(session)):
            raise c.WinError(c.get_last_error())
        if not flags.flags & 1 or session.value == 0:  # WSF_VISIBLE, nonservice session.
            raise DisplayUnavailable('Windows display tests require an existing interactive desktop session')
        desktop = self.u.OpenInputDesktop(0, False, 1)  # DESKTOP_READOBJECTS only.
        if not desktop:
            raise c.WinError(c.get_last_error())
        try:
            def name(handle):
                buffer = c.create_unicode_buffer(256)
                if not self.u.GetUserObjectInformationW(handle, 2, buffer,
                                                       c.sizeof(buffer), c.byref(used)):
                    raise c.WinError(c.get_last_error())
                return buffer.value
            current = name(self.u.GetThreadDesktop(self.k.GetCurrentThreadId()))
            active = name(desktop)
            if current != active:
                raise DisplayUnavailable('test thread is not on the current input desktop; no switching is allowed')
            return {'sessionId': session.value, 'desktop': current, 'visibleWindowStation': True}
        finally:
            self.u.CloseDesktop(desktop)

    def windows(self):
        result, errors = [], []
        deadline = time.monotonic() + 2
        def collect(hwnd, unused):
            if len(result) >= 4096 or time.monotonic() >= deadline:
                errors.append('window enumeration exceeded its bounded snapshot')
                return False
            result.append(int(hwnd))
            return True
        callback = self.callback_type(collect)
        succeeded = self.u.EnumWindows(callback, 0)
        if errors:
            raise RuntimeError(errors[0])
        if not succeeded:
            raise c.WinError(c.get_last_error())
        return result

    def owner(self, hwnd):
        pid = self.w.DWORD()
        tid = self.u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        return pid.value, tid

    def title(self, hwnd):
        text = c.create_unicode_buffer(256)
        self.u.GetWindowTextW(hwnd, text, len(text))
        return text.value

    def visible(self, hwnd):
        return bool(self.u.IsWindowVisible(hwnd))

    def creation(self, handle):
        values = [self.w.FILETIME() for _ in range(4)]
        if not self.k.GetProcessTimes(handle, *[c.byref(value) for value in values]):
            raise c.WinError(c.get_last_error())
        return values[0].dwHighDateTime << 32 | values[0].dwLowDateTime

    def belongs(self, handle, job):
        belongs = self.w.BOOL()
        if not self.k.IsProcessInJob(handle, job.handle, c.byref(belongs)):
            raise c.WinError(c.get_last_error())
        return bool(belongs.value)

    def retain(self, pid, job):
        # Caller obtained this PID from this Job's bounded snapshot, not a
        # system process list. Verify the same handle before retaining it.
        handle = self.k.OpenProcess(0x101000, False, pid)
        if not handle:
            raise c.WinError(c.get_last_error())
        try:
            if self.k.GetProcessId(handle) != pid or not self.belongs(handle, job):
                raise RuntimeError('window process no longer belongs to the private test Job')
            created = self.creation(handle)
            if self.k.WaitForSingleObject(handle, 0) != 258:
                raise RuntimeError('window process exited before input authorization')
            identity = (pid, created)
            if identity in job.members:
                return Member(pid, created, job.members[identity])
            if len(job.members) >= 4096:
                raise RuntimeError('owned window process retention budget exceeded')
            job.members[identity] = handle
            member = Member(pid, created, handle)
            handle = None  # Ownership transferred to WindowsJob.close().
            return member
        finally:
            if handle:
                self.k.CloseHandle(handle)

    def valid(self, member, job):
        return (self.k.WaitForSingleObject(member.handle, 0) == 258
                and self.k.GetProcessId(member.handle) == member.pid
                and self.creation(member.handle) == member.created
                and self.belongs(member.handle, job))

    def scan(self, key):
        scan = self.u.MapVirtualKeyW(key, 0)  # MAPVK_VK_TO_VSC, no layout mutation.
        if not scan or scan > 0xFF:
            raise RuntimeError('expected a single-byte Tab/Space scan code')
        return scan

    def post(self, hwnd, message, key, flags):
        if not hwnd or hwnd == 0xFFFF:
            raise RuntimeError('broadcast/null HWND is forbidden')
        if not self.u.PostMessageW(hwnd, message, key, flags):
            raise c.WinError(c.get_last_error())


def keyboard_messages(api):
    # GTK's Win32 backend builds a composed key event from WM_CHAR immediately
    # following WM_KEYDOWN. Standalone WM_CHAR does not prove its key path.
    # VK_PACKET avoids borrowing/mutating the user's global Shift/key state.
    # Queue acceptance alone is never treated as proof of GTK event delivery.
    result = []
    for char in 'Matrix':
        result.append([(WM_KEYDOWN, VK_PACKET, 1), (WM_CHAR, ord(char), 1),
                       (WM_KEYUP, VK_PACKET, 0xC0000001)])
    for key in (VK_TAB, VK_SPACE, VK_TAB, VK_SPACE):
        flags = 1 | api.scan(key) << 16
        result.append([(WM_KEYDOWN, key, flags), (WM_KEYUP, key, flags | 0xC0000000)])
    return result


class WindowsInput:
    """Only a retained, verified Job process can authorize a target HWND."""
    def __init__(self, owned, api, record, clock=time.monotonic):
        if owned.job is None:
            raise RuntimeError('Windows input requires a private Windows Job')
        self.owned, self.api, self.record, self.clock = owned, api, record, clock
        self.pending, self.handled = {}, set()
        record['windows'], record['messages'] = [], []

    def verify(self, window):
        # Called immediately before EACH post, including each message in a key
        # packet; retaining the process handle prevents PID reuse authorization.
        if (self.owned.job is None or not self.api.valid(window.member, self.owned.job)
                or self.api.owner(window.hwnd) != (window.member.pid, window.tid)
                or not self.api.visible(window.hwnd) or self.api.title(window.hwnd) != window.title):
            raise RuntimeError('owned HWND identity changed before input; refusing to post')

    def discover(self):
        ids = set(self.owned.job._member_ids())
        for hwnd in self.api.windows():
            pid, tid = self.api.owner(hwnd)
            if pid not in ids or not tid:
                continue  # Do not inspect unrelated window titles.
            member = self.api.retain(pid, self.owned.job)
            if not self.api.visible(hwnd):
                continue
            title = self.api.title(hwnd)
            if title not in EXPECTED_TITLES:
                continue
            window = Window(hwnd, tid, member, title)
            if window in self.pending or window in self.handled:
                continue
            if any(other.title == title for other in [*self.pending, *self.handled]):
                raise RuntimeError('duplicate fixture window identity in one route')
            self.verify(window)
            self.record['windows'].append(window.evidence())
            packets = ([[(WM_CLOSE, 0, 0)]] if title == CLOSE_TITLE else keyboard_messages(self.api))
            # The fixture requests entry focus just after showing the window.
            # No foreground/focus operation is performed by the harness.
            self.pending[window] = (self.clock() + .15, packets)

    def tick(self):
        with self.owned.lock:  # Do not race an output-budget-triggered Job close.
            if self.owned.job is None:
                raise RuntimeError('private UI Job was closed while driving input')
            self.discover()
            for window, (ready, packets) in list(self.pending.items()):
                if self.clock() < ready:
                    continue
                packet = packets.pop(0)
                for message, key, flags in packet:
                    self.verify(window)
                    self.api.post(window.hwnd, message, key, flags)
                    self.record['messages'].append({**window.evidence(), 'message': hex(message),
                                                    'wParam': key, 'lParam': flags})
                if packets:
                    self.pending[window] = (self.clock() + .06, packets)
                else:
                    self.handled.add(window)
                    del self.pending[window]

    def complete(self):
        if self.pending or {window.title for window in self.handled} != EXPECTED_TITLES:
            raise AssertionError('both owned keyboard and close fixture windows must receive their full sequence')


def checked(command, env, root, record, *, timeout=120, input_api=None, output_limit=None):
    """Capture bounded byte logs while driving input; finish the exact owned Job."""
    command = [str(part) for part in command]
    record.update(command=command, timeoutSeconds=timeout, cleanupVerified=False)
    record['stdout'], record['stderr'] = record['name'] + '.stdout', record['name'] + '.stderr'
    streams, readers, child, failure, driver = [], [], None, None, None
    started = time.monotonic()
    try:
        streams = [(root / record[key]).open('wb') for key in ('stdout', 'stderr')]
        child = OwnedProcess(command, env=env, cwd=root, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        budget = CaptureBudget(child.terminate, maximum=output_limit)
        readers = [PipeReader(pipe, budget, sink) for pipe, sink in
                   zip((child.process.stdout, child.process.stderr), streams)]
        if input_api is not None:
            driver = WindowsInput(child, input_api, record)
        deadline = started + timeout
        while child.poll() is None or not all(reader.done.is_set() for reader in readers):
            budget.check()
            for reader in readers:
                if reader.error:
                    raise reader.error
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(command, timeout)
            if driver is not None and child.poll() is None:
                driver.tick()
            time.sleep(.01)
        budget.check()
        child.wait(max(.01, deadline - time.monotonic()))
        record['exit'] = child.process.returncode
        if child.job:
            child.job.drain()  # Retained window process handles must be signaled, not just Job-empty.
        child.finish()
        record['cleanupVerified'] = True
        if record['exit'] != 0:
            raise AssertionError(f"{record['name']} failed with exit {record['exit']}; see captured logs")
        if driver is not None:
            driver.complete()
    except BaseException as error:
        failure = error
        record['error'] = str(error)
        if child is not None:
            try:
                child.kill()
                child.finish()
                record['cleanupVerified'] = True
            except BaseException as cleanup_error:
                record['cleanupError'] = str(cleanup_error)
                record['cleanupVerified'] = False
                failure = RuntimeError(f'{error}; owned cleanup failed: {cleanup_error}')
    finally:
        for reader in readers:
            try:
                reader.close()
            except BaseException as error:
                record['readerCleanupError'] = str(error)
                record['cleanupVerified'] = False
                failure = failure or error
        if child is not None:
            # Also close pipes when reader construction or driver setup failed.
            for pipe in (child.process.stdout, child.process.stderr):
                if pipe and not pipe.closed:
                    pipe.close()
            if child.job is not None:
                try:
                    child.finish()
                except BaseException as error:
                    record['cleanupError'] = str(error)
                    record['cleanupVerified'] = False
                    failure = failure or error
        for stream in streams:
            stream.close()
        record['elapsedSeconds'] = round(time.monotonic() - started, 3)
        for key in ('stdout', 'stderr'):
            path = root / record[key]
            if path.exists():
                record[key + 'Sha256'] = digest(path)
                record[key + 'Bytes'] = path.stat().st_size
    if failure is not None:
        raise failure
    return (root / record['stdout']).read_text(encoding='utf-8')


def validate_probe_summary(output, name):
    summaries = {
        'provider-display': ('UI runtime: ', ' checks passed (--display)'),
        'lifecycle-display': ('UI lifecycle: ', ' checks passed'),
    }
    prefix, suffix = summaries[name]
    match = re.fullmatch(re.escape(prefix) + r'([1-9][0-9]{0,9})' + re.escape(suffix) + r'\r?\n', output)
    # Both C++ fixtures count in a signed Windows int. Provider event polling
    # increments that counter, so timing can change its final value: require a
    # complete positive, representable summary, not an observed magic total.
    if match is None or int(match[1]) > 2147483647:
        raise AssertionError(f'{name} did not emit one complete bounded completion summary')
    count = int(match[1])
    # Unlike provider polling, the current lifecycle fixture executes a fixed
    # text/binary VM and native assertion sequence. Review this with any change
    # to tests/ui_lifecycle_tests.cpp; exit zero alone is never completion proof.
    if name == 'lifecycle-display' and count != 235:
        raise AssertionError('lifecycle-display completion summary must report 235 checks')
    return count


def validate_report(output, names, routes):
    report = json.loads(output)
    tests = report.get('tests')
    if not isinstance(tests, list) or report.get('errors') != []:
        raise AssertionError('niltest report must contain tests and no errors')
    expected = {(name, route) for name in names for route in routes}
    actual = [(test.get('name'), test.get('route')) for test in tests]
    if len(actual) != len(expected) or set(actual) != expected:
        raise AssertionError(f'niltest did not execute the exact fixture/route matrix: {actual}')
    if any(test.get('status') != 'passed' for test in tests):
        raise AssertionError('niltest UI assertion failed; see original JSON report')
    return report


def digest(path):
    with Path(path).open('rb') as source:
        result = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
        return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xvfb', help='Accepted for shared CLI compatibility; unused on Windows.')
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--require-display', action='store_true',
                        help='Qualification mode: known unavailable desktop is a failure, never a skip.')
    for name in ('tester', 'compiler', 'interpreter', 'sdk', 'source', 'interaction-source',
                 'provider-test', 'lifecycle-test', 'evidence-directory'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    if sys.platform != 'win32':
        print('SKIP: Windows HWND/Job display contract requires Windows')
        return 77
    api = Win32()
    if args.probe:
        preflight = desktop_preflight(api, args.require_display)
        if preflight['status'] == 'SKIP':
            print(json.dumps(preflight))
            return 77
        print(json.dumps({'status': 'PASS', 'desktop': preflight['desktop'],
                          'scope': 'interactive desktop access only; GTK/input not yet exercised'}))
        return 0
    required = ('tester', 'compiler', 'interpreter', 'sdk', 'source', 'interaction_source',
                'provider_test', 'lifecycle_test')
    if not all(getattr(args, name) for name in required):
        parser.error('full contract requires all tools, SDK, fixtures and both C++ test executables')
    if args.evidence_directory:
        args.evidence_directory.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='nil-ui-windows-', dir=args.evidence_directory))
    evidence = {'schema': 'nil.windows-ui-display.v1', 'status': 'FAIL', 'cases': [],
                'prerequisites': [], 'requireDisplay': args.require_display,
                'inputMethod': 'verified Job-owned HWND PostMessageW; no global input or focus change',
                'gtkDirectory': os.environ.get('NIL_UI_GTK_DIRECTORY'), 'llvm': args.llvm}
    try:
        paths = {name: getattr(args, name).resolve(strict=True) for name in required}
        evidence['inputs'] = {name: {'path': str(path), 'sha256': digest(path)}
                              for name, path in paths.items() if name != 'sdk'}
        evidence['sdk'] = str(paths['sdk'])
        env = dict(os.environ, GDK_BACKEND='win32', GSK_RENDERER='cairo', GTK_A11Y='none')
        # A configured but broken GTK runtime must not be hidden by a headless
        # build-host skip. This loader-only prerequisite cannot open a window;
        # no display fixture has started when desktop availability is checked.
        selection = configured_provider_selection(paths['provider_test'], os.environ)
        if selection is not None:
            prerequisite = {'name': 'configured-provider', 'selection': selection}
            evidence['prerequisites'].append(prerequisite)
            checked([paths['provider_test'], '--provider-present'], env, root, prerequisite, timeout=15)
        preflight = desktop_preflight(api, args.require_display)
        if preflight['status'] == 'SKIP':
            evidence.update(status='SKIP', reason=preflight['reason'], languageCases=0,
                            cleanupVerified=all(case['cleanupVerified'] for case in evidence['prerequisites']))
            print('SKIP: ' + preflight['reason'])
            return 77
        evidence['desktop'] = preflight['desktop']
        def run(name, command, drive=False):
            record = {'name': name}
            evidence['cases'].append(record)
            output = checked(command, env, root, record, input_api=api if drive else None)
            if name in ('provider-display', 'lifecycle-display'):
                record['completionChecks'] = validate_probe_summary(output, name)
            return output
        run('provider-display', [paths['provider_test'], '--display'])
        run('lifecycle-display', [paths['lifecycle_test'], '--display'])
        base = [paths['tester'], 'run', '--compiler', paths['compiler'], '--interpreter', paths['interpreter'],
                '--sdk-path', paths['sdk'], '--format', 'json', '--jobs', '1', '--timeout', '30']
        routes = ['vm', 'native'] if args.llvm == 'ON' else ['vm']
        desktop = run('desktop', [*base, '--target', 'all' if args.llvm == 'ON' else 'vm',
                                 '--filter', 'nil.std.ui.gtk.desktop', paths['source']])
        tests = validate_report(desktop, DESKTOP_NAMES, routes)['tests']
        for route in routes:
            output = run('interaction-' + route, [*base, '--target', route, '--filter',
                         'nil.std.ui.gtk.interaction', paths['interaction_source']], drive=True)
            tests += validate_report(output, INTERACTION_NAMES, [route])['tests']
        evidence.update(status='PASS', languageCases=len(tests), tests=tests,
                        cleanupVerified=all(case['cleanupVerified'] for case in
                                            evidence['cases'] + evidence['prerequisites']))
        print(f'Windows GTK display contracts passed ({len(tests)} VM/native cases; '
              'Ny assertions verified targeted native key/close events)')
        return 0
    except BaseException as error:
        evidence['error'] = str(error)
        raise
    finally:
        (root / 'report.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        print(f'Windows GTK display evidence retained: {root}', flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
