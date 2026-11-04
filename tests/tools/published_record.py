"""Bounded reads of immutable test records published by atomic rename."""
import os
from pathlib import Path


def windows_shared_handle(path: Path, access: int):
    import ctypes as c
    from ctypes import wintypes as w
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID,
                                  w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.CloseHandle.restype = w.BOOL
    # Rename publishes the name before releasing its DELETE handle. Share
    # read/write/delete so opening the completed record does not race it.
    handle = kernel.CreateFileW(str(path), access, 0x7, None, 3, 0x80, None)
    if handle == w.HANDLE(-1).value:
        error = c.WinError(c.get_last_error())
        error.filename = str(path)
        raise error
    return kernel, handle


def read_published_text(path: Path, limit: int = 4096) -> str:
    if os.name == "nt":
        import msvcrt
        kernel, handle = windows_shared_handle(path, 0x80000000)  # GENERIC_READ
        try:
            descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY | os.O_NOINHERIT)
        except BaseException:
            kernel.CloseHandle(handle)
            raise
    else:
        descriptor = os.open(path, os.O_RDONLY)
    try:
        with open(descriptor, "r", encoding="utf-8", closefd=False) as stream:
            raw = stream.read(limit + 1)
    finally:
        os.close(descriptor)  # Also closes the transferred Windows handle.
    if len(raw) > limit:
        raise ValueError(f"oversized published record: {path}")
    return raw
