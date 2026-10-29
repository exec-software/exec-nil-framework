#!/usr/bin/env python3
"""Exercise the optional UI provider on a private, authenticated X11 display."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes as c
import ctypes.util
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


def checked(command, env, timeout=180):
    process = subprocess.Popen(list(map(str, command)), env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    try:
        output, errors = process.communicate(timeout=timeout)
        assert process.returncode == 0, (command, process.returncode, output, errors)
        return subprocess.CompletedProcess(command, process.returncode, output, errors)
    finally:
        # Give niltest time to forward cancellation to its separately grouped
        # compiler/test children before killing the harness session.
        terminate(process)
        if process.stdout: process.stdout.close()
        if process.stderr: process.stderr.close()


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
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
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
