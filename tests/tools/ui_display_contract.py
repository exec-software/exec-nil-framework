#!/usr/bin/env python3
"""Exercise the desktop UI provider: a private X11 display on Linux, the
ambient window server session on macOS."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes as c
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def terminate(process):
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=3)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=3)


@contextmanager
def isolated_display(xvfb):
    with tempfile.TemporaryDirectory(prefix='nil-ui-display-') as directory:
        root = Path(directory)
        authority = root / 'authority'
        authority.touch(mode=0o600)
        cookie = secrets.token_hex(16)
        subprocess.run(['xauth', '-f', authority, 'add', ':0', '.', cookie],
                       check=True, capture_output=True, timeout=10)
        read_fd, write_fd = os.pipe()
        with (root / 'server.log').open('w+b') as log:
            server = subprocess.Popen([str(xvfb), '-displayfd', str(write_fd),
                '-screen', '0', '1024x768x24', '-nolisten', 'tcp', '-auth', str(authority)],
                stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                pass_fds=(write_fd,), start_new_session=True)
            os.close(write_fd)
            try:
                if not select.select([read_fd], [], [], 15)[0]:
                    raise AssertionError('Xvfb did not publish a display number within 15 seconds')
                display_number = os.read(read_fd, 64).decode('ascii').strip()
                if not display_number.isdecimal() or server.poll() is not None:
                    log.seek(0)
                    raise AssertionError('Xvfb startup failed: ' + log.read().decode(errors='replace'))
                display = ':' + display_number
                subprocess.run(['xauth', '-f', authority, 'add', display, '.', cookie],
                               check=True, capture_output=True, timeout=10)
                env = dict(os.environ)
                for key in ('WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'DBUS_SESSION_BUS_PID'):
                    env.pop(key, None)
                env.update(DISPLAY=display, XAUTHORITY=str(authority),
                    GDK_BACKEND='x11', GSK_RENDERER='cairo', GTK_A11Y='none',
                    XDG_RUNTIME_DIR=str(root))
                yield env
            finally:
                os.close(read_fd)
                terminate(server)


class X11Input:
    """Drive only the two known NIL test windows on our private display."""
    def __init__(self, env):
        self.x = c.CDLL(ctypes.util.find_library('X11') or 'libX11.so.6')
        self.xt = c.CDLL(ctypes.util.find_library('Xtst') or 'libXtst.so.6')
        self.x.XOpenDisplay.argtypes = [c.c_char_p]
        self.x.XOpenDisplay.restype = c.c_void_p
        self.x.XDefaultRootWindow.argtypes = [c.c_void_p]
        self.x.XDefaultRootWindow.restype = c.c_ulong
        self.x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong),
            c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
        self.x.XFetchName.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_void_p)]
        self.x.XFree.argtypes = [c.c_void_p]
        self.x.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
        self.x.XStringToKeysym.argtypes = [c.c_char_p]
        self.x.XStringToKeysym.restype = c.c_ulong
        self.x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
        self.x.XKeysymToKeycode.restype = c.c_ubyte
        self.x.XFlush.argtypes = [c.c_void_p]
        self.x.XSync.argtypes = [c.c_void_p, c.c_int]
        self.x.XCloseDisplay.argtypes = [c.c_void_p]
        self.x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
        self.x.XInternAtom.restype = c.c_ulong
        self.x.XSendEvent.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.c_void_p]
        self.xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
        previous = os.environ.get('XAUTHORITY')
        os.environ['XAUTHORITY'] = env['XAUTHORITY']
        try:
            self.display = self.x.XOpenDisplay(env['DISPLAY'].encode())
        finally:
            if previous is None:
                os.environ.pop('XAUTHORITY', None)
            else:
                os.environ['XAUTHORITY'] = previous
        assert self.display, 'cannot connect to the private authenticated X11 display'
        self.root = self.x.XDefaultRootWindow(self.display)
        self.handled = set()
        class Error(c.Structure):
            _fields_ = [('type', c.c_int), ('display', c.c_void_p), ('resourceid', c.c_ulong),
                        ('serial', c.c_ulong), ('code', c.c_ubyte), ('request', c.c_ubyte), ('minor', c.c_ubyte)]
        # A window may disappear between QueryTree, FetchName and focus. Xlib's
        # default handler exits the process for this ordinary stale-window race.
        self.xerrors = []
        callback = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Error))
        def on_error(display, error):
            self.xerrors.append((error.contents.code, error.contents.request))
            return 0
        self.error_callback = callback(on_error)
        self.x.XSetErrorHandler.argtypes = [c.c_void_p]
        self.x.XSetErrorHandler.restype = c.c_void_p
        self.old_error_handler = self.x.XSetErrorHandler(c.cast(self.error_callback, c.c_void_p))
        class Attributes(c.Structure):
            _fields_ = [(key, c.c_int) for key in ('x', 'y', 'width', 'height', 'border_width', 'depth')] + [
                ('visual', c.c_void_p), ('root', c.c_ulong), ('window_class', c.c_int),
                ('bit_gravity', c.c_int), ('win_gravity', c.c_int), ('backing_store', c.c_int),
                ('backing_planes', c.c_ulong), ('backing_pixel', c.c_ulong), ('save_under', c.c_int),
                ('colormap', c.c_ulong), ('map_installed', c.c_int), ('map_state', c.c_int),
                ('all_event_masks', c.c_long), ('your_event_mask', c.c_long), ('do_not_propagate_mask', c.c_long),
                ('override_redirect', c.c_int), ('screen', c.c_void_p)]
        self.Attributes = Attributes
        self.x.XGetWindowAttributes.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(Attributes)]

    def close(self):
        if self.display:
            self.x.XSync(self.display, False)
            self.x.XCloseDisplay(self.display)
            self.display = None
            self.x.XSetErrorHandler(self.old_error_handler)

    def key(self, name, down):
        code = self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(name.encode()))
        assert code, f'no keycode for {name}'
        assert self.xt.XTestFakeKeyEvent(self.display, code, int(down), 0)
        self.x.XFlush(self.display)

    def press(self, name, shifted=False):
        if shifted:
            self.key('Shift_L', True)
        self.key(name, True)
        self.key(name, False)
        if shifted:
            self.key('Shift_L', False)
        time.sleep(.06)

    def request_close(self, window):
        class Data(c.Union):
            _fields_ = [('b', c.c_char * 20), ('s', c.c_short * 10), ('l', c.c_long * 5)]
        class Message(c.Structure):
            _fields_ = [('type', c.c_int), ('serial', c.c_ulong), ('send_event', c.c_int),
                ('display', c.c_void_p), ('window', c.c_ulong), ('message_type', c.c_ulong),
                ('format', c.c_int), ('data', Data)]
        class Event(c.Union):
            _fields_ = [('message', Message), ('pad', c.c_long * 24)]
        event = Event()
        event.message.type = 33  # ClientMessage, from Xlib.h.
        event.message.display = self.display
        event.message.window = window
        event.message.message_type = self.x.XInternAtom(self.display, b'WM_PROTOCOLS', False)
        event.message.format = 32
        event.message.data.l[0] = self.x.XInternAtom(self.display, b'WM_DELETE_WINDOW', False)
        assert self.x.XSendEvent(self.display, window, False, 0, c.byref(event))
        self.x.XFlush(self.display)

    def drive_ready_window(self):
        root, parent = c.c_ulong(), c.c_ulong()
        children, count = c.POINTER(c.c_ulong)(), c.c_uint()
        if not self.x.XQueryTree(self.display, self.root, c.byref(root), c.byref(parent),
                                c.byref(children), c.byref(count)):
            return
        try:
            for index in range(count.value):
                window = children[index]
                name = c.c_void_p()
                if not self.x.XFetchName(self.display, window, c.byref(name)) or not name.value:
                    continue
                try:
                    title = c.string_at(name).decode('utf8', errors='replace')
                finally:
                    self.x.XFree(name)
                key = (window, title)
                if key in self.handled:
                    continue
                if title not in ('NIL2 UI keyboard probe', 'NIL2 UI close probe'):
                    continue
                attributes = self.Attributes()
                if not self.x.XGetWindowAttributes(self.display, window, c.byref(attributes)) or attributes.map_state != 2:
                    continue  # IsViewable: do not inject into an unmapped window.
                if title == 'NIL2 UI close probe':
                    self.handled.add(key)
                    self.request_close(window)
                    continue
                self.x.XSetInputFocus(self.display, window, 2, 0)
                self.x.XSync(self.display, False)
                if any(request == 42 for _, request in self.xerrors):
                    self.check_errors()
                    continue
                self.handled.add(key)
                time.sleep(.1)  # Allow GTK's entry focus request to finish.
                for char in 'Matrix':
                    self.press(char, char.isupper())
                self.press('Tab')
                self.press('space')
                self.press('Tab')
                self.press('space')
        finally:
            if children:
                self.x.XFree(children)
        self.check_errors()

    def check_errors(self):
        self.x.XSync(self.display, False)
        errors, self.xerrors = self.xerrors, []
        unexpected = [(code, request) for code, request in errors
                      if not (code == 3 and request in (3, 15, 20, 25, 42))
                      and not (code == 8 and request == 42)]
        assert not unexpected, ('unexpected X11 protocol error', unexpected)


def checked(command, env, timeout=180, capture_prefix=None):
    process = subprocess.Popen(list(map(str, command)), env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    output, errors = '', ''
    def partial(value):
        return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
    try:
        try:
            output, errors = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired as expired:
            # communicate() can expose bytes even in text mode. Keep the raw
            # partial streams before cancellation, then drain the owned child
            # once more so its cancellation diagnostics are preserved as well.
            output, errors = partial(expired.output), partial(expired.stderr)
            terminate(process)
            try:
                output, errors = process.communicate(timeout=3)
            except subprocess.TimeoutExpired as draining:
                output = partial(draining.output) or output
                errors = partial(draining.stderr) or errors
            raise
        assert process.returncode == 0, (command, process.returncode, output, errors)
        result = subprocess.CompletedProcess(command, process.returncode, output, errors)
        result.pid = process.pid
        return result
    finally:
        try:
            if capture_prefix is not None:
                Path(str(capture_prefix) + '.stdout').write_text(output, encoding='utf-8')
                Path(str(capture_prefix) + '.stderr').write_text(errors, encoding='utf-8')
        finally:
            # Give niltest time to forward cancellation to its separately grouped
            # compiler/test children before killing the harness session.
            terminate(process)
            if process.stdout: process.stdout.close()
            if process.stderr: process.stderr.close()


def validate_desktop_report(report, llvm):
    names = {'widgets_layout_and_model_binding', 'window_ownership_and_size_errors',
             'dynamic_subtrees_and_effective_state'}
    routes = {'vm', 'native'} if llvm == 'ON' else {'vm'}
    expected = {(name, 'nil.std.ui.gtk.desktop', route) for name in names for route in routes}
    tests = report['tests']
    assert report['schema'] == 'nil.test.results.v1' and not report['errors'], report
    assert len(tests) == len(expected), report
    assert {(test['name'], test['suite'], test['route']) for test in tests} == expected, report
    assert all(test['status'] == 'passed' and test['exitCode'] == 77 and
               not test['signal'] and not test['timedOut'] and not test['cancelled']
               for test in tests), report


def appkit_interaction_environment(environment, driver, directory, token, route, compiler):
    """Explicit fixture injection; ordinary display/build commands keep their env."""
    assert driver.is_absolute() and driver.is_file(), driver
    assert compiler.is_absolute() and compiler.is_file(), compiler
    assert directory.is_absolute() and directory.is_dir(), directory
    assert len(token) == 32 and all(byte in '0123456789abcdef' for byte in token), token
    assert route in ('vm', 'native'), route
    assert not environment.get('DYLD_INSERT_LIBRARIES'), 'AppKit input fixture cannot compose ambient injection'
    assert not any(key.startswith('NIL_TEST_APPKIT_INPUT_') for key in environment), 'AppKit fixture declaration already present'
    # The same-architecture test compiler adapter validates this declaration,
    # then removes it before exec'ing the real compiler. niltest and its actual
    # application children keep the input driver; system tools never load it.
    return dict(environment, DYLD_INSERT_LIBRARIES=str(driver),
                NIL_TEST_APPKIT_INPUT_TOKEN=token, NIL_TEST_APPKIT_INPUT_DIRECTORY=str(directory),
                NIL_TEST_APPKIT_INPUT_ROUTE=route, NIL_TEST_APPKIT_INPUT_DRIVER=str(driver),
                NIL_TEST_APPKIT_INPUT_COMPILER=str(compiler))


def validate_appkit_interaction(report, route, directory, token, parent_pid):
    expected = {'keyboard_events': (3, 8), 'close_request': (1, 0)}
    assert report['schema'] == 'nil.test.results.v1' and not report['errors'], report
    tests = report['tests']
    assert len(tests) == len(expected), report
    assert {(test['name'], test['suite'], test['route']) for test in tests} == {
        (name, 'nil.std.ui.gtk.interaction', route) for name in expected}, report
    pids, paths, records = set(), set(), []
    for test in tests:
        assert test['status'] == 'passed' and test['exitCode'] == 77 and not (
            test['signal'] or test['timedOut'] or test['cancelled']), test
        markers = [line.removeprefix('NIL_APPKIT_INPUT ') for line in test['stderr'].splitlines()
                   if line.startswith('NIL_APPKIT_INPUT ')]
        assert len(markers) == 1 and len(markers[0]) <= 8192, test
        record = json.loads(markers[0])
        pid = record['pid']
        assert type(pid) is int and 0 < pid <= 0x7fffffff and pid != parent_pid and pid not in pids, record
        assert all(type(record[field]) is int for field in ('parentPid', 'stage', 'keys')), record
        assert (record['schema'], record['token'], record['route'], record['test'], record['parentPid'],
                record['status'], record['stage'], record['keys']) == (
            'nil.appkit.input.v1', token, route, test['name'], parent_pid,
            'passed', *expected[test['name']]), record
        assert record['scope'] == 'owned-process AppKit responder input; no Accessibility or global input', record
        path = directory / f'{token}-{pid}.json'
        assert path.is_file() and not path.is_symlink() and path.stat().st_size <= 8192, path
        assert json.loads(path.read_text(encoding='utf-8')) == record, (path, record)
        pids.add(pid); paths.add(path); records.append(record)
    assert set(directory.iterdir()) == paths, ('unexpected/missing input evidence', directory)
    return records


def run_appkit_interaction(args, base, environment):
    assert args.interaction_source and args.appkit_input_driver and args.appkit_compile_driver, 'macOS interaction qualification requires its input and compiler fixtures'
    driver = args.appkit_input_driver.resolve(strict=True)
    compile_driver = args.appkit_compile_driver.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    assert compile_driver.is_file() and compiler.is_file(), (compile_driver, compiler)
    command_base = list(base)
    compiler_slot = command_base.index('--compiler') + 1
    assert Path(command_base[compiler_slot]).resolve() == compiler, 'niltest real compiler must match the explicit fixture declaration'
    command_base[compiler_slot] = compile_driver
    source = args.interaction_source.resolve(strict=True)
    parent = args.appkit_evidence_directory
    if parent:
        parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='nil-appkit-input-', dir=parent)).resolve()
    summary = {'schema': 'nil.appkit.interaction.v1', 'status': 'running', 'routes': [],
               'inputDriver': str(driver), 'inputDriverSha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
               'compileDriver': str(compile_driver), 'compileDriverSha256': hashlib.sha256(compile_driver.read_bytes()).hexdigest(),
               'compiler': str(compiler),
               'source': str(source), 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'scope': 'in-process AppKit responder input; test compiler adapter removes fixture injection before compilation; not default-cache proof',
               'crossProcessAccessibility': 'not exercised', 'evidenceDirectory': str(root)}
    print(f'AppKit interaction evidence: {root}', flush=True)
    try:
        for route in (['vm', 'native'] if args.llvm == 'ON' else ['vm']):
            directory = root / route
            directory.mkdir(mode=0o700)
            token = secrets.token_hex(16)
            env = appkit_interaction_environment(environment, driver, directory, token, route, compiler)
            command = [*command_base, '--target', route, '--filter', 'nil.std.ui.gtk.interaction', source]
            result = checked(command, env, timeout=120, capture_prefix=root / route)
            report = json.loads(result.stdout)
            print(json.dumps(report, indent=2), flush=True)
            records = validate_appkit_interaction(report, route, directory, token, result.pid)
            summary['routes'].append({'route': route, 'parentPid': result.pid,
                                      'naturalCompletion': True, 'languageCases': len(report['tests']),
                                      'inputs': records})
        summary['status'] = 'passed'
        return sum(route['languageCases'] for route in summary['routes'])
    except BaseException as error:
        summary.update(status='failed', error=str(error))
        raise
    finally:
        (root / 'report.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(summary, indent=2), flush=True)


def darwin(args):
    """The AppKit contract. macOS has no detachable display to create, so the
    runner's own window server session is used; without one there is nothing
    to test and the contract skips rather than reporting a provider fault."""
    core = c.CDLL(ctypes.util.find_library('CoreGraphics'))
    core.CGMainDisplayID.restype = c.c_uint32
    if not core.CGMainDisplayID():
        print('SKIP: macOS display contracts need a window server session')
        return 77
    if args.probe:
        print('macOS window server session present')
        return 0
    assert all((args.tester, args.compiler, args.interpreter, args.sdk, args.source))
    env = dict(os.environ)
    if args.provider_test:
        checked([args.provider_test.resolve(), '--display'], env)
    if args.lifecycle_test:
        checked([args.lifecycle_test.resolve(), '--display'], env)
    base = [args.tester.resolve(), 'run', '--compiler', args.compiler.resolve(),
            '--interpreter', args.interpreter.resolve(), '--sdk-path', args.sdk.resolve(),
            '--format', 'json', '--jobs', '1', '--timeout', '30']
    desktop = checked([*base, '--target', 'all' if args.llvm == 'ON' else 'vm',
                       '--filter', 'nil.std.ui.gtk.desktop', args.source.resolve()], env)
    report = json.loads(desktop.stdout)
    # Preserve each route's actual results in CTest's retained output, and reject
    # missing, duplicated or skipped cases rather than accepting any green subset.
    print(json.dumps(report, indent=2), flush=True)
    validate_desktop_report(report, args.llvm)
    interaction_cases = run_appkit_interaction(args, base, env)
    print(f'macOS desktop contract passed: {len(report["tests"])} desktop + '
          f'{interaction_cases} interaction language cases; in-process AppKit input, '
          'cross-process Accessibility not exercised')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xvfb', default=shutil.which('Xvfb'))
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--tester', type=Path)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--interpreter', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--interaction-source', type=Path)
    parser.add_argument('--provider-test', type=Path)
    parser.add_argument('--lifecycle-test', type=Path)
    parser.add_argument('--appkit-input-driver', type=Path)
    parser.add_argument('--appkit-compile-driver', type=Path)
    parser.add_argument('--appkit-evidence-directory', type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    if sys.platform == 'darwin':
        return darwin(args)
    if sys.platform != 'linux' or not args.xvfb or not Path(args.xvfb).is_file() or not shutil.which('xauth'):
        print('SKIP: GTK display contracts need Linux, Xvfb and xauth')
        return 77
    try:
        c.CDLL('libX11.so.6')
        c.CDLL('libXtst.so.6')
        if not args.probe: c.CDLL('libgtk-4.so.1')
    except OSError as error:
        print('SKIP: optional desktop test dependency is unavailable: ' + str(error))
        return 77
    with isolated_display(Path(args.xvfb).resolve()) as env:
        driver = X11Input(env)
        driver.close()
        if args.probe:
            print('Private Xvfb authentication and XTest connection passed')
            return 0
        assert all((args.tester, args.compiler, args.interpreter, args.sdk, args.source, args.interaction_source))
        base = [args.tester.resolve(), 'run', '--compiler', args.compiler.resolve(),
            '--interpreter', args.interpreter.resolve(), '--sdk-path', args.sdk.resolve(),
            '--format', 'json', '--jobs', '1', '--timeout', '30']
        session = ['dbus-run-session', '--'] if shutil.which('dbus-run-session') else []
        if args.provider_test:
            checked([*session, args.provider_test.resolve(), '--display'], env)
        if args.lifecycle_test:
            checked([*session, args.lifecycle_test.resolve(), '--display'], env)
        desktop = checked([*session, *base, '--target', 'all' if args.llvm == 'ON' else 'vm',
            '--filter', 'nil.std.ui.gtk.desktop', args.source.resolve()], env)
        report = json.loads(desktop.stdout)
        assert report['tests'] and not report['errors'], report
        assert all(test['status'] == 'passed' for test in report['tests']), report
        checks = len(report['tests'])
        for route in (['vm', 'native'] if args.llvm == 'ON' else ['vm']):
            command = [*session, *base, '--target', route, '--filter', 'nil.std.ui.gtk.interaction',
                       args.interaction_source.resolve()]
            process = subprocess.Popen(list(map(str, command)), env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, start_new_session=True)
            driver = None
            try:
                driver = X11Input(env)
                deadline = time.monotonic() + 120
                while process.poll() is None and time.monotonic() < deadline:
                    driver.drive_ready_window()
                    time.sleep(.04)
                if process.poll() is None:
                    raise AssertionError(f'{route} UI interaction exceeded its outer deadline')
                output, errors = process.communicate(timeout=3)
                assert process.returncode == 0, (route, output, errors)
                report = json.loads(output)
                assert len(report['tests']) == 2 and not report['errors'], report
                assert all(test['status'] == 'passed' for test in report['tests']), report
                assert {title for _, title in driver.handled} == {'NIL2 UI keyboard probe', 'NIL2 UI close probe'}
                checks += len(report['tests'])
            finally:
                if driver: driver.close()
                terminate(process)
                if process.stdout: process.stdout.close()
                if process.stderr: process.stderr.close()
        print(f'GTK display contracts passed ({checks} VM/native cases, real keyboard and close events)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
